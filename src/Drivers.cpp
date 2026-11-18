#include "Drivers.h"
#include "Lang.h"
#include "Hive.h"
#include "Repair.h"
#include "core/Platform.h"

namespace bd {

namespace {
bool exists(const wstr& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

// .inf files are ANSI, UTF-8 or UTF-16: as wide text either way
wstr infText(const std::string& b) {
    if (b.size() >= 2 && (uint8_t)b[0] == 0xFF && (uint8_t)b[1] == 0xFE) return wstr((const wchar_t*)(b.data() + 2), (b.size() - 2) / 2);
    return utf8ToWide(b);
}

wstr infValue(const wstr& t, const wchar_t* key) {
    // "Key = value" at the start of a line, any case, value without quotes and comments
    size_t pos = 0;
    wstr k = toLower(key), low = toLower(t);
    while ((pos = low.find(k, pos)) != wstr::npos) {
        size_t ls = low.rfind(L'\n', pos);
        ls = ls == wstr::npos ? 0 : ls + 1;
        bool startOfLine = low.find_first_not_of(L" \t", ls) == pos;
        size_t eq = low.find_first_not_of(L" \t", pos + k.size());
        if (startOfLine && eq != wstr::npos && low[eq] == L'=') {
            size_t e = t.find_first_of(L"\r\n;", eq);
            wstr v = t.substr(eq + 1, e == wstr::npos ? wstr::npos : e - eq - 1);
            while (!v.empty() && (v.back() == L' ' || v.back() == L'\t' || v.back() == L'"')) v.pop_back();
            while (!v.empty() && (v.front() == L' ' || v.front() == L'\t' || v.front() == L'"')) v.erase(0, 1);
            return v;
        }
        pos += k.size();
    }
    return L"";
}
}  // namespace

bool infIsStorageDriver(const std::string& bytes, wstr& provider) {
    wstr t = infText(bytes);
    wstr cls = toLower(infValue(t, L"Class")), guid = toLower(infValue(t, L"ClassGuid"));
    bool storage = cls == L"scsiadapter" || cls == L"hdc" || guid == L"{4d36e97b-e325-11ce-bfc1-08002be10318}" || guid == L"{4d36e96a-e325-11ce-bfc1-08002be10318}";
    if (!storage) return false;
    provider = infValue(t, L"Provider");
    if (!provider.empty() && provider[0] == L'%') {  // %Intel% -> its text in [Strings]
        wstr key = provider.substr(1, provider.size() - 2);
        wstr v = infValue(t, key.c_str());
        if (!v.empty()) provider = v;
    }
    return true;
}

std::vector<DriverInf> findStorageDrivers(const std::vector<wstr>& roots, volatile bool* cancel) {
    std::vector<DriverInf> out;
    int budget = 20000;
    for (const wstr& root : roots) {
        std::vector<wstr> level = {root};
        for (int depth = 0; depth < 5 && !level.empty(); ++depth) {
            std::vector<wstr> next;
            for (const wstr& dir : level) {
                if ((cancel && *cancel) || --budget < 0) return out;
                WIN32_FIND_DATAW fd{};
                HANDLE h = FindFirstFileW((dir + L"*").c_str(), &fd);
                if (h == INVALID_HANDLE_VALUE) continue;
                do {
                    wstr n = fd.cFileName;
                    if (n == L"." || n == L"..") continue;
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && !iequals(n, L"Windows") && !iequals(n, L"$Recycle.Bin") && !iequals(n, L"System Volume Information"))
                            next.push_back(dir + n + L"\\");
                        continue;
                    }
                    if (n.size() < 5 || !iequals(n.substr(n.size() - 4), L".inf") || fd.nFileSizeLow > (2u << 20)) continue;
                    std::string bytes;
                    wstr provider;
                    if (!readFileText(dir + n, bytes) || !infIsStorageDriver(bytes, provider)) continue;
                    DriverInf d;
                    d.path = dir + n;
                    d.provider = provider;
                    d.title = (provider.empty() ? wstr() : provider + L" · ") + n;
                    out.push_back(d);
                } while (FindNextFileW(h, &fd) && out.size() < 60);
                FindClose(h);
            }
            level.swap(next);
        }
    }
    return out;
}

bool loadDriver(const wstr& inf, wstr& result) {
    wstr tool = g_sys.winPE ? findSystemTool(L"drvload.exe") : findSystemTool(L"pnputil.exe");
    if (tool.empty()) {
        result = g_sys.winPE ? T(L"This Windows PE has no driver loader (drvload.exe).") : T(L"Windows' driver tool (pnputil.exe) was not found.");
        return false;
    }
    ToolResult t = g_sys.winPE ? runTool(tool, L"\"" + inf + L"\"") : runTool(tool, L"/add-driver \"" + inf + L"\" /install");
    bool ok = t.started && (t.code == 0 || t.code == 3010);
    result = ok ? TF(L"The driver {0} was loaded.", inf.substr(inf.find_last_of(L'\\') + 1))
                : T(L"The driver could not be loaded. It may be for another Windows version or processor type (it must be a 64-bit driver). ") + t.output.substr(t.output.size() > 300 ? t.output.size() - 300 : 0);
    return ok;
}

wstr fileCompany(const wstr& path) {
    std::string b;
    if (!readFileText(path, b)) return L"";
    // the version information holds L"CompanyName", some padding, then the name
    static const wchar_t key[] = L"CompanyName";
    const char* k = (const char*)key;
    size_t klen = sizeof key - 2;
    for (size_t i = 0; i + klen + 8 < b.size(); ++i) {
        if (b[i] != 'C' || memcmp(b.data() + i, k, klen)) continue;
        if (i < 6 || b[i - 2] != 1 || b[i - 1] != 0) continue;  // a text entry of the version information, not any other "CompanyName"
        size_t p = i + klen;
        while (p + 1 < b.size() && b[p] == 0 && b[p + 1] == 0) p += 2;
        wstr out;
        for (; p + 1 < b.size() && out.size() < 80; p += 2) {
            wchar_t c = (wchar_t)((uint8_t)b[p] | (uint8_t)b[p + 1] << 8);
            if (!c) break;
            out += c;
        }
        return out;
    }
    return L"";
}

std::vector<BootDriver> listBootDrivers(const Install& in, const Partition& P, wstr& err) {
    std::vector<BootDriver> out;
    LoadedHive hive;
    HKEY root = nullptr;
    wstr base;
    if (in.running) {
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM", 0, KEY_READ, &root) != ERROR_SUCCESS) {
            err = T(L"The registry could not be read.");
            return out;
        }
    } else {
        if (!hive.load(in.winDir + L"\\System32\\config\\SYSTEM", true, err)) return out;
        root = hive.root();
    }
    DWORD cur = 1;
    regDword(root, L"Select", L"Current", cur);
    wchar_t cs[32];
    swprintf(cs, 32, L"ControlSet%03u", cur);
    base = wstr(cs) + L"\\Services";
    for (const wstr& name : regSubkeys(root, base)) {
        DWORD type = 0, start = 9;
        wstr key = base + L"\\" + name;
        if (!regDword(root, key, L"Type", type) || !regDword(root, key, L"Start", start)) continue;
        if (!(type & 3) || start > 1) continue;  // kernel and file-system drivers that start with Windows
        BootDriver d;
        d.name = name;
        d.start = (int)start;
        d.display = regString(root, key, L"DisplayName");
        if (!d.display.empty() && d.display[0] == L'@') d.display.clear();  // a resource reference, not a text
        d.group = regString(root, key, L"Group");
        wstr img = regString(root, key, L"ImagePath");
        if (img.empty()) img = L"System32\\drivers\\" + name + L".sys";
        for (const wchar_t* pre : {L"\\SystemRoot\\", L"\\??\\", L"%SystemRoot%\\"})
            if (istartsWith(img, pre)) img = img.substr(wcslen(pre));
        if (img.size() > 2 && img[1] == L':') {
            size_t w = toLower(img).find(L"\\windows\\");
            img = w == wstr::npos ? img.substr(3) : img.substr(w + 9);
            d.file = (w == wstr::npos ? in.root : in.winDir + L"\\") + img;
        } else d.file = in.winDir + L"\\" + img;
        d.missing = !exists(d.file);
        d.company = d.missing ? wstr() : fileCompany(d.file);
        d.microsoft = icontains(d.company, L"Microsoft");
        out.push_back(d);
    }
    if (in.running) RegCloseKey(root);
    std::sort(out.begin(), out.end(), [](const BootDriver& a, const BootDriver& b) {
        if (a.microsoft != b.microsoft) return !a.microsoft;
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return out;
}

}  // namespace bd

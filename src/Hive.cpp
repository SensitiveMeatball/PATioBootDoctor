#include "Hive.h"
#include "Lang.h"

namespace bd {

Health hiveBytesHealth(const std::string& h, wstr& why) {
    if (h.size() < 4096) {
        why = T(L"The file is too small to be a registry file.");
        return Health::Bad;
    }
    if (memcmp(h.data(), "regf", 4)) {
        why = T(L"The file does not start like a registry file. It is damaged or was overwritten.");
        return Health::Bad;
    }
    uint32_t sum = 0, stored = 0, seq1 = 0, seq2 = 0;
    for (int i = 0; i < 127; ++i) {
        uint32_t v;
        memcpy(&v, &h[i * 4], 4);
        sum ^= v;
    }
    if (sum == 0xFFFFFFFFu) sum = 0xFFFFFFFEu;
    if (sum == 0) sum = 1;
    memcpy(&stored, &h[0x1FC], 4);
    if (sum != stored) {
        why = T(L"The registry file's header is damaged.");
        return Health::Bad;
    }
    memcpy(&seq1, &h[4], 4);
    memcpy(&seq2, &h[8], 4);
    if (seq1 != seq2) {
        why = T(L"The registry file was not saved cleanly the last time (often after a power cut). Windows can usually repair this by itself.");
        return Health::Warn;
    }
    return Health::Ok;
}

Health hiveFileHealth(const wstr& path, wstr& why) {
    std::string head;
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        why = T(L"The file is missing.");
        return Health::Bad;
    }
    if (!readFileText(path, head, 4096)) {
        // A hive in use by the running Windows cannot be opened; that means it is fine.
        DWORD e = GetLastError();
        if (e == ERROR_SHARING_VIOLATION || e == ERROR_LOCK_VIOLATION) return Health::Ok;
        why = T(L"The file could not be read.");
        return Health::Unknown;
    }
    return hiveBytesHealth(head, why);
}

static int g_hiveCounter = 0;

bool LoadedHive::load(const wstr& file, bool copy, wstr& err) {
    unload();
    enableAllPrivileges();
    wstr src = file;
    if (copy) {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        temp_ = wstr(tmp) + L"PATioBD_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(++g_hiveCounter);
        if (!CopyFileW(file.c_str(), temp_.c_str(), FALSE)) {
            err = T(L"The file could not be copied (error ") + std::to_wstring(GetLastError()) + L").";
            temp_.clear();
            return false;
        }
        SetFileAttributesW(temp_.c_str(), FILE_ATTRIBUTE_NORMAL);
        for (const wchar_t* ext : {L".LOG", L".LOG1", L".LOG2"})
            if (CopyFileW((file + ext).c_str(), (temp_ + ext).c_str(), FALSE)) SetFileAttributesW((temp_ + ext).c_str(), FILE_ATTRIBUTE_NORMAL);
        src = temp_;
    }
    // First a private "app hive": no global name, no privileges, closed with the key.
    // Some Windows builds refuse RegLoadKey but still allow this.
    typedef LONG(WINAPI * LoadAppKeyFn)(LPCWSTR, PHKEY, REGSAM, DWORD, DWORD);
    auto loadApp = (LoadAppKeyFn)GetProcAddress(GetModuleHandleW(L"advapi32.dll"), "RegLoadAppKeyW");
    if (loadApp) {
        LONG a = loadApp(src.c_str(), &root_, KEY_ALL_ACCESS, 0, 0);
        if (a != ERROR_SUCCESS) a = loadApp(src.c_str(), &root_, KEY_READ, 0, 0);
        if (a == ERROR_SUCCESS) return true;
        root_ = nullptr;
    }
    name_ = L"PATioBD_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(++g_hiveCounter);
    LONG r = RegLoadKeyW(HKEY_LOCAL_MACHINE, name_.c_str(), src.c_str());
    if (r != ERROR_SUCCESS && copy) {
        // Transaction logs that don't match the file make loading fail; the file alone is still readable.
        for (const wchar_t* ext : {L".LOG", L".LOG1", L".LOG2", L".blf"}) {
            SetFileAttributesW((temp_ + ext).c_str(), FILE_ATTRIBUTE_NORMAL);
            DeleteFileW((temp_ + ext).c_str());
        }
        r = RegLoadKeyW(HKEY_LOCAL_MACHINE, name_.c_str(), src.c_str());
    }
    if (r != ERROR_SUCCESS) {
        err = r == ERROR_BADDB || r == ERROR_REGISTRY_CORRUPT ? T(L"The file is damaged and could not be opened.")
                                                             : T(L"The file could not be opened (error ") + std::to_wstring(r) + L").";
        name_.clear();
        unload();
        return false;
    }
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, name_.c_str(), 0, KEY_READ | KEY_WRITE, &root_) != ERROR_SUCCESS &&
        RegOpenKeyExW(HKEY_LOCAL_MACHINE, name_.c_str(), 0, KEY_READ, &root_) != ERROR_SUCCESS) {
        err = T(L"The file was loaded but could not be read.");
        unload();
        return false;
    }
    return true;
}

void LoadedHive::unload() {
    if (root_) {
        RegCloseKey(root_);
        root_ = nullptr;
    }
    if (!name_.empty()) {
        RegFlushKey(HKEY_LOCAL_MACHINE);
        for (int i = 0; i < 5 && RegUnLoadKeyW(HKEY_LOCAL_MACHINE, name_.c_str()) != ERROR_SUCCESS; ++i) Sleep(50);
        name_.clear();
    }
    if (!temp_.empty()) {
        for (int i = 0; i < 20 && !DeleteFileW(temp_.c_str()) && GetFileAttributesW(temp_.c_str()) != INVALID_FILE_ATTRIBUTES; ++i) Sleep(25);
        for (const wchar_t* ext : {L".LOG", L".LOG1", L".LOG2", L".blf"}) DeleteFileW((temp_ + ext).c_str());
        temp_.clear();
    }
}

void unloadStaleHives() {
    enableAllPrivileges();
    for (const wstr& k : regSubkeys(HKEY_LOCAL_MACHINE, L""))
        if (istartsWith(k, L"PATioBD_")) RegUnLoadKeyW(HKEY_LOCAL_MACHINE, k.c_str());
}

static bool query(HKEY root, const wstr& sub, const wchar_t* value, DWORD& type, std::vector<uint8_t>& data) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    DWORD n = 0;
    LONG r = RegQueryValueExW(k, value, nullptr, &type, nullptr, &n);
    if (r == ERROR_SUCCESS) {
        data.assign(n + 4, 0);
        r = RegQueryValueExW(k, value, nullptr, &type, data.data(), &n);
        data.resize(n);
    }
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

bool regDword(HKEY root, const wstr& sub, const wchar_t* value, DWORD& out) {
    DWORD type = 0;
    std::vector<uint8_t> d;
    if (!query(root, sub, value, type, d) || d.size() < 4) return false;
    memcpy(&out, d.data(), 4);
    return true;
}

wstr regString(HKEY root, const wstr& sub, const wchar_t* value) {
    DWORD type = 0;
    std::vector<uint8_t> d;
    if (!query(root, sub, value, type, d) || (type != REG_SZ && type != REG_EXPAND_SZ)) return L"";
    wstr s((const wchar_t*)d.data(), d.size() / 2);
    while (!s.empty() && !s.back()) s.pop_back();
    return s;
}

std::vector<wstr> regMulti(HKEY root, const wstr& sub, const wchar_t* value) {
    std::vector<wstr> out;
    DWORD type = 0;
    std::vector<uint8_t> d;
    if (!query(root, sub, value, type, d)) return out;
    const wchar_t* p = (const wchar_t*)d.data();
    size_t n = d.size() / 2, i = 0;
    while (i < n) {
        size_t len = 0;
        while (i + len < n && p[i + len]) ++len;
        if (len) out.push_back(wstr(p + i, len));
        i += len + 1;
    }
    return out;
}

bool regBinary(HKEY root, const wstr& sub, const wchar_t* value, std::string& out) {
    DWORD type = 0;
    std::vector<uint8_t> d;
    if (!query(root, sub, value, type, d)) return false;
    out.assign((const char*)d.data(), d.size());
    return true;
}

std::vector<wstr> regSubkeys(HKEY root, const wstr& sub) {
    std::vector<wstr> out;
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, sub.empty() ? nullptr : sub.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return out;
    wchar_t name[256];
    for (DWORD i = 0;; ++i) {
        DWORD n = 256;
        if (RegEnumKeyExW(k, i, name, &n, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        out.push_back(name);
    }
    RegCloseKey(k);
    return out;
}

bool regSetDword(HKEY root, const wstr& sub, const wchar_t* value, DWORD v) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return false;
    bool ok = RegSetValueExW(k, value, 0, REG_DWORD, (const BYTE*)&v, sizeof v) == ERROR_SUCCESS;
    RegCloseKey(k);
    return ok;
}

bool regSetString(HKEY root, const wstr& sub, const wchar_t* value, const wstr& v) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return false;
    bool ok = RegSetValueExW(k, value, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * 2)) == ERROR_SUCCESS;
    RegCloseKey(k);
    return ok;
}

bool regSetBinary(HKEY root, const wstr& sub, const wchar_t* value, const std::string& v) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return false;
    bool ok = RegSetValueExW(k, value, 0, REG_BINARY, (const BYTE*)v.data(), (DWORD)v.size()) == ERROR_SUCCESS;
    RegCloseKey(k);
    return ok;
}

bool regEnsureKey(HKEY root, const wstr& sub) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(root, sub.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    RegCloseKey(k);
    return true;
}

bool regDefaultValueType(HKEY root, const wstr& sub, DWORD& type) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    DWORD t = 0;
    LONG r = RegQueryValueExW(k, L"", nullptr, &t, nullptr, nullptr);
    RegCloseKey(k);
    if (r == ERROR_SUCCESS) type = t;
    return r == ERROR_SUCCESS;
}

}  // namespace bd

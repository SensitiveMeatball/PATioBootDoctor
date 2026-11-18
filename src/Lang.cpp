#include "Lang.h"
#include <mutex>
#include <unordered_map>

namespace i18n {

namespace {

std::mutex g_lock;
std::unordered_map<wstr, wstr> g_map;        // English -> chosen language
std::unordered_map<wstr, wstr> g_fallback;   // English -> English (stable references)
wstr g_code = L"en";
bool g_rtl = false;
wstr g_exeDir;
std::vector<LangInfo> g_langs;

// The built-in packs (resources LANG_<CODE>), in the order shown in Settings
const wchar_t* const kBuiltIn[] = {L"en", L"tr", L"de", L"es", L"fr", L"pt", L"it", L"nl", L"ar", L"ru", L"uk", L"pl", L"zh", L"ja", L"ko", L"vi"};

wstr unescape(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) {
            o += s[i];
            continue;
        }
        char c = s[++i];
        o += c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c;
    }
    return utf8ToWide(o);
}

std::string escape(const wstr& w) {
    std::string s = wideToUtf8(w), o;
    for (char c : s) {
        if (c == '\\') o += "\\\\";
        else if (c == '"') o += "\\\"";
        else if (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else if (c == '\r') o += "\\r";
        else o += c;
    }
    return o;
}

// The quoted part of a line: "text"
bool quoted(const std::string& line, std::string& out) {
    size_t a = line.find('"'), b = line.rfind('"');
    if (a == std::string::npos || b <= a) return false;
    out = line.substr(a + 1, b - a - 1);
    return true;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::string resourceText(const wstr& code) {
    wstr name = L"LANG_" + code;
    for (wchar_t& c : name) c = (wchar_t)towupper(c);
    return loadResourceText(name.c_str());
}

bool writable(const wstr& dir) {
    CreateDirectoryW(dir.c_str(), nullptr);
    wstr probe = dir + L"\\.probe";
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

}  // namespace

bool parsePack(const std::string& textIn, Pack& out) {
    std::string text = textIn;
    if (text.size() >= 3 && (uint8_t)text[0] == 0xEF && (uint8_t)text[1] == 0xBB && (uint8_t)text[2] == 0xBF) text = text.substr(3);
    std::string id, str;
    int mode = 0;  // 1 reading msgid, 2 reading msgstr
    auto flush = [&] {
        if (mode == 2 && !id.empty() && !str.empty()) out.entries.push_back({unescape(id), unescape(str)});
        id.clear();
        str.clear();
        mode = 0;
    };
    for (size_t a = 0; a <= text.size();) {
        size_t b = text.find('\n', a);
        if (b == std::string::npos) b = text.size();
        std::string line = trim(text.substr(a, b - a));
        a = b + 1;
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '@') {
            flush();
            size_t sp = line.find_first_of(" \t");
            std::string key = line.substr(1, sp == std::string::npos ? std::string::npos : sp - 1);
            std::string val = sp == std::string::npos ? std::string() : trim(line.substr(sp));
            if (key == "code") out.code = utf8ToWide(val);
            else if (key == "name") out.name = utf8ToWide(val);
            else if (key == "rtl") out.rtl = val == "yes" || val == "1" || val == "true";
            continue;
        }
        std::string q;
        if (line.rfind("msgid", 0) == 0) {
            flush();
            if (quoted(line, q)) id = q;
            mode = 1;
        } else if (line.rfind("msgstr", 0) == 0) {
            if (quoted(line, q)) str = q;
            mode = 2;
        } else if (line[0] == '"' && quoted(line, q)) {
            (mode == 1 ? id : str) += q;  // a continued line
        }
    }
    flush();
    return !out.code.empty();
}

wstr userFolder() {
    wstr local = g_exeDir + L"\\lang";
    if (writable(local)) return local;
    wchar_t app[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", app, MAX_PATH);
    wstr base = n ? wstr(app) + L"\\PATio Boot Doctor" : g_exeDir;
    CreateDirectoryW(base.c_str(), nullptr);
    CreateDirectoryW((base + L"\\lang").c_str(), nullptr);
    return base + L"\\lang";
}

static void scanFolder(const wstr& dir, std::vector<LangInfo>& out) {
    WIN32_FIND_DATAW fd{};
    HANDLE f = FindFirstFileW((dir + L"\\*.lang").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        std::string text;
        Pack p;
        wstr path = dir + L"\\" + fd.cFileName;
        if (!readFileText(path, text, 8u << 20) || !parsePack(text, p)) continue;
        LangInfo li;
        li.code = toLower(p.code);
        li.name = p.name.empty() ? p.code : p.name;
        li.file = path;
        li.rtl = p.rtl;
        li.strings = p.entries.size();
        bool replaced = false;
        for (LangInfo& x : out)
            if (iequals(x.code, li.code)) {
                x = li;  // a pack in the folder overrides the built-in one
                replaced = true;
            }
        if (!replaced) out.push_back(li);
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

void init(const wstr& exeDir) {
    g_exeDir = exeDir;
    while (!g_exeDir.empty() && g_exeDir.back() == L'\\') g_exeDir.pop_back();
    g_langs.clear();
    LangInfo en;
    en.code = L"en";
    en.name = L"English";
    en.builtIn = true;
    g_langs.push_back(en);
    for (const wchar_t* c : kBuiltIn) {
        if (!wcscmp(c, L"en")) continue;
        std::string text = resourceText(c);
        Pack p;
        if (text.empty() || !parsePack(text, p)) continue;
        LangInfo li;
        li.code = c;
        li.name = p.name;
        li.rtl = p.rtl;
        li.builtIn = true;
        li.strings = p.entries.size();
        g_langs.push_back(li);
    }
    scanFolder(g_exeDir + L"\\lang", g_langs);
    wstr uf = userFolder();
    if (!iequals(uf, g_exeDir + L"\\lang")) scanFolder(uf, g_langs);
}

std::vector<LangInfo> languages() { return g_langs; }
const wstr& current() { return g_code; }
bool rtl() { return g_rtl; }

bool setLanguage(const wstr& codeIn) {
    wstr code = toLower(codeIn);
    std::unordered_map<wstr, wstr> map;
    bool isRtl = false;
    const LangInfo* li = nullptr;
    for (const LangInfo& x : g_langs)
        if (x.code == code) li = &x;
    if (!li) return false;
    if (code != L"en" || !li->file.empty()) {  // English is built in; an en.lang next to the exe can still reword it
        std::string text;
        if (li->file.empty()) text = resourceText(code);
        else readFileText(li->file, text, 8u << 20);
        Pack p;
        if (!parsePack(text, p)) return false;
        // A user pack for a built-in language only overrides the texts it has
        if (!li->file.empty() && li->builtIn == false) {
            std::string base = resourceText(code);
            Pack b;
            if (!base.empty() && parsePack(base, b))
                for (auto& e : b.entries) map[e.first] = e.second;
        }
        for (auto& e : p.entries) map[e.first] = e.second;
        isRtl = p.rtl;
    }
    std::lock_guard<std::mutex> l(g_lock);
    g_map.swap(map);
    g_code = code;
    g_rtl = isRtl;
    return true;
}

const wstr& tr(const wstr& english) {
    std::lock_guard<std::mutex> l(g_lock);
    auto it = g_map.find(english);
    if (it != g_map.end()) return it->second;
    auto f = g_fallback.find(english);
    if (f != g_fallback.end()) return f->second;
    return g_fallback.emplace(english, english).first->second;
}

const wstr& tr(const wchar_t* english) { return tr(wstr(english ? english : L"")); }

wstr trf(const wchar_t* english, std::initializer_list<wstr> args) {
    wstr s = tr(english);
    size_t i = 0;
    for (const wstr& a : args) {
        wstr key = L"{" + std::to_wstring(i++) + L"}";
        for (size_t p = s.find(key); p != wstr::npos; p = s.find(key, p + a.size())) s.replace(p, key.size(), a);
    }
    return s;
}

bool importPack(const wstr& file, wstr& code, wstr& err) {
    std::string text;
    Pack p;
    if (!readFileText(file, text, 8u << 20)) {
        err = L"The file could not be read.";
        return false;
    }
    if (!parsePack(text, p)) {
        err = L"This is not a language pack: it has no \"@code\" line. Start from a template (Settings menu > Make a template for a new language).";
        return false;
    }
    if (p.entries.empty()) {
        err = L"The language pack has no translated texts yet.";
        return false;
    }
    code = toLower(p.code);
    wstr dest = userFolder() + L"\\" + code + L".lang";
    if (!iequals(dest, file) && !CopyFileW(file.c_str(), dest.c_str(), FALSE)) {
        err = L"The pack could not be copied to " + dest + L".";
        return false;
    }
    init(g_exeDir);
    return true;
}

// Every text in the program: the English pack, extracted from the source when it is built (resource LANG_TEMPLATE).
bool writeTemplate(const wstr& path, const wstr& code, const wstr& name, bool isRtl, wstr& err) {
    std::string keys = loadResourceText(L"LANG_TEMPLATE");
    Pack all;
    parsePack(keys, all);
    std::string out = "# PATio Boot Doctor language pack\n"
                      "# Translate each msgstr. Keep {0}, {1}, ... where they are (they are filled in by the program),\n"
                      "# and keep \\n (a new line). Empty msgstr lines stay in English.\n\n";
    out += "@code " + wideToUtf8(code) + "\n@name " + wideToUtf8(name) + "\n@rtl " + (isRtl ? "yes" : "no") + "\n\n";
    for (auto& e : all.entries) out += "msgid \"" + escape(e.first) + "\"\nmsgstr \"\"\n\n";
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        err = L"The file could not be written.";
        return false;
    }
    DWORD wr = 0;
    std::string bom = "\xEF\xBB\xBF";
    WriteFile(h, bom.data(), 3, &wr, nullptr);
    WriteFile(h, out.data(), (DWORD)out.size(), &wr, nullptr);
    CloseHandle(h);
    return true;
}

}  // namespace i18n

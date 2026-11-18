#include "Update.h"
#include "Lang.h"
#include "Diagnose.h"
#include "core/Platform.h"

namespace bd {

const wchar_t* const kDefaultUpdateUrl = L"https://www.muspc.com/updates/patio-boot-doctor.txt";

int compareVersions(const wstr& a, const wstr& b) {
    auto parts = [](const wstr& v) {
        std::vector<int> p;
        int cur = 0;
        bool any = false;
        for (wchar_t c : v) {
            if (c >= L'0' && c <= L'9') {
                cur = cur * 10 + (c - L'0');
                any = true;
            } else if (c == L'.') {
                p.push_back(cur);
                cur = 0;
                any = false;
            } else break;
        }
        if (any || !p.empty()) p.push_back(cur);
        while (p.size() < 4) p.push_back(0);
        return p;
    };
    std::vector<int> x = parts(a), y = parts(b);
    for (size_t i = 0; i < 4; ++i)
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

UpdateInfo parseUpdateFile(const std::string& text) {
    UpdateInfo u;
    wstr t = utf8ToWide(text);
    size_t a = 0;
    while (a < t.size()) {
        size_t b = t.find(L'\n', a);
        if (b == wstr::npos) b = t.size();
        wstr line = t.substr(a, b - a);
        a = b + 1;
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();
        size_t eq = line.find(L'=');
        if (eq == wstr::npos) continue;
        wstr k = toLower(line.substr(0, eq)), v = line.substr(eq + 1);
        if (k == L"version") u.version = v;
        else if (k == L"url") u.url = v;
        else if (k == L"sha256") u.sha256 = toLower(v);
        else if (k == L"notes") u.notes = v;
        else if (k == L"sig") u.sig = toLower(v);
    }
    u.ok = !u.version.empty() && u.url.rfind(L"https://", 0) == 0 && u.sha256.size() == 64;
    if (!u.ok) u.error = T(L"The update file is not complete (it needs version, an https:// url and a sha256 checksum).");
    else if (updateKeyBuiltIn() && !verifyChecksum(u.sha256, u.sig)) {
        u.ok = false;
        u.error = T(L"The update file is not signed by MusPC (or was changed on the way), so it was not used.");
    }
    return u;
}

// ---------------------------------------------------------------- HTTPS (winhttp.dll, loaded at run time)
namespace {
typedef void* HINT;
typedef HINT(WINAPI* OpenFn)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
typedef HINT(WINAPI* ConnectFn)(HINT, LPCWSTR, WORD, DWORD);
typedef HINT(WINAPI* OpenReqFn)(HINT, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD);
typedef BOOL(WINAPI* SendFn)(HINT, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
typedef BOOL(WINAPI* RecvFn)(HINT, LPVOID);
typedef BOOL(WINAPI* QueryFn)(HINT, DWORD, LPCWSTR, LPVOID, LPDWORD, LPDWORD);
typedef BOOL(WINAPI* ReadFn2)(HINT, LPVOID, DWORD, LPDWORD);
typedef BOOL(WINAPI* CloseFn)(HINT);
typedef BOOL(WINAPI* TimeoutsFn)(HINT, int, int, int, int);

bool httpsGet(const wstr& url, std::string& out, uint64_t maxBytes, const std::function<void(float)>& progress, volatile bool* cancel, wstr& err) {
    HMODULE m = systemDll(L"winhttp.dll");
    if (!m) {
        err = T(L"This system has no internet support (winhttp.dll). Download the new version on another PC and use \"Update from a file\".");
        return false;
    }
    auto open = (OpenFn)GetProcAddress(m, "WinHttpOpen");
    auto connect = (ConnectFn)GetProcAddress(m, "WinHttpConnect");
    auto openReq = (OpenReqFn)GetProcAddress(m, "WinHttpOpenRequest");
    auto send = (SendFn)GetProcAddress(m, "WinHttpSendRequest");
    auto recv = (RecvFn)GetProcAddress(m, "WinHttpReceiveResponse");
    auto query = (QueryFn)GetProcAddress(m, "WinHttpQueryHeaders");
    auto read = (ReadFn2)GetProcAddress(m, "WinHttpReadData");
    auto close = (CloseFn)GetProcAddress(m, "WinHttpCloseHandle");
    auto timeouts = (TimeoutsFn)GetProcAddress(m, "WinHttpSetTimeouts");
    if (!open || !connect || !openReq || !send || !recv || !query || !read || !close) {
        err = T(L"This system's internet support is incomplete.");
        return false;
    }
    if (url.rfind(L"https://", 0) != 0) {
        err = T(L"Only https:// addresses are used.");
        return false;
    }
    wstr rest = url.substr(8);
    size_t slash = rest.find(L'/');
    wstr host = rest.substr(0, slash), path = slash == wstr::npos ? wstr(L"/") : rest.substr(slash);
    WORD port = 443;
    size_t colon = host.find(L':');
    if (colon != wstr::npos) {
        port = (WORD)_wtoi(host.substr(colon + 1).c_str());
        host = host.substr(0, colon);
    }
    // a generic client name: nothing about this PC or its user is sent
    HINT s = open(L"PATioBootDoctor-Update/1", 0 /* default proxy */, nullptr, nullptr, 0);
    if (!s) {
        err = T(L"The internet could not be used (error ") + std::to_wstring(GetLastError()) + L").";
        return false;
    }
    if (timeouts) timeouts(s, 15000, 15000, 30000, 30000);
    HINT c = connect(s, host.c_str(), port, 0);
    HINT r = c ? openReq(c, L"GET", path.c_str(), nullptr, nullptr, nullptr, 0x00800000 /* WINHTTP_FLAG_SECURE */) : nullptr;
    bool ok = r && send(r, nullptr, 0, nullptr, 0, 0, 0) && recv(r, nullptr);
    DWORD status = 0, len = sizeof status, total = 0, tlen = sizeof total;
    if (ok) ok = query(r, 0x20000000 | 19 /* WINHTTP_QUERY_FLAG_NUMBER | STATUS_CODE */, nullptr, &status, &len, nullptr) && status == 200;
    if (!ok && err.empty())
        err = status ? T(L"The server answered with code ") + std::to_wstring(status) + L"."
                     : T(L"The update address could not be reached (error ") + std::to_wstring(GetLastError()) + T(L"). Check the internet connection.");
    if (ok) query(r, 0x20000000 | 5 /* CONTENT_LENGTH */, nullptr, &total, &tlen, nullptr);
    std::vector<char> buf(256 * 1024);
    while (ok) {
        if (cancel && *cancel) {
            ok = false;
            err = T(L"Stopped.");
            break;
        }
        DWORD got = 0;
        if (!read(r, buf.data(), (DWORD)buf.size(), &got)) {
            ok = false;
            err = T(L"The download broke off.");
            break;
        }
        if (!got) break;
        out.append(buf.data(), got);
        if (out.size() > maxBytes) {
            ok = false;
            err = T(L"The download is far bigger than expected, so it was stopped.");
            break;
        }
        if (progress && total) progress((float)out.size() / total);
    }
    if (r) close(r);
    if (c) close(c);
    close(s);
    return ok;
}
}  // namespace

std::string sha256Hex(const std::string& data) {
    typedef LONG(WINAPI * OpenAlgFn)(void**, LPCWSTR, LPCWSTR, ULONG);
    typedef LONG(WINAPI * CloseAlgFn)(void*, ULONG);
    typedef LONG(WINAPI * CreateHashFn)(void*, void**, PUCHAR, ULONG, PUCHAR, ULONG, ULONG);
    typedef LONG(WINAPI * HashDataFn)(void*, PUCHAR, ULONG, ULONG);
    typedef LONG(WINAPI * FinishHashFn)(void*, PUCHAR, ULONG, ULONG);
    typedef LONG(WINAPI * DestroyHashFn)(void*);
    HMODULE m = systemDll(L"bcrypt.dll");
    if (!m) return "";
    auto openA = (OpenAlgFn)GetProcAddress(m, "BCryptOpenAlgorithmProvider");
    auto closeA = (CloseAlgFn)GetProcAddress(m, "BCryptCloseAlgorithmProvider");
    auto create = (CreateHashFn)GetProcAddress(m, "BCryptCreateHash");
    auto hashD = (HashDataFn)GetProcAddress(m, "BCryptHashData");
    auto finish = (FinishHashFn)GetProcAddress(m, "BCryptFinishHash");
    auto destroy = (DestroyHashFn)GetProcAddress(m, "BCryptDestroyHash");
    void* alg = nullptr;
    void* h = nullptr;
    uint8_t d[32];
    if (!openA || openA(&alg, L"SHA256", nullptr, 0) != 0) return "";
    bool ok = create(alg, &h, nullptr, 0, nullptr, 0, 0) == 0;
    for (size_t i = 0; ok && i < data.size(); i += 1 << 24)
        ok = hashD(h, (PUCHAR)data.data() + i, (ULONG)std::min<size_t>(1 << 24, data.size() - i), 0) == 0;
    ok = ok && finish(h, d, 32, 0) == 0;
    if (h) destroy(h);
    closeA(alg, 0);
    if (!ok) return "";
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (uint8_t b : d) {
        out += hex[b >> 4];
        out += hex[b & 15];
    }
    return out;
}

UpdateInfo checkForUpdate(const wstr& updateUrl) {
    UpdateInfo u;
    std::string text;
    wstr err;
    if (!httpsGet(updateUrl, text, 64 * 1024, nullptr, nullptr, err)) {
        u.error = err;
        return u;
    }
    toolLog(L"[update] " + updateUrl + L": " + std::to_wstring(text.size()) + L" bytes");
    return parseUpdateFile(text);
}

bool checkProgramFile(const wstr& path, const wstr& sha256, wstr& version, wstr& err) {
    std::string data;
    if (!readFileText(path, data) || data.size() < 4096) {
        err = T(L"The file could not be read, or it is not a program.");
        return false;
    }
    if (!sha256.empty() && sha256Hex(data) != wideToUtf8(sha256)) {
        err = T(L"The checksum of the file does not match the update file, so it was not used (it may be damaged or changed on the way).");
        return false;
    }
    if (peArch(path) != L"x64") {
        err = T(L"This is not a 64-bit Windows program.");
        return false;
    }
    int ma = 0, mi = 0, bu = 0;
    if (!fileVersion(path, ma, mi, bu)) {
        err = T(L"The file has no version, so it is not a PATio Boot Doctor program.");
        return false;
    }
    version = std::to_wstring(ma) + L"." + std::to_wstring(mi) + L"." + std::to_wstring(bu);
    std::string tag = "PATio Boot Doctor";
    std::wstring wtag = L"PATio Boot Doctor";
    if (data.find(tag) == std::string::npos &&
        std::search(data.begin(), data.end(), (const char*)wtag.data(), (const char*)(wtag.data() + wtag.size())) == data.end()) {
        err = T(L"This is not a PATio Boot Doctor program.");
        return false;
    }
    return true;
}

wstr downloadUpdate(const UpdateInfo& u, const std::function<void(float)>& progress, volatile bool* cancel, wstr& err) {
    std::string data;
    if (!httpsGet(u.url, data, 200ull << 20, progress, cancel, err)) return L"";
    if (sha256Hex(data) != wideToUtf8(u.sha256)) {
        err = T(L"The checksum of the download does not match the update file, so it was not used (it may be damaged or changed on the way).");
        return L"";
    }
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wstr out = wstr(exe) + L".new";
    HANDLE h = CreateFileW(out.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD wr = 0;
    bool ok = h != INVALID_HANDLE_VALUE && WriteFile(h, data.data(), (DWORD)data.size(), &wr, nullptr) && wr == data.size();
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    if (!ok) {
        err = T(L"The new version could not be saved next to this program (is the folder read-only?).");
        DeleteFileW(out.c_str());
        return L"";
    }
    wstr v;
    if (!checkProgramFile(out, u.sha256, v, err)) {
        DeleteFileW(out.c_str());
        return L"";
    }
    return out;
}

bool installUpdate(const wstr& newExe, wstr& err) {
    wchar_t exeBuf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exeBuf, MAX_PATH);
    wstr exe = exeBuf, old = exe + L".old";
    DeleteFileW(old.c_str());
    // A running program can be renamed, not overwritten: this one steps aside, the new one takes its name.
    if (!MoveFileExW(exe.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        err = T(L"This program could not be replaced (error ") + std::to_wstring(GetLastError()) + T(L"). Is its folder read-only?");
        return false;
    }
    if (!CopyFileW(newExe.c_str(), exe.c_str(), FALSE)) {
        DWORD e = GetLastError();
        MoveFileExW(old.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);  // put the old one back
        err = T(L"The new version could not be put in place (error ") + std::to_wstring(e) + T(L"). Nothing changed.");
        return false;
    }
    if (!iequals(newExe, exe) && newExe.size() > 4 && newExe.substr(newExe.size() - 4) == L".new") DeleteFileW(newExe.c_str());
    toolLog(L"[update] installed " + exe);
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    wstr cmd = L"\"" + exe + L"\"";
    std::vector<wchar_t> b(cmd.begin(), cmd.end());
    b.push_back(0);
    if (CreateProcessW(exe.c_str(), b.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    return true;
}

void removeOldVersion() {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    DeleteFileW((wstr(exe) + L".old").c_str());
    DeleteFileW((wstr(exe) + L".new").c_str());
}

// ================================================================== signatures (bcrypt.dll, loaded at run time)
namespace {
typedef LONG(WINAPI* BOpenFn)(void**, LPCWSTR, LPCWSTR, ULONG);
typedef LONG(WINAPI* BCloseFn)(void*, ULONG);
typedef LONG(WINAPI* BGenFn)(void*, void**, ULONG, ULONG);
typedef LONG(WINAPI* BFinFn)(void*, ULONG);
typedef LONG(WINAPI* BExportFn)(void*, void*, LPCWSTR, PUCHAR, ULONG, ULONG*, ULONG);
typedef LONG(WINAPI* BImportFn)(void*, void*, LPCWSTR, void**, PUCHAR, ULONG, ULONG);
typedef LONG(WINAPI* BSignFn)(void*, void*, PUCHAR, ULONG, PUCHAR, ULONG, ULONG*, ULONG);
typedef LONG(WINAPI* BVerifyFn)(void*, void*, PUCHAR, ULONG, PUCHAR, ULONG, ULONG);
typedef LONG(WINAPI* BDestroyFn)(void*);

struct Ecc {
    HMODULE m = systemDll(L"bcrypt.dll");
    BOpenFn open = m ? (BOpenFn)GetProcAddress(m, "BCryptOpenAlgorithmProvider") : nullptr;
    BCloseFn close = m ? (BCloseFn)GetProcAddress(m, "BCryptCloseAlgorithmProvider") : nullptr;
    BGenFn gen = m ? (BGenFn)GetProcAddress(m, "BCryptGenerateKeyPair") : nullptr;
    BFinFn fin = m ? (BFinFn)GetProcAddress(m, "BCryptFinalizeKeyPair") : nullptr;
    BExportFn exp = m ? (BExportFn)GetProcAddress(m, "BCryptExportKey") : nullptr;
    BImportFn imp = m ? (BImportFn)GetProcAddress(m, "BCryptImportKeyPair") : nullptr;
    BSignFn sign = m ? (BSignFn)GetProcAddress(m, "BCryptSignHash") : nullptr;
    BVerifyFn verify = m ? (BVerifyFn)GetProcAddress(m, "BCryptVerifySignature") : nullptr;
    BDestroyFn destroy = m ? (BDestroyFn)GetProcAddress(m, "BCryptDestroyKey") : nullptr;
    bool ok() const { return open && close && gen && fin && exp && imp && sign && verify && destroy; }
};

std::string toHex(const std::string& b) {
    static const char* hex = "0123456789abcdef";
    std::string o;
    for (unsigned char c : b) {
        o += hex[c >> 4];
        o += hex[c & 15];
    }
    return o;
}

std::string fromHex(const std::string& h) {
    std::string o;
    auto v = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    for (size_t i = 0; i + 1 < h.size(); i += 2) {
        int a = v(h[i]), b = v(h[i + 1]);
        if (a < 0 || b < 0) break;
        o += (char)(a << 4 | b);
    }
    return o;
}

std::string exportKey(Ecc& e, void* key, const wchar_t* type) {
    ULONG n = 0;
    if (e.exp(key, nullptr, type, nullptr, 0, &n, 0) != 0) return "";
    std::string b(n, '\0');
    return e.exp(key, nullptr, type, (PUCHAR)&b[0], n, &n, 0) == 0 ? b : std::string();
}

std::string builtInKey() {
    HRSRC r = FindResourceW(nullptr, L"UPDATE_PUBKEY", MAKEINTRESOURCEW(10));
    HGLOBAL g = r ? LoadResource(nullptr, r) : nullptr;
    const char* p = g ? (const char*)LockResource(g) : nullptr;
    std::string s = p ? std::string(p, SizeofResource(nullptr, r)) : std::string();
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s.size() >= 100 ? s : std::string();  // "none": no key built in
}
}  // namespace

bool updateKeyBuiltIn() { return !builtInKey().empty(); }

bool makeUpdateKey(const wstr& privateFile, const wstr& publicFile, wstr& err) {
    Ecc e;
    void* alg = nullptr;
    void* key = nullptr;
    if (!e.ok() || e.open(&alg, L"ECDSA_P256", nullptr, 0) != 0) {
        err = L"ECDSA is not available on this system.";
        return false;
    }
    bool ok = e.gen(alg, &key, 256, 0) == 0 && e.fin(key, 0) == 0;
    std::string priv = ok ? toHex(exportKey(e, key, L"ECCPRIVATEBLOB")) : std::string(), pub = ok ? toHex(exportKey(e, key, L"ECCPUBLICBLOB")) : std::string();
    if (key) e.destroy(key);
    e.close(alg, 0);
    auto put = [](const wstr& f, const std::string& d) {
        HANDLE h = CreateFileW(f.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);  // never overwrite a key
        if (h == INVALID_HANDLE_VALUE) return false;
        DWORD wr = 0;
        bool w = WriteFile(h, d.data(), (DWORD)d.size(), &wr, nullptr) && wr == d.size();
        CloseHandle(h);
        return w;
    };
    if (priv.empty() || pub.empty()) {
        err = L"The key could not be made.";
        return false;
    }
    if (!put(privateFile, priv)) {
        err = L"The private key file exists already or cannot be written: " + privateFile;
        return false;
    }
    DeleteFileW(publicFile.c_str());
    if (!put(publicFile, pub)) {
        err = L"The public key file cannot be written: " + publicFile;
        return false;
    }
    return true;
}

wstr signChecksum(const wstr& sha256, const wstr& privateFile, wstr& err) {
    Ecc e;
    std::string hex, hash = fromHex(wideToUtf8(sha256));
    if (!readFileText(privateFile, hex) || hash.size() != 32) {
        err = L"The private key file could not be read, or the checksum is not 64 hex digits.";
        return L"";
    }
    std::string blob = fromHex(hex);
    void* alg = nullptr;
    void* key = nullptr;
    std::string sig(64, '\0');
    ULONG n = 0;
    bool ok = e.ok() && e.open(&alg, L"ECDSA_P256", nullptr, 0) == 0 && e.imp(alg, nullptr, L"ECCPRIVATEBLOB", &key, (PUCHAR)blob.data(), (ULONG)blob.size(), 0) == 0 &&
              e.sign(key, nullptr, (PUCHAR)hash.data(), 32, (PUCHAR)&sig[0], 64, &n, 0) == 0 && n == 64;
    if (key) e.destroy(key);
    if (alg) e.close(alg, 0);
    if (!ok) {
        err = L"The checksum could not be signed (is this the right private key file?).";
        return L"";
    }
    return utf8ToWide(toHex(sig));
}

bool verifyChecksum(const wstr& sha256, const wstr& sigHex, const std::string& publicKey) {
    Ecc e;
    std::string hash = fromHex(wideToUtf8(sha256)), sig = fromHex(wideToUtf8(sigHex)), blob = fromHex(publicKey);
    if (!e.ok() || hash.size() != 32 || sig.size() != 64 || blob.size() < 72) return false;
    void* alg = nullptr;
    void* key = nullptr;
    bool ok = e.open(&alg, L"ECDSA_P256", nullptr, 0) == 0 && e.imp(alg, nullptr, L"ECCPUBLICBLOB", &key, (PUCHAR)blob.data(), (ULONG)blob.size(), 0) == 0 &&
              e.verify(key, nullptr, (PUCHAR)hash.data(), 32, (PUCHAR)sig.data(), 64, 0) == 0;
    if (key) e.destroy(key);
    if (alg) e.close(alg, 0);
    return ok;
}

bool verifyChecksum(const wstr& sha256, const wstr& sig) { return verifyChecksum(sha256, sig, builtInKey()); }

bool writeUpdateFile(const wstr& exe, const wstr& privateFile, const wstr& url, const wstr& notes, wstr& out, wstr& err) {
    std::string data;
    wstr version;
    if (!readFileText(exe, data) || !checkProgramFile(exe, L"", version, err)) {
        if (err.empty()) err = L"The program file could not be read.";
        return false;
    }
    wstr sha = utf8ToWide(sha256Hex(data));
    wstr sig = signChecksum(sha, privateFile, err);
    if (sig.empty()) return false;
    wstr text = L"version=" + version + L"\r\nurl=" + url + L"\r\nsha256=" + sha + L"\r\nsig=" + sig + L"\r\nnotes=" + notes + L"\r\n";
    out = exe + L".update.txt";
    std::string u = wideToUtf8(text);
    HANDLE h = CreateFileW(out.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD wr = 0;
    bool ok = h != INVALID_HANDLE_VALUE && WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    if (!ok) err = L"The update file could not be written: " + out;
    return ok;
}

}  // namespace bd

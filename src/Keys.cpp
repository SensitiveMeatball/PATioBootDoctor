#include "Keys.h"
#include "Lang.h"
#include "core/Platform.h"
#include "core/Settings.h"
#include <wincrypt.h>

namespace bd {

wstr keyDigits(const wstr& s) {
    wstr d;
    for (wchar_t c : s)
        if (c >= L'0' && c <= L'9') d += c;
    return d;
}

wstr formatRecoveryKey(const wstr& digits) {
    wstr d = keyDigits(digits), out;
    for (size_t i = 0; i < d.size(); ++i) {
        if (i && i % 6 == 0) out += L'-';
        out += d[i];
    }
    return out;
}

bool validRecoveryKey(const wstr& s) {
    wstr d = keyDigits(s);
    if (d.size() != 48) return false;
    for (size_t g = 0; g < 8; ++g) {
        long v = wcstol(d.substr(g * 6, 6).c_str(), nullptr, 10);
        if (v % 11 || v >= 720896) return false;
    }
    return true;
}

wstr keyInputFormat(const wstr& before, const wstr& after, int& caret) {
    // Digits in front of the caret decide where it goes after reformatting.
    caret = std::max(0, std::min(caret, (int)after.size()));
    wstr a = after;
    size_t dBefore = keyDigits(before).size(), dAfter = keyDigits(a).size();
    // Backspace over a dash removes nothing that counts: remove the digit in front of it instead.
    if (a.size() + 1 == before.size() && dAfter == dBefore && caret > 0) {
        int k = caret - 1;
        while (k >= 0 && !(a[k] >= L'0' && a[k] <= L'9')) --k;
        if (k >= 0) {
            a.erase((size_t)k, 1);
            caret = k;
        }
    }
    int digitsBeforeCaret = 0;
    for (int i = 0; i < caret && i < (int)a.size(); ++i)
        if (a[i] >= L'0' && a[i] <= L'9') ++digitsBeforeCaret;
    wstr d = keyDigits(a);
    if (d.size() > 48) d.resize(48);
    digitsBeforeCaret = std::min(digitsBeforeCaret, (int)d.size());
    wstr out = formatRecoveryKey(d);
    // caret after the n-th digit (and after a dash that follows a full group, so typing goes on smoothly)
    int pos = 0, seen = 0;
    while (pos < (int)out.size() && seen < digitsBeforeCaret) {
        if (out[pos] >= L'0' && out[pos] <= L'9') ++seen;
        ++pos;
    }
    caret = pos;
    return out;
}

// ---------------------------------------------------------------- DPAPI (crypt32, loaded at run time)
namespace {
typedef BOOL(WINAPI* ProtectFn)(DATA_BLOB*, LPCWSTR, DATA_BLOB*, PVOID, PVOID, DWORD, DATA_BLOB*);
typedef BOOL(WINAPI* UnprotectFn)(DATA_BLOB*, LPWSTR*, DATA_BLOB*, PVOID, PVOID, DWORD, DATA_BLOB*);
HMODULE crypt32() {
    static HMODULE m = systemDll(L"crypt32.dll");
    return m;
}
const char kEntropy[] = "PATio Boot Doctor recovery keys";
}  // namespace

bool protectBytes(const std::string& in, std::string& out) {
    auto fn = crypt32() ? (ProtectFn)GetProcAddress(crypt32(), "CryptProtectData") : nullptr;
    if (!fn) return false;
    DATA_BLOB a{(DWORD)in.size(), (BYTE*)in.data()}, e{(DWORD)sizeof kEntropy, (BYTE*)kEntropy}, o{};
    if (!fn(&a, L"PATio Boot Doctor", &e, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &o)) return false;
    out.assign((const char*)o.pbData, o.cbData);
    LocalFree(o.pbData);
    return true;
}

bool unprotectBytes(const std::string& in, std::string& out) {
    auto fn = crypt32() ? (UnprotectFn)GetProcAddress(crypt32(), "CryptUnprotectData") : nullptr;
    if (!fn) return false;
    DATA_BLOB a{(DWORD)in.size(), (BYTE*)in.data()}, e{(DWORD)sizeof kEntropy, (BYTE*)kEntropy}, o{};
    if (!fn(&a, nullptr, &e, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &o)) return false;
    out.assign((const char*)o.pbData, o.cbData);
    SecureZeroMemory(o.pbData, o.cbData);
    LocalFree(o.pbData);
    return true;
}

// ---------------------------------------------------------------- password encryption (bcrypt.dll, loaded at run time)
namespace {
typedef LONG(WINAPI* OpenAlgFn)(void**, LPCWSTR, LPCWSTR, ULONG);
typedef LONG(WINAPI* CloseAlgFn)(void*, ULONG);
typedef LONG(WINAPI* SetPropFn)(void*, LPCWSTR, PUCHAR, ULONG, ULONG);
typedef LONG(WINAPI* GenKeyFn)(void*, void**, PUCHAR, ULONG, PUCHAR, ULONG, ULONG);
typedef LONG(WINAPI* DestroyKeyFn)(void*);
typedef LONG(WINAPI* CryptFn)(void*, PUCHAR, ULONG, void*, PUCHAR, ULONG, PUCHAR, ULONG, ULONG*, ULONG);
typedef LONG(WINAPI* Pbkdf2Fn)(void*, PUCHAR, ULONG, PUCHAR, ULONG, ULONGLONG, PUCHAR, ULONG, ULONG);
typedef LONG(WINAPI* RandFn)(void*, PUCHAR, ULONG, ULONG);
typedef LONG(WINAPI* CreateHashFn)(void*, void**, PUCHAR, ULONG, PUCHAR, ULONG, ULONG);
typedef LONG(WINAPI* HashDataFn)(void*, PUCHAR, ULONG, ULONG);
typedef LONG(WINAPI* FinishHashFn)(void*, PUCHAR, ULONG, ULONG);
typedef LONG(WINAPI* DestroyHashFn)(void*);

struct Cng {
    HMODULE m = systemDll(L"bcrypt.dll");
    OpenAlgFn open = m ? (OpenAlgFn)GetProcAddress(m, "BCryptOpenAlgorithmProvider") : nullptr;
    CloseAlgFn close = m ? (CloseAlgFn)GetProcAddress(m, "BCryptCloseAlgorithmProvider") : nullptr;
    SetPropFn setProp = m ? (SetPropFn)GetProcAddress(m, "BCryptSetProperty") : nullptr;
    GenKeyFn genKey = m ? (GenKeyFn)GetProcAddress(m, "BCryptGenerateSymmetricKey") : nullptr;
    DestroyKeyFn destroyKey = m ? (DestroyKeyFn)GetProcAddress(m, "BCryptDestroyKey") : nullptr;
    CryptFn enc = m ? (CryptFn)GetProcAddress(m, "BCryptEncrypt") : nullptr;
    CryptFn dec = m ? (CryptFn)GetProcAddress(m, "BCryptDecrypt") : nullptr;
    Pbkdf2Fn pbkdf2 = m ? (Pbkdf2Fn)GetProcAddress(m, "BCryptDeriveKeyPBKDF2") : nullptr;
    RandFn rand = m ? (RandFn)GetProcAddress(m, "BCryptGenRandom") : nullptr;
    CreateHashFn createHash = m ? (CreateHashFn)GetProcAddress(m, "BCryptCreateHash") : nullptr;
    HashDataFn hashData = m ? (HashDataFn)GetProcAddress(m, "BCryptHashData") : nullptr;
    FinishHashFn finishHash = m ? (FinishHashFn)GetProcAddress(m, "BCryptFinishHash") : nullptr;
    DestroyHashFn destroyHash = m ? (DestroyHashFn)GetProcAddress(m, "BCryptDestroyHash") : nullptr;
    bool ok() const { return open && close && setProp && genKey && destroyKey && enc && dec && pbkdf2 && rand && createHash && hashData && finishHash && destroyHash; }
};
Cng& cng() {
    static Cng c;
    return c;
}
const ULONG kAllocHmac = 0x00000008;       // BCRYPT_ALG_HANDLE_HMAC_FLAG
const ULONG kPadBlock = 0x00000001;        // BCRYPT_BLOCK_PADDING
const ULONG kSystemRng = 0x00000002;       // BCRYPT_USE_SYSTEM_PREFERRED_RNG
const char kMagic[8] = {'P', 'B', 'D', 'K', 'E', 'Y', 'S', '1'};

bool derive(const wstr& password, const std::string& salt, uint32_t iter, uint8_t out[64]) {
    Cng& c = cng();
    void* prf = nullptr;
    if (c.open(&prf, L"SHA256", nullptr, kAllocHmac) != 0) return false;
    std::string pw = wideToUtf8(password);
    LONG st = c.pbkdf2(prf, (PUCHAR)pw.data(), (ULONG)pw.size(), (PUCHAR)salt.data(), (ULONG)salt.size(), iter, out, 64, 0);
    SecureZeroMemory(&pw[0], pw.size());
    c.close(prf, 0);
    return st == 0;
}

bool hmac(const uint8_t* key, const std::string& data, uint8_t out[32]) {
    Cng& c = cng();
    void* alg = nullptr;
    void* h = nullptr;
    if (c.open(&alg, L"SHA256", nullptr, kAllocHmac) != 0) return false;
    bool ok = c.createHash(alg, &h, nullptr, 0, (PUCHAR)key, 32, 0) == 0 && c.hashData(h, (PUCHAR)data.data(), (ULONG)data.size(), 0) == 0 &&
              c.finishHash(h, out, 32, 0) == 0;
    if (h) c.destroyHash(h);
    c.close(alg, 0);
    return ok;
}

bool aes(bool encrypt, const uint8_t* key, const std::string& iv, const std::string& in, std::string& out) {
    Cng& c = cng();
    void* alg = nullptr;
    void* k = nullptr;
    if (c.open(&alg, L"AES", nullptr, 0) != 0) return false;
    const wchar_t* mode = L"ChainingModeCBC";
    bool ok = c.setProp(alg, L"ChainingMode", (PUCHAR)mode, (ULONG)((wcslen(mode) + 1) * sizeof(wchar_t)), 0) == 0 && c.genKey(alg, &k, nullptr, 0, (PUCHAR)key, 32, 0) == 0;
    if (ok) {
        std::string ivc = iv;
        ULONG n = 0;
        auto fn = encrypt ? c.enc : c.dec;
        ok = fn(k, (PUCHAR)in.data(), (ULONG)in.size(), nullptr, (PUCHAR)&ivc[0], (ULONG)ivc.size(), nullptr, 0, &n, kPadBlock) == 0;
        if (ok) {
            out.assign(n, '\0');
            ivc = iv;
            ok = fn(k, (PUCHAR)in.data(), (ULONG)in.size(), nullptr, (PUCHAR)&ivc[0], (ULONG)ivc.size(), (PUCHAR)&out[0], n, &n, kPadBlock) == 0;
            out.resize(n);
        }
    }
    if (k) c.destroyKey(k);
    c.close(alg, 0);
    return ok;
}

uint32_t rd32(const std::string& s, size_t o) { return (uint8_t)s[o] | (uint8_t)s[o + 1] << 8 | (uint8_t)s[o + 2] << 16 | (uint32_t)(uint8_t)s[o + 3] << 24; }
}  // namespace

bool encryptBlob(const std::string& plain, const wstr& password, std::string& out, uint32_t iter) {
    Cng& c = cng();
    if (!c.ok() || password.empty()) return false;
    std::string salt(16, '\0'), iv(16, '\0');
    if (c.rand(nullptr, (PUCHAR)&salt[0], 16, kSystemRng) != 0 || c.rand(nullptr, (PUCHAR)&iv[0], 16, kSystemRng) != 0) return false;
    uint8_t k[64];
    if (!derive(password, salt, iter, k)) return false;
    std::string ct;
    bool ok = aes(true, k, iv, plain, ct);
    if (ok) {
        out.assign(kMagic, 8);
        out += salt;
        for (int i = 0; i < 4; ++i) out += (char)(iter >> (8 * i));
        out += iv;
        out += ct;
        uint8_t mac[32];
        ok = hmac(k + 32, out, mac);
        out.append((const char*)mac, 32);
    }
    SecureZeroMemory(k, sizeof k);
    return ok;
}

bool decryptBlob(const std::string& blob, const wstr& password, std::string& out) {
    if (!cng().ok() || blob.size() < 8 + 16 + 4 + 16 + 16 + 32 || memcmp(blob.data(), kMagic, 8)) return false;
    std::string salt = blob.substr(8, 16), iv = blob.substr(28, 16);
    uint32_t iter = rd32(blob, 24);
    if (iter < 1000 || iter > 10000000) return false;
    uint8_t k[64], mac[32];
    if (!derive(password, salt, iter, k)) return false;
    std::string body = blob.substr(0, blob.size() - 32);
    bool ok = hmac(k + 32, body, mac);
    // compare without stopping at the first difference
    uint8_t diff = 0;
    for (int i = 0; i < 32; ++i) diff |= mac[i] ^ (uint8_t)blob[blob.size() - 32 + i];
    ok = ok && diff == 0 && aes(false, k, iv, body.substr(44), out);
    SecureZeroMemory(k, sizeof k);
    return ok;
}

// ---------------------------------------------------------------- the store
static wstr stickRoot() {
    // Running from the rescue stick: the program sits in memory (X:); the stick is found by its marker file.
    if (!(g_sys.winPE && g_sys.exeDir.size() >= 2 && towupper(g_sys.exeDir[0]) == L'X')) return L"";
    DWORD drives = GetLogicalDrives();
    for (wchar_t c = L'C'; c <= L'Z'; ++c)
        if ((drives & (1u << (c - L'A'))) && c != L'X') {
            wstr root = wstr(1, c) + L":\\";
            if (GetFileAttributesW((root + L"PATioBootDoctor.stick").c_str()) != INVALID_FILE_ATTRIBUTES) return root;
        }
    return L"";
}

// Everything this program keeps (saved keys, settings, notes, job sheets):
// <PATioAppData>\PATioBootDoctorData (see patioAppData in PatKit). Folders of earlier versions are moved in.
wstr dataFolder() {
    static wstr cached;
    if (!cached.empty()) return cached;
    wstr base = patioAppData(), dir = base + L"\\PATioBootDoctorData";
    auto there = [](const wstr& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; };
    if (!there(dir)) {
        wstr top = base.substr(0, base.find_last_of(L'\\'));  // the stick's root, or the suite folder
        for (const wstr& old : {top + L"\\PATioBootDoctorData", top + L"\\PATioBootDoctor", g_sys.exeDir + L"\\PATioBootDoctorData"})
            if (there(old) && !there(dir) && !iequals(old, dir)) MoveFileW(old.c_str(), dir.c_str());
    }
    CreateDirectoryW(dir.c_str(), nullptr);
    for (const wchar_t* f : {L"bitlocker-keys.dat", L"PATioBootDoctor notes.txt", L"PATioBootDoctor.ini"}) {  // files of the earliest versions
        wstr from = g_sys.exeDir + L"\\" + f, to = dir + L"\\" + f;
        if (there(from) && !there(to)) MoveFileW(from.c_str(), to.c_str());
    }
    cached = dir;
    return dir;
}

wstr rescueStickRoot() { return stickRoot(); }

// Running from the rescue stick: its data folder, else empty.
wstr stickFolder() { return stickRoot().empty() ? wstr() : dataFolder(); }

wstr keyStorePath() { return dataFolder() + L"\\bitlocker-keys.dat"; }

wstr thisPcName() {
    if (g_sys.winPE) return L"";
    wchar_t b[256] = {};
    DWORD n = 256;
    return GetComputerNameExW(ComputerNamePhysicalDnsHostname, b, &n) ? wstr(b) : wstr();
}

static wstr oldStorePath() {
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    return n && n < MAX_PATH ? wstr(buf) + L"\\PATio Boot Doctor\\bitlocker-keys.dat" : wstr();
}

static bool readAll(const wstr& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    out.resize((size_t)std::min<LONGLONG>(sz.QuadPart, 1 << 20));
    DWORD rd = 0;
    bool ok = out.empty() || ReadFile(h, &out[0], (DWORD)out.size(), &rd, nullptr);
    CloseHandle(h);
    out.resize(rd);
    return ok;
}

static bool writeAll(const wstr& path, const std::string& data) {
    wstr tmp = path + L".new";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    bool ok = WriteFile(h, data.data(), (DWORD)data.size(), &wr, nullptr) && wr == data.size();
    FlushFileBuffers(h);
    CloseHandle(h);
    // replace in one step, so a power cut never leaves half a file
    ok = ok && MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) DeleteFileW(tmp.c_str());
    return ok;
}

static std::vector<SavedKey> parseKeys(const std::string& plainIn) {
    std::vector<SavedKey> out;
    wstr text = utf8ToWide(plainIn);
    size_t a = 0;
    while (a < text.size()) {
        size_t b = text.find(L'\n', a);
        if (b == wstr::npos) b = text.size();
        wstr line = text.substr(a, b - a);
        a = b + 1;
        std::vector<wstr> f;
        size_t p = 0;
        for (;;) {
            size_t q = line.find(L'\t', p);
            f.push_back(line.substr(p, q == wstr::npos ? wstr::npos : q - p));
            if (q == wstr::npos) break;
            p = q + 1;
        }
        if (f.size() >= 4 && validRecoveryKey(f[3])) {
            SavedKey k;
            k.id = f[0];
            k.name = f[1];
            k.when = f[2];
            k.key = formatRecoveryKey(f[3]);
            if (f.size() > 4) k.pc = f[4];
            if (f.size() > 5) k.owner = f[5];
            if (f.size() > 6) k.archived = f[6] == L"1";
            if (f.size() > 7) k.model = f[7];
            out.push_back(k);
        }
    }
    SecureZeroMemory(&text[0], text.size() * sizeof(wchar_t));
    return out;
}

bool keyStoreExists() { return GetFileAttributesW(keyStorePath().c_str()) != INVALID_FILE_ATTRIBUTES; }

int loadKeys(const wstr& password, std::vector<SavedKey>& out) { return loadKeysFile(keyStorePath(), password, out); }

int loadKeysFile(const wstr& path, const wstr& password, std::vector<SavedKey>& out) {
    out.clear();
    std::string enc, plain;
    if (!readAll(path, enc)) return KL_NONE;
    if (enc.empty()) return KL_NONE;
    if (!decryptBlob(enc, password, plain)) return memcmp(enc.data(), kMagic, std::min<size_t>(8, enc.size())) ? KL_ERROR : KL_BADPASS;
    out = parseKeys(plain);
    SecureZeroMemory(&plain[0], plain.size());
    return KL_OK;
}

const SavedKey* findKey(const std::vector<SavedKey>& keys, const wstr& id) {
    for (const SavedKey& k : keys)
        if (!id.empty() && k.id == id) return &k;
    return nullptr;
}

static bool storeKeys(const std::vector<SavedKey>& keys, const wstr& password, wstr& err) { return storeKeysFile(keyStorePath(), keys, password, err); }

bool storeKeysFile(const wstr& path, const std::vector<SavedKey>& keys, const wstr& password, wstr& err) {
    if (keys.empty()) {
        DeleteFileW(path.c_str());
        return true;
    }
    wstr text;
    auto clean = [](wstr t) {
        for (wchar_t& c : t)
            if (c == L'\t' || c == L'\n' || c == L'\r') c = L' ';
        return t;
    };
    for (const SavedKey& k : keys)
        text += k.id + L"\t" + clean(k.name) + L"\t" + k.when + L"\t" + k.key + L"\t" + clean(k.pc) + L"\t" + clean(k.owner) + L"\t" + (k.archived ? L"1" : L"0") + L"\t" + clean(k.model) + L"\n";
    std::string plain = wideToUtf8(text), enc;
    bool ok = encryptBlob(plain, password, enc);
    SecureZeroMemory(&plain[0], plain.size());
    SecureZeroMemory(&text[0], text.size() * sizeof(wchar_t));
    if (!ok) {
        err = T(L"The key could not be encrypted on this system, so it was not saved.");
        return false;
    }
    if (!writeAll(path, enc)) {
        err = T(L"The file for saved keys could not be written (is the drive read-only?): ") + path;
        return false;
    }
    return true;
}

bool saveKey(const SavedKey& k, const wstr& password, wstr& err) {
    std::vector<SavedKey> keys;
    int r = loadKeys(password, keys);
    if (r == KL_BADPASS || r == KL_ERROR) {
        err = T(L"The password for the saved keys is not right, so the key was not saved.");
        return false;
    }
    SavedKey n = k;
    for (const SavedKey& x : keys)
        if (x.id == k.id) {
            if (n.owner.empty()) n.owner = x.owner;
            if (n.pc.empty()) n.pc = x.pc;
            if (n.model.empty()) n.model = x.model;
        }
    keys.erase(std::remove_if(keys.begin(), keys.end(), [&](const SavedKey& x) { return x.id == k.id; }), keys.end());
    n.key = formatRecoveryKey(k.key);
    if (n.when.empty()) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        wchar_t b[32];
        swprintf(b, 32, L"%04d-%02d-%02d %02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
        n.when = b;
    }
    keys.push_back(n);
    return storeKeys(keys, password, err);
}

bool deleteKey(const wstr& id, const wstr& password) {
    std::vector<SavedKey> keys;
    if (loadKeys(password, keys) != KL_OK) return false;
    keys.erase(std::remove_if(keys.begin(), keys.end(), [&](const SavedKey& x) { return x.id == id; }), keys.end());
    wstr err;
    return storeKeys(keys, password, err);
}

bool editKeys(const wstr& password, const std::function<void(std::vector<SavedKey>&)>& edit, wstr& err) {
    std::vector<SavedKey> keys;
    if (loadKeys(password, keys) != KL_OK) {
        err = T(L"The saved keys could not be opened with this password.");
        return false;
    }
    edit(keys);
    return storeKeys(keys, password, err);
}

int mergeKeyFiles(const wstr& into, const wstr& from, const wstr& password) {
    std::vector<SavedKey> a, b;
    if (loadKeysFile(into, password, a) != KL_OK || loadKeysFile(from, password, b) != KL_OK) return -1;
    int added = 0;
    for (const SavedKey& x : b) {
        bool have = false;
        for (SavedKey& y : a)
            if (y.id == x.id) {
                have = true;
                if (x.when > y.when) {  // the newer one wins, but a name given before is kept
                    wstr owner = y.owner.empty() ? x.owner : y.owner;
                    y = x;
                    y.owner = owner;
                }
            }
        if (!have) {
            a.push_back(x);
            ++added;
        }
    }
    wstr err;
    return storeKeysFile(into, a, password, err) ? added : -1;
}

bool deleteAllKeys() {
    wstr p = keyStorePath();
    return DeleteFileW(p.c_str()) || GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES;
}

bool changeKeyPassword(const wstr& oldPassword, const wstr& newPassword, wstr& err) {
    std::vector<SavedKey> keys;
    if (loadKeys(oldPassword, keys) != KL_OK) {
        err = T(L"The current password is not right.");
        return false;
    }
    return storeKeys(keys, newPassword, err);
}

int migrateOldKeys(const wstr& password) {
    wstr old = oldStorePath();
    std::string enc, plain;
    if (old.empty() || !readAll(old, enc) || enc.empty() || !unprotectBytes(enc, plain)) return 0;
    std::vector<SavedKey> add = parseKeys(plain);
    SecureZeroMemory(&plain[0], plain.size());
    int n = 0;
    for (const SavedKey& k : add) {
        wstr err;
        if (saveKey(k, password, err)) ++n;
    }
    if (n == (int)add.size()) DeleteFileW(old.c_str());
    return n;
}

}  // namespace bd

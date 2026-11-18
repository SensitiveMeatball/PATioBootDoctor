#include "SecureBoot.h"
#include "Lang.h"
#include <wincrypt.h>

namespace bd {

namespace {

// EFI_CERT_X509_GUID  a5c059a1-94e4-4aa7-87b5-ab155c2bf072  (mixed-endian, as stored)
const uint8_t kX509[16] = {0xA1, 0x59, 0xC0, 0xA5, 0xE4, 0x94, 0xA7, 0x4A, 0x87, 0xB5, 0xAB, 0x15, 0x5C, 0x2B, 0xF0, 0x72};
// EFI_CERT_SHA256_GUID c1c41626-504c-4092-aca9-41f936934328
const uint8_t kSha256[16] = {0x26, 0x16, 0xC4, 0xC1, 0x4C, 0x50, 0x92, 0x40, 0xAC, 0xA9, 0x41, 0xF9, 0x36, 0x93, 0x43, 0x28};

uint32_t rd32(const std::string& s, size_t o) {
    return o + 4 <= s.size() ? (uint8_t)s[o] | ((uint8_t)s[o + 1] << 8) | ((uint8_t)s[o + 2] << 16) | ((uint32_t)(uint8_t)s[o + 3] << 24) : 0;
}

// The common name straight from the DER, when crypt32 is not there: the LAST commonName (the
// subject comes after the issuer). OID 2.5.4.3 = 06 03 55 04 03, then a string tag, length, bytes.
wstr certCommonNameScan(const std::string& d) {
    const uint8_t oid[5] = {0x06, 0x03, 0x55, 0x04, 0x03};
    wstr out;
    for (size_t i = 0; i + 7 < d.size(); ++i) {
        if (memcmp(d.data() + i, oid, 5)) continue;
        size_t p = i + 5;
        uint8_t tag = (uint8_t)d[p++];
        if (tag != 0x0C && tag != 0x13 && tag != 0x16 && tag != 0x14) continue;  // UTF8/Printable/IA5/Teletex
        if (p >= d.size()) break;
        size_t len = (uint8_t)d[p++];
        if (len == 0x81) {
            if (p >= d.size()) break;
            len = (uint8_t)d[p++];
        } else if (len >= 0x80)
            continue;  // too long for a name
        if (p + len > d.size() || len == 0 || len > 128) continue;
        std::string s = d.substr(p, len);
        out = tag == 0x0C ? utf8ToWide(s) : wstr(s.begin(), s.end());  // the last one wins
    }
    return out;
}

}  // namespace

wstr certCommonName(const std::string& der) {
    if (der.size() < 8) return L"";
    HMODULE m = systemDll(L"crypt32.dll");
    if (m) {
        typedef const CERT_CONTEXT*(WINAPI * CreateFn)(DWORD, const BYTE*, DWORD);
        typedef DWORD(WINAPI * NameFn)(const CERT_CONTEXT*, DWORD, DWORD, void*, LPWSTR, DWORD);
        typedef BOOL(WINAPI * FreeFn)(const CERT_CONTEXT*);
        auto create = (CreateFn)GetProcAddress(m, "CertCreateCertificateContext");
        auto getname = (NameFn)GetProcAddress(m, "CertGetNameStringW");
        auto freec = (FreeFn)GetProcAddress(m, "CertFreeCertificateContext");
        if (create && getname && freec) {
            const CERT_CONTEXT* c = create(X509_ASN_ENCODING, (const BYTE*)der.data(), (DWORD)der.size());
            if (c) {
                wchar_t buf[256] = {};
                DWORD n = getname(c, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, buf, 256);
                freec(c);
                if (n > 1) return buf;
            }
        }
    }
    return certCommonNameScan(der);
}

void efiSigListCerts(const std::string& var, std::vector<std::string>& ders, int& sha256Count) {
    sha256Count = 0;
    size_t at = 0;
    while (at + 28 <= var.size()) {
        bool x509 = memcmp(var.data() + at, kX509, 16) == 0;
        bool sha = memcmp(var.data() + at, kSha256, 16) == 0;
        uint32_t listSize = rd32(var, at + 16);
        uint32_t hdrSize = rd32(var, at + 20);
        uint32_t sigSize = rd32(var, at + 24);
        if (listSize < 28 || sigSize < 16 || at + listSize > var.size()) break;  // malformed: stop
        size_t entry = at + 28 + hdrSize;
        size_t end = at + listSize;
        while (entry + sigSize <= end) {
            if (x509)
                ders.push_back(var.substr(entry + 16, sigSize - 16));  // skip the 16-byte owner GUID
            else if (sha)
                ++sha256Count;
            entry += sigSize;
        }
        at += listSize;
    }
}

namespace {

bool readFwVarGuid(const wchar_t* name, std::string& out) {
    enablePrivilege(L"SeSystemEnvironmentPrivilege");
    static const wchar_t* const kSecDb = L"{d719b2cb-3d3a-4596-a3bc-dad00e67656f}";
    std::vector<uint8_t> buf(96 * 1024);
    DWORD n = GetFirmwareEnvironmentVariableW(name, kSecDb, buf.data(), (DWORD)buf.size());
    if (!n) return false;
    out.assign((const char*)buf.data(), n);
    return true;
}

void collect(const std::string& var, std::vector<wstr>& names, int& hashes) {
    std::vector<std::string> ders;
    efiSigListCerts(var, ders, hashes);
    for (const std::string& d : ders) {
        wstr cn = certCommonName(d);
        names.push_back(cn.empty() ? wstr(T(L"(a certificate)")) : cn);
    }
}

}  // namespace

SecureBootCerts readSecureBootCerts() {
    SecureBootCerts c;
    std::string db, kek, dbx;
    bool any = false;
    int junk = 0;
    if (readFwVarGuid(L"db", db)) {
        collect(db, c.db, junk);
        any = true;
    }
    if (readFwVarGuid(L"KEK", kek)) {
        collect(kek, c.kek, junk);
        any = true;
    }
    if (readFwVarGuid(L"dbx", dbx)) {
        collect(dbx, c.dbx, c.dbxHashes);
        any = true;
    }
    c.read = any;
    for (const wstr& n : c.db) {
        if (icontains(n, L"Production PCA 2011")) c.dbHas2011 = true;
        if (icontains(n, L"UEFI CA 2023")) c.dbHas2023 = true;
    }
    for (const wstr& n : c.dbx)
        if (icontains(n, L"Production PCA 2011")) c.dbxRevokes2011 = true;
    c.dbxLarge = c.dbxHashes >= 100;
    return c;
}

wstr secureBoot2023Note(const SecureBootCerts& c, const Firmware& fw) {
    if (!fw.uefi || !c.read) return L"";
    wstr s;
    s += T(L"Secure Boot certificates (the 2011/2026 change):") + wstr(L"\n");
    // what the firmware trusts now
    wstr trusts;
    if (c.dbHas2011) trusts += (trusts.empty() ? L"" : L", ") + wstr(T(L"the 2011 Windows certificate"));
    if (c.dbHas2023) trusts += (trusts.empty() ? L"" : L", ") + wstr(T(L"the 2023 certificate"));
    if (!trusts.empty()) s += TF(L"- Trusted for starting (db): {0}.", trusts) + L"\n";
    s += TF(L"- Forbidden list (dbx): {0} revoked file hashes", std::to_wstring(c.dbxHashes));
    s += c.dbxRevokes2011 ? wstr(T(L", and the 2011 Windows boot certificate itself.")) : wstr(L".");
    s += L"\n\n";
    // the diagnosis
    if (c.dbxRevokes2011 && !c.dbHas2023)
        s += T(L"This PC has revoked the 2011 Windows boot certificate but does not yet trust the 2023 one. A boot manager signed with the 2011 certificate will be refused (\"Secure Boot Violation\"). To get it going again: turn Secure Boot off in the firmware settings so it starts, install every Windows update (these add the 2023 certificate and a 2023-signed boot manager), then rebuild the start-up files (Windows Repair) and turn Secure Boot back on.");
    else if (c.dbxRevokes2011 && c.dbHas2023)
        s += T(L"This PC trusts the 2023 certificate and has revoked the 2011 one. If it still shows a \"Secure Boot Violation\", its boot files are older than this change: rebuild the start-up files (Windows Repair) so the current, 2023-signed boot manager is written, or turn Secure Boot off to start, finish Windows Update, and turn it back on.");
    else if (c.dbHas2023)
        s += T(L"This PC already trusts the 2023 certificate. Nothing needs doing for the certificate change; a \"Secure Boot Violation\" here points to unsigned boot files or another operating system, not the 2023 roll-out.");
    else
        s += T(L"This PC still trusts only the 2011 certificates. They work until 2026. When Windows updates add the 2023 certificate, keep the boot files up to date (rebuild the start-up files after a certificate update) so a later revocation of the 2011 certificate does not stop it starting.");
    return s;
}

}  // namespace bd

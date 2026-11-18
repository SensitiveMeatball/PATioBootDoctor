#include "Accounts.h"
#include "Hive.h"
#include "Lang.h"
#include <algorithm>

namespace bd {

namespace {

uint32_t rd32(const std::string& s, size_t o) {
    return o + 4 <= s.size() ? (uint8_t)s[o] | ((uint8_t)s[o + 1] << 8) | ((uint8_t)s[o + 2] << 16) | ((uint32_t)(uint8_t)s[o + 3] << 24) : 0;
}
void wr32(std::string& s, size_t o, uint32_t v) {
    if (o + 4 > s.size()) return;
    s[o] = (char)(v & 0xFF);
    s[o + 1] = (char)(v >> 8 & 0xFF);
    s[o + 2] = (char)(v >> 16 & 0xFF);
    s[o + 3] = (char)(v >> 24 & 0xFF);
}

wstr fileTimeText(uint64_t ft) {
    if (!ft) return L"";
    FILETIME f{(DWORD)(ft & 0xFFFFFFFF), (DWORD)(ft >> 32)}, lf;
    SYSTEMTIME st;
    if (!FileTimeToLocalFileTime(&f, &lf) || !FileTimeToSystemTime(&lf, &st)) return L"";
    wchar_t b[32];
    swprintf(b, 32, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return b;
}

}  // namespace

// ---- the raw structures ----------------------------------------------------

bool samParseF(const std::string& f, uint16_t& acb) {
    if (f.size() < 0x3A) return false;
    acb = (uint8_t)f[0x38] | ((uint8_t)f[0x39] << 8);
    return true;
}

void samSetAcb(std::string& f, uint16_t acb) {
    if (f.size() < 0x3A) return;
    f[0x38] = (char)(acb & 0xFF);
    f[0x39] = (char)(acb >> 8 & 0xFF);
}

// The V structure: a table of (offset, length) items at its start, the data from offset 0xCC on.
// Item offsets are counted from 0xCC. Username is the item at 0x0C, full name at 0x18,
// the LM hash length at 0xA0 and the NT hash length at 0xAC.
bool samParseV(const std::string& v, wstr& name, wstr& fullName, uint32_t& lmLen, uint32_t& ntLen) {
    if (v.size() < 0xCC) return false;
    const uint32_t base = 0xCC;
    auto field = [&](size_t hdr) -> wstr {
        uint32_t off = rd32(v, hdr), len = rd32(v, hdr + 4);
        if (!len || (uint64_t)base + off + len > v.size()) return L"";
        return wstr((const wchar_t*)(v.data() + base + off), len / 2);
    };
    name = field(0x0C);
    fullName = field(0x18);
    lmLen = rd32(v, 0xA0);
    ntLen = rd32(v, 0xAC);
    return true;
}

bool samBlankV(std::string& v) {
    if (v.size() < 0xCC) return false;
    wr32(v, 0xA0, 0);  // LM hash length
    wr32(v, 0xAC, 0);  // NT hash length
    return true;
}

// ---- reading and changing the real SAM -------------------------------------

std::vector<LocalAccount> readLocalAccounts(const wstr& samFile, wstr& err) {
    std::vector<LocalAccount> out;
    if (GetFileAttributesW(samFile.c_str()) == INVALID_FILE_ATTRIBUTES) {
        err = T(L"The accounts file (SAM) is missing from this Windows.");
        return out;
    }
    LoadedHive h;
    if (!h.load(samFile, true, err)) return out;  // a copy: nothing is changed
    const wstr users = L"SAM\\Domains\\Account\\Users", names = users + L"\\Names";
    for (const wstr& n : regSubkeys(h.root(), names)) {
        DWORD rid = 0;
        if (!regDefaultValueType(h.root(), names + L"\\" + n, rid) || rid == 0) continue;
        LocalAccount a;
        a.name = n;
        a.rid = rid;
        a.builtin = rid < 1000;
        wchar_t rk[16];
        swprintf(rk, 16, L"%08X", rid);
        const wstr key = users + L"\\" + rk;
        std::string f, v;
        if (regBinary(h.root(), key, L"F", f)) {
            uint16_t acb = 0;
            if (samParseF(f, acb)) {
                a.disabled = (acb & ACB_DISABLED) != 0;
                a.locked = (acb & ACB_AUTOLOCK) != 0;
                a.pwNotRequired = (acb & ACB_PWNOTREQ) != 0;
            }
            if (f.size() >= 0x10) {
                uint64_t t = 0;
                memcpy(&t, f.data() + 0x08, 8);
                a.lastLogon = fileTimeText(t);
            }
        }
        if (regBinary(h.root(), key, L"V", v)) {
            wstr nm, fn;
            uint32_t lm = 0, nt = 0;
            if (samParseV(v, nm, fn, lm, nt)) {
                if (!fn.empty()) a.fullName = fn;
                a.blankPassword = nt == 0;
            }
        }
        out.push_back(a);
    }
    std::sort(out.begin(), out.end(), [](const LocalAccount& a, const LocalAccount& b) { return a.rid < b.rid; });
    if (out.empty() && err.empty()) err = T(L"No local accounts were found in this Windows.");
    return out;
}

bool changeAccount(const wstr& samFile, uint32_t rid, int op, wstr& err) {
    LoadedHive h;
    if (!h.load(samFile, false, err)) return false;  // the real file: the change is written to it
    wchar_t rk[16];
    swprintf(rk, 16, L"%08X", rid);
    const wstr key = L"SAM\\Domains\\Account\\Users\\" + wstr(rk);
    if (op == AO_CLEAR_PASSWORD) {
        std::string v;
        if (!regBinary(h.root(), key, L"V", v)) {
            err = T(L"This account's data could not be read.");
            return false;
        }
        if (!samBlankV(v)) {
            err = T(L"This account's data was not in the expected form, so nothing was changed.");
            return false;
        }
        if (!regSetBinary(h.root(), key, L"V", v)) {
            err = T(L"The change could not be written to the accounts file.");
            return false;
        }
        return true;
    }
    std::string f;
    if (!regBinary(h.root(), key, L"F", f)) {
        err = T(L"This account's data could not be read.");
        return false;
    }
    uint16_t acb = 0;
    if (!samParseF(f, acb)) {
        err = T(L"This account's data was not in the expected form, so nothing was changed.");
        return false;
    }
    if (op == AO_ENABLE)
        acb &= ~(uint16_t)ACB_DISABLED;
    else if (op == AO_UNLOCK)
        acb &= ~(uint16_t)ACB_AUTOLOCK;
    samSetAcb(f, acb);
    if (!regSetBinary(h.root(), key, L"F", f)) {
        err = T(L"The change could not be written to the accounts file.");
        return false;
    }
    return true;
}

}  // namespace bd

#include "HardwareInfo.h"
#include "StorageDrivers.h"
#include "Hive.h"
#include "Lang.h"
#include "core/Platform.h"

namespace bd {

namespace {

wstr trimmed(const wstr& s) {
    size_t a = s.find_first_not_of(L" \t"), b = s.find_last_not_of(L" \t");
    return a == wstr::npos ? wstr() : s.substr(a, b - a + 1);
}

const wchar_t* memTypeName(int t) {
    switch (t) {
        case 0x12: return L"DDR";
        case 0x13: return L"DDR2";
        case 0x18: return L"DDR3";
        case 0x1A: return L"DDR4";
        case 0x1B: return L"LPDDR";
        case 0x1C: return L"LPDDR2";
        case 0x1D: return L"LPDDR3";
        case 0x1E: return L"LPDDR4";
        case 0x22: return L"DDR5";
        case 0x23: return L"LPDDR5";
        default: return L"";
    }
}

const wchar_t* chassisName(int t) {
    switch (t & 0x7F) {
        case 3: return N(L"Desktop");
        case 4: return N(L"Low-profile desktop");
        case 6: return N(L"Mini tower");
        case 7: return N(L"Tower");
        case 8: return N(L"Portable");
        case 9: return N(L"Laptop");
        case 10: return N(L"Notebook");
        case 13: return N(L"All-in-one");
        case 14: return N(L"Sub-notebook");
        case 30: return N(L"Tablet");
        case 31: return N(L"Convertible");
        case 32: return N(L"Detachable");
        case 35: return N(L"Mini PC");
        case 36: return N(L"Stick PC");
        default: return L"";
    }
}

}  // namespace

bool parseSmbios(const std::string& t, SmbiosInfo& s) {
    size_t p = 0;
    auto u8 = [&](size_t o) -> int { return o < t.size() ? (uint8_t)t[o] : 0; };
    auto u16 = [&](size_t o) -> int { return u8(o) | u8(o + 1) << 8; };
    auto u32 = [&](size_t o) -> uint32_t { return (uint32_t)u16(o) | (uint32_t)u16(o + 2) << 16; };
    while (p + 4 <= t.size()) {
        int type = u8(p), len = u8(p + 1);
        if (len < 4 || p + len > t.size()) break;
        // the strings follow the formatted part; they end with an empty string
        std::vector<wstr> strs;
        size_t q = p + len;
        while (q < t.size() && t[q] != 0) {
            size_t e = t.find('\0', q);
            if (e == std::string::npos) e = t.size();
            wstr w;
            for (size_t k = q; k < e; ++k) w += (wchar_t)(uint8_t)t[k];
            strs.push_back(trimmed(w));
            q = e + 1;
        }
        size_t next = (q < t.size() && t[q] == 0 && strs.empty()) ? q + 2 : q + 1;  // no strings: two zeros
        auto str = [&](size_t off) -> wstr {
            if ((int)off >= len) return L"";
            int n = u8(p + off);
            return n >= 1 && n <= (int)strs.size() ? strs[n - 1] : wstr();
        };
        switch (type) {
            case 0:
                s.biosVendor = str(4);
                s.biosVersion = str(5);
                s.biosDate = str(8);
                break;
            case 1:
                s.maker = str(4);
                s.model = str(5);
                s.version = str(6);
                s.serial = str(7);
                if (len >= 0x19) {
                    const uint8_t* g = (const uint8_t*)t.data() + p + 8;
                    bool blank = true;
                    for (int i = 0; i < 16; ++i) blank &= g[i] == 0 || g[i] == 0xFF;
                    if (!blank) {
                        wchar_t b[48];
                        swprintf(b, 48, L"%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X", g[3], g[2], g[1], g[0], g[5], g[4], g[7], g[6], g[8], g[9], g[10], g[11], g[12],
                                 g[13], g[14], g[15]);
                        s.uuid = b;
                    }
                }
                if (len > 0x19) s.sku = str(0x19);
                if (len > 0x1A) s.family = str(0x1A);
                break;
            case 2:
                s.boardMaker = str(4);
                s.boardModel = str(5);
                s.boardSerial = str(7);
                break;
            case 3:
                s.chassisMaker = str(4);
                s.chassisType = chassisName(u8(p + 5));
                s.chassisSerial = str(7);
                break;
            case 4: {
                SmbiosInfo::Cpu c;
                c.socket = str(4);
                c.maker = str(7);
                c.name = str(0x10);
                if (len > 0x17) {
                    c.maxMHz = u16(p + 0x14);
                    c.curMHz = u16(p + 0x16);
                }
                if (len > 0x25) {
                    c.cores = u8(p + 0x23);
                    c.threads = u8(p + 0x25);
                }
                bool populated = len > 0x18 ? (u8(p + 0x18) & 0x40) != 0 : true;
                if (populated) s.cpus.push_back(c);
                break;
            }
            case 16:
                if (len > 0x0E) {
                    s.memMaxMb = u32(p + 7) / 1024;
                    s.memSlots += u16(p + 0x0D);
                }
                break;
            case 17: {
                if (len < 0x15) break;
                int size = u16(p + 0x0C);
                if (size == 0 || size == 0xFFFF) break;  // empty slot, or unknown
                SmbiosInfo::Mem m;
                if (size == 0x7FFF && len >= 0x20) m.mb = u32(p + 0x1C);
                else m.mb = (size & 0x8000) ? (uint64_t)(size & 0x7FFF) / 1024 : (uint64_t)size;
                m.slot = str(0x10);
                m.bank = str(0x11);
                m.type = memTypeName(u8(p + 0x12));
                if (len > 0x16) m.mhz = u16(p + 0x15);
                if (len > 0x17) m.maker = str(0x17);
                if (len > 0x18) m.serial = str(0x18);
                if (len > 0x1A) m.part = str(0x1A);
                if (len > 0x21 && u16(p + 0x20)) m.mhz = u16(p + 0x20);  // the speed it runs at
                s.mems.push_back(m);
                break;
            }
        }
        s.found = true;
        if (type == 127) break;
        p = next;
    }
    return s.found;
}

SmbiosInfo readSmbios() {
    SmbiosInfo s;
    const DWORD rsmb = 'RSMB';
    UINT n = GetSystemFirmwareTable(rsmb, 0, nullptr, 0);
    if (!n) return s;
    std::string b(n, '\0');
    if (GetSystemFirmwareTable(rsmb, 0, &b[0], n) != n || n < 8) return s;
    uint32_t len = (uint8_t)b[4] | (uint8_t)b[5] << 8 | (uint8_t)b[6] << 16 | (uint32_t)(uint8_t)b[7] << 24;
    parseSmbios(b.substr(8, std::min<size_t>(len, n - 8)), s);
    return s;
}

std::vector<PciDevice> livePciDevices(const wchar_t* classGuid) {
    std::vector<PciDevice> out;
    wstr want = classGuid;
    for (wchar_t& c : want) c = (wchar_t)towlower(c);
    typedef DWORD(WINAPI * LocateFn)(DWORD*, const wchar_t*, ULONG);
    HMODULE cm = systemDll(L"cfgmgr32.dll");
    LocateFn locate = cm ? (LocateFn)GetProcAddress(cm, "CM_Locate_DevNodeW") : nullptr;
    const wstr base = L"SYSTEM\\CurrentControlSet\\Enum\\PCI";
    for (const wstr& dev : regSubkeys(HKEY_LOCAL_MACHINE, base))
        for (const wstr& inst : regSubkeys(HKEY_LOCAL_MACHINE, base + L"\\" + dev)) {
            wstr key = base + L"\\" + dev + L"\\" + inst;
            wstr cls = regString(HKEY_LOCAL_MACHINE, key, L"ClassGUID");
            for (wchar_t& c : cls) c = (wchar_t)towlower(c);
            if (cls != want) continue;
            if (locate) {
                DWORD dn = 0;
                if (locate(&dn, (L"PCI\\" + dev + L"\\" + inst).c_str(), 0) != 0) continue;
            }
            PciDevice d;
            wstr desc = regString(HKEY_LOCAL_MACHINE, key, L"FriendlyName");
            if (desc.empty()) desc = regString(HKEY_LOCAL_MACHINE, key, L"DeviceDesc");
            size_t semi = desc.find_last_of(L';');
            d.name = semi == wstr::npos ? desc : desc.substr(semi + 1);
            size_t v = dev.find(L"VEN_"), dd = dev.find(L"&DEV_");
            if (v != wstr::npos && dd != wstr::npos) d.venDev = dev.substr(v, dd + 9 - v);
            d.service = regString(HKEY_LOCAL_MACHINE, key, L"Service");
            out.push_back(d);
        }
    return out;
}

wstr hardwareTitle(const SmbiosInfo& s, const wstr& fallback) {
    // "HP" + "HP ProBook 450 G5": the model often starts with the maker already
    bool repeats = !s.maker.empty() && s.model.size() >= s.maker.size() && _wcsnicmp(s.model.c_str(), s.maker.c_str(), s.maker.size()) == 0;
    wstr t = trimmed(repeats ? s.model : s.maker + L" " + s.model);
    if (t.empty()) t = fallback;
    if (!s.serial.empty()) t += L" (" + T(L"serial") + L" " + s.serial + L")";
    return t;
}

wstr hardwareReport(const Report& r, const wstr& technician, const wstr& version) {
    SmbiosInfo s = readSmbios();
    wstr o;
    auto line = [&](const wstr& k, const wstr& v) {
        if (!trimmed(v).empty()) o += L"  " + k + L": " + v + L"\r\n";
    };
    auto head = [&](const wstr& h) { o += L"\r\n" + h + L"\r\n"; };
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t when[32];
    swprintf(when, 32, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    wchar_t host[256] = {};
    DWORD hn = 256;
    GetComputerNameW(host, &hn);
    o += T(L"HARDWARE DETAILS") + L" — " + hardwareTitle(s, r.m.fw.maker + L" " + r.m.fw.model) + L"\r\n";
    line(T(L"Collected by"), technician.empty() ? wstr(T(L"(no name given)")) : technician);
    line(T(L"Collected on"), when);
    line(T(L"Collected with"), L"PATio Boot Doctor " + version + L", " + (g_sys.winPE ? T(L"started from a rescue or recovery system") : T(L"running in Windows")) + L" (" + host + L")");
    head(T(L"PC"));
    line(T(L"Maker"), s.maker);
    line(T(L"Model"), s.model + (s.version.empty() || s.version == s.model ? wstr() : L" · " + s.version));
    line(T(L"Family / SKU"), s.family + (s.sku.empty() ? wstr() : L" · " + s.sku));
    line(T(L"Serial number"), s.serial);
    line(T(L"UUID"), s.uuid);
    line(T(L"Case"), wstr(T(s.chassisType.c_str())) + (s.chassisSerial.empty() || s.chassisSerial == s.serial ? wstr() : L" · " + T(L"serial") + L" " + s.chassisSerial));
    line(T(L"Mainboard"), trimmed(s.boardMaker + L" " + s.boardModel) + (s.boardSerial.empty() ? wstr() : L" · " + T(L"serial") + L" " + s.boardSerial));
    line(T(L"BIOS / firmware"), trimmed(s.biosVendor + L" " + s.biosVersion) + (s.biosDate.empty() ? wstr() : L" · " + s.biosDate));
    line(T(L"Start-up mode"), (r.m.fw.uefi ? wstr(L"UEFI") : wstr(T(L"Legacy BIOS"))) +
                                 (r.m.fw.secureBootKnown ? (r.m.fw.secureBoot ? T(L", Secure Boot on") : T(L", Secure Boot off")) : wstr()));
    head(T(L"Processor"));
    {
        wstr name = regString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString");
        DWORD mhz = 0;
        regDword(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"~MHz", mhz);
        SYSTEM_INFO si{};
        GetNativeSystemInfo(&si);
        line(T(L"Name"), trimmed(name));
        int cores = 0, threads = 0;
        for (const auto& c : s.cpus) {
            cores += c.cores;
            threads += c.threads;
        }
        line(T(L"Cores / threads"), (cores ? std::to_wstring(cores) : wstr(L"?")) + L" / " + std::to_wstring(threads ? threads : (int)si.dwNumberOfProcessors));
        if (mhz) line(T(L"Speed"), std::to_wstring(mhz) + L" MHz");
        for (const auto& c : s.cpus) line(T(L"Socket"), c.socket + (c.maxMHz ? L" · " + TF(L"up to {0} MHz", std::to_wstring(c.maxMHz)) : wstr()));
    }
    head(T(L"Memory"));
    {
        MEMORYSTATUSEX ms{sizeof ms};
        GlobalMemoryStatusEx(&ms);
        uint64_t total = 0;
        for (const auto& m : s.mems) total += m.mb;
        line(T(L"Installed"), (total ? std::to_wstring(total / 1024) + L" GB" : sizeText(ms.ullTotalPhys)) + L" · " + TF(L"{0} modules in {1} slots", std::to_wstring(s.mems.size()), s.memSlots ? std::to_wstring(s.memSlots) : wstr(L"?")) +
                                  (s.memMaxMb ? L" · " + TF(L"up to {0} GB", std::to_wstring(s.memMaxMb / 1024)) : wstr()));
        for (const auto& m : s.mems)
            line(m.slot.empty() ? T(L"Module") : m.slot,
                 std::to_wstring(m.mb >= 1024 ? m.mb / 1024 : m.mb) + (m.mb >= 1024 ? L" GB" : L" MB") + (m.type.empty() ? wstr() : L" " + m.type) + (m.mhz ? L" " + std::to_wstring(m.mhz) + L" MHz" : wstr()) +
                     (m.maker.empty() ? wstr() : L" · " + m.maker) + (m.part.empty() ? wstr() : L" · " + m.part) + (m.serial.empty() ? wstr() : L" · " + T(L"serial") + L" " + m.serial));
    }
    head(T(L"Disks"));
    for (const Disk& d : r.m.disks) {
        wstr h = !d.health.known ? wstr(T(L"health not reported")) : d.health.level == 2 ? wstr(T(L"FAILING")) : d.health.level == 1 ? wstr(T(L"worn")) : wstr(T(L"healthy"));
        line(T(L"Disk ") + std::to_wstring(d.number), d.model + L" · " + sizeText(d.size) + L" · " + d.typeText + L" · " + h + (d.health.summary.empty() ? wstr() : L" (" + d.health.summary + L")"));
    }
    head(T(L"Storage controllers"));
    for (const StorageController& k : liveStorageControllers())
        line(storageModeName(k.mode), k.name + L" · " + k.venDev + (k.liveService.empty() ? wstr() : L" · " + T(L"driver") + L" " + k.liveService));
    head(T(L"Graphics"));
    for (const PciDevice& d : livePciDevices(L"{4d36e968-e325-11ce-bfc1-08002be10318}")) line(d.venDev, d.name);
    head(T(L"Network"));
    for (const PciDevice& d : livePciDevices(L"{4d36e972-e325-11ce-bfc1-08002be10318}")) line(d.venDev, d.name);
    head(T(L"Systems found"));
    for (const Install& in : r.installs) line(in.where, in.label() + (in.build ? L" · build " + std::to_wstring(in.build) : wstr()) + (in.edition.empty() ? wstr() : L" · " + in.edition));
    if (r.installs.empty()) o += L"  " + T(L"none") + L"\r\n";
    return o;
}

}  // namespace bd

#include "StorageDrivers.h"
#include "Hive.h"
#include "Lang.h"
#include <algorithm>

namespace bd {

const wchar_t* storageModeName(int m) {
    switch (m) {
        case SM_IDE: return L"IDE";
        case SM_AHCI: return L"AHCI";
        case SM_RAID: return L"RAID";
        case SM_NVME: return L"NVMe";
        case SM_VMD: return L"Intel VMD";
        default: return L"?";
    }
}

const std::vector<KnownStorageDriver>& knownStorageDrivers() {
    static const std::vector<KnownStorageDriver> k = {
        {L"disk", SM_CORE, true, N(L"Disk driver"), SM_UNKNOWN},
        {L"partmgr", SM_CORE, true, N(L"Partition manager"), SM_UNKNOWN},
        {L"volmgr", SM_CORE, true, N(L"Volume manager"), SM_UNKNOWN},
        {L"Ntfs", SM_CORE, true, N(L"NTFS file system"), SM_UNKNOWN},
        {L"pciide", SM_IDE, true, N(L"Standard IDE controller"), SM_UNKNOWN},
        {L"intelide", SM_IDE, true, N(L"Intel IDE controller"), SM_UNKNOWN},
        {L"atapi", SM_IDE, true, N(L"IDE/ATAPI port driver"), SM_UNKNOWN},
        {L"msahci", SM_AHCI, true, N(L"AHCI controller (Windows 7)"), SM_UNKNOWN},
        {L"storahci", SM_AHCI, true, N(L"AHCI controller (Windows 8 and newer)"), SM_UNKNOWN},
        {L"amdsata", SM_AHCI, false, N(L"AMD SATA controller"), SM_UNKNOWN},
        {L"iaStorV", SM_RAID, true, N(L"Intel Rapid Storage, built into Windows (RAID)"), SM_UNKNOWN},
        {L"iaStor", SM_RAID, false, N(L"Intel Rapid Storage, older driver package (AHCI and RAID)"), SM_AHCI},
        {L"iaStorA", SM_RAID, false, N(L"Intel Rapid Storage (AHCI and RAID)"), SM_AHCI},
        {L"iaStorAC", SM_RAID, false, N(L"Intel Rapid Storage (AHCI and RAID)"), SM_AHCI},
        {L"iaStorAVC", SM_RAID, false, N(L"Intel Rapid Storage (AHCI and RAID)"), SM_AHCI},
        {L"rcraid", SM_RAID, false, N(L"AMD RAID"), SM_UNKNOWN},
        {L"stornvme", SM_NVME, true, N(L"NVMe controller (Windows 8.1 and newer)"), SM_UNKNOWN},
        {L"iaStorVD", SM_VMD, false, N(L"Intel VMD / Rapid Storage"), SM_UNKNOWN},
    };
    return k;
}

int modeFromHardwareId(const wstr& idIn) {
    wstr id = idIn;
    for (wchar_t& c : id) c = (wchar_t)towupper(c);
    size_t p = id.find(L"CC_");
    if (p == wstr::npos) return SM_UNKNOWN;
    wstr cc = id.substr(p + 3, 6);
    while (!cc.empty() && !iswxdigit(cc.back())) cc.pop_back();
    if (cc.compare(0, 4, L"0101") == 0) return SM_IDE;
    if (cc.compare(0, 4, L"0104") == 0) return SM_RAID;
    if (cc.compare(0, 4, L"0108") == 0) return SM_NVME;
    if (cc.compare(0, 4, L"0106") == 0) return cc.size() >= 6 && cc.compare(4, 2, L"00") == 0 ? SM_IDE : SM_AHCI;
    return SM_UNKNOWN;
}

static wstr upper(wstr s) {
    for (wchar_t& c : s) c = (wchar_t)towupper(c);
    return s;
}

static wstr lower(wstr s) {
    for (wchar_t& c : s) c = (wchar_t)towlower(c);
    return s;
}

std::vector<StorageController> liveStorageControllers() {
    std::vector<StorageController> out;
    static const wchar_t* const kHdc = L"{4d36e96a-e325-11ce-bfc1-08002be10318}";   // IDE ATA/ATAPI controllers (incl. AHCI)
    static const wchar_t* const kScsi = L"{4d36e97b-e325-11ce-bfc1-08002be10318}";  // storage controllers (RAID, NVMe)
    typedef DWORD(WINAPI * LocateFn)(DWORD*, const wchar_t*, ULONG);
    HMODULE cm = systemDll(L"cfgmgr32.dll");
    LocateFn locate = cm ? (LocateFn)GetProcAddress(cm, "CM_Locate_DevNodeW") : nullptr;
    const wstr base = L"SYSTEM\\CurrentControlSet\\Enum\\PCI";
    for (const wstr& dev : regSubkeys(HKEY_LOCAL_MACHINE, base))
        for (const wstr& inst : regSubkeys(HKEY_LOCAL_MACHINE, base + L"\\" + dev)) {
            wstr key = base + L"\\" + dev + L"\\" + inst;
            wstr cls = lower(regString(HKEY_LOCAL_MACHINE, key, L"ClassGUID"));
            if (cls != kHdc && cls != kScsi) continue;
            if (locate) {  // only devices that are here now (Windows keeps entries of earlier hardware too)
                DWORD dn = 0;
                wstr instId = L"PCI\\" + dev + L"\\" + inst;
                if (locate(&dn, instId.c_str(), 0) != 0) continue;
            }
            StorageController c;
            for (const wstr& id : regMulti(HKEY_LOCAL_MACHINE, key, L"HardwareID")) {
                wstr u = upper(id);
                if (u.find(L"&CC_") != wstr::npos && (c.hardwareId.empty() || u.size() < c.hardwareId.size() || modeFromHardwareId(c.hardwareId) == SM_UNKNOWN))
                    c.hardwareId = u;
            }
            if (c.hardwareId.empty()) continue;
            c.mode = modeFromHardwareId(c.hardwareId);
            size_t v = c.hardwareId.find(L"VEN_"), d = c.hardwareId.find(L"&DEV_");
            if (v != wstr::npos && d != wstr::npos) c.venDev = c.hardwareId.substr(v, d + 9 - v);
            size_t cc = c.hardwareId.find(L"CC_");
            if (cc != wstr::npos) c.cc = c.hardwareId.substr(cc + 3, 4);
            c.liveService = regString(HKEY_LOCAL_MACHINE, key, L"Service");
            if (iequals(c.liveService, L"iaStorVD")) c.mode = SM_VMD;
            wstr desc = regString(HKEY_LOCAL_MACHINE, key, L"FriendlyName");
            if (desc.empty()) desc = regString(HKEY_LOCAL_MACHINE, key, L"DeviceDesc");
            size_t semi = desc.find_last_of(L';');
            c.name = semi == wstr::npos ? desc : desc.substr(semi + 1);
            if (c.mode != SM_UNKNOWN) out.push_back(c);
        }
    return out;
}

void judgeStorage(StorageCheck& c, int targetMode) {
    int target = targetMode >= 0 ? targetMode : c.mode;
    c.turnOn.clear();
    c.cddb.clear();
    c.setUpFor.clear();
    c.needPresent = c.needOn = false;
    auto has = [](const std::vector<wstr>& v, const wstr& s) {
        for (const wstr& x : v)
            if (iequals(x, s)) return true;
        return false;
    };
    for (const DriverState& d : c.drivers) {
        if (d.mode == SM_CORE) {
            if (d.present && d.start != 0) c.turnOn.push_back(d.service);
            continue;
        }
        for (int m : {d.mode, d.mode2})
            if (m != SM_UNKNOWN && d.present && d.start == 0 && std::find(c.setUpFor.begin(), c.setUpFor.end(), m) == c.setUpFor.end()) c.setUpFor.push_back(m);
    }
    wstr setUp;
    for (int m : c.setUpFor) setUp += (setUp.empty() ? L"" : L", ") + wstr(storageModeName(m));
    if (target == SM_UNKNOWN) {
        c.verdict = T(L"The storage controller's mode could not be read on this system.");
        c.advice = setUp.empty() ? T(L"This Windows starts no controller driver at all: turn on all its built-in storage drivers.")
                                 : TF(L"This Windows was set up for {0}. If the firmware's SATA mode may have changed, turn on all its built-in storage drivers so it starts in any mode.", setUp);
        return;
    }
    wstr names;
    for (const DriverState& d : c.drivers) {
        if (!d.serves(target)) continue;
        if (d.present && d.fileThere) {
            c.needPresent = true;
            if (d.start == 0) c.needOn = true;
        }
    }
    for (const DriverState& d : c.drivers)
        if (d.serves(target) && d.present && d.fileThere) {
            if (c.needOn && d.start != 0) continue;  // one for this mode already starts: the others stay as they are
            names += (names.empty() ? L"" : L", ") + d.service;
            if (d.start != 0 && !has(c.turnOn, d.service)) c.turnOn.push_back(d.service);
        }
    const wchar_t* mode = storageModeName(target);
    bool planning = targetMode >= 0 && targetMode != c.mode;
    if (c.needOn && !planning) {
        c.verdict = TF(L"This Windows starts the driver its disk controller needs in {0} mode ({1}).", wstr(mode), names);
        c.advice = T(L"The storage driver is not what stops it. Look at the disk itself (health, surface test, check the drive) and the start-up files.");
    } else if (c.needPresent) {
        c.verdict = planning ? TF(L"Before the firmware is switched to {0}: this Windows has the {0} driver ({1}) but does not start it yet.", wstr(mode), names)
                             : TF(L"The driver this PC's controller needs in {0} mode ({1}) is installed in this Windows, but it does not start with it.", wstr(mode), names);
        c.advice = planning ? T(L"Turn it on first, then change the mode in the firmware settings. Windows then starts in the new mode.")
                            : T(L"That alone stops Windows with INACCESSIBLE_BOOT_DEVICE. Turning it on fixes it.");
    } else {
        c.verdict = TF(L"This Windows has no driver for {0} mode.", wstr(mode));
        if (target == SM_VMD)
            c.advice = T(L"Intel VMD needs Intel's iaStorVD driver: add it with \"Add a driver (.inf) to this Windows\", or switch VMD off in the firmware settings.");
        else if (!setUp.empty())
            c.advice = TF(L"It was set up for {0}: set the firmware's SATA mode back to that, or add the {1} driver with \"Add a driver (.inf) to this Windows\".", setUp, wstr(mode));
        else
            c.advice = TF(L"Add the {0} driver with \"Add a driver (.inf) to this Windows\".", wstr(mode));
    }
    // Windows 7 also maps the controller's ID to its driver ("critical device database")
    if (c.win7 && !planning)
        for (const StorageController& k : c.controllers) {
            if (k.mode != target || k.venDev.empty() || k.cc.empty()) continue;
            wstr key = lower(L"pci#" + k.venDev + L"&cc_" + k.cc);
            if (has(c.cddbHave, key)) continue;
            bool byClass = false;  // Windows 7 also maps whole classes: pci#cc_0101 (IDE), pci#cc_010601 (AHCI)
            for (const wstr& hv : c.cddbHave)
                if (istartsWith(hv, lower(L"pci#cc_" + k.cc))) byClass = true;
            if (byClass) continue;
            wstr svc;
            for (const DriverState& d : c.drivers)
                if (d.serves(target) && d.present && d.fileThere && (svc.empty() || d.inbox)) svc = d.service;
            if (!svc.empty()) c.cddb.push_back(key + L"=" + svc);
        }
    if (!c.cddb.empty() && c.advice.find(L"critical") == wstr::npos)
        c.advice += L" " + T(L"Windows 7 also needs this controller listed in its critical-device database; the fix adds it.");
}

std::vector<wstr> allInboxStorageDrivers(const StorageCheck& c) {
    std::vector<wstr> out;
    for (const DriverState& d : c.drivers)
        if (d.inbox && d.present && d.fileThere && d.start != 0) out.push_back(d.service);
    return out;
}

StorageCheck checkStorage(const Install& in, const Partition&, const wstr& diskBus) {
    StorageCheck c;
    c.win7 = in.major == 6 && in.minor <= 1;
    c.controllers = liveStorageControllers();
    const StorageController* pick = nullptr;
    bool nvme = iequals(diskBus, L"NVMe");
    for (const StorageController& k : c.controllers)
        if (!pick && (nvme ? k.mode == SM_NVME : (k.mode == SM_IDE || k.mode == SM_AHCI || k.mode == SM_RAID || k.mode == SM_VMD))) pick = &k;
    if (!pick && !c.controllers.empty()) pick = &c.controllers[0];
    c.mode = pick ? pick->mode : SM_UNKNOWN;
    LoadedHive h;
    if (!h.load(in.winDir + L"\\System32\\config\\SYSTEM", true, c.err)) return c;
    DWORD cur = 1;
    regDword(h.root(), L"Select", L"Current", cur);
    wchar_t csb[32];
    swprintf(csb, 32, L"ControlSet%03u", (unsigned)cur);
    wstr cs = csb;
    for (const KnownStorageDriver& k : knownStorageDrivers()) {
        DriverState d;
        d.service = k.service;
        d.title = T(k.title);
        d.mode = k.mode;
        d.mode2 = k.mode2;
        d.inbox = k.inbox;
        wstr key = cs + L"\\Services\\" + k.service;
        DWORD st = 0;
        d.present = regDword(h.root(), key, L"Start", st);
        if (d.present) d.start = st;
        wstr img = regString(h.root(), key, L"ImagePath");
        wstr rel = img;
        if (istartsWith(rel, L"\\SystemRoot\\")) rel = rel.substr(12);
        else if (istartsWith(rel, L"%SystemRoot%\\")) rel = rel.substr(13);
        if (rel.empty()) rel = wstr(L"System32\\drivers\\") + k.service + L".sys";
        d.fileThere = d.present && GetFileAttributesW((in.winDir + L"\\" + rel).c_str()) != INVALID_FILE_ATTRIBUTES;
        if (k.mode == SM_CORE && d.present) d.fileThere = true;  // Ntfs and friends are part of the kernel image set
        c.drivers.push_back(d);
    }
    for (const wstr& k : regSubkeys(h.root(), cs + L"\\Control\\CriticalDeviceDatabase")) c.cddbHave.push_back(lower(k));
    c.ok = true;
    judgeStorage(c);
    return c;
}

}  // namespace bd

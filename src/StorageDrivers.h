// The storage controller of this PC, the mode the firmware runs it in (IDE, AHCI, RAID, NVMe, Intel
// VMD), and whether an offline Windows has - and starts with - the driver that mode needs.
//
// A Windows that stops with INACCESSIBLE_BOOT_DEVICE although its start-up files are fine has very often
// lost its storage driver: the firmware's SATA mode was changed (AHCI <-> RAID <-> IDE), the disk was
// moved to another PC, or the driver was switched off. The kernel then cannot reach its own disk.
//
// The mode comes from the controller's PCI class code (CC_0101 IDE, CC_0104 RAID, CC_010601 AHCI,
// CC_0108 NVMe); the drivers and their start type from the offline Windows' SYSTEM registry file.
// Windows 7 also needs a "critical device" entry that maps the controller's ID to its driver.
#pragma once
#include "Diagnose.h"

namespace bd {

enum StorageMode { SM_UNKNOWN, SM_IDE, SM_AHCI, SM_RAID, SM_NVME, SM_VMD, SM_CORE };
const wchar_t* storageModeName(int mode);  // "AHCI", "RAID", ...

struct StorageController {
    wstr name;         // "Standard SATA AHCI Controller"
    wstr hardwareId;   // "PCI\VEN_8086&DEV_1C02&CC_010601"
    wstr venDev;       // "VEN_8086&DEV_1C02"
    wstr cc;           // the class code's first 4 digits: "0106"
    wstr liveService;  // the driver this system uses for it now (empty: none)
    int mode = SM_UNKNOWN;
};
int modeFromHardwareId(const wstr& id);
std::vector<StorageController> liveStorageControllers();  // the storage controllers present now

struct KnownStorageDriver {
    const wchar_t* service;
    int mode;
    bool inbox;  // part of Windows itself (safe to start everywhere)
    const wchar_t* title;
    int mode2;   // a second mode it also serves (Intel Rapid Storage: RAID and AHCI), else SM_UNKNOWN
};
const std::vector<KnownStorageDriver>& knownStorageDrivers();

struct DriverState {
    wstr service, title;
    int mode = SM_UNKNOWN, mode2 = SM_UNKNOWN;
    bool inbox = false, present = false, fileThere = false;
    bool serves(int m) const { return m != SM_UNKNOWN && (mode == m || mode2 == m); }
    DWORD start = 0xFFFFFFFF;  // 0 boot, 1 system, 2 automatic, 3 manual, 4 off
};

struct StorageCheck {
    bool ok = false;
    wstr err;
    bool win7 = false;
    std::vector<StorageController> controllers;
    std::vector<DriverState> drivers;  // the known storage drivers as this Windows has them
    int mode = SM_UNKNOWN;             // the mode Windows' disk controller runs in now
    std::vector<int> setUpFor;         // the modes this Windows starts a driver for (how it was installed)
    bool needPresent = false, needOn = false;
    std::vector<wstr> turnOn;          // services the fix sets to start with Windows (Start = 0)
    std::vector<wstr> cddb;            // Windows 7: "pci#ven_8086&dev_2822&cc_0104=iaStorV" entries to add
    std::vector<wstr> cddbHave;        // the critical-device entries it has (lower case)
    wstr verdict, advice;
};

// diskBus: the Windows disk's bus ("NVMe", "SATA", "RAID" ...), to pick its controller when there are several.
StorageCheck checkStorage(const Install& in, const Partition& winPart, const wstr& diskBus);
// The verdict and plan from the controllers and drivers already filled in (also for the self-test).
// targetMode >= 0 plans for a mode the firmware is about to be switched to, instead of the current one.
void judgeStorage(StorageCheck& c, int targetMode = -1);
// Every built-in (Microsoft) storage driver this Windows has, so it starts in any controller mode.
std::vector<wstr> allInboxStorageDrivers(const StorageCheck& c);

}  // namespace bd

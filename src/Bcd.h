// The boot settings store (BCD): the list Windows Boot Manager reads to know
// which Windows to start and where it is. Read directly from the registry hive,
// so it works the same in every language and on stores of other disks.
#pragma once
#include "Disk.h"

namespace bd {

enum BcdApp { APP_OTHER = 0, APP_FWBOOTMGR = 1, APP_BOOTMGR = 2, APP_LOADER = 3, APP_RESUME = 4, APP_MEMTEST = 5, APP_NTLDR = 6, APP_BOOTSECTOR = 8 };

struct BcdEntry {
    wstr id;              // {guid}
    uint32_t type = 0;
    int app = APP_OTHER;  // type & 0xFFFFF
    wstr description, path, systemRoot;
    std::string device, osDevice;  // raw device elements
    int devPart = -1, osPart = -1;  // matching partitions (index into Machine::parts); -1 = none
    bool hasDevice = false, hasOsDevice = false;
    bool ramdisk = false;           // starts from a .wim/.vhd file (recovery, setup)
    bool recoveryEnabled = true;    // automatic repair after failed starts
    bool inMenu = false, isDefault = false;
};

struct BcdStore {
    wstr file;            // e.g. S:\EFI\Microsoft\Boot\BCD
    int part = -1;        // the partition the file is on
    bool live = false;    // the store the running system has open
    bool loaded = false;
    Health health = Health::Unknown;
    wstr problem;         // plain-language reason it could not be read
    wstr defaultId;
    std::vector<wstr> order;
    int timeout = -1;
    bool hasBootmgr = false;
    bool empty = false;   // intact, but it lists no system to start (not damage: a leftover, or its last entry was removed)
    wstr bootmgrPath;     // \EFI\Microsoft\Boot\bootmgfw.efi or empty (BIOS)
    std::vector<BcdEntry> entries;
    const BcdEntry* find(const wstr& id) const;
};

// Finds and reads every BCD file on the machine's partitions.
std::vector<BcdStore> readStores(const Machine& m);
// Reads one store file (copy = load a copy, never the original).
bool readStore(const wstr& file, bool liveRegistry, const Machine& m, BcdStore& out);
// Parses an already loaded store (root = the hive root: has Objects\...)
void parseStore(HKEY root, const Machine& m, BcdStore& out);
// Which partition a raw device element names (-1 if none or not on these disks)
int matchDevice(const std::string& blob, const Machine& m);
bool deviceIsFile(const std::string& blob);  // a .wim / .vhd ramdisk

wstr liveStoreDevicePath();  // \Device\HarddiskVolumeN\EFI\Microsoft\Boot\BCD of the running system, or empty
wstr devicePathOf(const wstr& volume);  // \Device\HarddiskVolumeN for \\?\Volume{...}\

}  // namespace bd

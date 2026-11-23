// Checks that an EFI system partition (ESP) is really sound, straight from its
// raw sectors, without a drive letter. The boot chain's "Start-up partition" step
// only sees that the files exist and the BCD parses; this looks one level deeper,
// at the FAT32 file system itself, so a damaged ESP (the kind that makes
// `bootrec /fixboot` say "Access is denied") is caught instead of passing as fine.
//
// What it verifies:
//   - the FAT32 boot sector is valid and its backup copy (sector 6) matches it;
//   - the two FAT copies agree;
//   - every file's cluster chain is sound: no loop, no cluster shared by two
//     files, and the chain length matches the size in the directory entry;
//   - \EFI\Microsoft\Boot\bootmgfw.efi and \EFI\Boot\bootx64.efi read in full and
//     are 64-bit PE images;
//   - the BCD store (\EFI\Microsoft\Boot\BCD) loads (a valid registry hive).
// A separate write-open test says whether the partition can be written at all.
#pragma once
#include "Disk.h"

namespace bd {

enum class EspFault {
    None,            // sound
    NotChecked,      // the boot sector could not be read: nothing to judge
    NotFat32,        // not a FAT32 volume (unexpected for an ESP)
    BootSector,      // the FAT32 boot sector is invalid (no 55 AA / bad numbers)
    BackupBootSector,// the backup copy in sector 6 does not match the boot sector
    FatCopies,       // the two FAT copies do not agree
    ClusterChain,    // a file's cluster chain loops, is shared, or is the wrong length
    Bootmgr,         // bootmgfw.efi is missing, unreadable, or not a 64-bit program
    FallbackLoader,  // \EFI\Boot\bootx64.efi is missing, unreadable, or not 64-bit
    Bcd,             // the BCD store on the ESP does not load
    ReadOnly         // the partition cannot be opened for writing
};

struct EspCheck {
    bool ran = false;       // the file-system check actually ran
    bool fat32 = false;     // the volume is FAT32
    EspFault fault = EspFault::NotChecked;
    wstr detail;            // a short technical note (file name, cluster, ...), not translated
};

// Reads the ESP from its sectors (rd reads at partition-relative byte offsets) and
// checks the file system and the boot files. No writing, no drive letter.
EspCheck checkEsp(const ReadFn& rd, uint64_t partLength);

// True when the partition can be opened for writing (what /fixboot needs). It opens
// the volume for write and closes it again at once, changing nothing. When it cannot
// be told apart (no volume path), returns true so nothing is reported wrongly.
bool espWritable(const Partition& p);

// A short, untranslated tag for a fault, for the technical line.
const wchar_t* espFaultWord(EspFault f);

}  // namespace bd

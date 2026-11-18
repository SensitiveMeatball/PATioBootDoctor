// Partition and file recovery, read-only until the user picks what to bring back.
//
// Lost partitions: looks in unused space (or on a disk with no partition table)
// for the first sector of a file system (NTFS, FAT32, exFAT, ext2/3/4, BitLocker).
//   quick: every 1 MB boundary (where Windows and modern tools start partitions)
//   full:  every sector, also finding the backup copy NTFS, FAT32 and exFAT keep
// Restoring adds the partition to the table again; its contents are not touched.
//
// Deleted files (NTFS): the file list ($MFT) keeps entries of deleted files until
// they are reused.
//   quick: reads the file list
//   full:  also reads the whole partition for file-list records of an older file
//          list (after a quick format) and for pictures and PDFs by their contents
// Files are copied to another drive; nothing is written to the scanned one.
#pragma once
#include "Disk.h"
#include "Backup.h"
#include "Repair.h"

namespace bd {

using ScanProgress = std::function<void(const wstr& step, float frac)>;

struct LostPartition {
    uint64_t offset = 0, length = 0;
    wstr fs, label;
    bool fromBackup = false;  // found through the file system's backup copy of its first sector
    bool restorable = true;
    wstr why;                 // why not restorable
};

// Reads a file system's first sector (and the 4 KB from there). size = its length in bytes (0 = unknown).
bool parseBootSector(const std::string& first4k, wstr& fs, uint64_t& size, wstr& label);
std::vector<LostPartition> findLostPartitions(const Report& r, int disk, bool full, const ScanProgress& progress, volatile bool* cancel);
FixOutcome restorePartition(const LostPartition& lp, int disk, const Report& r, Journal& j);

enum FileKind { FK_OTHER, FK_PICTURE, FK_DOCUMENT, FK_MEDIA, FK_ARCHIVE, FK_REGISTRY, FK_COUNT };
int fileKindOf(const wstr& name);

struct FoundFile {
    wstr name, folder;          // folder: where it was ("Users\Ann\Pictures"), or a group name
    uint64_t size = 0;
    FILETIME modified{};
    int kind = FK_OTHER;
    int state = 0;              // 0 good, 1 parts are used again (may be damaged), 2 nothing left
    bool resident = false;      // small file kept inside the file list itself
    std::string data;           // resident contents
    std::vector<std::pair<int64_t, uint64_t>> runs;  // clusters: (first cluster or -1 for a hole, count)
    bool carved = false;        // found by contents: one piece from offset
    uint64_t carvedOffset = 0;  // bytes from the partition start
};

struct FileScan {
    bool ntfs = false;
    uint32_t cluster = 4096;
    uint64_t scanned = 0;       // records or bytes looked at
    wstr note;                  // a plain sentence about the result
    std::vector<FoundFile> files;
};

FileScan scanDeletedFiles(const Partition& p, bool full, const ScanProgress& progress, volatile bool* cancel);
// Copies the chosen files to toDir (keeping their folders). Returns how many came back whole.
int recoverFiles(const Partition& p, const FileScan& s, const std::vector<int>& which, const wstr& toDir, const ScanProgress& progress,
                 volatile bool* cancel, int& failed, wstr& log);

// The files that matter most when rescuing an old Windows: its registry (SYSTEM, SOFTWARE, SAM,
// SECURITY, DEFAULT, NTUSER.DAT, UsrClass.dat ...), the keys that unlock saved passwords (DPAPI),
// mail files, browser profiles and the user folders.
bool isWindowsEssential(const FoundFile& f);
// A registry file found by its contents: its name from its own header ("SOFTWARE", "ntuser.dat")
wstr hiveNameFromHeader(const std::string& first4k);

// Which files of an NTFS partition sit on unreadable sectors (from a surface test or disk copy), and
// whether each is one Windows needs to start. diskRanges: offset and length in bytes from the disk's start.
struct FileOnBad {
    wstr path;              // "Windows\System32\config\SYSTEM", or "$MFT"
    bool critical = false;  // needed to start Windows: registry, drivers, kernel, boot files, the file system's own
    uint64_t bytes = 0;     // how much of it is unreadable
};
std::vector<FileOnBad> filesOnBadRanges(const Partition& p, const std::vector<std::pair<uint64_t, uint64_t>>& diskRanges, wstr& note, const ScanProgress& progress,
                                        volatile bool* cancel);
bool isStartupCritical(const wstr& path);

// For the self-test: NTFS record parsing on a buffer.
bool ntfsApplyFixups(std::string& rec, uint32_t sector);
bool ntfsParseRecord(const std::string& rec, FoundFile& f, uint64_t& parent, bool& inUse, bool& isDir, bool& isBase);
std::vector<std::pair<int64_t, uint64_t>> ntfsRuns(const uint8_t* p, size_t n);

}  // namespace bd

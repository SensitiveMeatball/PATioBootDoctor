// Disks, partitions, volumes and the firmware: everything read straight from
// the hardware, with no user interface. Only kernel32 and advapi32 calls, so it
// works the same in Windows 7 to 11, Windows PE and WinRE.
#pragma once
#include "core/Common.h"
#include <functional>

namespace bd {

enum class Health { Ok, Info, Warn, Bad, Unknown };
Health worse(Health a, Health b);

enum Role { R_UNKNOWN, R_ESP, R_MSR, R_RECOVERY, R_SYSTEM, R_DATA, R_EXTENDED, R_LINUX, R_DYNAMIC, R_EMPTY };
const wchar_t* roleName(int role);   // "EFI system partition (start-up partition)" ...
const wchar_t* roleShort(int role);  // "Start-up", "Reserved", ...

struct Partition {
    int disk = -1, number = 0;  // number: 1-based, as Windows numbers them
    uint64_t offset = 0, length = 0;
    bool gpt = false;
    GUID type{}, id{};
    uint8_t mbrType = 0;
    bool active = false;  // MBR "active" (boot) flag
    uint64_t gptAttrs = 0;
    wstr gptName;
    wstr volume;      // \\?\Volume{...}\ ; empty when Windows has no volume for it
    wstr letter;      // "C:" or empty
    wstr root;        // folder to read files from: "C:\" or the volume path; empty = not readable
    wstr fs, label;   // as Windows reports it
    wstr rawFs;       // file system seen in its first sector: NTFS, FAT32, FAT, exFAT, ReFS, BitLocker, ext2/3/4, btrfs, XFS, LUKS, swap, or empty
    wstr fsUuid;      // the file system's own id as Linux writes it (fstab, GRUB): lower case
    bool linuxRoot = false;  // GPT type says "Linux root"
    uint64_t totalBytes = 0, freeBytes = 0;
    bool bitlocker = false, locked = false, readable = false, dirty = false, pastEnd = false, overlaps = false;
    int role = R_UNKNOWN;
    wstr key;         // stays the same across restarts: "<disk id>@<offset>"
    wstr where() const;  // "Disk 0, partition 3"
    wstr name() const;   // "C: · Windows" or "Disk 0, partition 1 · EFI system partition"
};

// Disk health (S.M.A.R.T. / NVMe health log), see Smart.cpp
struct DiskHealth {
    bool known = false;
    int level = 0;            // 0 good, 1 warning, 2 failing
    wstr kind, summary;       // "S.M.A.R.T." / "NVMe"; "Good · 3214 hours in use · 34 °C"
    int tempC = -1, percentUsed = -1, spare = -1, spareThreshold = -1;
    int64_t hours = -1, reallocated = -1, pending = -1, uncorrectable = -1, mediaErrors = -1;
    std::vector<wstr> problems;  // plain sentences
};
DiskHealth readDiskHealth(int disk);
void parseAtaSmart(const uint8_t* attrs512, const uint8_t* thresholds512, DiskHealth& h);
void parseNvmeLog(const uint8_t* log512, DiskHealth& h);

enum DiskGroup { DG_FIXED, DG_REMOVABLE, DG_OPTICAL, DG_ENTERPRISE, DG_COUNT };
const wchar_t* diskGroupName(int group);

// Optical, floppy and network drives: not disks that can be partitioned, listed for completeness.
struct OtherDrive {
    wstr letter, model, label, fs, typeText;
    int group = DG_OPTICAL;
    bool hasMedia = false;
    uint64_t size = 0;
};
std::vector<OtherDrive> readOtherDrives();

struct Disk {
    int number = -1;
    uint64_t size = 0;
    uint32_t sector = 512;
    int style = 0;  // 0 not set up (raw), 1 MBR, 2 GPT
    uint32_t signature = 0;
    GUID guid{};
    wstr model, bus;
    bool removable = false, readable = false;
    bool sig55aa = false, bootCode = false;          // MBR checks
    bool grubMbr = false;                            // the start-up code in the first sector is GRUB (Linux)
    bool gptPrimaryOk = false, gptBackupOk = false;  // GPT checks
    bool gptBackupMisplaced = false;                 // backup copy not at the end (disk was cloned to a bigger one)
    uint64_t largestGap = 0;                         // biggest unused space
    std::vector<std::pair<uint64_t, uint64_t>> gaps; // unused spaces of 1 MB or more: offset, length
    std::vector<int> parts;                          // indexes into the flat partition list
    std::vector<wstr> problems;                      // plain-language notes
    DiskHealth health;
    // what kind of storage it is (DiskKind.cpp)
    int busType = 0, group = DG_FIXED, rpm = 0;      // rpm: 0 unknown, 1 no spinning parts
    bool seekKnown = false, noSeekPenalty = false, removableMedia = false;
    wstr formFactor, typeText;                       // "2.5\"", "SATA SSD · 2.5\""
    wstr id() const;    // "mbr:1A2B3C4D" or "gpt:{...}"
    wstr name() const;  // "Disk 0 · Samsung SSD · 476 GB"
};

struct FirmwareEntry {
    int num = 0;
    wstr description, path;
    GUID part{};
    bool hasPart = false, inOrder = false;
};

struct Firmware {
    bool uefi = false, secureBoot = false, varsReadable = false;
    bool secureBootKnown = false;  // read from the firmware itself (or Windows' record of it)
    bool setupMode = false;        // Secure Boot keys are cleared: it cannot be switched on until the keys are restored
    bool canOpenSettings = false;  // the firmware supports "restart into firmware settings"
    wstr maker, model;             // PC maker and model, for maker-specific hints
    std::vector<FirmwareEntry> entries;
    std::vector<int> order;
};

void classifyDisk(Disk& d);
void readDiskKind(HANDLE h, Disk& d);

struct Machine {
    Firmware fw;
    std::vector<OtherDrive> others;
    std::vector<Disk> disks;
    std::vector<Partition> parts;
};

// Reading the hardware
Machine readMachine();
Firmware readFirmware();
bool readFwVar(const wstr& name, std::string& out);           // a UEFI global variable (Boot####, BootOrder)
bool writeFwVar(const wstr& name, const std::string& data);   // empty data deletes it
// Asks the firmware to open its settings screen at the next start, then restarts the PC.
bool restartIntoFirmware(wstr& err);
bool restartPc(wstr& err);
bool shutdownPc(wstr& err);
bool hibernatePc(wstr& err);
bool canHibernate();  // the PC supports hibernation and it is switched on (never in Windows PE)
bool enablePrivilege(const wchar_t* name);
HMODULE systemDll(const wchar_t* name);
// Files Windows keeps for its system account only (the recovery partition's \Recovery\WindowsRE):
// read with the administrator's backup permission.
bool protectedFileExists(const wstr& path);
bool copyProtectedFile(const wstr& from, const wstr& to, wstr& err);  // loads a DLL from System32 by full path (only kernel32/user32/gdi32/advapi32 are linked)
void enableAllPrivileges();

// Reads bytes inside one partition (offset from the partition start). Keeps the disk open while it lives.
using ReadFn = std::function<bool(uint64_t offset, uint32_t length, std::string& out)>;
ReadFn partitionReader(const Partition& p);
// The file system's id from its first 4 KB (and the 4 KB at 64 KB for btrfs), as Linux prints it.
wstr fsUuidFrom(const wstr& fs, const std::string& first, const std::string& at64k);
wstr uuidText(const uint8_t* b16);  // 16 bytes in order -> 01234567-89ab-cdef-0123-456789abcdef

// Raw disk access (offset and length in bytes, rounded to whole sectors)
// Reads bytes anywhere on a disk; keeps the disk open while it lives. size gets the disk size.
ReadFn diskReader(int disk, uint64_t* size = nullptr);
bool readDisk(int disk, uint64_t offset, uint32_t length, std::string& out);
bool writeDisk(int disk, uint64_t offset, const std::string& data, wstr& err);
bool rewritePartitionTable(int disk, wstr& err);    // writes the same layout again (repairs a GPT backup copy)
bool setActivePartition(int disk, int number, wstr& err);
// Moving partitions: raw access with one open handle, the table entry's new start, and locking a volume.
HANDLE openDiskHandle(int disk, bool write);
uint32_t diskSectorSize(HANDLE h);
bool readDiskAt(HANDLE h, uint64_t offset, uint32_t length, std::string& out);
bool writeDiskAt(HANDLE h, uint64_t offset, const std::string& data);   // whole sectors only
bool setPartitionOffset(int disk, uint64_t oldOffset, uint64_t newOffset, wstr& err);
HANDLE lockVolume(const wstr& volume, wstr& err);                       // INVALID_HANDLE_VALUE on failure
// Adds one partition to the table without touching its contents (partition recovery). A disk
// with no table at all first gets an empty GPT (gptIfRaw) or MBR table.
bool addPartitionEntry(int disk, uint64_t offset, uint64_t length, bool gptIfRaw, const GUID& gptType, uint8_t mbrType, const wstr& name, wstr& err);

// Temporary drive letters for partitions that have none (tools such as bcdboot need one)
wstr freeLetter();
bool mountAt(const wstr& volume, const wstr& letter, wstr& err);
void unmount(const wstr& letter);

// The command console: every command and system change is reported here (thread-safe; no-op until set).
void setToolLogger(std::function<void(const wstr&)> fn);
void toolLog(const wstr& line);

// Helpers
wstr guidText(const GUID& g, bool braces = true);
bool parseGuid(const wstr& s, GUID& g);
wstr sizeText(uint64_t bytes);
uint32_t crc32(const void* data, size_t n, uint32_t crc = 0);
wstr sniffFs(const std::string& sector);  // NTFS / FAT32 / ... from a first sector
bool isEsp(const Partition& p);

}  // namespace bd

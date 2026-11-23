// Finds every Windows installation and works out, step by step, what it needs
// to start: the "boot chain". Each step gets a plain-language verdict and the
// fixes that would make it work. No user interface here.
#pragma once
#include "Disk.h"
#include "Bcd.h"
#include <functional>

namespace bd {

enum FixKind {
    FX_NONE, FX_REBUILD_BOOT, FX_ADD_ENTRY, FX_SHOW_IN_MENU, FX_MAKE_DEFAULT, FX_MARK_ACTIVE, FX_MBR_CODE, FX_CREATE_SYSPART,
    FX_FIX_GPT, FX_CHKDSK, FX_UNLOCK, FX_RESTORE_HIVE, FX_SFC, FX_ENABLE_DRIVERS, FX_CLEAR_SETUP, FX_REVERT_UPDATES,
    FX_STOP_AUTOREPAIR, FX_MOVE_TO_ROOT, FX_DELETE_BCD_ENTRY, FX_REMOVE_FROM_MENU, FX_DELETE_FW_ENTRY,
    FX_ADD_FW_ENTRY, FX_FW_FIRST, FX_DELETE_EFI_DIR, FX_LINUX_USB, FX_FIRMWARE_SETTINGS,
    FX_ENABLE_WINRE, FX_REBUILD_WINRE, FX_CLEAN_SPACE, FX_GROW_WINDOWS, FX_RENAME_SYSFOLDERS,
    FX_CREATE_MSR, FX_RESTORE_WINDOWS_OLD, FX_LAYOUT, FX_RENAME_ENTRY, FX_SET_TIMEOUT, FX_CAPTURE_KEYS,
    FX_DISM_HEAL, FX_REMOVE_UPDATE, FX_SAFE_MODE, FX_DRIVER_START, FX_BOOT_OPTION, FX_ADD_DRIVER, FX_REBUILD_ESP, FX_COUNT
};

struct Fix {
    int kind = FX_NONE;
    int install = -1, part = -1, disk = -1, store = -1;
    wstr entry;           // BCD entry id for the BCD fixes
    int fwNum = -1;       // Boot#### number for the firmware fixes
    wstr arg;             // extra input (recovery key; renamed folders: the Windows folder; clean-up: which parts, "1101")
    wstr arg2, arg3;      // renamed folders: the system folder and the settings folder inside it
    wstr title, what, takes;
    bool danger = false;      // changes partitions or cannot be undone
    bool available = true;    // the tools for it are here
    wstr notAvailable;        // why not, and what to do instead
    bool undoable = true;     // a backup can put it back
};

struct Link {
    std::string icon;
    wstr name;    // "Start-up partition"
    wstr plain;   // one or two sentences anyone can follow
    wstr tech;    // details for a technician
    wstr lesson;  // what this step is, for someone who has never heard of it
    bool aboutSecureBoot = false;  // show the Secure Boot explanation and how to switch it
    wstr cause;   // what may have caused a problem here (only for problems)
    Health health = Health::Unknown;
    std::vector<int> fixes;  // indexes into Install::plan
};

struct Install {
    int part = -1;
    wstr root;           // the partition root, ends with a backslash
    wstr folder;         // "" at the top of the drive, else e.g. "Backup\" (ends with a backslash)
    wstr winDir;         // root + folder + "Windows"
    wstr systemRoot;     // as the start-up settings write it: \Windows or \Backup\Windows
    wstr name;           // "Windows 10"
    wstr edition;        // "Pro" when known
    int major = 0, minor = 0, build = 0;
    wstr arch;           // x64 / x86 / ARM64
    bool running = false, atRoot = true, windowsOld = false;
    // how it starts
    int sysPart = -1;    // its start-up partition (found or planned)
    int store = -1;      // index into Report::stores of the store that lists it
    wstr entryId;
    bool inMenu = false, isDefault = false, active = false;
    bool moved = false;  // it was moved: a start-up entry still points to where it was
    wstr movedNote;
    // health
    Health verdict = Health::Unknown;
    wstr verdictTitle, verdictText;
    bool loop = false;
    std::vector<wstr> loopSigns;
    std::vector<Link> chain;
    std::vector<Fix> plan;
    wstr where;          // "D: · Disk 0, partition 3"
    // Linux
    bool linux = false;
    bool readable = true;     // the app can look inside its partition (ext2/3/4)
    wstr distroId;            // ubuntu, fedora, ...
    int bootPart = -1;        // a separate /boot partition
    std::vector<wstr> kernels;
    int loader = -1;          // index into Report::linuxLoaders
    wstr label() const;  // "Windows 10 (64-bit) on D:"
};

struct Finding {
    Health health = Health::Info;
    wstr title, text;
    wstr lesson;  // what the thing it is about is, in plain words
    std::vector<Fix> fixes;
};

// A Linux boot loader found on an EFI partition
struct LinuxLoader {
    int esp = -1;
    wstr dir;           // EFI\ubuntu
    wstr file;          // EFI\ubuntu\shimx64.efi (the one the firmware starts)
    bool shim = false, systemdBoot = false, fullConfig = false;
    std::string uuid;   // where its settings point
    std::string prefix;
    bool used = false;
};

// What this program is running in, explained, with what to do next.
struct Environment {
    bool winPE = false, winRE = false, virtualMachine = false, vhdBoot = false, safeMode = false;
    wstr vmName;
    wstr title, text, next;  // empty title: an ordinary Windows on a real PC
};

struct Report {
    Machine m;
    std::vector<BcdStore> stores;
    std::vector<Install> installs;
    std::vector<Finding> pc;
    std::vector<LinuxLoader> linuxLoaders;
    wstr host;            // what this app is running on
    bool hostPE = false;
    SYSTEMTIME when{};
    bool demo = false;
    Environment env;
    Install pcChain;      // the boot chain of the whole PC, drawn when no system was found
    // How the EFI-partition step reads a partition's raw sectors. scan() sets it to the
    // real reader; left empty by the self-test and demo, which have no real disks, so the
    // step then reports from what is already known instead of reading the hardware.
    std::function<ReadFn(const Partition&)> rawReader;
    int problems() const;
};

using Progress = std::function<void(const wstr& step, float frac)>;

Report scan(const Progress& progress);
// The parts of scan that only look at files, so the self-test can run them on folders.
void findInstalls(const wstr& root, int part, std::vector<Install>& out);
void diagnose(Report& r);
Environment detectEnvironment(const Report& r);
void fillEnvironmentText(Environment& e);  // the explanation and next steps for the flags set
void addCauses(Install& in);
void buildPcChain(Report& r);
wstr osName(int major, int minor, int build);
bool fileVersion(const wstr& path, int& major, int& minor, int& build);
wstr peArch(const wstr& path);  // x64 / x86 / ARM64 / "" (not a program file)
bool looksLikeProgram(const wstr& path);  // exists, not empty, starts with MZ
Fix makeFix(int kind);
wstr lessonFor(const wstr& stepName);  // a short plain explanation of a boot chain step
void applyLessons(Install& in);
wstr systemKindFromPath(const wstr& efiPath);  // "Windows", "Ubuntu (Linux)", ... from \EFI\<folder>\...
extern const wchar_t* const kLessonHowPcStarts;
extern const wchar_t* const kLessonMenuEntry;
extern const wchar_t* const kLessonFirmwareList;
extern const wchar_t* const kLessonPartitionTable;
extern const wchar_t* const kLessonSecureBoot;
// Step-by-step: how to switch Secure Boot on or off, with hints for the PC's maker.
wstr secureBootHowTo(const Firmware& fw, bool turnOn);
wstr secureBootState(const Firmware& fw);  // "Secure Boot is on" / ... in plain words
// When no Windows was found: looks deeper for Windows' own folders under other names
// (Windows, System32, config renamed), so they can be named back.
void deepScanSystemFolders(Report& r, int budget = 60000);
struct RenamedWindows {
    int part = -1;
    wstr windowsRel;  // the folder that holds the system folder, from the drive root ("Win_backup" or "Old\Win")
    wstr systemName;  // the system folder's name now ("System32" when unchanged)
    wstr configName;  // the settings folder's name now ("config" when unchanged)
};
std::vector<RenamedWindows> findRenamedWindows(const wstr& root, int part, int budget);
bool anyLockedDrive(const Report& r);
// Windows' own record of its repair tools (System32\Recovery\ReAgent.xml)
struct ReAgentInfo {
    bool found = false, enabled = false;
    uint64_t offset = 0;  // the partition that holds them (bytes from the disk start), 0 = none
};
ReAgentInfo readReAgent(const wstr& winDir);
// Start-up entries (in BCD stores and in the PC's own list) that lead to nothing.
void findDeadEntries(Report& r);
// Start-up settings that are empty, name a missing first choice, or cannot be read; each with what to do.
void findStoreProblems(Report& r);
wstr reportText(const Report& r);
const wchar_t* healthWord(Health h);

}  // namespace bd

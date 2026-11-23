#include "Diagnose.h"
#include "Lang.h"
#include "Hive.h"
#include "core/Platform.h"
#include "Linux.h"
#include "Space.h"
#include "DiskTool.h"
#include "EfiCheck.h"

namespace bd {

// ---------------------------------------------------------------- small helpers
static bool exists(const wstr& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
static bool isDir(const wstr& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static int64_t fileSize(const wstr& p) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d)) return -1;
    return ((int64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}
static ULONGLONG fileTime(const wstr& p) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d)) return 0;
    return ((ULONGLONG)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime;
}
static ULONGLONG nowTime() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}
static const ULONGLONG kDay = 864000000000ull;
static wstr dateText(ULONGLONG t) {
    FILETIME ft{(DWORD)t, (DWORD)(t >> 32)}, lt;
    SYSTEMTIME st{};
    FileTimeToLocalFileTime(&ft, &lt);
    FileTimeToSystemTime(&lt, &st);
    static const wchar_t* mon[] = {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};
    wchar_t b[48];
    swprintf(b, 48, L"%d %s %d", st.wDay, mon[(st.wMonth + 11) % 12], st.wYear);
    return b;
}

const wchar_t* healthWord(Health h) {
    switch (h) {
        case Health::Ok: return N(L"Fine");
        case Health::Info: return N(L"Note");
        case Health::Warn: return N(L"Warning");
        case Health::Bad: return N(L"Problem");
        default: return N(L"Not checked");
    }
}

int Report::problems() const {
    int n = 0;
    for (const Install& i : installs)
        if (i.verdict == Health::Bad || i.verdict == Health::Warn) ++n;
    for (const Finding& f : pc)
        if (f.health == Health::Bad || f.health == Health::Warn) ++n;
    return n;
}

wstr Install::label() const {
    wstr s = name;
    if (!arch.empty()) s += arch == L"x86" ? T(L" (32-bit)") : arch == L"ARM64" ? T(L" (ARM)") : T(L" (64-bit)");
    return s;
}

bool looksLikeProgram(const wstr& path) {
    std::string head;
    if (!readFileText(path, head, 2)) return false;
    return head.size() == 2 && head[0] == 'M' && head[1] == 'Z';
}

wstr peArch(const wstr& path) {
    std::string h;
    if (!readFileText(path, h, 4096) || h.size() < 0x40 || h[0] != 'M' || h[1] != 'Z') return L"";
    uint32_t pe;
    memcpy(&pe, &h[0x3C], 4);
    if (pe + 6 > h.size() || memcmp(&h[pe], "PE\0\0", 4)) return L"";
    uint16_t machine;
    memcpy(&machine, &h[pe + 4], 2);
    switch (machine) {
        case 0x8664: return L"x64";
        case 0x014C: return L"x86";
        case 0xAA64: return L"ARM64";
    }
    return L"";
}

bool fileVersion(const wstr& path, int& major, int& minor, int& build) {
    HMODULE m = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | 0x20 /* AS_IMAGE_RESOURCE */);
    if (!m) m = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (!m) return false;
    bool ok = false;
    HRSRC r = FindResourceW(m, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(16) /* RT_VERSION */);
    if (r) {
        const uint8_t* p = (const uint8_t*)LockResource(LoadResource(m, r));
        DWORD n = SizeofResource(m, r);
        for (DWORD i = 0; p && i + 52 <= n; i += 4) {
            uint32_t sig;
            memcpy(&sig, p + i, 4);
            if (sig != 0xFEEF04BD) continue;
            uint32_t ms, ls;
            memcpy(&ms, p + i + 16, 4);  // product version
            memcpy(&ls, p + i + 20, 4);
            major = (int)(ms >> 16);
            minor = (int)(ms & 0xFFFF);
            build = (int)(ls >> 16);
            ok = true;
            break;
        }
    }
    FreeLibrary(m);
    return ok;
}

wstr osName(int major, int minor, int build) {
    if (major == 10) return build >= 22000 ? L"Windows 11" : L"Windows 10";
    if (major == 6 && minor == 3) return L"Windows 8.1";
    if (major == 6 && minor == 2) return L"Windows 8";
    if (major == 6 && minor == 1) return L"Windows 7";
    if (major == 6 && minor == 0) return L"Windows Vista";
    if (major == 5) return L"Windows XP";
    return T(L"Windows");
}

// ---------------------------------------------------------------- fixes
Fix makeFix(int kind) {
    Fix f;
    f.kind = kind;
    switch (kind) {
        case FX_REBUILD_BOOT:
            f.title = T(L"Rebuild the start-up files");
            f.what = T(L"Copies fresh start-up files from this Windows to the start-up partition, writes a new start-up entry for it and puts it first.");
            f.takes = T(L"About a minute. Personal files are not touched. The old start-up files are backed up first.");
            break;
        case FX_ADD_ENTRY:
            f.title = T(L"Add it to the start-up menu");
            f.what = T(L"Adds this Windows to the menu shown when the PC starts, after the systems that are already there.");
            f.takes = T(L"About a minute. The other systems keep starting as before.");
            break;
        case FX_SHOW_IN_MENU:
            f.title = T(L"Show it in the start-up menu");
            f.what = T(L"This Windows has a start-up entry, but the entry is hidden from the menu. This puts it back in the menu.");
            f.takes = T(L"A few seconds.");
            break;
        case FX_MAKE_DEFAULT:
            f.title = T(L"Make it start first");
            f.what = T(L"Makes this Windows the one that starts when nobody chooses in the start-up menu.");
            f.takes = T(L"A few seconds.");
            break;
        case FX_MARK_ACTIVE:
            f.title = T(L"Mark the start-up partition as active");
            f.what = T(L"Older PCs (legacy BIOS) only start from the partition marked \"active\". This sets that mark on the right partition.");
            f.takes = T(L"A few seconds. The partition table is backed up first.");
            break;
        case FX_MBR_CODE:
            f.title = T(L"Write new start-up code to the disk");
            f.what = T(L"Writes fresh start-up code to the first sector of the disk and of the start-up partition, so an older PC (legacy BIOS) can start from it.");
            f.takes = T(L"A few seconds. The old first sectors are backed up first. Other systems' start-up menus (for example Linux GRUB) on this disk are replaced.");
            f.danger = true;
            break;
        case FX_CREATE_SYSPART:
            f.title = T(L"Create a new start-up partition");
            f.what = T(L"Makes a small start-up partition (300 MB) in free space on the disk, taking the room from the end of the Windows partition if needed, and puts fresh start-up files on it.");
            f.takes = T(L"A few minutes. The Windows partition gets 300 MB smaller. Copy important files somewhere safe first if you can.");
            f.danger = true;
            break;
        case FX_FIX_GPT:
            f.title = T(L"Repair the partition table");
            f.what = T(L"Writes both copies of the partition table again from the copy that Windows can still read. The partitions themselves are not moved.");
            f.takes = T(L"A few seconds. The old table is backed up first.");
            f.danger = true;
            break;
        case FX_CHKDSK:
            f.title = T(L"Check and repair the drive");
            f.what = T(L"Runs the Windows disk check, which repairs damaged file-system records. Files in damaged places can be lost, as with any disk check.");
            f.takes = T(L"From a few minutes to an hour on a large drive. This cannot be undone.");
            f.undoable = false;
            break;
        case FX_UNLOCK:
            f.title = T(L"Unlock with the BitLocker recovery key");
            f.what = T(L"This drive is encrypted. Type its 48-digit recovery key to unlock it, so it can be checked and repaired. You find the key in your Microsoft account (aka.ms/myrecoverykey), on a printout, or with your IT department.");
            f.takes = T(L"A few seconds. Nothing is changed on the drive.");
            break;
        case FX_RESTORE_HIVE:
            f.title = T(L"Put back the registry's backup copy");
            f.what = T(L"Windows' settings file (registry) is damaged. Windows keeps an automatic backup copy of it, and this puts that copy back. Settings changed since the backup was made are lost.");
            f.takes = T(L"A few seconds. The damaged file is kept in the backup, so it can be put back too.");
            break;
        case FX_SFC:
            f.title = T(L"Repair Windows system files");
            f.what = T(L"Runs the Windows file checker, which puts back damaged or missing Windows files from Windows' own store of originals.");
            f.takes = T(L"10 to 30 minutes. Personal files and programs are not touched. This cannot be undone, but it only puts back Windows' own files.");
            f.undoable = false;
            break;
        case FX_ENABLE_DRIVERS:
            f.title = T(L"Turn the disk drivers back on");
            f.what = T(L"Windows needs a few drivers to read its own disk while it starts. Some of them are turned off, which causes the stop error INACCESSIBLE_BOOT_DEVICE. This turns them back on.");
            f.takes = T(L"A few seconds. The settings file is backed up first.");
            break;
        case FX_CLEAR_SETUP:
            f.title = T(L"Clear the unfinished set-up mark");
            f.what = T(L"Windows is marked as still being set up, so it keeps restarting into set-up (\"The computer restarted unexpectedly\"). This clears the mark. Only do this on a Windows that was already working before.");
            f.takes = T(L"A few seconds. The settings file is backed up first.");
            f.danger = true;
            break;
        case FX_REVERT_UPDATES:
            f.title = T(L"Undo unfinished updates");
            f.what = T(L"Windows updates that were half installed can make Windows restart over and over. This undoes the updates that are still waiting to finish.");
            f.takes = T(L"5 to 20 minutes. Those updates can be installed again later from Windows Update.");
            f.undoable = false;
            break;
        case FX_STOP_AUTOREPAIR:
            f.title = T(L"Show the real error instead of Automatic Repair");
            f.what = T(L"Turns off Automatic Repair for this Windows, so the PC stops going round in circles and shows the actual error message on screen. You can turn it back on with Undo.");
            f.takes = T(L"A few seconds.");
            break;
        case FX_MOVE_TO_ROOT:
            f.title = T(L"Move Windows back to the top of the drive");
            f.what = T(L"This Windows sits inside a folder, where it cannot start. This moves its folders (Windows, Program Files, Users and the rest) back to the top of the drive, where they belong.");
            f.takes = T(L"A few seconds: the files are only renamed, not copied. Undo moves them back.");
            break;
        case FX_DELETE_BCD_ENTRY:
            f.title = T(L"Remove this entry");
            f.what = T(L"Deletes this line from the start-up settings, so it no longer appears in the start-up menu. No Windows, files or partitions are touched.");
            f.takes = T(L"A few seconds. The start-up settings are backed up first, so Undo brings the entry back.");
            break;
        case FX_REMOVE_FROM_MENU:
            f.title = T(L"Only hide it from the menu");
            f.what = T(L"Keeps the entry in the start-up settings but takes it off the start-up menu. Choose this if you think the system may come back (for example a disk you will connect again).");
            f.takes = T(L"A few seconds. Undo puts it back in the menu.");
            break;
        case FX_FIRMWARE_SETTINGS:
            f.title = T(L"Restart into the PC's firmware settings");
            f.what = T(L"Restarts the PC straight into its firmware settings screen (where Secure Boot, the start-up order and CSM are switched). Close your programs first.");
            f.takes = T(L"The PC restarts at once. Nothing on the disks changes.");
            f.undoable = false;
            break;
        case FX_ADD_FW_ENTRY:
            f.title = T(L"Add Linux to the PC's start-up list");
            f.what = T(L"Writes a new entry into the start-up list kept inside the PC, pointing to this Linux's boot loader. Nothing on any disk changes.");
            f.takes = T(L"A few seconds. Undo removes the entry again.");
            break;
        case FX_FW_FIRST:
            f.title = T(L"Make Linux start first");
            f.what = T(L"Moves this Linux to the top of the PC's start-up list. Its boot loader (GRUB) usually shows a menu where Windows can still be chosen.");
            f.takes = T(L"A few seconds. Undo puts the old order back.");
            break;
        case FX_DELETE_EFI_DIR:
            f.title = T(L"Remove the leftover boot loader");
            f.what = T(L"Deletes the boot loader folder of a Linux that is gone from the start-up partition, and its line in the PC's start-up list.");
            f.takes = T(L"A few seconds. The folder and the entry are backed up first, so Undo brings them back.");
            break;
        case FX_LINUX_USB:
            f.title = T(L"Repair Linux from a Linux USB stick");
            f.what = T(L"This needs to be done from Linux itself:");
            f.takes = T(L"About 15 minutes with a Linux USB stick and an internet connection.");
            f.available = false;
            break;
        case FX_ENABLE_WINRE:
            f.title = T(L"Switch the troubleshoot options back on");
            f.what = T(L"Windows' repair tools (the blue \"Choose an option\" screen with Troubleshoot, Startup Repair and System Restore) are on this PC but switched off. This switches them on again with Windows' own tool (reagentc).");
            f.takes = T(L"A few seconds. No partition changes.");
            break;
        case FX_REBUILD_WINRE:
            f.title = T(L"Recreate the recovery partition");
            f.what = T(L"Makes a new hidden recovery partition (about 1 GB) and puts Windows' repair tools on it, so the blue \"Choose an option\" screen with Troubleshoot, Startup Repair and System Restore works again. Uses unused space or an empty recovery partition if there is one; otherwise it takes 1 GB from the end of the Windows partition.");
            f.takes = T(L"A few minutes. Making the Windows partition smaller is safe in almost all cases, but a power cut during it can damage the drive: back up important files first, and keep a laptop plugged in. This cannot be undone here.");
            f.danger = true;
            f.undoable = false;
            break;
        case FX_CLEAN_SPACE:
            f.title = T(L"Free up space");
            f.what = T(L"Removes files Windows does not need: temporary files, downloaded updates, crash reports and, if you choose, browser caches. You see how much each frees before anything is removed.");
            f.takes = T(L"A minute or two. Personal files, programs and settings are not touched. Removed files cannot be put back (none of them are needed).");
            f.undoable = false;
            break;
        case FX_GROW_WINDOWS:
            f.title = T(L"Make the Windows partition bigger");
            f.what = T(L"Adds unused space right after the Windows partition to it, or, if you choose, deletes the partition right after it and adds that space. Space before it cannot be used. You see a preview of the disk before anything changes.");
            f.takes = T(L"A minute. Changing partitions always carries a small risk: back up important files first. Deleting the partition after it erases everything on that partition.");
            f.danger = true;
            f.undoable = false;
            break;
        case FX_RENAME_SYSFOLDERS:
            f.title = T(L"Give Windows' folders their names back");
            f.what = T(L"Windows only starts from folders with their own names (Windows, System32, config). This renames the folders found back to those names. Nothing is copied or deleted.");
            f.takes = T(L"A few seconds. Undo renames them back.");
            break;
        case FX_CREATE_MSR:
            f.title = T(L"Create the reserved partition (MSR)");
            f.what = T(L"Makes the small 16 MB reserved partition Windows expects on a GPT disk, right after the start-up partition when there is room there, otherwise at the end of the Windows partition.");
            f.takes = T(L"A few seconds. If there is no unused space, the Windows partition gives up 16 MB. The partition table is backed up first.");
            f.danger = true;
            break;
        case FX_LAYOUT:
            f.title = T(L"Put the partitions in Windows' own order…");
            f.what = T(L"Opens a plan that puts this disk's partitions in the order Windows Setup makes them (start-up, reserved, Windows, recovery) by moving and resizing them, step by step, each one shown and confirmed first. Only from the rescue USB stick or another Windows, never on the disk in use.");
            f.takes = T(L"From minutes to hours, depending on how much must be moved. Back up important files first.");
            f.danger = true;
            f.undoable = false;
            break;
        case FX_DISM_HEAL:
            f.title = T(L"Repair the Windows image (DISM)");
            f.what = T(L"Checks Windows' store of its own components and puts back damaged ones. The running Windows takes good copies from Windows Update; a Windows that does not start takes them from Windows install media of the same version (install.wim or install.esd). Run \"Repair Windows system files\" afterwards.");
            f.takes = T(L"15 to 60 minutes. Personal files and programs are not touched. This cannot be undone, but it only puts back Windows' own parts.");
            f.undoable = false;
            break;
        case FX_REMOVE_UPDATE:
            f.title = T(L"Remove a recent update…");
            f.what = T(L"Shows the updates installed last and removes the one you choose. If Windows started failing right after an update, removing that update often makes it start again.");
            f.takes = T(L"A few minutes. Windows Update may offer the update again later; pause updates for a while if it caused trouble.");
            f.undoable = false;
            break;
        case FX_SAFE_MODE:
            f.title = T(L"Start in Safe Mode next time");
            f.what = T(L"Makes this Windows start in Safe Mode (only the most basic drivers) until it is switched back, so a faulty driver or program can be removed. Switch it back with \"Normal start again\".");
            f.takes = T(L"A few seconds. Undo switches it back.");
            break;
        case FX_DRIVER_START:
            f.title = T(L"Switch this driver off");
            f.what = T(L"Stops Windows from loading this driver when it starts (its Start value is set to 4, in every control set). The driver's files stay. Use it for a driver of another maker that makes Windows crash while starting; never for Microsoft's own disk and file-system drivers.");
            f.takes = T(L"A few seconds. The settings file is backed up first, so Undo switches it on again.");
            f.danger = true;
            break;
        case FX_ADD_DRIVER:
            f.title = T(L"Add a driver to this Windows");
            f.what = T(L"Puts a driver (its .inf, .sys and .cat files) into this Windows' own driver store, so Windows loads it the next time it starts. This is what a Windows needs when it stops with INACCESSIBLE_BOOT_DEVICE because it has no driver for its disk controller (Intel VMD / RST, RAID).");
            f.takes = T(L"About a minute. The driver must be 64-bit and made for this Windows version. This cannot be undone here (the driver can be removed in Windows' Device Manager).");
            f.undoable = false;
            break;
        case FX_BOOT_OPTION:
            f.title = T(L"Change a start-up option");
            f.what = T(L"Changes how this Windows starts the next time.");
            f.takes = T(L"A few seconds. Undo puts the old setting back.");
            break;
        case FX_CAPTURE_KEYS:
            f.title = T(L"Save the BitLocker recovery keys now…");
            f.what = T(L"Reads the 48-digit recovery key of every unlocked BitLocker drive while Windows is running (Windows can show it now; once Windows does not start, it cannot), and saves them next to this program, locked with your password.");
            f.takes = T(L"A few seconds. Nothing on the drives changes.");
            f.undoable = false;
            break;
        case FX_RENAME_ENTRY:
            f.title = T(L"Rename it in the start-up menu");
            f.what = T(L"Changes the name shown for this Windows in the start-up menu, so several Windows can be told apart.");
            f.takes = T(L"A few seconds. Undo puts the old name back.");
            break;
        case FX_SET_TIMEOUT:
            f.title = T(L"Set how long the start-up menu waits");
            f.what = T(L"Sets how many seconds the start-up menu is shown before the first Windows starts by itself.");
            f.takes = T(L"A few seconds. Undo puts the old time back.");
            break;
        case FX_RESTORE_WINDOWS_OLD:
            f.title = T(L"Bring back the Windows in Windows.old");
            f.what = T(L"Puts the earlier Windows kept in Windows.old back in place: the current Windows, Program Files, ProgramData and Users folders are moved into a new folder (Windows.replaced), the ones in Windows.old are moved to the top of the drive, and fresh start-up files are written for it.");
            f.takes = T(L"A minute: folders are only moved, not copied. Programs installed and settings changed since the update are lost (they stay in Windows.replaced). Undo moves everything back.");
            f.danger = true;
            break;
        case FX_DELETE_FW_ENTRY:
            f.title = T(L"Remove it from the PC's start-up list");
            f.what = T(L"Deletes this line from the list of start-up choices kept inside the PC itself (not on any disk). Nothing on any disk changes.");
            f.takes = T(L"A few seconds. The entry is backed up first, so Undo brings it back.");
            break;
        case FX_REBUILD_ESP:
            f.title = T(L"Rebuild the start-up partition");
            f.what = T(L"The start-up (EFI) partition's file system is damaged or cannot be written to, so the start-up files cannot be read or repaired. This makes a fresh, empty FAT32 file system on it (the partition itself and its type stay) and writes fresh start-up files for this Windows. Any other makers' boot folders on it (HP, Dell, Lenovo tools, Linux GRUB) are kept and put back.");
            f.takes = T(L"A minute or two. A full image of the partition is saved first, so Undo puts it back exactly. If the Windows drive is encrypted with BitLocker, have its recovery key ready.");
            f.danger = true;
            break;
    }
    return f;
}

// ---------------------------------------------------------------- lessons
const wchar_t* const kLessonHowPcStarts =
    N(L"Starting a PC is like a relay race: each runner hands a baton to the next. First the PC's built-in program (firmware) wakes up and looks at the disk. On the disk it finds a small start-up partition with Windows Boot Manager. The boot manager reads a short list of start-up settings to learn where Windows is, then hands over to the Windows loader, which loads the heart of Windows, its drivers and its settings. If any runner drops the baton, starting stops there. The Boot chain view shows these runners in order, so you can see exactly where it stops.");
const wchar_t* const kLessonMenuEntry =
    N(L"What is a start-up menu entry? When a PC has more than one system, a menu appears when it starts. Each line in that menu is an entry that says where a system lives (which disk, which partition, which folder). When that system is deleted, moved, or its disk is taken out, the entry stays behind and leads nowhere. Removing such an entry deletes nothing else: it only tidies up the menu.");
const wchar_t* const kLessonFirmwareList =
    N(L"What is the PC's own start-up list? Newer PCs keep a short list inside the PC itself (in the firmware, not on any disk) of things they can start: Windows Boot Manager, Linux, USB sticks, the network. You see it in the boot menu that opens with a key while the PC starts (often F12, F11, F8 or Esc). When a system is removed, its line can stay in that list. Removing the line changes nothing on any disk.");
const wchar_t* const kLessonPartitionTable =
    N(L"What is a partition table? A disk is divided into parts called partitions, for example a small start-up partition and a big C: drive. A tiny table at the very start of the disk (with a spare copy at the end, on newer disks) records where each part begins and ends, like the table of contents of a book. If the table is damaged, the PC cannot find anything on the disk, even though the files are still there.");

const wchar_t* const kLessonSecureBoot =
    N(L"What is Secure Boot? It is a safety check built into newer PCs. When it is on, the PC only starts programs that carry a valid digital signature, like a passport check at the door. That stops harmful programs (bootkits) from hiding in the start-up process before Windows or your antivirus runs.\n\nWindows 8, 10 and 11 are signed, and so are most big Linux systems (Ubuntu, Fedora, openSUSE, Mint) through a helper called shim. Windows 7, older Windows, many smaller Linux systems and some repair tools are not signed, so they only start with Secure Boot off.\n\nKeep it on if everything you use starts with it on. Turn it off only for a system or tool that refuses to start, and back on afterwards if you can. Switching it does not change or delete anything on the disks. If the Windows drive is encrypted with BitLocker, have its recovery key ready: Windows may ask for it once after Secure Boot is switched.");

wstr secureBootState(const Firmware& fw) {
    if (!fw.uefi) return T(L"This PC was started in legacy (BIOS) mode, which has no Secure Boot. Secure Boot only works in UEFI mode.");
    if (!fw.secureBootKnown) return T(L"Whether Secure Boot is on could not be read here.");
    if (fw.secureBoot) return T(L"Secure Boot is on.");
    if (fw.setupMode) return T(L"Secure Boot is off, and its keys are cleared (\"Setup mode\"). To switch it on, first restore the factory keys in the firmware settings.");
    return T(L"Secure Boot is off.");
}

wstr secureBootHowTo(const Firmware& fw, bool on) {
    wstr maker = toLower(fw.maker), key = T(L"Delete or F2"), where = T(L"the Boot or Security tab");
    if (icontains(maker, L"hewlett") || maker == L"hp") { key = L"Esc, then F10"; where = L"Advanced (or System Configuration) > Secure Boot Configuration"; }
    else if (icontains(maker, L"dell")) { key = L"F2"; where = L"Boot Configuration (or Secure Boot) > Secure Boot Enable"; }
    else if (icontains(maker, L"lenovo")) { key = L"F1 (ThinkPad, ThinkCentre) or F2 (IdeaPad, Legion)"; where = L"Security > Secure Boot"; }
    else if (icontains(maker, L"asus")) { key = L"Delete or F2"; where = L"Boot > Secure Boot (in Advanced mode, F7)"; }
    else if (icontains(maker, L"acer")) { key = L"F2"; where = L"Boot (set a supervisor password first if the option is greyed out)"; }
    else if (icontains(maker, L"micro-star") || maker == L"msi") { key = L"Delete"; where = L"Settings > Security > Secure Boot"; }
    else if (icontains(maker, L"gigabyte")) { key = L"Delete"; where = L"Boot > Secure Boot (CSM Support must be off)"; }
    else if (icontains(maker, L"microsoft")) { key = L"Volume up while pressing power"; where = L"Security > Secure Boot"; }
    else if (icontains(maker, L"samsung")) { key = L"F2"; where = L"Boot > Secure Boot Control"; }
    else if (icontains(maker, L"toshiba") || icontains(maker, L"dynabook")) { key = L"F2"; where = L"Security > Secure Boot"; }
    else if (icontains(maker, L"vmware") || icontains(maker, L"innotek") || icontains(maker, L"qemu")) { key = L"the virtual machine's settings"; where = L"its firmware or boot options"; }
    wstr s = wstr(on ? T(L"How to switch Secure Boot on:\n") : T(L"How to switch Secure Boot off:\n"));
    s += fw.canOpenSettings ? T(L"1. Use \"Restart into the PC's firmware settings\", or restart the PC and press ") + key + T(L" right after switching it on.\n")
                            : T(L"1. Restart the PC and press ") + key + T(L" right after switching it on, until the firmware settings appear.\n");
    s += L"2. Go to " + where + L".\n";
    if (on) {
        s += T(L"3. Set \"Secure Boot\" to Enabled. If it is greyed out: switch \"CSM\" or \"Legacy\" support off and set the OS type to \"Windows UEFI mode\".\n");
        s += T(L"4. If it says Setup mode or keys are missing, choose \"Restore factory keys\" (or \"Install default keys\").\n");
    } else {
        s += T(L"3. Set \"Secure Boot\" to Disabled. On some PCs you first set the OS type to \"Other OS\", or set a supervisor password to unlock the option.\n");
        s += T(L"4. Leave CSM / Legacy as it is, unless the system you want to start needs it.\n");
    }
    s += T(L"5. Save and exit (often F10). The PC restarts.");
    if (!fw.maker.empty()) s += T(L"\n\nThis PC: ") + fw.maker + (fw.model.empty() ? L"" : L" " + fw.model) + L". The names in its menus may differ a little.";
    return s;
}

wstr lessonFor(const wstr& n) {
    if (n == L"PC firmware")
        return T(L"Every PC has a small built-in program on a chip on its main board, called the firmware (UEFI on newer PCs, BIOS on older ones). It is the first thing that runs when you press the power button. It checks the hardware and then looks for a disk to start from. UEFI and BIOS start Windows in different ways, so Windows must be set up for the way your PC starts.");
    if (n == L"Disk")
        return T(L"The disk (hard disk or SSD) stores Windows and your files. At its very start there is a small table that says how the disk is divided into parts (partitions), like the table of contents of a book. If the disk cannot be read or that table is damaged, the PC cannot find Windows.");
    if (n == L"Start-up partition")
        return T(L"Besides the big partition with Windows (usually C:), the disk has a small hidden partition used only for starting: the EFI system partition on newer PCs, or the \"active\" partition on older ones. It holds the few files the PC needs to begin starting Windows. You normally never see it, and it has no drive letter.");
    if (n == L"Start-up partition files")
        return T(L"The start-up partition holds its files in a FAT32 file system, with a little map (the FAT, kept in two copies) saying where each file sits. If that map or the partition's first sector is damaged, the firmware can no longer read the start-up files, even though the partition is there and has the right size. This is the hidden fault behind \"bootrec /fixboot: Access is denied\": the files look present, but the file system underneath them is broken, so nothing can read or repair them.");
    if (n == L"Boot manager")
        return T(L"Windows Boot Manager is a small program on the start-up partition. The firmware starts it, and it decides which Windows to start, showing a menu when there is more than one. On newer PCs the PC also keeps its own list of start-up choices, and Windows Boot Manager must be in it.");
    if (n == L"Start-up settings")
        return T(L"The start-up settings (called BCD) are a short list that Windows Boot Manager reads. Each line says \"this Windows is on this partition, in this folder\". If the line is missing or points to the wrong place, that Windows cannot start, even if Windows itself is perfectly fine.");
    if (n == L"Windows partition")
        return T(L"This is the partition where Windows is installed, usually drive C:. To start, it must be readable: not damaged, not locked by BitLocker encryption, and with some free space left.");
    if (n == L"Windows loader")
        return T(L"The Windows loader (a file called winload) is what actually starts loading Windows into the PC's memory. Windows Boot Manager hands over to it.");
    if (n == L"Core system files")
        return T(L"These are the heart of Windows (the kernel) and the first drivers it needs, for example the ones that let Windows read its own disk. If one of them is missing or damaged, Windows stops with a blue screen while it starts.");
    if (n == L"System settings")
        return T(L"Windows keeps its settings in a few special files called the registry. Windows reads them very early, for example to know which drivers to load. A damaged registry file means Windows cannot finish starting.");
    if (n == L"Starting up")
        return T(L"The last step: Windows starts its services and shows the sign-in screen. If something goes wrong here, Windows may restart over and over (a restart loop) or keep opening Automatic Repair.");
    if (n == L"PC's start-up list")
        return T(L"Newer PCs keep a short list inside the PC itself (not on any disk) of systems they can start, in order. The PC starts the first one that works. Windows and Linux each add their own line. Windows updates sometimes move Windows back to the top, so Linux then seems to have vanished; it is only lower in the list.");
    if (n == L"Linux boot loader")
        return T(L"Linux is started by a small program called GRUB (sometimes with a helper called shim, which makes it work with Secure Boot, or by systemd-boot). It lives in a folder on the start-up partition, next to Windows Boot Manager.");
    if (n == L"GRUB settings")
        return T(L"A tiny settings file next to GRUB says on which partition Linux lives, using that partition's id. If the partition was formatted or deleted, the id no longer matches anything and GRUB stops at a \"grub rescue>\" prompt.");
    if (n == L"Disk start-up code")
        return T(L"On older PCs (legacy BIOS) the very first sector of the disk holds a little start-up program. For Linux this is GRUB. Installing or repairing Windows often replaces it with Windows' own, and then the PC no longer offers Linux.");
    if (n == L"Linux partition")
        return T(L"This is the partition where Linux is installed. Linux uses its own file systems (ext4, btrfs, XFS), which Windows cannot open. This program can look inside ext2, ext3 and ext4, read-only; it never writes to them.");
    if (n == L"Linux kernel")
        return T(L"The kernel is the heart of Linux, kept in the /boot folder as files named vmlinuz. Linux keeps one or two older kernels too, so that if an update goes wrong you can pick an older one in the GRUB menu.");
    if (n == L"GRUB menu")
        return T(L"The GRUB menu is the list of choices GRUB shows when the PC starts (Linux, older kernels, often Windows too). It is a file called grub.cfg. Without it, GRUB only shows a bare \"grub>\" prompt.");
    if (n == L"Linux settings")
        return T(L"Linux keeps a list of the partitions it uses (the file /etc/fstab): where Linux is, where /boot is, the swap space and so on. If a partition in that list is gone, Linux stops while starting and shows an \"emergency mode\" prompt.");
    if (n == L"Location")
        return T(L"Windows only starts when it sits at the top of its drive (for example C:\\Windows). A Windows inside another folder is usually a copy made while backing up or moving files, or the old Windows kept after an upgrade (Windows.old).");
    return L"";
}

void applyLessons(Install& in) {
    for (Link& l : in.chain)
        if (l.lesson.empty()) l.lesson = lessonFor(l.name);
}

wstr systemKindFromPath(const wstr& path) {
    wstr p = toLower(path);
    if (p.find(L"\\microsoft\\") != wstr::npos || p.find(L"bootmgfw") != wstr::npos) return L"Windows";
    static const struct { const wchar_t *dir, *name; } known[] = {
        {L"ubuntu", N(L"Ubuntu (Linux)")}, {L"debian", N(L"Debian (Linux)")}, {L"fedora", N(L"Fedora (Linux)")}, {L"opensuse", N(L"openSUSE (Linux)")},
        {L"arch", N(L"Arch Linux")}, {L"manjaro", N(L"Manjaro (Linux)")}, {L"centos", N(L"CentOS (Linux)")}, {L"rocky", N(L"Rocky Linux")}, {L"almalinux", N(L"AlmaLinux")},
        {L"redhat", N(L"Red Hat (Linux)")}, {L"kali", N(L"Kali Linux")}, {L"linuxmint", N(L"Linux Mint")}, {L"pop", N(L"Pop!_OS (Linux)")}, {L"zorin", N(L"Zorin OS (Linux)")},
        {L"neon", N(L"KDE neon (Linux)")}, {L"elementary", N(L"elementary OS (Linux)")}, {L"endeavouros", N(L"EndeavourOS (Linux)")}, {L"systemd", N(L"Linux")},
        {L"grub", N(L"Linux (GRUB)")}, {L"refind", N(L"rEFInd boot menu")}, {L"clover", N(L"Clover boot menu")}, {L"opencore", N(L"OpenCore boot menu")},
        {L"hp", N(L"PC maker's tools (HP)")}, {L"dell", N(L"PC maker's tools (Dell)")}, {L"lenovo", N(L"PC maker's tools (Lenovo)")}, {L"boot", N(L"a USB stick or another system")}};
    size_t a = p.find(L"\\efi\\");
    if (a != wstr::npos) {
        size_t b = p.find(L'\\', a + 5);
        wstr dir = p.substr(a + 5, b == wstr::npos ? wstr::npos : b - a - 5);
        for (const auto& k : known)
            if (dir == k.dir) return T(k.name);
    }
    if (p.find(L"shim") != wstr::npos || p.find(L"grub") != wstr::npos) return L"Linux";
    return T(L"another system");
}

// The Windows (if any) that starts through this start-up partition.
static const Install* installUsing(const Report& r, int part) {
    for (const Install& in : r.installs)
        if (!in.linux && in.sysPart == part) return &in;
    return nullptr;
}

// Does the PC try this partition first when it starts?
static bool firmwareStartsFrom(const Report& r, const Partition& sp) {
    if (!r.m.fw.varsReadable || r.m.fw.order.empty() || !sp.gpt) return false;
    for (const FirmwareEntry& fe : r.m.fw.entries)
        if (fe.num == r.m.fw.order[0] && fe.hasPart && IsEqualGUID(fe.part, sp.id)) return true;
    return false;
}

// Windows Boot Manager files that no Windows on this PC uses can be removed as a
// whole (UEFI start-up partitions only, and never the ones the running system uses).
static bool leftoverRemovable(const Report& r, const BcdStore& s) {
    return !s.live && s.part >= 0 && icontains(s.file, L"\\EFI\\Microsoft\\Boot\\BCD") && !installUsing(r, s.part);
}

static Fix removeLeftoverWindowsFiles(const Report& r, const BcdStore& s) {
    Fix d = makeFix(FX_DELETE_EFI_DIR);
    d.part = s.part;
    d.disk = r.m.parts[s.part].disk;
    d.arg = L"EFI\\Microsoft";
    d.title = T(L"Remove the leftover Windows start-up files");
    d.what = T(L"Deletes the Windows Boot Manager folder (\\EFI\\Microsoft) from this start-up partition, and its line in the PC's start-up list. No Windows, personal files or other partitions are touched, and the start-up files of the Windows you use are on another partition.");
    return d;
}

// Start-up settings that are empty, point at a missing first choice, or cannot be read.
// Every finding says what to do about it.
void findStoreProblems(Report& r) {
    for (const BcdStore& s : r.stores) {
        if (s.part < 0) continue;
        const Partition& sp = r.m.parts[s.part];
        const Install* user = installUsing(r, s.part);
        bool inUse = s.live || firmwareStartsFrom(r, sp);
        const wstr rebuildHint = user ? T(L"Use \"Rebuild the start-up files\" in the report of ") + user->label() + T(L": that writes new, correct settings.") : wstr();
        if (s.loaded && !s.defaultId.empty() && !s.find(s.defaultId)) {
            Finding f;
            f.health = Health::Warn;
            f.title = T(L"The start-up menu's first choice is missing");
            f.text = T(L"The start-up settings on ") + sp.where() + T(L" say to start an entry that does not exist.");
            if (user) f.text += L" " + rebuildHint;
            f.lesson = T(kLessonMenuEntry);
            r.pc.push_back(f);
        }
        if (s.loaded && s.empty) {
            Finding f;
            f.lesson = T(kLessonMenuEntry);
            if (user) {
                f.health = inUse ? Health::Bad : Health::Warn;
                f.title = T(L"The start-up settings on ") + sp.where() + T(L" list no system");
                f.text = T(L"The settings file is intact, but it names no system to start, although ") + user->label() + T(L" starts through this partition. ") + rebuildHint;
            } else {
                f.health = inUse ? Health::Warn : Health::Info;
                f.title = T(L"Leftover Windows start-up files on ") + sp.where();
                f.text = T(L"This start-up partition still holds Windows Boot Manager and its settings, but the settings list no system to start: the Windows they belonged to is gone, or its entry was removed. Nothing is damaged. ");
                f.text += inUse ? T(L"The PC's start-up list tries this partition first, shows an error, and only then goes on to the next choice.")
                                : T(L"This PC does not start from this partition, so the files do no harm where they are.");
                f.text += T(L" If the PC is ever started from Disk ") + std::to_wstring(sp.disk) + T(L", it shows a start-up error instead of a system.\n\nYou can leave the files, or remove them to tidy up. If an entry was removed by mistake, Undo in the backups list brings it back.");
                if (leftoverRemovable(r, s)) f.fixes.push_back(removeLeftoverWindowsFiles(r, s));
            }
            r.pc.push_back(f);
        } else if (s.health == Health::Bad) {
            Finding f;
            f.health = Health::Bad;
            f.title = T(L"Damaged start-up settings on ") + sp.where();
            f.text = s.problem;
            f.lesson = T(kLessonMenuEntry);
            if (user) f.text += L"\n\n" + rebuildHint;
            else if (leftoverRemovable(r, s)) {
                f.health = inUse ? Health::Bad : Health::Warn;
                f.text += T(L"\n\nNo Windows found on this PC starts through this partition, so these are leftover files. Removing them is safe.");
                f.fixes.push_back(removeLeftoverWindowsFiles(r, s));
            } else {
                f.text += T(L"\n\nNo Windows found on this PC uses these settings. If a Windows should start from this disk, it was not found: check that its disk is connected and unlocked.");
            }
            r.pc.push_back(f);
        }
    }
}

void findDeadEntries(Report& r) {
    auto exists2 = [](const wstr& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; };
    // A Windows without a start-up entry of its own: the dead entry may well be its old one.
    wstr homeless;
    for (const Install& in : r.installs)
        if (!in.linux && in.atRoot && !in.running && in.entryId.empty()) homeless = in.label() + L" on " + (r.m.parts[in.part].letter.empty() ? r.m.parts[in.part].where() : r.m.parts[in.part].letter);
    // 1. Lines in the start-up menus (BCD stores)
    for (size_t si = 0; si < r.stores.size(); ++si) {
        const BcdStore& s = r.stores[si];
        if (!s.loaded) continue;
        const Partition& sp = r.m.parts[s.part];
        for (const BcdEntry& e : s.entries) {
            bool loader = e.app == APP_LOADER, other = e.app == APP_NTLDR || e.app == APP_BOOTSECTOR;
            if ((!loader && !other) || e.ramdisk) continue;
            bool hasDev = loader ? e.hasOsDevice : e.hasDevice;
            int part = loader ? e.osPart : e.devPart;
            if (!hasDev) continue;
            wstr why;
            if (part < 0) why = T(L"the partition it points to is not on any disk now");
            else if (loader) {
                const Partition& p = r.m.parts[part];
                if (p.locked) {
                    wstr name = e.description.empty() ? wstr(L"(no name)") : e.description;
                    Finding f;
                    f.health = Health::Warn;
                    f.title = T(L"A start-up entry points to a drive locked by BitLocker: \"") + name + L"\"";
                    f.text = TF(L"The start-up menu on {0} lists \"{1}\" on {2}, but that drive is encrypted with BitLocker and locked, so it cannot be checked. Windows is very likely inside it.\n\nDo not remove this entry. Unlock the drive first (with its 48-digit recovery key) and scan again: only then can it be seen whether the entry is fine.",
                                sp.where(), name, p.name());
                    f.lesson = T(L"BitLocker encrypts a whole drive. While it is locked, no program can see the Windows inside it, so a working start-up entry looks as if it leads nowhere.");
                    Fix u = makeFix(FX_UNLOCK);
                    u.part = part;
                    u.disk = p.disk;
                    f.fixes.push_back(u);
                    r.pc.push_back(f);
                    continue;
                }
                if (!p.readable || p.root.empty() || e.systemRoot.empty()) continue;
                wstr rel = e.systemRoot[0] == L'\\' ? e.systemRoot.substr(1) : e.systemRoot;
                if (exists2(p.root + rel + L"\\System32")) continue;
                why = T(L"there is no Windows in ") + e.systemRoot + L" on " + p.name() + T(L" any more");
            } else continue;
            wstr name = e.description.empty() ? wstr(L"(no name)") : e.description;
            // Is this the menu the PC really uses (the running one, or the first in the PC's own list)?
            bool inUse = s.live;
            if (!inUse && r.m.fw.varsReadable && !r.m.fw.order.empty() && sp.gpt)
                for (const FirmwareEntry& fe : r.m.fw.entries)
                    if (fe.num == r.m.fw.order[0] && fe.hasPart && IsEqualGUID(fe.part, sp.id)) inUse = true;
            Finding f;
            f.health = e.isDefault && inUse ? Health::Bad : e.inMenu ? Health::Warn : Health::Info;
            f.title = T(L"A start-up menu entry leads nowhere: \"") + name + L"\"";
            f.text = T(L"The start-up menu on ") + sp.where() + T(L" lists \"") + name + T(L"\", but ") + why + L". ";
            if (e.isDefault && inUse) f.text += T(L"It is even set to start first, so every time the PC starts it tries this entry, fails, and you see an error or a blue screen.");
            else if (e.isDefault)
                f.text += T(L"This PC does not start from that menu now, so it does no harm today. But if the PC is ever started from Disk ") + std::to_wstring(sp.disk) +
                          T(L", it tries this entry first and fails. It is probably left over from a copied (cloned) disk.");
            else if (e.inMenu) f.text += T(L"If someone picks it in the start-up menu, the PC shows an error. It also makes the menu confusing.");
            else f.text += T(L"It is hidden from the menu, so it does no harm; it is only leftover clutter.");
            if (!homeless.empty())
                f.text += T(L"\n\nNote: ") + homeless + T(L" has no start-up entry. If this entry used to belong to it (because it was moved), use \"Rebuild the start-up files\" in its report instead: that writes a correct entry. Then remove this one.");
            f.lesson = T(kLessonMenuEntry);
            if (anyLockedDrive(r)) {
                f.text += T(L"\n\nCareful: a drive on this PC is locked by BitLocker. The Windows this entry belongs to may be inside it. Unlock the locked drive first and scan again before removing anything.");
                f.health = worse(f.health, Health::Warn);
            }
            // The only system these settings list, on a partition no Windows uses: removing just
            // the entry would leave an empty Boot Manager behind, so offer to remove all of it.
            int others = 0;
            for (const BcdEntry& o : s.entries)
                if (o.app == APP_LOADER && !o.ramdisk && !iequals(o.id, e.id)) ++others;
            if (!others && !inUse && leftoverRemovable(r, s)) {
                f.text += T(L"\n\nIt is the only system these start-up settings list, and no Windows on this PC starts through that partition. The whole set of leftover Windows start-up files there can be removed in one go.");
                f.fixes.push_back(removeLeftoverWindowsFiles(r, s));
            }
            Fix del = makeFix(FX_DELETE_BCD_ENTRY);
            del.store = (int)si;
            del.entry = e.id;
            del.part = s.part;
            del.disk = sp.disk;
            f.fixes.push_back(del);
            if (e.inMenu && !e.isDefault) {
                Fix hide = makeFix(FX_REMOVE_FROM_MENU);
                hide.store = (int)si;
                hide.entry = e.id;
                hide.part = s.part;
                hide.disk = sp.disk;
                f.fixes.push_back(hide);
            }
            r.pc.push_back(f);
        }
        // The menu lists something that is not in the settings at all
        for (const wstr& id : s.order) {
            if (s.find(id)) continue;
            Finding f;
            f.health = Health::Warn;
            f.title = T(L"The start-up menu lists an entry that does not exist");
            f.text = T(L"The start-up menu on ") + sp.where() + T(L" has a line (") + id + T(L") whose settings are gone. It shows up as an empty or broken choice.");
            f.lesson = T(kLessonMenuEntry);
            Fix hide = makeFix(FX_REMOVE_FROM_MENU);
            hide.title = T(L"Take it off the menu");
            hide.store = (int)si;
            hide.entry = id;
            hide.part = s.part;
            hide.disk = sp.disk;
            f.fixes.push_back(hide);
            r.pc.push_back(f);
        }
    }
    // 2. Lines in the PC's own start-up list (UEFI firmware)
    if (!r.m.fw.varsReadable) return;
    for (const FirmwareEntry& e : r.m.fw.entries) {
        if (!e.hasPart) continue;  // built-in choices (network, USB ports, shell) point to no partition
        int pi = -1;
        for (size_t i = 0; i < r.m.parts.size(); ++i)
            if (r.m.parts[i].gpt && IsEqualGUID(r.m.parts[i].id, e.part)) pi = (int)i;
        wstr kind = systemKindFromPath(e.path), why;
        if (pi < 0) why = T(L"the disk it is on is not connected now");
        else {
            const Partition& p = r.m.parts[pi];
            if (!p.readable || p.root.empty() || e.path.empty()) continue;
            if (exists2(p.root + (e.path[0] == L'\\' ? e.path.substr(1) : e.path))) continue;
            why = T(L"its start-up file (") + e.path + T(L") is no longer on ") + p.where();
        }
        wchar_t num[16];
        swprintf(num, 16, L"Boot%04X", e.num);
        Finding f;
        bool maybeUsb = pi < 0 && (icontains(e.description, L"usb") || icontains(e.description, L"uefi:") || icontains(e.description, L"removable"));
        f.health = maybeUsb || !e.inOrder ? Health::Info : Health::Warn;
        f.title = T(L"The PC's start-up list has an entry that leads nowhere: \"") + e.description + L"\"";
        f.text = L"\"" + e.description + L"\" (" + kind + T(L") is in the start-up list kept inside the PC, but ") + why + L". ";
        if (pi < 0)
            f.text += T(L"If it belongs to a USB stick or a disk you connect now and then, keep it. If that disk is gone for good, remove the entry.");
        else
            f.text += T(L"The system it used to start (") + kind + T(L") was probably deleted or uninstalled, but its line stayed behind.");
        if (e.inOrder) f.text += T(L" The PC tries it every time it starts, which can slow starting down or show an error before Windows appears.");
        if (pi < 0 && anyLockedDrive(r))
            f.text += T(L"\n\nCareful: a drive on this PC is locked by BitLocker. Unlock it first and scan again before removing anything.");
        f.text += T(L"\n\nFor a technician: ") + wstr(num) + (e.path.empty() ? L"" : L", " + e.path);
        f.lesson = T(kLessonFirmwareList);
        Fix del = makeFix(FX_DELETE_FW_ENTRY);
        del.fwNum = e.num;
        del.part = pi;
        f.fixes.push_back(del);
        r.pc.push_back(f);
    }
}

// ---------------------------------------------------------------- finding installations
static void addIfWindows(const wstr& root, const wstr& folder, int part, std::vector<Install>& out) {
    wstr win = root + folder + L"Windows";
    if (!exists(win + L"\\System32\\ntoskrnl.exe") && !exists(win + L"\\System32\\config\\SYSTEM")) return;
    for (const Install& i : out)
        if (iequals(i.winDir, win)) return;
    Install in;
    in.part = part;
    in.root = root;
    in.folder = folder;
    in.winDir = win;
    wstr rel = folder;
    for (wchar_t& c : rel)
        if (c == L'/') c = L'\\';
    in.systemRoot = L"\\" + rel + L"Windows";
    in.atRoot = folder.empty();
    in.windowsOld = istartsWith(folder, L"Windows.old\\");
    out.push_back(in);
}

static bool skipFolder(const wstr& n) {
    static const wchar_t* skip[] = {L"Windows", L"$Recycle.Bin", L"System Volume Information", L"Program Files", L"Program Files (x86)",
                                    L"ProgramData", L"Recovery", L"Boot", L"EFI", L"PerfLogs", L"$WinREAgent", L"$SysReset", L"Config.Msi",
                                    L"Documents and Settings", L"MSOCache", L"Intel", L"AMD", L"NVIDIA", L"OneDriveTemp", L"$GetCurrent",
                                    L"$WINDOWS.~BT", L"$Windows.~WS", L"inetpub", L"Drivers", L"Apps", L"Users"};
    if (n == L"." || n == L"..") return true;
    for (const wchar_t* s : skip)
        if (iequals(n, s)) return true;
    return false;
}

static std::vector<wstr> subfolders(const wstr& dir, size_t cap) {
    std::vector<wstr> out;
    WIN32_FIND_DATAW fd{};
    HANDLE f = FindFirstFileExW((dir + L"*").c_str(), FindExInfoBasic, &fd, FindExSearchLimitToDirectories, nullptr, 0);
    if (f == INVALID_HANDLE_VALUE) return out;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && !skipFolder(fd.cFileName))
            out.push_back(fd.cFileName);
    } while (out.size() < cap && FindNextFileW(f, &fd));
    FindClose(f);
    return out;
}

void findInstalls(const wstr& root, int part, std::vector<Install>& out) {
    addIfWindows(root, L"", part, out);
    if (exists(root + L"Users") && isDir(root + L"Users")) {
        // Also look in user folders one level down? Copies usually land on the desktop or in a backup folder.
    }
    int budget = 3000;
    for (const wstr& a : subfolders(root, 400)) {
        if (--budget < 0) break;
        wstr fa = a + L"\\";
        addIfWindows(root, fa, part, out);
        for (const wstr& b : subfolders(root + fa, 120)) {
            if (--budget < 0) break;
            addIfWindows(root, fa + b + L"\\", part, out);
        }
    }
}

// ---------------------------------------------------------------- the chain
namespace {

struct Ctx {
    Report& r;
    Install& in;
    const Partition& P;
    const Disk* D = nullptr;
    bool uefi = false;
    Ctx(Report& rr, Install& i) : r(rr), in(i), P(rr.m.parts[i.part]) {
        for (const Disk& d : r.m.disks)
            if (d.number == P.disk) D = &d;
    }
    int fix(int kind, int part = -1, int store = -1, const wstr& entry = wstr()) {
        for (size_t i = 0; i < in.plan.size(); ++i)
            if (in.plan[i].kind == kind && in.plan[i].part == part) return (int)i;
        Fix f = makeFix(kind);
        f.part = part;
        f.store = store;
        f.entry = entry;
        f.disk = part >= 0 ? r.m.parts[part].disk : (D ? D->number : -1);
        in.plan.push_back(f);
        return (int)in.plan.size() - 1;
    }
    Link& link(const char* icon, const wchar_t* name) {
        Link l;
        l.icon = icon;
        l.name = name;
        in.chain.push_back(l);
        return in.chain.back();
    }
};

void set(Link& l, Health h, const wstr& plain, const wstr& tech = wstr()) {
    l.health = h;
    l.plain = plain;
    if (!tech.empty()) l.tech = tech;
}

wstr partText(const Partition& p) {
    wstr s = p.where() + L" (" + sizeText(p.length);
    if (!p.fs.empty()) s += L", " + p.fs;
    else if (!p.rawFs.empty()) s += L", " + p.rawFs;
    if (!p.letter.empty()) s += L", " + p.letter;
    return s + L")";
}

int pickSysPart(Ctx& c) {
    const Report& r = c.r;
    if (c.in.store >= 0) return r.stores[c.in.store].part;
    if (c.uefi) {
        // The EFI partition the firmware starts first, then one on the same disk, then any.
        if (r.m.fw.varsReadable)
            for (int n : r.m.fw.order)
                for (const FirmwareEntry& e : r.m.fw.entries)
                    if (e.num == n && e.hasPart && icontains(e.path, L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi"))
                        for (size_t i = 0; i < r.m.parts.size(); ++i)
                            if (r.m.parts[i].gpt && IsEqualGUID(r.m.parts[i].id, e.part) && r.m.parts[i].disk == c.P.disk) return (int)i;
        for (size_t i = 0; i < r.m.parts.size(); ++i)
            if (isEsp(r.m.parts[i]) && r.m.parts[i].disk == c.P.disk) return (int)i;
        for (size_t i = 0; i < r.m.parts.size(); ++i)
            if (isEsp(r.m.parts[i])) return (int)i;
        // A FAT32 partition on an MBR disk can serve as the start-up partition in UEFI mode
        if (c.D && c.D->style == 1)
            for (int i : c.D->parts)
                if (r.m.parts[i].active && (r.m.parts[i].fs == L"FAT32" || r.m.parts[i].rawFs == L"FAT32")) return i;
        return -1;
    }
    if (c.D)
        for (int i : c.D->parts)
            if (r.m.parts[i].active) return i;
    for (const Disk& d : r.m.disks)
        if (!d.removable && d.style == 1)
            for (int i : d.parts)
                if (r.m.parts[i].active) return i;
    return -1;
}

void findEntry(Ctx& c) {
    Report& r = c.r;
    Install& in = c.in;
    int bestStore = -1;
    const BcdEntry* best = nullptr;
    int bestScore = -1;
    for (size_t s = 0; s < r.stores.size(); ++s)
        for (const BcdEntry& e : r.stores[s].entries) {
            if (e.app != APP_LOADER || e.ramdisk || e.osPart != in.part || !iequals(e.systemRoot, in.systemRoot)) continue;
            int score = (e.isDefault ? 4 : 0) + (e.inMenu ? 2 : 0) + (r.m.parts[r.stores[s].part].disk == c.P.disk ? 1 : 0) +
                        (r.stores[s].live ? 8 : 0);
            if (score > bestScore) {
                bestScore = score;
                best = &e;
                bestStore = (int)s;
            }
        }
    if (best) {
        in.store = bestStore;
        in.entryId = best->id;
        in.inMenu = best->inMenu;
        in.isDefault = best->isDefault;
        return;
    }
    // Moved? An entry that points to this partition but another folder, or to a partition that is gone.
    for (const BcdStore& s : r.stores)
        for (const BcdEntry& e : s.entries) {
            if (e.app != APP_LOADER || e.ramdisk) continue;
            if (e.osPart == in.part && !iequals(e.systemRoot, in.systemRoot)) {
                bool other = false;
                for (const Install& x : r.installs)
                    if (x.part == in.part && iequals(x.systemRoot, e.systemRoot)) other = true;
                if (!other) {
                    in.moved = true;
                    in.movedNote = T(L"The start-up entry \"") + e.description + T(L"\" expects Windows in ") + e.systemRoot +
                                   T(L" on this drive, but it is now in ") + in.systemRoot + L". It was moved.";
                }
            } else if (e.hasOsDevice && e.osPart < 0 && !in.moved) {
                in.moved = true;
                in.movedNote = T(L"The start-up entry \"") + e.description +
                               T(L"\" points to a partition that is no longer there. Windows may have been moved or copied to this drive from there.");
            }
        }
}

}  // namespace

static void buildChain(Report& r, Install& in) {
    Ctx c(r, in);
    const Partition& P = c.P;
    const Disk* D = c.D;
    c.uefi = r.m.fw.uefi;
    findEntry(c);
    in.sysPart = pickSysPart(c);
    const Partition* S = in.sysPart >= 0 ? &r.m.parts[in.sysPart] : nullptr;
    const BcdStore* store = in.store >= 0 ? &r.stores[in.store] : nullptr;
    if (!store && S)
        for (const BcdStore& s : r.stores)
            if (s.part == in.sysPart && (c.uefi ? icontains(s.file, L"\\EFI\\") : !icontains(s.file, L"\\EFI\\"))) store = &s;
    int storeIdx = store ? (int)(store - r.stores.data()) : -1;
    int otherActive = 0;  // other Windows that already start from here
    for (const Install& x : r.installs)
        if (&x != &in && !x.entryId.empty()) ++otherActive;

    // 1. Firmware
    {
        Link& l = c.link("bd_chip", N(L"PC firmware"));
        if (c.uefi) {
            set(l, Health::Ok, T(L"The PC starts in UEFI mode, the modern way of starting."),
                wstr(T(L"UEFI firmware")) + (r.m.fw.secureBoot ? T(L", Secure Boot on") : T(L", Secure Boot off")));
            if (D && D->style == 1 && !S)
                set(l, Health::Warn, T(L"The PC starts in UEFI mode, but this Windows is on an older-style (MBR) disk. It can only start if \"Legacy\" or \"CSM\" is turned on in the PC's firmware settings, or after a start-up partition is created for it."));
            if (in.arch == L"x86")
                set(l, Health::Warn, T(L"This is a 32-bit Windows. Most PCs that start in UEFI mode cannot start 32-bit Windows; turn on \"Legacy\" or \"CSM\" in the firmware settings."));
            l.aboutSecureBoot = true;
            const Firmware& fw = r.m.fw;
            bool old = in.major == 6 && in.minor <= 1;  // Windows 7 and Vista know nothing of Secure Boot
            if (old && fw.secureBoot) {
                set(l, Health::Bad, T(L"Secure Boot is on, but ") + in.name + T(L" was made before Secure Boot existed, so the PC refuses to start it. Switch Secure Boot off in the PC's firmware settings."));
                l.fixes.push_back(c.fix(FX_FIRMWARE_SETTINGS, -1));
            } else if (in.name == L"Windows 11" && fw.secureBootKnown && !fw.secureBoot) {
                l.plain += T(L" Secure Boot is off. Windows 11 still starts, but it is less protected, Windows may say the PC is not supported, and some games refuse to run. You can switch it on in the firmware settings.");
                l.fixes.push_back(c.fix(FX_FIRMWARE_SETTINGS, -1));
            } else if (fw.secureBootKnown)
                l.plain += fw.secureBoot ? T(L" Secure Boot is on, which protects the start against harmful programs.") : T(L" Secure Boot is off.");
        } else {
            set(l, Health::Ok, T(L"The PC starts in legacy (BIOS) mode, the older way of starting."), T(L"Legacy BIOS firmware (or UEFI with CSM)"));
            if (D && D->style == 2)
                set(l, Health::Bad, T(L"This Windows is on a GPT disk, which can only start in UEFI mode, but the PC was started in legacy (BIOS) mode. Turn on UEFI mode in the PC's firmware settings (often called \"Boot mode\", or turn \"CSM\" off)."),
                    T(L"BIOS boot + GPT disk"));
        }
    }
    // 2. Disk
    {
        Link& l = c.link("disk_hw", N(L"Disk"));
        if (!D || !D->readable) set(l, Health::Bad, T(L"The disk cannot be read. It may be failing or badly connected."));
        else {
            wstr tech = D->name() + L", " + (D->style == 2 ? L"GPT" : D->style == 1 ? L"MBR" : T(L"no partition table"));
            if (!D->bus.empty()) tech += L", " + D->bus;
            set(l, Health::Ok, T(L"The disk with this Windows (") + D->name() + T(L") can be read and its partition table is fine."), tech);
            if (D->style == 2 && (!D->gptPrimaryOk || !D->gptBackupOk || D->gptBackupMisplaced)) {
                set(l, D->gptPrimaryOk ? Health::Warn : Health::Bad,
                    !D->gptPrimaryOk ? T(L"The main copy of the disk's partition table is damaged. Windows is using the backup copy for now.")
                    : !D->gptBackupOk ? T(L"The backup copy of the disk's partition table is damaged. Windows still starts, but there is no safety copy.")
                                      : T(L"The backup copy of the partition table is not at the end of the disk. This happens after copying a disk to a bigger one."));
                l.fixes.push_back(c.fix(FX_FIX_GPT, -1));
                in.plan.back().disk = D->number;
            }
            if (P.pastEnd || P.overlaps)
                set(l, Health::Bad, P.pastEnd ? T(L"The Windows partition runs past the end of the disk, so part of it is missing. The disk was probably copied to a smaller one.")
                                              : T(L"The Windows partition overlaps another partition. One of them gets damaged when written to."));
        }
    }
    // 3. Start-up partition
    {
        Link& l = c.link("partition", N(L"Start-up partition"));
        if (c.uefi) {
            if (!S) {
                set(l, Health::Bad, T(L"There is no EFI system partition (the small start-up partition a UEFI PC starts from). It is missing or was deleted."));
                l.fixes.push_back(c.fix(FX_CREATE_SYSPART, in.part));
            } else if (!S->readable) {
                set(l, Health::Bad, S->locked ? T(L"The start-up partition cannot be opened.") : T(L"The start-up partition is damaged: its files cannot be read."), partText(*S));
                l.fixes.push_back(c.fix(FX_CHKDSK, in.sysPart));
                l.fixes.push_back(c.fix(FX_CREATE_SYSPART, in.part));
            } else {
                set(l, Health::Ok, T(L"The start-up partition is there and can be read."), partText(*S));
                if (S->fs != L"FAT32" && S->fs != L"FAT")
                    set(l, Health::Bad, T(L"The start-up partition uses ") + S->fs + T(L", which the PC's firmware cannot read. It must be FAT32."), partText(*S));
                else if (S->freeBytes < (5ull << 20))
                    set(l, Health::Warn, T(L"The start-up partition is almost full (") + sizeText(S->freeBytes) + T(L" free). New start-up files may not fit."), partText(*S));
                if (S->disk != P.disk)
                    l.plain += T(L" It is on another disk (Disk ") + std::to_wstring(S->disk) + T(L"), so this Windows only starts while that disk is connected.");
            }
        } else {
            if (!S) {
                int cand = -1;
                if (D)
                    for (int i : D->parts)
                        if (exists(r.m.parts[i].root + L"bootmgr") || r.m.parts[i].label == L"System Reserved") cand = i;
                set(l, Health::Bad, T(L"No partition on this disk is marked \"active\", so an older PC (legacy BIOS) does not know where to start."));
                if (D && D->style == 1) l.fixes.push_back(c.fix(FX_MARK_ACTIVE, cand >= 0 ? cand : in.part));
                if (cand < 0) l.fixes.push_back(c.fix(FX_REBUILD_BOOT, in.part));
                if (cand >= 0) in.sysPart = cand;
                S = in.sysPart >= 0 ? &r.m.parts[in.sysPart] : nullptr;
            } else if (!S->readable) {
                set(l, Health::Bad, T(L"The active (start-up) partition cannot be read. Its file system is damaged."), partText(*S));
                l.fixes.push_back(c.fix(FX_CHKDSK, in.sysPart));
            } else {
                set(l, Health::Ok, T(L"The active start-up partition is there and can be read."), partText(*S));
                if (S->disk != P.disk)
                    l.plain += T(L" It is on another disk (Disk ") + std::to_wstring(S->disk) + T(L"), so this Windows only starts while that disk is connected.");
            }
        }
    }
    // 3b. Start-up partition files (the file system itself and the boot files on it)
    {
        Link& l = c.link("partition", N(L"Start-up partition files"));
        if (!S || !S->readable) {
            set(l, Health::Unknown, T(L"Cannot be checked until the start-up partition is fixed."));
        } else {
            ReadFn rd = r.rawReader ? r.rawReader(*S) : ReadFn();
            bool fat = S->fs == L"FAT32" || S->fs == L"FAT" || S->rawFs == L"FAT32" || S->rawFs == L"FAT";
            EspCheck chk;
            bool checked = rd && c.uefi && fat;
            if (checked) chk = checkEsp(rd, S->length);
            bool wrote = (!r.rawReader || in.running) ? true : espWritable(*S);  // only on a real scan, never the running system's own files
            wstr tag = checked && chk.ran ? T(L"ESP check: ") + wstr(espFaultWord(chk.fault)) + (chk.detail.empty() ? wstr() : L" — " + chk.detail) : wstr();
            bool fsDamage = checked && chk.ran &&
                            (chk.fault == EspFault::BootSector || chk.fault == EspFault::BackupBootSector ||
                             chk.fault == EspFault::FatCopies || chk.fault == EspFault::ClusterChain || chk.fault == EspFault::NotFat32);
            bool fileDamage = checked && chk.ran &&
                              (chk.fault == EspFault::Bootmgr || chk.fault == EspFault::FallbackLoader || chk.fault == EspFault::Bcd);
            if (fsDamage) {
                set(l, Health::Bad,
                    T(L"The start-up partition's file system (FAT32) is damaged. The firmware can no longer read its start-up files, and Windows' own repair (bootrec /fixboot) then fails with \"Access is denied\". The rest of the boot chain can look fine while this is wrong."),
                    tag);
                l.fixes.push_back(c.fix(FX_REBUILD_ESP, in.sysPart));
            } else if (fileDamage) {
                set(l, Health::Bad,
                    chk.fault == EspFault::Bcd ? T(L"The start-up settings (BCD) on the start-up partition do not load, although the file is there.")
                                               : T(L"A start-up program on the start-up partition is missing or is not a 64-bit program, although the file system is sound."),
                    tag);
                l.fixes.push_back(c.fix(FX_REBUILD_BOOT, in.sysPart));
            } else if (!wrote) {
                set(l, Health::Bad,
                    T(L"The start-up partition cannot be written to, so Windows' own repair (bootrec /fixboot) fails with \"Access is denied\" and new start-up files cannot be put on it. Rebuilding the partition clears this."),
                    T(L"The start-up partition could not be opened for writing."));
                l.fixes.push_back(c.fix(FX_REBUILD_ESP, in.sysPart));
            } else if (checked && chk.ran) {
                set(l, Health::Ok, T(L"The start-up partition's file system is sound and it can be written to."),
                    T(L"FAT32 checked: boot sector + backup, both FAT copies, cluster chains, bootmgfw.efi + bootx64.efi (x64), BCD"));
            } else {
                set(l, Health::Ok, T(L"The start-up partition can be opened and written to."));
            }
        }
    }
    // 4. Boot manager (and the disk's start-up code)
    {
        Link& l = c.link("bd_key", N(L"Boot manager"));
        if (!S || !S->readable) set(l, Health::Unknown, T(L"Cannot be checked until the start-up partition is fixed."));
        else if (c.uefi) {
            wstr bm = S->root + L"EFI\\Microsoft\\Boot\\bootmgfw.efi";
            if (!looksLikeProgram(bm)) {
                set(l, Health::Bad, exists(bm) ? L"Windows Boot Manager on the start-up partition is damaged." : L"Windows Boot Manager is missing from the start-up partition.",
                    L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi");
                l.fixes.push_back(c.fix(FX_REBUILD_BOOT, in.sysPart));
            } else {
                set(l, Health::Ok, T(L"Windows Boot Manager is on the start-up partition."), L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi");
                if (r.m.fw.varsReadable && S->gpt) {
                    bool entry = false, ordered = false;
                    for (const FirmwareEntry& e : r.m.fw.entries)
                        if (e.hasPart && IsEqualGUID(e.part, S->id) && icontains(e.path, L"bootmgfw.efi")) {
                            entry = true;
                            ordered |= e.inOrder;
                        }
                    bool fallback = looksLikeProgram(S->root + L"EFI\\Boot\\bootx64.efi") || looksLikeProgram(S->root + L"EFI\\Boot\\bootia32.efi") ||
                                    looksLikeProgram(S->root + L"EFI\\Boot\\bootaa64.efi");
                    if (!entry || !ordered) {
                        set(l, fallback ? Health::Warn : Health::Bad,
                            wstr(T(L"The PC's own start-up list has no ")) + (entry ? L"active " : L"") +
                                T(L"entry for Windows Boot Manager on this partition.") +
                                (fallback ? T(L" It may still start through the standard fallback file.") : T(L" So the PC does not try to start it.")),
                            T(L"No firmware Boot#### entry for bootmgfw.efi on ") + S->where());
                        l.fixes.push_back(c.fix(FX_REBUILD_BOOT, in.sysPart));
                    }
                }
            }
        } else {
            if (D && D->style == 1 && !D->bootCode && S->disk == P.disk) {
                set(l, Health::Bad, T(L"The disk's start-up code (in its first sector) is empty, so the PC cannot begin starting from this disk."), T(L"MBR boot code is zero"));
                l.fixes.push_back(c.fix(FX_MBR_CODE, in.sysPart));
            } else if (!looksLikeProgram(S->root + L"bootmgr") && !exists(S->root + L"bootmgr")) {
                set(l, Health::Bad, T(L"Windows Boot Manager (the file \"bootmgr\") is missing from the start-up partition."), S->root + L"bootmgr");
                l.fixes.push_back(c.fix(FX_REBUILD_BOOT, in.sysPart));
            } else if (S->rawFs.empty() && S->bitlocker == false && !S->readable) {
                set(l, Health::Bad, T(L"The start-up partition's first sector is damaged."));
                l.fixes.push_back(c.fix(FX_MBR_CODE, in.sysPart));
            } else set(l, Health::Ok, T(L"The disk's start-up code and Windows Boot Manager are in place."), S->root + L"bootmgr");
        }
    }
    // 5. Boot settings (BCD)
    {
        Link& l = c.link("bd_list", N(L"Start-up settings"));
        if (!S || !S->readable) {
            set(l, Health::Unknown, T(L"Cannot be checked until the start-up partition is fixed."));
        } else if (!store) {
            set(l, Health::Bad, T(L"The start-up settings file (BCD) is missing, so Windows Boot Manager does not know which Windows to start."),
                S->root + (c.uefi ? L"EFI\\Microsoft\\Boot\\BCD" : L"Boot\\BCD"));
            l.fixes.push_back(c.fix(FX_REBUILD_BOOT, in.sysPart));
        } else if (store->health == Health::Bad && !store->loaded) {
            set(l, Health::Bad, store->problem, store->file);
            l.fixes.push_back(c.fix(FX_REBUILD_BOOT, in.sysPart));
        } else if (in.entryId.empty()) {
            wstr tech = store->file + T(L": no entry with osdevice ") + P.where() + T(L" and systemroot ") + in.systemRoot;
            if (in.moved) {
                set(l, Health::Bad, T(L"Windows was moved: ") + in.movedNote, tech);
                l.fixes.push_back(c.fix(otherActive ? FX_ADD_ENTRY : FX_REBUILD_BOOT, in.sysPart));
            } else if (otherActive) {
                set(l, Health::Warn, T(L"This Windows is not in the start-up menu, so it never starts (it is inactive). The menu only lists the other Windows."), tech);
                l.fixes.push_back(c.fix(FX_ADD_ENTRY, in.sysPart));
            } else {
                set(l, Health::Bad, T(L"The start-up settings have no entry for this Windows, so it is never started."), tech);
                l.fixes.push_back(c.fix(FX_REBUILD_BOOT, in.sysPart));
            }
        } else {
            const BcdEntry* e = store->find(in.entryId);
            wstr tech = store->file + T(L", entry ") + in.entryId + (e ? L" \"" + e->description + T(L"\" path ") + e->path : wstr());
            wstr loader = e ? e->path : wstr();
            bool wantEfi = c.uefi;
            if (e && !loader.empty() && !exists(P.root + (loader[0] == L'\\' ? loader.substr(1) : loader))) {
                set(l, Health::Bad, T(L"The start-up entry for this Windows points to a loader file that is not there (") + loader + L").", tech);
                l.fixes.push_back(c.fix(FX_REBUILD_BOOT, in.sysPart));
            } else if (e && !loader.empty() && wantEfi != icontains(loader, L".efi")) {
                set(l, Health::Bad, wantEfi ? T(L"The start-up entry is made for legacy (BIOS) start-up, but the PC starts in UEFI mode.")
                                            : T(L"The start-up entry is made for UEFI start-up, but the PC started in legacy (BIOS) mode."),
                    tech);
                l.fixes.push_back(c.fix(FX_REBUILD_BOOT, in.sysPart));
            } else if (e && e->devPart >= 0 && e->devPart != in.part) {
                set(l, Health::Warn, T(L"The start-up entry loads Windows' loader from another partition than the one Windows is on."), tech);
                l.fixes.push_back(c.fix(FX_REBUILD_BOOT, in.sysPart));
            } else if (!in.inMenu && !in.isDefault) {
                set(l, Health::Warn, T(L"This Windows has a start-up entry, but it is hidden from the start-up menu, so it never starts."), tech);
                l.fixes.push_back(c.fix(FX_SHOW_IN_MENU, in.sysPart, storeIdx, in.entryId));
            } else {
                set(l, Health::Ok, wstr(T(L"The start-up settings list this Windows")) + (e && !e->description.empty() ? L" as \"" + e->description + L"\"" : wstr()) +
                                       (in.isDefault ? T(L", and it starts first.") : L". Another system starts first; this one is picked from the menu."),
                    tech);
                if (!in.isDefault) l.fixes.push_back(c.fix(FX_MAKE_DEFAULT, in.sysPart, storeIdx, in.entryId));
                if (store->timeout == 0 && store->order.size() > 1 && !in.isDefault)
                    l.plain += T(L" The menu is set to wait 0 seconds, so you never see it.");
            }
        }
        if (store && S && S->disk != P.disk && !in.entryId.empty()) l.tech += L" (store on Disk " + std::to_wstring(S->disk) + L")";
    }
    in.active = !in.entryId.empty() && (in.inMenu || in.isDefault);
    // 6. Windows partition
    {
        Link& l = c.link("drive", N(L"Windows partition"));
        if (P.locked) {
            set(l, Health::Bad, T(L"The Windows drive is locked by BitLocker encryption. It must be unlocked with its recovery key before it can be checked."), partText(P));
            l.fixes.push_back(c.fix(FX_UNLOCK, in.part));
        } else {
            set(l, Health::Ok, T(L"The partition with Windows can be read."), partText(P));
            if (P.dirty) {
                set(l, Health::Warn, T(L"The Windows drive is marked as needing a disk check. Windows may run the check while starting, or get stuck on it."), partText(P));
                l.fixes.push_back(c.fix(FX_CHKDSK, in.part));
            } else if (P.totalBytes && P.freeBytes < (1ull << 30)) {
                set(l, Health::Warn, T(L"The Windows drive is almost full (") + sizeText(P.freeBytes) + T(L" free). Windows needs some free space to start and to update. Delete or move some files."), partText(P));
            }
            if (P.bitlocker) l.plain += T(L" It is encrypted with BitLocker and unlocked right now.");
        }
    }
    if (P.locked) {  // nothing more can be seen
        for (const wchar_t* n : {L"Windows loader", L"Core system files", L"System settings", L"Starting up"}) {
            Link& l = c.link(n[0] == L'W' ? "bd_rocket" : n[0] == L'C' ? "file_app" : n[0] == L'S' && n[1] == L'y' ? "settings" : "restart", n);
            set(l, Health::Unknown, T(L"Cannot be checked while the drive is locked."));
        }
        return;
    }
    // 7. Windows loader
    {
        Link& l = c.link("bd_rocket", N(L"Windows loader"));
        wstr wl = in.winDir + (c.uefi ? L"\\System32\\winload.efi" : L"\\System32\\winload.exe");
        if (!looksLikeProgram(wl)) {
            set(l, Health::Bad, exists(wl) ? L"Windows' loader (the file that starts Windows itself) is damaged." : L"Windows' loader (the file that starts Windows itself) is missing.", wl);
            l.fixes.push_back(c.fix(FX_SFC, in.part));
        } else set(l, Health::Ok, T(L"Windows' loader, the file that starts Windows itself, is in place."), wl);
    }
    // 8. Core system files
    {
        Link& l = c.link("file_app", N(L"Core system files"));
        static const wchar_t* files[] = {L"ntoskrnl.exe", L"hal.dll", L"ci.dll", L"drivers\\ntfs.sys", L"drivers\\disk.sys", L"drivers\\classpnp.sys",
                                         L"drivers\\volmgr.sys", L"drivers\\partmgr.sys", L"drivers\\pci.sys", L"drivers\\acpi.sys"};
        wstr missing, damaged;
        for (const wchar_t* f : files) {
            wstr p = in.winDir + L"\\System32\\" + f;
            if (!exists(p)) missing += (missing.empty() ? L"" : L", ") + wstr(f);
            else if (!looksLikeProgram(p)) damaged += (damaged.empty() ? L"" : L", ") + wstr(f);
        }
        if (!missing.empty() || !damaged.empty()) {
            set(l, Health::Bad, T(L"Some of the files Windows needs to start are ") + wstr(!missing.empty() ? L"missing" : L"damaged") + L".",
                (missing.empty() ? L"" : T(L"Missing: ") + missing + L". ") + (damaged.empty() ? L"" : T(L"Damaged: ") + damaged + L"."));
            l.fixes.push_back(c.fix(FX_SFC, in.part));
        } else {
            set(l, Health::Ok, T(L"The Windows core and the drivers it needs to read its disk are in place."), T(L"ntoskrnl.exe, hal.dll, ci.dll, ntfs.sys, disk.sys and others"));
            wstr src = in.winDir + (c.uefi ? L"\\Boot\\EFI\\bootmgfw.efi" : L"\\Boot\\PCAT\\bootmgr");
            if (!exists(src)) {
                set(l, Health::Warn, T(L"The spare copy of the start-up files that Windows keeps (to rebuild them) is missing. Rebuilding the start-up files from this Windows will not work until system files are repaired."), src);
                l.fixes.push_back(c.fix(FX_SFC, in.part));
                for (Fix& f : in.plan)
                    if (f.kind == FX_REBUILD_BOOT || f.kind == FX_ADD_ENTRY || f.kind == FX_CREATE_SYSPART) {
                        f.available = false;
                        f.notAvailable = T(L"This Windows' spare start-up files are missing. Repair the system files first.");
                    }
            }
        }
    }
    // 9. System settings (registry)
    bool setupFlag = false;
    {
        Link& l = c.link("settings", N(L"System settings"));
        wstr cfg = in.winDir + L"\\System32\\config\\";
        wstr whySys, whySw;
        Health hs = hiveFileHealth(cfg + L"SYSTEM", whySys);
        Health hw = hiveFileHealth(cfg + L"SOFTWARE", whySw);
        auto regBack = [&](const wstr& n) {
            wstr w;
            return fileSize(cfg + L"RegBack\\" + n) > 4096 && hiveFileHealth(cfg + L"RegBack\\" + n, w) == Health::Ok;
        };
        if (hs == Health::Bad || hw == Health::Bad) {
            wstr which = hs == Health::Bad ? L"SYSTEM" : L"SOFTWARE";
            set(l, Health::Bad, T(L"Windows' settings file (registry, part ") + which + T(L") is damaged. ") + (hs == Health::Bad ? whySys : whySw),
                cfg + which);
            if (regBack(which.c_str())) l.fixes.push_back(c.fix(FX_RESTORE_HIVE, in.part));
            else
                l.plain += T(L" There is no usable automatic backup copy of it. Start Windows' recovery tools and choose System Restore, if restore points exist.");
        } else {
            set(l, hs == Health::Warn || hw == Health::Warn ? Health::Warn : Health::Ok,
                hs == Health::Warn ? whySys : hw == Health::Warn ? whySw : T(L"Windows' settings file (registry) looks fine."), cfg + T(L"SYSTEM, SOFTWARE"));
            // Look inside SYSTEM
            LoadedHive hive;
            HKEY root = nullptr;
            wstr base, err;
            if (in.running) {
                root = HKEY_LOCAL_MACHINE;
                base = L"SYSTEM\\";
            } else if (hive.load(cfg + L"SYSTEM", true, err)) root = hive.root();
            if (root) {
                DWORD cur = 1;
                regDword(root, base + L"Select", L"Current", cur);
                wchar_t cs[32];
                swprintf(cs, 32, L"ControlSet%03lu\\", cur);
                wstr svc = base + cs + L"Services\\";
                // disk, volmgr and partmgr must load with Windows (Start 0). Ntfs is loaded by
                // the loader whatever its Start says, so only "disabled" (4) is wrong.
                wstr off, ctl;
                for (const wchar_t* n : {L"disk", L"volmgr", L"partmgr"}) {
                    DWORD start = 0;
                    if (regDword(root, svc + n, L"Start", start) && start != 0) off += (off.empty() ? L"" : L", ") + wstr(n) + L" (" + std::to_wstring(start) + L")";
                }
                DWORD ntfs = 0;
                if (regDword(root, svc + L"Ntfs", L"Start", ntfs) && ntfs == 4) off += (off.empty() ? L"" : L", ") + wstr(L"Ntfs (4)");
                // The driver for this kind of disk controller: off after moving the disk to another PC or changing the storage mode
                std::vector<const wchar_t*> ctrl;
                if (D && D->bus == L"NVMe" && in.major >= 10) ctrl.push_back(L"stornvme");
                if (D && D->bus == L"SATA" && (in.major > 6 || (in.major == 6 && in.minor >= 2))) ctrl.push_back(L"storahci");
                if (D && D->bus == L"SATA" && in.major == 6 && in.minor == 1) ctrl.push_back(L"msahci");
                for (const wchar_t* n : ctrl) {
                    DWORD start = 0;
                    if (regDword(root, svc + n, L"Start", start) && start != 0) ctl += (ctl.empty() ? L"" : L", ") + wstr(n) + L" (" + std::to_wstring(start) + L")";
                }
                if (!off.empty()) {
                    set(l, Health::Bad, T(L"Drivers Windows needs to read its own disk while starting are turned off. Windows then stops with INACCESSIBLE_BOOT_DEVICE."),
                        T(L"Services with the wrong Start value: ") + off);
                    l.fixes.push_back(c.fix(FX_ENABLE_DRIVERS, in.part));
                } else if (!ctl.empty() && !in.running) {
                    set(l, Health::Warn, T(L"The driver for this disk's controller (") + D->bus + T(L") does not load while Windows starts. If Windows stops with INACCESSIBLE_BOOT_DEVICE (common after moving the disk to another PC), turning it on usually fixes it."),
                        T(L"Controller driver with Start not 0: ") + ctl);
                    l.fixes.push_back(c.fix(FX_ENABLE_DRIVERS, in.part));
                }
                DWORD sip = 0, oobe = 0, type = 0;
                regDword(root, base + L"Setup", L"SystemSetupInProgress", sip);
                regDword(root, base + L"Setup", L"OOBEInProgress", oobe);
                regDword(root, base + L"Setup", L"SetupType", type);
                if (!in.running && (sip || oobe || type == 1 || type == 2 || type == 4) && exists(in.root + in.folder + L"Users")) {
                    setupFlag = true;
                    in.loopSigns.push_back(T(L"Windows is marked as still being set up (SystemSetupInProgress ") + std::to_wstring(sip) + T(L", SetupType ") +
                                           std::to_wstring(type) + T(L"). This causes the \"computer restarted unexpectedly\" loop."));
                    l.fixes.push_back(c.fix(FX_CLEAR_SETUP, in.part));
                    if (l.health == Health::Ok) set(l, Health::Warn, T(L"Windows is marked as still being set up, which can make it restart into set-up again and again."));
                }
                std::vector<wstr> be = regMulti(root, base + cs + L"Control\\Session Manager", L"BootExecute");
                for (const wstr& b : be)
                    if (!icontains(b, L"autocheck autochk")) in.loopSigns.push_back(L"A program runs before Windows starts: " + b);
                    else if (!iequals(b, L"autocheck autochk *")) in.loopSigns.push_back(L"A disk check is scheduled for the next start.");
            } else if (!err.empty())
                l.tech += L" (could not look inside: " + err + L")";
        }
    }
    // 10. Starting up (loops)
    {
        Link& l = c.link("restart", N(L"Starting up"));
        ULONGLONG now = nowTime(), since = now - 14 * kDay;
        bool srt = false, pending = false;
        int dumps = 0;
        ULONGLONG t = fileTime(in.winDir + L"\\System32\\LogFiles\\Srt\\SrtTrail.txt");
        if (t > since) {
            srt = true;
            in.loopSigns.push_back(T(L"Automatic Repair ran on ") + dateText(t) + T(L" because Windows could not start."));
        }
        WIN32_FIND_DATAW fd{};
        HANDLE f = FindFirstFileW((in.winDir + L"\\Minidump\\*.dmp").c_str(), &fd);
        ULONGLONG first = 0;
        if (f != INVALID_HANDLE_VALUE) {
            do {
                ULONGLONG ft = ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
                if (ft > since) {
                    ++dumps;
                    if (!first || ft < first) first = ft;
                }
            } while (FindNextFileW(f, &fd));
            FindClose(f);
        }
        t = fileTime(in.winDir + L"\\MEMORY.DMP");
        if (t > since && !dumps) {
            dumps = 1;
            first = t;
        }
        if (dumps)
            in.loopSigns.push_back(T(L"Windows crashed with a blue screen ") + std::to_wstring(dumps) + (dumps == 1 ? T(L" time") : T(L" times")) + T(L" since ") + dateText(first) + L".");
        if (exists(in.winDir + L"\\WinSxS\\pending.xml")) {
            pending = true;
            in.loopSigns.push_back(T(L"Windows updates are waiting to finish installing."));
        }
        if (isDir(in.root + L"$WINDOWS.~BT") && fileTime(in.root + L"$WINDOWS.~BT") > since)
            in.loopSigns.push_back(T(L"An upgrade to a newer Windows was started recently."));
        in.loop = !in.running && (srt || dumps >= 2 || setupFlag || (pending && dumps));
        if (in.running) {
            set(l, in.loopSigns.empty() ? Health::Ok : Health::Info,
                in.loopSigns.empty() ? T(L"This Windows is running now, so it started fine.") : T(L"This Windows is running now. Earlier trouble was found, listed below."));
        } else if (in.loop) {
            set(l, Health::Bad, T(L"Windows seems to be stuck in a loop: it starts, fails and restarts, or keeps going into Automatic Repair."));
        } else if (!in.loopSigns.empty()) {
            set(l, Health::Warn, T(L"No restart loop was found, but there are signs of earlier trouble."));
        } else set(l, Health::Ok, T(L"No signs that Windows keeps restarting or failing to start."));
        for (const wstr& s : in.loopSigns) l.tech += (l.tech.empty() ? L"" : L"\n") + s;
        if (!in.running && pending && (in.loop || dumps)) l.fixes.push_back(c.fix(FX_REVERT_UPDATES, in.part));
        if (in.loop && store && !in.entryId.empty()) {
            const BcdEntry* e = store->find(in.entryId);
            if (e && e->recoveryEnabled) l.fixes.push_back(c.fix(FX_STOP_AUTOREPAIR, in.sysPart, storeIdx, in.entryId));
        }
    }
}

static void finishInstall(Report& r, Install& in) {
    Health worst = Health::Ok;
    for (const Link& l : in.chain)
        if (l.health != Health::Unknown) worst = worse(worst, l.health == Health::Info ? Health::Ok : l.health);
    // The rebuild already does what "add to the menu" does
    bool rebuild = false, create = false;
    for (const Fix& f : in.plan) {
        rebuild |= f.kind == FX_REBUILD_BOOT;
        create |= f.kind == FX_CREATE_SYSPART;
    }
    std::vector<Fix> plan;
    std::vector<int> remap(in.plan.size(), -1);
    // Order: unlock, partition table, start-up partition, disk check, files, registry, then start-up files and menu.
    static const int order[] = {FX_FIRMWARE_SETTINGS, FX_UNLOCK, FX_FIX_GPT, FX_MOVE_TO_ROOT, FX_CREATE_SYSPART, FX_REBUILD_ESP, FX_MARK_ACTIVE, FX_CHKDSK, FX_RESTORE_HIVE, FX_ENABLE_DRIVERS,
                                FX_CLEAR_SETUP, FX_SFC, FX_REVERT_UPDATES, FX_MBR_CODE, FX_REBUILD_BOOT, FX_ADD_ENTRY, FX_SHOW_IN_MENU, FX_MAKE_DEFAULT,
                                FX_STOP_AUTOREPAIR};
    for (int k : order)
        for (size_t i = 0; i < in.plan.size(); ++i) {
            const Fix& f = in.plan[i];
            if (f.kind != k) continue;
            if (create && f.kind == FX_REBUILD_BOOT) continue;          // creating the partition writes the start-up files too
            if ((rebuild || create) && (f.kind == FX_ADD_ENTRY || f.kind == FX_SHOW_IN_MENU)) continue;
            if ((rebuild || create) && f.kind == FX_MAKE_DEFAULT) continue;
            remap[i] = (int)plan.size();
            plan.push_back(f);
        }
    // Fixes merged away point to the one that replaces them
    for (size_t i = 0; i < in.plan.size(); ++i)
        if (remap[i] < 0)
            for (size_t j = 0; j < plan.size(); ++j)
                if (plan[j].kind == (create ? FX_CREATE_SYSPART : FX_REBUILD_BOOT)) remap[i] = (int)j;
    for (Link& l : in.chain) {
        std::vector<int> fx;
        for (int i : l.fixes)
            if (i >= 0 && i < (int)remap.size() && remap[i] >= 0 && std::find(fx.begin(), fx.end(), remap[i]) == fx.end()) fx.push_back(remap[i]);
        l.fixes = fx;
    }
    in.plan = plan;
    for (Fix& f : in.plan) {
        f.install = (int)(&in - r.installs.data());
        if (f.disk < 0) f.disk = r.m.parts[in.part].disk;
        // Don't offer to undo what's done to the running Windows' own files that are in use
        if (in.running && (f.kind == FX_RESTORE_HIVE || f.kind == FX_ENABLE_DRIVERS || f.kind == FX_CLEAR_SETUP || f.kind == FX_REVERT_UPDATES || f.kind == FX_MOVE_TO_ROOT)) {
            f.available = false;
            f.notAvailable = T(L"This is the Windows that is running now. Start the PC from a Windows USB stick or Windows' recovery tools and run this program there.");
        }
    }
    in.verdict = worst;
    if (in.running) {
        in.verdictTitle = T(L"Running now");
        in.verdictText = worst == Health::Ok ? T(L"This is the Windows you are using now. Everything it needs to start is in place.")
                                             : T(L"This is the Windows you are using now, so it starts. Some things still need attention.");
        if (in.verdict == Health::Bad) in.verdict = Health::Warn;
    } else if (worst == Health::Bad) {
        in.verdictTitle = in.loop ? T(L"Stuck in a restart loop") : T(L"Will not start");
        in.verdictText = in.loop ? T(L"This Windows keeps failing to start.") : T(L"Something this Windows needs to start is missing or damaged.");
    } else if (worst == Health::Warn) {
        in.verdictTitle = in.active ? T(L"May have trouble starting") : T(L"Inactive");
        in.verdictText = in.active ? T(L"It should start, but some things need attention.") : T(L"This Windows looks healthy but is not in the start-up menu, so it never starts.");
    } else {
        in.verdictTitle = T(L"Should start normally");
        in.verdictText = T(L"Everything this Windows needs to start is in place.");
    }
}

static void folderInstall(Report& r, Install& in) {
    const Partition& P = r.m.parts[in.part];
    in.verdict = in.windowsOld ? Health::Info : Health::Bad;
    Link& l = in.chain.emplace_back();
    l.icon = "folder";
    l.name = N(L"Location");
    l.tech = in.winDir;
    if (in.windowsOld) {
        in.verdictTitle = T(L"Old copy kept after an upgrade");
        in.verdictText = T(L"Windows keeps the previous Windows in Windows.old for a while after an upgrade. It cannot be started, but files can be copied out of it.");
        l.health = Health::Info;
        l.plain = in.verdictText;
        return;
    }
    in.verdictTitle = T(L"Moved into a folder");
    in.verdictText = T(L"This Windows sits inside the folder \"") + in.folder + T(L"\", so it cannot start. It was probably copied or moved there.");
    l.health = Health::Bad;
    l.plain = in.verdictText + T(L" To start it again, its folders must go back to the top of the drive.");
    bool clash = exists(in.root + L"Windows");
    Fix mv = makeFix(FX_MOVE_TO_ROOT);
    mv.part = in.part;
    mv.disk = P.disk;
    mv.install = (int)(&in - r.installs.data());
    if (clash) {
        mv.available = false;
        mv.notAvailable = T(L"The top of this drive already has its own Windows folder. Move that one away first, or copy the user files you need out of this folder.");
    }
    in.plan.push_back(mv);
    l.fixes.push_back(0);
    if (!clash) {
        Fix rb = makeFix(FX_REBUILD_BOOT);
        rb.part = -1;
        rb.disk = P.disk;
        rb.install = mv.install;
        rb.what = T(L"After the move, copies fresh start-up files and writes a start-up entry for this Windows.");
        in.plan.push_back(rb);
        l.fixes.push_back(1);
    }
}

// ---------------------------------------------------------------- likely causes
static wstr causeFor(const Link& l) {
    const wstr& n = l.name;
    if (n == L"PC firmware")
        return icontains(l.plain, L"Secure Boot")
                   ? T(L"Secure Boot was switched on (by you, by a firmware update, or because the firmware settings were reset to their defaults).")
                   : T(L"The firmware settings were reset (after a firmware update or when the small battery on the main board ran flat), they were changed by hand, or the disk came from another PC.");
    if (n == L"Disk") return T(L"A loose or failing cable, a failing disk, or the disk was copied (cloned) to another disk of a different size.");
    if (n == L"Start-up partition")
        return T(L"It was deleted while making room for another system, the disk was copied without it, Windows was installed while another disk was connected (so the start-up partition went to that disk), or a partitioning tool removed or reformatted it.");
    if (n == L"Boot manager")
        return T(L"A Windows update interrupted by a power cut, an antivirus that removed it, another system's installer (often Linux) that replaced it, or disk errors.");
    if (n == L"Start-up settings")
        return T(L"A Windows update or repair interrupted by a power cut, the disk was copied or moved to another PC, another boot manager was installed over it, or Windows was moved to another partition.");
    if (n == L"Windows partition")
        return T(L"BitLocker locks the drive after firmware or hardware changes; a power cut while writing leaves it needing a disk check; or the drive ran full.");
    if (n == L"Windows loader" || n == L"Core system files")
        return T(L"An update that failed halfway, an antivirus that quarantined a file, disk errors, or a cleaning tool that removed files it should not have.");
    if (n == L"System settings")
        return T(L"A power cut while Windows was saving its settings, a failing disk, or the disk was moved to a PC with a different disk controller, so the right driver was not set to load.");
    if (n == L"Starting up")
        return T(L"An update or driver installed shortly before it began, a failing disk or memory, malware, or a setting changed just before the trouble started.");
    if (n == L"Location") return T(L"The Windows folders were copied or moved while backing up or reorganising files, or a reset kept the old Windows in a folder.");
    if (n == L"PC's start-up list")
        return T(L"A Windows or firmware update removed or moved Linux's entry, or the firmware settings were reset to their defaults.");
    if (n == L"Linux boot loader") return T(L"Windows was reinstalled or repaired (it rewrites the start-up partition), or the start-up partition was formatted.");
    if (n == L"GRUB settings") return T(L"The Linux partition was deleted, formatted or recreated (which gives it a new id), or the disk was copied.");
    if (n == L"Disk start-up code") return T(L"Windows was installed or repaired after Linux, and wrote its own start-up code over GRUB.");
    if (n == L"Linux kernel") return T(L"A Linux update failed halfway, or /boot ran out of space during an update.");
    if (n == L"GRUB menu") return T(L"\"update-grub\" failed or was interrupted, or /boot was cleaned up by hand.");
    if (n == L"Linux settings") return T(L"A partition was removed or reformatted after Linux was set up, or a disk was disconnected.");
    if (n == L"Systems") return T(L"The disk is not detected (cable, or the storage mode RAID / VMD in the firmware settings needs a driver), the system partition was deleted or formatted, or it is locked by BitLocker.");
    return L"";
}

void addCauses(Install& in) {
    for (Link& l : in.chain)
        if ((l.health == Health::Bad || l.health == Health::Warn) && l.cause.empty()) l.cause = causeFor(l);
}

// ---------------------------------------------------------------- the PC's own chain (no system found)
void buildPcChain(Report& r) {
    Install& c = r.pcChain;
    c = Install();
    c.name = T(L"This PC");
    c.where = T(L"No Windows or Linux found");
    auto link = [&](const char* icon, const wstr& name, Health h, const wstr& plain, const wstr& tech = wstr()) {
        Link l;
        l.icon = icon;
        l.name = name;
        l.health = h;
        l.plain = plain;
        l.tech = tech;
        c.chain.push_back(l);
    };
    const Firmware& fw = r.m.fw;
    link("bd_chip", N(L"PC firmware"), Health::Ok, fw.uefi ? T(L"The PC starts in UEFI mode, the modern way of starting.") : T(L"The PC starts in legacy (BIOS) mode, the older way of starting."));
    c.chain.back().aboutSecureBoot = fw.uefi;
    int readable = 0;
    for (const Disk& d : r.m.disks) readable += d.readable;
    if (r.m.disks.empty()) link("disk_hw", N(L"Disk"), Health::Bad, T(L"No disk was found at all."));
    else if (!readable) link("disk_hw", N(L"Disk"), Health::Bad, T(L"None of the disks can be read."));
    else link("disk_hw", N(L"Disk"), Health::Ok, std::to_wstring(r.m.disks.size()) + (r.m.disks.size() == 1 ? T(L" disk was found and can be read.") : T(L" disks were found and can be read.")));
    int esp = -1, active = -1;
    for (size_t i = 0; i < r.m.parts.size(); ++i) {
        if (isEsp(r.m.parts[i]) && esp < 0) esp = (int)i;
        if (r.m.parts[i].active && active < 0) active = (int)i;
    }
    int sys = fw.uefi ? esp : active;
    if (sys < 0)
        link("partition", N(L"Start-up partition"), Health::Bad,
             fw.uefi ? T(L"No EFI system partition (start-up partition) was found on any disk.") : T(L"No partition is marked active, so an older PC has nothing to start from."));
    else link("partition", N(L"Start-up partition"), Health::Ok, T(L"A start-up partition was found."), r.m.parts[sys].where());
    bool bm = sys >= 0 && !r.m.parts[sys].root.empty() &&
              (fw.uefi ? looksLikeProgram(r.m.parts[sys].root + L"EFI\\Microsoft\\Boot\\bootmgfw.efi") || looksLikeProgram(r.m.parts[sys].root + L"EFI\\Boot\\bootx64.efi")
                       : GetFileAttributesW((r.m.parts[sys].root + L"bootmgr").c_str()) != INVALID_FILE_ATTRIBUTES);
    if (sys < 0) link("bd_key", N(L"Boot manager"), Health::Unknown, T(L"Cannot be checked without a start-up partition."));
    else if (bm) link("bd_key", N(L"Boot manager"), Health::Ok, T(L"A boot manager is on the start-up partition."));
    else link("bd_key", N(L"Boot manager"), Health::Bad, T(L"There is no boot manager on the start-up partition."));
    size_t entries = 0;
    for (const BcdStore& s : r.stores)
        for (const BcdEntry& e : s.entries) entries += e.app == APP_LOADER && !e.ramdisk;
    if (r.stores.empty()) link("bd_list", N(L"Start-up settings"), sys < 0 ? Health::Unknown : Health::Bad, T(L"No start-up settings (BCD) were found."));
    else if (!entries) link("bd_list", N(L"Start-up settings"), Health::Bad, T(L"The start-up settings list no system to start."));
    else link("bd_list", N(L"Start-up settings"), Health::Warn, T(L"The start-up settings list ") + std::to_wstring(entries) + T(L" system(s), but none of them was found on the disks."));
    link("drive", N(L"Systems"), Health::Bad,
         T(L"No Windows or Linux was found on any disk, so there is nothing for the PC to start. Starting stops here."));
    addCauses(c);
    applyLessons(c);
    c.chain.back().lesson = T(L"A system (Windows or Linux) is the program that actually runs the PC. Everything before this step only exists to find and start it.");
    c.verdict = Health::Bad;
    c.verdictTitle = T(L"Nothing to start");
    c.verdictText = T(L"No Windows or Linux was found.");
}

// ---------------------------------------------------------------- where this program runs
Environment detectEnvironment(const Report& r) {
    Environment e;
    e.winPE = g_sys.winPE;
    e.safeMode = g_sys.safeMode;
    wchar_t wd[MAX_PATH] = {};
    GetWindowsDirectoryW(wd, MAX_PATH);
    wstr drive = wstr(wd).substr(0, 3);
    e.winRE = e.winPE && (GetFileAttributesW((drive + L"sources\\recovery\\RecEnv.exe").c_str()) != INVALID_FILE_ATTRIBUTES ||
                          GetFileAttributesW((wstr(wd) + L"\\System32\\recenv.exe").c_str()) != INVALID_FILE_ATTRIBUTES);
    wstr maker = toLower(r.m.fw.maker), model = toLower(r.m.fw.model);
    if (icontains(maker, L"vmware")) e.vmName = L"VMware";
    else if (icontains(maker, L"innotek") || icontains(model, L"virtualbox")) e.vmName = L"VirtualBox";
    else if (icontains(maker, L"qemu") || icontains(model, L"kvm") || icontains(model, L"standard pc")) e.vmName = L"QEMU / KVM";
    else if (icontains(maker, L"parallels")) e.vmName = L"Parallels";
    else if (icontains(maker, L"xen")) e.vmName = L"Xen";
    else if (icontains(maker, L"microsoft") && icontains(model, L"virtual")) e.vmName = L"Hyper-V";
    e.virtualMachine = !e.vmName.empty();
    for (const Install& in : r.installs)
        if (in.running)
            for (const Disk& d : r.m.disks)
                if (d.number == r.m.parts[in.part].disk && (d.bus == L"Virtual" || icontains(d.model, L"virtual disk")) && !e.virtualMachine) e.vhdBoot = true;
    fillEnvironmentText(e);
    return e;
}

void fillEnvironmentText(Environment& e) {
    if (e.winRE) {
        e.title = T(L"Running in the Windows Recovery Environment (WinRE)");
        e.text = T(L"WinRE is Windows' own small repair system. It opens by itself after Windows fails to start twice, or from Settings > Recovery > Advanced start-up > Troubleshoot > Advanced options. It runs from memory (drive X:), so anything saved on X: is gone after a restart; your disks keep their normal content.");
        e.next = T(L"Next steps: work through the report below. Repairs here are safe, because the Windows on the disk is not running. When you are done, close this program and choose \"Continue\" (or restart) to start Windows. If it fails again, WinRE opens again by itself.");
    } else if (e.winPE) {
        e.title = T(L"Running in Windows PE");
        e.text = T(L"Windows PE is a small Windows started from a USB stick, a DVD or the network, made for installing and repairing PCs. It runs from memory (drive X:), so anything saved on X: is gone after a restart. The drive letters of your disks may differ from the ones you know.");
        e.next = T(L"Next steps: work through the report below. Repairs here are safe, because the Windows on the disk is not running. Backups are kept on a real disk, not on X:. When you are done, remove the USB stick or DVD and restart, so the PC starts from its own disk.");
    } else if (e.virtualMachine) {
        e.title = T(L"Running in a virtual machine (") + e.vmName + L")";
        e.text = T(L"This \"PC\" is simulated by ") + e.vmName + L". Its firmware, Secure Boot and start-up order are set in the virtual machine's own settings, and its disks are files on the real computer.";
        e.next = T(L"Next steps: take a snapshot (checkpoint) of the virtual machine before repairs, so you can go back in one click. To change Secure Boot or the start-up order, switch the virtual machine off and change its settings in ") + e.vmName + L".";
    } else if (e.vhdBoot) {
        e.title = T(L"Windows is running from a virtual disk file (VHD)");
        e.text = T(L"This Windows lives inside a single large file (a VHD or VHDX) on a real disk, and starts from it directly. Its start-up files are on the real disk's start-up partition, while Windows itself is inside the file.");
        e.next = T(L"Next steps: before repairs, copy the VHD file somewhere safe if you can (it is the whole Windows). Never move or rename the file: the start-up settings point to its exact place.");
    } else if (e.safeMode) {
        e.title = T(L"Windows is in Safe Mode");
        e.text = T(L"Safe Mode starts Windows with only the most basic drivers, to get past a driver or program that stops it from starting normally.");
        e.next = T(L"Next steps: check the report below; restarting normally leaves Safe Mode.");
    }
}

bool anyLockedDrive(const Report& r) {
    for (const Partition& p : r.m.parts)
        if (p.locked) return true;
    return false;
}

// ---------------------------------------------------------------- renamed system folders
std::vector<RenamedWindows> findRenamedWindows(const wstr& rootIn, int part, int budget) {
    std::vector<RenamedWindows> out;
    wstr root = rootIn;
    if (!root.empty() && root.back() != L'\\') root += L'\\';
    // Breadth first, so the shallow places Windows lives in are looked at before deep ones
    std::vector<wstr> level = {L""};
    for (int depth = 0; depth < 4 && !level.empty() && budget > 0; ++depth) {
        std::vector<wstr> next;
        for (const wstr& rel : level) {
            if (--budget < 0) break;
            wstr dir = root + rel;
            // Is this a system folder? It holds the kernel and the Windows loader.
            if (!rel.empty() && exists(dir + L"ntoskrnl.exe") && (exists(dir + L"winload.exe") || exists(dir + L"winload.efi"))) {
                wstr cfg;
                if (exists(dir + L"config\\SYSTEM")) cfg = L"config";
                else
                    for (const wstr& sub : subfolders(dir, 4000))
                        if (exists(dir + sub + L"\\SYSTEM") && exists(dir + sub + L"\\SOFTWARE")) {
                            cfg = sub;
                            break;
                        }
                wstr r2 = rel.substr(0, rel.size() - 1);
                size_t sl = r2.find_last_of(L'\\');
                RenamedWindows w;
                w.part = part;
                w.systemName = sl == wstr::npos ? r2 : r2.substr(sl + 1);
                w.windowsRel = sl == wstr::npos ? wstr() : r2.substr(0, sl);
                w.configName = cfg;
                bool plain = iequals(w.systemName, L"System32") && iequals(cfg, L"config") &&
                             (iequals(w.windowsRel, L"Windows") || (w.windowsRel.size() > 8 && iequals(w.windowsRel.substr(w.windowsRel.size() - 8), L"\\Windows")));
                bool sysWow = iequals(w.systemName, L"SysWOW64") || iequals(w.systemName, L"SysNative");
                if (!plain && !sysWow && !w.windowsRel.empty() && !cfg.empty()) out.push_back(w);
                continue;  // nothing to find inside a system folder
            }
            WIN32_FIND_DATAW fd{};
            HANDLE h = FindFirstFileExW((dir + L"*").c_str(), FindExInfoBasic, &fd, FindExSearchLimitToDirectories, nullptr, 0);
            if (h == INVALID_HANDLE_VALUE) continue;
            do {
                wstr n = fd.cFileName;
                if (n == L"." || n == L".." || !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
                if (iequals(n, L"WinSxS") || iequals(n, L"$Recycle.Bin") || iequals(n, L"System Volume Information") || iequals(n, L"AppData") ||
                    iequals(n, L"node_modules") || iequals(n, L"servicing") || iequals(n, L"assembly") || iequals(n, L"Installer"))
                    continue;
                next.push_back(rel + n + L"\\");
            } while (FindNextFileW(h, &fd) && (int)next.size() < budget);
            FindClose(h);
        }
        level.swap(next);
    }
    return out;
}

void deepScanSystemFolders(Report& r, int budget) {
    for (size_t i = 0; i < r.m.parts.size(); ++i) {
        const Partition& p = r.m.parts[i];
        if (p.root.empty() || !p.readable || p.role == R_ESP || p.role == R_MSR || p.fs != L"NTFS") continue;
        for (const RenamedWindows& w : findRenamedWindows(p.root, (int)i, budget)) {
            Finding f;
            f.health = Health::Warn;
            f.title = T(L"Windows' folders were found under other names");
            wstr list = L"\\" + w.windowsRel + L"\\" + w.systemName + L"\\" + w.configName;
            f.text = TF(L"On {0} there are folders that hold a Windows (its heart, its loader and its settings), but under other names: {1}. Windows only starts from folders called Windows, System32 and config, so this one cannot start as it is. They were probably renamed by hand, by a program or by a failed update.",
                        p.name(), list);
            wstr lastName = w.windowsRel.substr(w.windowsRel.find_last_of(L'\\') == wstr::npos ? 0 : w.windowsRel.find_last_of(L'\\') + 1);
            if (!iequals(lastName, L"Windows") && exists(p.root + (w.windowsRel.find(L'\\') == wstr::npos ? wstr() : w.windowsRel.substr(0, w.windowsRel.find_last_of(L'\\') + 1)) + L"Windows"))
                f.text += T(L"\n\nA folder called Windows already exists next to it, so it cannot simply be renamed back. Look at both folders first (for example with the command prompt) and rename one by hand.");
            else {
                Fix x = makeFix(FX_RENAME_SYSFOLDERS);
                x.part = (int)i;
                x.disk = p.disk;
                x.arg = w.windowsRel;
                x.arg2 = w.systemName;
                x.arg3 = w.configName;
                f.fixes.push_back(x);
                f.text += T(L"\n\nAfter renaming, scan again: if the Windows folder is inside another folder, the report then offers to move it back to the top of the drive.");
            }
            f.lesson = T(L"Windows looks for itself by fixed names: the folder Windows at the top of the drive, with System32 inside it, and config inside that for its settings. A different name is enough to stop it from starting, even though every file is still there.");
            r.pc.push_back(f);
        }
    }
}

// ---------------------------------------------------------------- troubleshoot options (WinRE)
// Windows' own record of its repair tools (System32\Recovery\ReAgent.xml): switched on, and on which partition.
ReAgentInfo readReAgent(const wstr& winDir) {
    ReAgentInfo a;
    std::string x;
    if (!readFileText(winDir + L"\\System32\\Recovery\\ReAgent.xml", x)) return a;
    a.found = true;
    auto attr = [&](const char* tag, const char* name) -> std::string {
        size_t t = x.find(std::string("<") + tag);
        if (t == std::string::npos) return "";
        size_t e = x.find('>', t);
        size_t n = x.find(std::string(name) + "=\"", t);
        if (n == std::string::npos || n > e) return "";
        n += strlen(name) + 2;
        size_t q = x.find('"', n);
        return q == std::string::npos ? "" : x.substr(n, q - n);
    };
    a.enabled = attr("InstallState", "state") == "1";
    a.offset = _strtoui64(attr("WinreLocation", "offset").c_str(), nullptr, 10);
    return a;
}

static void addRecoveryFindings(Report& r) {
    if (r.demo) return;
    for (size_t ii = 0; ii < r.installs.size(); ++ii) {
        const Install& in = r.installs[ii];
        if (in.linux || !in.atRoot || in.windowsOld || in.major < 6 || (in.major == 6 && in.minor < 1)) continue;
        const Partition& P = r.m.parts[in.part];
        bool onPartition = false;
        for (const Partition& q : r.m.parts)
            if (q.disk == P.disk && !q.root.empty() && protectedFileExists(q.root + L"Recovery\\WindowsRE\\Winre.wim")) onPartition = true;
        // Windows' own record counts too: its recovery folder may be closed even to administrators
        ReAgentInfo ra = readReAgent(in.winDir);
        if (ra.enabled && ra.offset)
            for (const Partition& q : r.m.parts)
                if (q.disk == P.disk && q.offset == ra.offset) onPartition = true;
        bool local = protectedFileExists(in.winDir + L"\\System32\\Recovery\\Winre.wim");
        if (onPartition) continue;
        Finding f;
        f.lesson = T(L"The troubleshoot options are the blue \"Choose an option\" screen Windows opens when it cannot start: Startup Repair, System Restore, uninstalling updates, the command prompt and \"Reset this PC\". They live in a small hidden recovery partition (or inside Windows itself).");
        if (local) {
            f.health = Health::Info;
            f.title = TF(L"The troubleshoot options of {0} are switched off or have no partition of their own", in.label());
            f.text = T(L"Windows' repair tools are inside this Windows (in System32\\Recovery), so they can be switched on again without changing any partition. Recreating a recovery partition keeps them safe even if the Windows partition is damaged.");
            Fix e = makeFix(FX_ENABLE_WINRE);
            e.install = (int)ii;
            e.part = in.part;
            e.disk = P.disk;
            e.entry = in.entryId;
            f.fixes.push_back(e);
        } else {
            f.health = Health::Warn;
            f.title = TF(L"The troubleshoot options of {0} are missing", in.label());
            f.text = T(L"When this Windows cannot start, it cannot show its repair tools (the blue \"Choose an option\" screen): the recovery partition is gone or empty. They can be recreated from a copy of the tools found on this PC or on a Windows install USB stick.");
        }
        Fix b = makeFix(FX_REBUILD_WINRE);
        b.install = (int)ii;
        b.part = in.part;
        b.disk = P.disk;
        if (P.bitlocker) b.takes += T(L" This drive uses BitLocker: have its recovery key ready, Windows may ask for it at the next start.");
        f.fixes.push_back(b);
        Fix l = makeFix(FX_LAYOUT);
        l.install = (int)ii;
        l.part = in.part;
        l.disk = P.disk;
        f.fixes.push_back(l);
        r.pc.push_back(f);
    }
}

// ---------------------------------------------------------------- the reserved partition (MSR)
static void addMsrFindings(Report& r) {
    std::vector<int> seen;
    for (size_t ii = 0; ii < r.installs.size(); ++ii) {
        const Install& in = r.installs[ii];
        if (in.linux || !in.atRoot || in.windowsOld || in.major < 6 || (in.major == 6 && in.minor < 2 && !r.m.fw.uefi)) continue;
        const Partition& P = r.m.parts[in.part];
        const Disk* d = nullptr;
        for (const Disk& x : r.m.disks)
            if (x.number == P.disk) d = &x;
        if (!d || d->style != 2 || std::find(seen.begin(), seen.end(), d->number) != seen.end()) continue;
        bool msr = false;
        for (int i : d->parts) msr |= r.m.parts[i].role == R_MSR;
        if (msr) continue;
        seen.push_back(d->number);
        Finding f;
        f.health = Health::Info;
        f.title = TF(L"Disk {0} has no reserved partition (MSR)", std::to_wstring(d->number));
        f.text = TF(L"Windows expects a small 16 MB reserved partition (MSR) on a GPT disk, right after the start-up partition. {0} starts without it, but some updates, BitLocker and disk conversions can fail. It is usually missing when the partitions were made by hand or by a cloning tool.",
                    in.label());
        f.lesson = T(L"The Microsoft reserved partition (MSR) holds nothing you can see. Windows keeps it free for its own use when it changes the disk later.");
        Fix x = makeFix(FX_CREATE_MSR);
        x.install = (int)ii;
        x.part = in.part;
        x.disk = d->number;
        f.fixes.push_back(x);
        Fix l = makeFix(FX_LAYOUT);
        l.install = (int)ii;
        l.part = in.part;
        l.disk = d->number;
        f.fixes.push_back(l);
        r.pc.push_back(f);
    }
}

// ---------------------------------------------------------------- Windows.old: can it be brought back?
struct OldHealth {
    bool ok = false;
    std::vector<wstr> good, bad;
};

static OldHealth checkWindowsOld(const Install& in) {
    OldHealth h;
    auto need = [&](const wstr& rel, const wstr& what) {
        bool e = exists(in.winDir + L"\\" + rel);
        (e ? h.good : h.bad).push_back(what);
        return e;
    };
    bool k = need(L"System32\\ntoskrnl.exe", T(L"the heart of Windows (ntoskrnl.exe)"));
    need(L"System32\\winload.exe", T(L"the Windows loader"));
    bool reg = need(L"System32\\config\\SYSTEM", T(L"the system settings (registry SYSTEM)"));
    need(L"System32\\config\\SOFTWARE", T(L"the program settings (registry SOFTWARE)"));
    if (reg) {
        int64_t sz = fileSize(in.winDir + L"\\System32\\config\\SYSTEM");
        if (sz < 256 * 1024) h.bad.push_back(T(L"the system settings file is too small to be whole"));
    }
    wstr base = in.root + in.folder;
    if (exists(base + L"Program Files")) h.good.push_back(T(L"Program Files"));
    else h.bad.push_back(T(L"Program Files"));
    if (exists(base + L"Users")) h.good.push_back(T(L"the user folders (Users)"));
    else h.bad.push_back(T(L"the user folders (Users)"));
    h.ok = k && reg && h.bad.size() <= 1 && exists(base + L"Program Files");
    return h;
}

static void addWindowsOldFindings(Report& r) {
    for (size_t ii = 0; ii < r.installs.size(); ++ii) {
        const Install& in = r.installs[ii];
        if (in.linux || !in.windowsOld) continue;
        OldHealth h = checkWindowsOld(in);
        wstr good, bad;
        for (const wstr& g : h.good) good += (good.empty() ? L"" : L", ") + g;
        for (const wstr& b : h.bad) bad += (bad.empty() ? L"" : L", ") + b;
        Finding f;
        f.health = h.ok ? Health::Info : Health::Warn;
        f.title = h.ok ? TF(L"The earlier Windows in {0} can be brought back", in.winDir.substr(0, in.winDir.size() - 8))
                       : TF(L"The earlier Windows in {0} cannot be brought back", in.winDir.substr(0, in.winDir.size() - 8));
        f.text = TF(L"{0} keeps the Windows that was there before an upgrade or a reset ({1}).", in.root + in.folder.substr(0, in.folder.size() - 1), in.name.empty() ? wstr(L"Windows") : in.name);
        if (!good.empty()) f.text += L"\n" + TF(L"Found: {0}.", good);
        if (!bad.empty()) f.text += L"\n" + TF(L"Missing or damaged: {0}.", bad);
        f.text += h.ok ? T(L"\n\nIt looks whole, so it can be put back in place of the current Windows. Programs installed and settings changed since then are lost, and personal files saved since then stay in the folder Windows.replaced.")
                       : T(L"\n\nToo much is missing for it to start. Copy the personal files you need out of its Users folder instead.");
        f.lesson = T(L"Windows.old is made by Windows when it is upgraded or reset. Windows deletes it by itself after about 10 days, so bringing it back only works soon after the change.");
        if (h.ok) {
            Fix x = makeFix(FX_RESTORE_WINDOWS_OLD);
            x.install = (int)ii;
            x.part = in.part;
            x.disk = r.m.parts[in.part].disk;
            f.fixes.push_back(x);
        }
        r.pc.push_back(f);
    }
}

// ---------------------------------------------------------------- free space
static void addSpaceFindings(Report& r) {
    for (size_t ii = 0; ii < r.installs.size(); ++ii) {
        const Install& in = r.installs[ii];
        if (in.linux || in.windowsOld) continue;
        const Partition& P = r.m.parts[in.part];
        if (!P.readable || !P.totalBytes || !lowSpace(P.freeBytes, P.totalBytes)) continue;
        Finding f;
        f.health = P.freeBytes < (500ull << 20) ? Health::Bad : Health::Warn;
        f.title = TF(L"The Windows drive is almost full ({0} free)", sizeText(P.freeBytes));
        f.text = TF(L"{0} has only {1} free of {2}. Windows needs free room to start, to install updates and to undo failed ones; with too little it can get stuck restarting or show errors.\n\nFirst free up space (nothing personal is removed). If that is not enough, the partition can be made bigger with unused space right after it, or with the partition after it.",
                    P.name(), sizeText(P.freeBytes), sizeText(P.totalBytes));
        f.lesson = T(L"A partition can only grow into space that comes right after it on the disk, never into space before it. Unused space elsewhere first has to be moved, which this program does not do.");
        Fix c = makeFix(FX_CLEAN_SPACE);
        c.install = (int)ii;
        c.part = in.part;
        c.disk = P.disk;
        f.fixes.push_back(c);
        int nx = nextPartition(r, in.part);
        if (unusedAfter(r, in.part) >= (64ull << 20) || nx >= 0) {
            Fix g = makeFix(FX_GROW_WINDOWS);
            g.install = (int)ii;
            g.part = in.part;
            g.disk = P.disk;
            f.fixes.push_back(g);
        }
        r.pc.insert(r.pc.begin(), f);
    }
}

void diagnose(Report& r) {
    wchar_t wd[MAX_PATH] = {};
    GetWindowsDirectoryW(wd, MAX_PATH);
    wstr runningVol;
    if (!r.hostPE && wd[0] && wd[1] == L':') {
        wchar_t v[MAX_PATH] = {};
        if (GetVolumeNameForVolumeMountPointW((wstr(wd, 2) + L"\\").c_str(), v, MAX_PATH)) runningVol = v;
    }
    for (Install& in : r.installs) {
        if (in.linux) continue;
        const Partition& P = r.m.parts[in.part];
        int ma = 0, mi = 0, bu = 0;
        if (fileVersion(in.winDir + L"\\System32\\ntoskrnl.exe", ma, mi, bu)) {
            in.major = ma;
            in.minor = mi;
            in.build = bu;
        }
        in.name = osName(in.major, in.minor, in.build);
        in.arch = peArch(in.winDir + L"\\System32\\ntoskrnl.exe");
        in.running = !runningVol.empty() && iequals(runningVol, P.volume) && iequals(wstr(wd + 2), in.systemRoot);
        if (in.running) {
            HKEY k = nullptr;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS) {
                wchar_t ed[128] = {};
                DWORD n = sizeof ed;
                if (RegQueryValueExW(k, L"EditionID", nullptr, nullptr, (BYTE*)ed, &n) == ERROR_SUCCESS) in.edition = ed;
                RegCloseKey(k);
            }
        }
        in.where = (P.letter.empty() ? wstr() : P.letter + L" · ") + P.where();
    }
    for (Install& in : r.installs) {
        if (r.m.parts[in.part].locked) {
            // Cannot happen: files of a locked drive cannot be seen. Kept for completeness.
            continue;
        }
        if (in.linux) buildLinuxChain(r, in);
        else if (!in.atRoot) folderInstall(r, in);
        else buildChain(r, in);
    }
    for (Install& in : r.installs)
        if (in.atRoot && !in.linux) finishInstall(r, in);

    // In a normal Windows session the keys of unlocked BitLocker drives can still be read: offer to keep them
    if (!r.demo && !r.hostPE) {
        wstr names;
        for (const Partition& p : r.m.parts)
            if (p.bitlocker && !p.locked) names += (names.empty() ? L"" : L", ") + p.name();
        if (!names.empty()) {
            Finding f;
            f.health = Health::Info;
            f.title = T(L"Keep the BitLocker recovery keys while Windows runs");
            f.text = TF(L"{0} is encrypted with BitLocker. If Windows ever stops starting, the drive can only be opened with its 48-digit recovery key. Windows can show that key now; this program can save it next to itself (locked with your password), and a rescue USB stick made afterwards carries it too.", names);
            f.lesson = T(L"BitLocker asks for the recovery key after hardware or firmware changes, failed updates or start-up repairs. Without the key the files cannot be reached by anyone. A copy is usually also in the Microsoft account of the PC's owner (aka.ms/myrecoverykey).");
            f.fixes.push_back(makeFix(FX_CAPTURE_KEYS));
            r.pc.push_back(f);
        }
    }
    // Locked drives may hold a Windows we cannot see
    for (size_t i = 0; i < r.m.parts.size(); ++i) {
        const Partition& p = r.m.parts[i];
        if (!p.locked) continue;
        Finding f;
        f.health = Health::Warn;
        f.title = (p.letter.empty() ? p.where() : p.letter) + T(L" is locked by BitLocker");
        f.text = T(L"This drive is encrypted and locked, so it cannot be checked for Windows. Unlock it with its recovery key and scan again. Do this first, before any other change on this PC: start-up entries that seem to lead nowhere may belong to a Windows inside this drive.");
        f.lesson = T(L"BitLocker encrypts a whole drive. Without its 48-digit recovery key nothing on it can be read. The key is in the Microsoft account of the PC's owner (aka.ms/myrecoverykey), on a printout, or with the IT department.");
        Fix u = makeFix(FX_UNLOCK);
        u.part = (int)i;
        u.disk = p.disk;
        f.fixes.push_back(u);
        r.pc.push_back(f);
    }
    // Disk problems
    for (const Disk& d : r.m.disks) {
        if (d.problems.empty()) continue;
        Finding f;
        f.health = !d.readable || d.style == 0 || (d.style == 2 && !d.gptPrimaryOk) ? Health::Bad : Health::Warn;
        f.title = d.name();
        for (const wstr& p : d.problems) f.text += (f.text.empty() ? L"" : L"\n") + p;
        if (d.style == 2 && (!d.gptPrimaryOk || !d.gptBackupOk || d.gptBackupMisplaced)) {
            Fix x = makeFix(FX_FIX_GPT);
            x.disk = d.number;
            f.fixes.push_back(x);
        }
        for (int i : d.parts)
            if (r.m.parts[i].dirty) {
                Fix x = makeFix(FX_CHKDSK);
                x.part = i;
                x.disk = d.number;
                x.title = T(L"Check ") + (r.m.parts[i].letter.empty() ? r.m.parts[i].where() : r.m.parts[i].letter);
                f.fixes.push_back(x);
            }
        if (d.style == 0)
            f.text += T(L"\nIf this disk held Windows or your files, do not set it up again: a partition recovery tool (such as PATio LifeSaver) can often find the lost partitions.");
        r.pc.push_back(f);
    }
    // Disks that report they are failing come first: back up before anything else
    for (const Disk& dk : r.m.disks) {
        if (!dk.health.known || dk.health.level == 0) continue;
        Finding f;
        f.health = dk.health.level == 2 ? Health::Bad : Health::Warn;
        f.title = dk.health.level == 2 ? TF(L"{0} may be failing", dk.name()) : TF(L"{0} shows signs of wear", dk.name());
        for (const wstr& p : dk.health.problems) f.text += (f.text.empty() ? L"" : L"\n") + (L"- " + p);
        f.text += dk.health.level == 2
                      ? T(L"\n\nCopy the files you cannot lose to another disk now, before anything else. Avoid repairs that write a lot to this disk (moving or resizing partitions, disk checks, the partition order) until your files are safe, and replace the disk. Moving Windows to a new disk (Disks and partitions > Move Windows to another drive) can help.")
                      : T(L"\n\nIt still works, but keep a recent backup of your files and plan to replace it.");
        f.lesson = T(L"S.M.A.R.T. is the self-check built into disks and SSDs. It counts damaged and replaced sectors, read errors, wear and heat. Rising counts often come weeks before a disk stops working.");
        r.pc.insert(r.pc.begin(), f);
    }
    // Start-up entries (menu and the PC's own list) that lead to nothing
    findDeadEntries(r);
    findStoreProblems(r);
    for (Install& in : r.installs) {
        applyLessons(in);
        addCauses(in);
    }
    buildPcChain(r);
    if (!r.demo) r.env = detectEnvironment(r);
    for (Finding& f : r.pc)
        if (f.lesson.empty() && f.title.find(L"Disk ") == 0) f.lesson = T(kLessonPartitionTable);
    // Rebuilding Windows' start-up files puts Windows first: tell when a Linux shares the PC
    bool anyLinux = false;
    for (const Install& in : r.installs) anyLinux |= in.linux;
    if (anyLinux)
        for (Install& in : r.installs)
            for (Fix& f : in.plan)
                if (!in.linux && (f.kind == FX_REBUILD_BOOT || f.kind == FX_CREATE_SYSPART || f.kind == FX_MBR_CODE))
                    f.takes += r.m.fw.uefi ? T(L" Linux is also on this PC: afterwards Windows starts first. Linux stays installed; use \"Make Linux start first\" in its report to bring its menu back.")
                                           : T(L" Linux is also on this PC: on this older (BIOS) PC this replaces GRUB, so Linux then needs repairing from a Linux USB stick.");
    bool elevated = true;
    HANDLE tok = nullptr;
    if (!r.demo && OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        TOKEN_ELEVATION e{};
        DWORD n = 0;
        if (GetTokenInformation(tok, TokenElevation, &e, sizeof e, &n)) elevated = e.TokenIsElevated != 0;
        CloseHandle(tok);
    }
    if (r.m.disks.empty() && !elevated) {
        Finding f;
        f.health = Health::Bad;
        f.title = T(L"Administrator rights are needed");
        f.text = T(L"Windows only shows disks to programs started as administrator. Close this program, right-click it and choose \"Run as administrator\".");
        r.pc.push_back(f);
    } else if (std::none_of(r.installs.begin(), r.installs.end(), [](const Install& i) { return !i.linux; })) {
        if (anyLockedDrive(r)) {
            Finding b;
            b.health = Health::Bad;
            b.title = T(L"Windows is probably inside a drive locked by BitLocker");
            wstr names;
            for (size_t i = 0; i < r.m.parts.size(); ++i)
                if (r.m.parts[i].locked) {
                    names += (names.empty() ? L"" : L", ") + r.m.parts[i].name();
                    Fix u = makeFix(FX_UNLOCK);
                    u.part = (int)i;
                    u.disk = r.m.parts[i].disk;
                    if (b.fixes.empty()) b.fixes.push_back(u);
                }
            b.text = TF(L"No Windows could be seen, but {0} is encrypted with BitLocker and locked. Windows is very likely inside it.\n\nUnlock it first, with its 48-digit recovery key. Until then, do not remove start-up entries, delete partitions or rebuild anything: they may all belong to the Windows inside the locked drive.", names);
            b.lesson = T(L"BitLocker encrypts a whole drive. Without its 48-digit recovery key nothing on it can be read. The key is in the Microsoft account of the PC's owner (aka.ms/myrecoverykey), on a printout, or with the IT department.");
            r.pc.insert(r.pc.begin(), b);
        }
        deepScanSystemFolders(r);
        Finding f;
        f.health = anyLinux ? Health::Info : Health::Bad;
        f.title = T(L"No Windows was found");
        f.text = wstr(anyLinux ? T(L"Only Linux was found on this PC. ") : L"") + T(L"None of the disks has a Windows folder that could be started. If you expected one:\n- The disk may not be detected. Check its cable, or the storage mode (AHCI/RAID/VMD) in the PC's firmware settings.\n- A drive may be locked by BitLocker (listed above, if so).\n- The partition may have been deleted. A partition recovery tool (such as PATio LifeSaver) can often find it again.");
        r.pc.push_back(f);
    }
    // Recovery tools (troubleshoot options) and free space
    addRecoveryFindings(r);
    addSpaceFindings(r);
    addMsrFindings(r);
    addWindowsOldFindings(r);
}

Report scan(const Progress& progress) {
    Report r;
    GetLocalTime(&r.when);
    r.host = g_sys.describe();
    r.hostPE = g_sys.winPE;
    enableAllPrivileges();
    unloadStaleHives();
    if (progress) progress(T(L"Reading disks and partitions"), 0.05f);
    r.m = readMachine();
    r.rawReader = [](const Partition& p) { return partitionReader(p); };
    if (progress) progress(T(L"Reading the disks' health"), 0.2f);
    for (Disk& dk : r.m.disks) dk.health = readDiskHealth(dk.number);
    if (progress) progress(T(L"Reading the start-up settings"), 0.25f);
    r.stores = readStores(r.m);
    int n = 0, total = 0;
    for (const Partition& p : r.m.parts)
        if (!p.root.empty() && p.readable) ++total;
    for (size_t i = 0; i < r.m.parts.size(); ++i) {
        const Partition& p = r.m.parts[i];
        if (p.root.empty() || !p.readable || p.role == R_ESP || p.role == R_MSR) continue;
        if (progress) progress(T(L"Looking for Windows on ") + (p.letter.empty() ? p.where() : p.letter), 0.3f + 0.4f * (float)n++ / std::max(1, total));
        findInstalls(p.root, (int)i, r.installs);
    }
    if (progress) progress(T(L"Looking for Linux"), 0.72f);
    findLinux(r);
    if (progress) progress(T(L"Checking each system"), 0.75f);
    diagnose(r);
    if (progress) progress(T(L"Done"), 1.f);
    return r;
}

// ---------------------------------------------------------------- report text
wstr reportText(const Report& r) {
    wstr s;
    wchar_t when[64];
    swprintf(when, 64, L"%04d-%02d-%02d %02d:%02d", r.when.wYear, r.when.wMonth, r.when.wDay, r.when.wHour, r.when.wMinute);
    s += T(L"PATio Boot Doctor report, ") + wstr(when) + T(L"\r\nChecked from: ") + r.host + L"\r\n";
    s += wstr(T(L"Firmware: ")) + (r.m.fw.uefi ? L"UEFI" : T(L"Legacy BIOS")) + (r.m.fw.secureBoot ? T(L", Secure Boot on") : L"") + L"\r\n\r\n";
    s += T(L"DISKS\r\n");
    for (const Disk& d : r.m.disks) {
        s += L"  " + d.name() + L" · " + (d.style == 2 ? L"GPT" : d.style == 1 ? L"MBR" : T(L"no partition table")) + L"\r\n";
        for (int i : d.parts) {
            const Partition& p = r.m.parts[i];
            wstr fs = !p.fs.empty() ? p.fs : !p.rawFs.empty() ? p.rawFs : p.role == R_MSR ? T(L"no file system (normal)") : L"unreadable";
            s += L"    " + std::to_wstring(p.number) + L". " + T(roleShort(p.role)) + L", " + sizeText(p.length) + L", " + fs +
                 (p.letter.empty() ? L"" : L", " + p.letter) + (p.label.empty() ? L"" : L", \"" + p.label + L"\"") + (p.active ? T(L", active") : L"") +
                 (p.fsUuid.empty() || !p.fs.empty() ? L"" : L", id " + p.fsUuid) + L"\r\n";
        }
        for (const wstr& pr : d.problems) s += L"    ! " + pr + L"\r\n";
    }
    s += T(L"\r\nSYSTEMS FOUND (WINDOWS AND LINUX)\r\n");
    if (r.installs.empty()) s += T(L"  None\r\n");
    for (const Install& in : r.installs) {
        s += L"  " + in.label() + (in.build ? T(L" build ") + std::to_wstring(in.build) : wstr()) + (in.linux ? L" on " : L" in ") + in.winDir +
             (in.edition.empty() ? L"" : L", " + in.edition) + L"\r\n";
        s += L"    " + in.verdictTitle + L": " + in.verdictText + L"\r\n";
        for (const Link& l : in.chain) {
            s += L"    [" + wstr(T(healthWord(l.health))) + L"] " + T(l.name) + L": " + l.plain + L"\r\n";
            if (!l.cause.empty()) s += T(L"       What may have caused this: ") + l.cause + L"\r\n";
            if (!l.tech.empty()) {
                wstr t = l.tech;
                for (size_t p = 0; (p = t.find(L'\n', p)) != wstr::npos; p += 8) t.replace(p, 1, L"\r\n       ");
                s += L"       " + t + L"\r\n";
            }
        }
        if (!in.plan.empty()) {
            s += T(L"    What it will take:\r\n");
            int k = 1;
            for (const Fix& f : in.plan)
                s += L"      " + std::to_wstring(k++) + L". " + f.title + L" - " + f.takes + (f.available ? L"" : L" (not possible here: " + f.notAvailable + L")") + L"\r\n";
        }
    }
    s += T(L"\r\nOTHER FINDINGS\r\n");
    if (r.pc.empty()) s += T(L"  None\r\n");
    for (const Finding& f : r.pc) {
        wstr t = f.text;
        for (size_t p = 0; (p = t.find(L'\n', p)) != wstr::npos; p += 6) t.replace(p, 1, L"\r\n    ");
        s += L"  [" + wstr(T(healthWord(f.health))) + L"] " + f.title + L"\r\n    " + t + L"\r\n";
    }
    return s;
}

}  // namespace bd

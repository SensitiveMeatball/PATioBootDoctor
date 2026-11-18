#include "DiskTool.h"
#include "Lang.h"
#include "core/Platform.h"

namespace bd {

static wstr g_protectedFolder;
void setProtectedFolder(const wstr& f) { g_protectedFolder = f; }

const wchar_t* fsName(int fs) { return fs == 0 ? L"NTFS" : fs == 1 ? L"FAT32" : fs == 2 ? L"exFAT" : L"none"; }

std::vector<wstr> freeLetters() {
    std::vector<wstr> out;
    DWORD used = GetLogicalDrives();
    for (wchar_t c = L'D'; c <= L'Z'; ++c)
        if (!(used & (1u << (c - L'A')))) out.push_back(wstr(1, c) + L":");
    return out;
}

DiskOpInfo diskOpInfo(int k) {
    DiskOpInfo i;
    switch (k) {
        case DO_CREATE:
            i.title = T(L"Create a partition");
            i.what = T(L"Makes a new partition in this unused space, prepares it with a file system and can give it a drive letter.");
            i.lesson = T(L"Unused space is part of the disk that belongs to no partition, so nothing can be stored there yet. A new partition turns it into a drive you can use.");
            break;
        case DO_DELETE:
            i.title = T(L"Delete the partition");
            i.what = T(L"Removes the partition. Its space becomes unused. Undo puts the partition table back, which usually brings the partition and its files back, as long as nothing new was written there in the meantime.");
            i.lesson = T(L"Deleting a partition removes its entry from the disk's table of contents; the files are not wiped at once, but they can no longer be reached.");
            i.danger = true;
            i.losesData = true;
            break;
        case DO_FORMAT:
            i.title = T(L"Format");
            i.what = T(L"Prepares the partition with a new, empty file system. Everything on it is erased. This cannot be undone.");
            i.lesson = T(L"A file system is the way files are organised on a partition. NTFS is Windows' own; FAT32 is understood by almost everything (but holds files of at most 4 GB); exFAT suits USB sticks and memory cards.");
            i.danger = true;
            i.undoable = false;
            i.losesData = true;
            break;
        case DO_EXTEND:
            i.title = T(L"Make it bigger");
            i.what = T(L"Grows the partition into the unused space right after it. The files on it stay. This cannot be undone (it can be made smaller again).");
            i.lesson = T(L"A partition can only grow into unused space that comes directly after it on the disk.");
            i.undoable = false;
            break;
        case DO_SHRINK:
            i.title = T(L"Make it smaller");
            i.what = T(L"Gives up space at the end of the partition, which becomes unused space for a new partition. The files on it stay. This cannot be undone (it can be made bigger again).");
            i.lesson = T(L"Windows can only give up free space that is at the end of the partition, so it may be able to shrink less than the free space shown.");
            i.undoable = false;
            break;
        case DO_LETTER:
            i.title = T(L"Change the drive letter");
            i.what = T(L"Gives the partition a drive letter (like E:), or a different one, so it appears in File Explorer. Nothing on it changes. Programs that remember the old letter may not find their files.");
            i.lesson = T(L"A drive letter is only a name Windows uses for a partition; the partition itself does not change.");
            break;
        case DO_REMOVE_LETTER:
            i.title = T(L"Remove the drive letter");
            i.what = T(L"Hides the partition from File Explorer. Nothing on it changes, and it can get a letter again any time.");
            i.lesson = T(L"Start-up and recovery partitions normally have no letter, so they cannot be changed by accident.");
            break;
        case DO_LABEL:
            i.title = T(L"Rename");
            i.what = T(L"Changes the name shown next to the drive letter (for example \"Photos\"). Nothing on it changes.");
            break;
        case DO_ACTIVE:
            i.title = T(L"Mark as active");
            i.what = T(L"Older PCs (legacy BIOS) start from the partition marked active. Only one partition per disk can be active.");
            i.lesson = T(L"\"Active\" only matters for disks with an MBR partition table on PCs that start in legacy (BIOS) mode.");
            break;
        case DO_INACTIVE:
            i.title = T(L"Mark as not active");
            i.what = T(L"Removes the active mark, so an older PC (legacy BIOS) will no longer try to start from this partition.");
            break;
        case DO_TYPE_DATA:
            i.title = T(L"Make it a normal data partition");
            i.what = T(L"Changes the partition's type to an ordinary data partition, so Windows shows it and lets you use it. Its files stay.");
            i.lesson = T(L"Every partition carries a type that tells systems what it is for: data, start-up (EFI), recovery and so on.");
            break;
        case DO_TYPE_ESP:
            i.title = T(L"Make it a start-up (EFI) partition");
            i.what = T(L"Changes the partition's type so the PC's firmware treats it as a start-up partition. It must use FAT32. Its files stay.");
            i.lesson = T(L"The EFI system partition is the small partition a modern PC starts from; it holds the boot managers of Windows and Linux.");
            break;
        case DO_TYPE_RECOVERY:
            i.title = T(L"Make it a recovery partition");
            i.what = T(L"Changes the partition's type to a hidden recovery partition, the kind that holds Windows' repair tools. Windows then hides it and protects it. Its files stay.");
            break;
        case DO_CHKDSK:
            i.title = T(L"Check for errors");
            i.what = T(L"Runs the Windows disk check, which repairs damaged file-system records. Files in damaged places can be lost, as with any disk check. This cannot be undone.");
            i.undoable = false;
            break;
        case DO_INIT_GPT:
            i.title = T(L"Set up as GPT");
            i.what = T(L"Gives this empty disk a partition table, so partitions can be made on it. GPT is the modern kind, for UEFI PCs and disks over 2 TB.");
            i.lesson = T(L"A new disk has no partition table yet, so Windows cannot use it until it is set up.");
            break;
        case DO_INIT_MBR:
            i.title = T(L"Set up as MBR");
            i.what = T(L"Gives this empty disk an older-style MBR partition table: for older PCs (legacy BIOS) and disks up to 2 TB.");
            break;
        case DO_CONVERT_GPT:
            i.title = T(L"Change to GPT");
            i.what = T(L"Changes this disk's empty partition table from MBR to GPT.");
            i.lesson = T(L"MBR is the older kind of partition table (up to 2 TB, 4 main partitions); GPT is the modern kind (huge disks, 128 partitions, needed for UEFI start-up).");
            break;
        case DO_CONVERT_MBR:
            i.title = T(L"Change to MBR");
            i.what = T(L"Changes this disk's empty partition table from GPT to MBR.");
            break;
        case DO_MBR2GPT:
            i.title = T(L"Change to GPT, keeping Windows and files");
            i.what = T(L"Uses Windows' own MBR2GPT tool to change the disk to GPT without deleting anything, and makes the start-up partition a UEFI one. Afterwards the PC must be switched from legacy (BIOS/CSM) to UEFI mode in its firmware settings, or Windows will not start. This cannot be undone here.");
            i.lesson = T(L"This is the usual step before switching on Secure Boot or upgrading to Windows 11 on an older installation.");
            i.danger = true;
            i.undoable = false;
            break;
        case DO_CLEAN:
            i.title = T(L"Wipe the disk");
            i.what = T(L"Removes the whole partition table, so the disk looks empty and new. Undo can put the table back (and with it the partitions), as long as nothing new was written to the disk in the meantime.");
            i.lesson = T(L"This only erases the disk's table of contents, not every byte; to destroy the data for good a disk-erasing tool is needed.");
            i.danger = true;
            i.losesData = true;
            break;
        case DO_ONLINE:
            i.title = T(L"Bring online");
            i.what = T(L"Switches the disk on for Windows, so its partitions can be used. Windows leaves some disks offline, for example a second disk with a copied Windows.");
            break;
        case DO_OFFLINE:
            i.title = T(L"Take offline");
            i.what = T(L"Switches the disk off for Windows: its drives disappear until it is brought online again. Nothing on it changes.");
            break;
        case DO_UNLOCK:
            i.title = T(L"Unlock BitLocker");
            i.what = T(L"Opens the drive with its 48-digit BitLocker recovery key, so its files can be read and checked. Nothing on it changes, and it locks again when the PC restarts.");
            i.lesson = T(L"BitLocker encrypts a whole drive. Without the key, nothing on it can be read: not by Windows, not by this program. Unlock it before changing anything else on this disk.");
            break;
        case DO_FIND_LOST:
            i.title = T(L"Find lost partitions");
            i.what = T(L"Looks through unused space for partitions that were deleted or lost (NTFS, FAT32, exFAT, Linux ext). Only reads the disk; what is found can be put back afterwards. Quick: looks where partitions normally start, takes minutes. Full: reads every sector, can take hours.");
            i.lesson = T(L"Deleting a partition only removes its entry from the disk's table of contents. Until something new is written there, the files are still on the disk and the partition can often be put back.");
            break;
        case DO_RECOVER_FILES:
            i.title = T(L"Recover deleted files");
            i.what = T(L"Looks for deleted files on this drive and copies the ones you choose to another drive. Nothing is written to this drive. Quick: reads Windows' list of files (NTFS). Full: also reads the whole drive, finding files after a quick format and pictures and PDFs by their contents.");
            i.lesson = T(L"A deleted file stays on the disk until its space is used for something new. Stop using the drive as soon as you notice, and save what you recover to a different drive.");
            break;
        case DO_MERGE_NEXT:
            i.title = T(L"Make it bigger using the partition after it");
            i.what = T(L"Deletes the partition right after this one and adds its space (and any unused space after it) to this partition. Everything on the deleted partition is lost. The files on this partition stay.");
            i.lesson = T(L"A partition can only grow into space directly after it, never into space before it.");
            i.danger = true;
            i.undoable = false;
            i.losesData = true;
            break;
        case DO_BITLOCKER_OFF:
            i.title = T(L"Turn BitLocker off");
            i.what = T(L"Decrypts the whole drive, so it no longer needs a recovery key. Your files stay. Windows does it in the background and it can take hours on a big drive; the drive can be used meanwhile. Keep the PC on and plugged in until it is finished (manage-bde -status shows how far it is).");
            i.lesson = T(L"Without BitLocker anyone who takes out the disk can read it. Turn it off only when you need to, for example before big repairs or to move the disk to another PC, and turn it on again afterwards in Windows' settings.");
            i.undoable = false;
            break;
        case DO_BITLOCKER_SUSPEND:
            i.title = T(L"Pause BitLocker");
            i.what = T(L"Keeps the drive encrypted but stops BitLocker from asking for the recovery key at start-up, until it is resumed. Useful before firmware updates, Secure Boot changes or start-up repairs. Resume it afterwards with: manage-bde -protectors -enable C: (or in Windows' BitLocker settings).");
            i.lesson = T(L"While BitLocker is paused, the key that opens the drive is stored on the drive itself, so the drive is not protected. Resume it as soon as the work is done.");
            i.undoable = false;
            break;
        case DO_SURFACE:
            i.title = T(L"Test the surface (read every sector)");
            i.what = T(L"Reads every sector of the disk once and counts the ones that cannot be read. Nothing is written, so it is safe on any disk, also the one Windows runs from. It takes about as long as copying the whole disk: roughly 5 to 20 minutes for every 100 GB.");
            i.lesson = T(L"The disk's own health report (S.M.A.R.T.) misses many failing disks. Sectors that cannot be read are the surest sign that a disk is dying.");
            break;
        case DO_LAYOUT:
            i.title = T(L"Put the partitions in Windows' own order…");
            i.what = T(L"Shows a plan to put the partitions of the Windows on this disk in the order Windows Setup makes them (UEFI: start-up, reserved, Windows, recovery; legacy BIOS: System Reserved, Windows, recovery), moving and resizing them step by step. Each step is shown and confirmed first.");
            i.lesson = T(L"Windows' troubleshoot options need their recovery partition right after Windows, and updates expect the partitions in Windows' own order. Disks set up by hand or by cloning tools often differ.");
            i.danger = true;
            i.undoable = false;
            break;
        case DO_CLEAR_READONLY:
            i.title = T(L"Allow writing");
            i.what = T(L"Clears the disk's read-only mark, which can stop Windows from changing anything on it. A switch on the disk or card itself is not affected.");
            break;
    }
    return i;
}

static const Disk* diskByNumber(const Report& r, int n) {
    for (const Disk& d : r.m.disks)
        if (d.number == n) return &d;
    return nullptr;
}

static bool sameDrive(const wstr& path, const Partition& p) {
    return !p.letter.empty() && path.size() >= 2 && towupper(path[0]) == towupper(p.letter[0]) && path[1] == L':';
}

uint64_t unusedAfterEnd(const Report& r, int disk, uint64_t end) {
    for (const Disk& d : r.m.disks)
        if (d.number == disk)
            for (const auto& g : d.gaps)
                if (g.first >= end && g.first - end < (1ull << 20)) return g.second + (g.first - end);
    return 0;
}

int nextPartition(const Report& r, int pi) {
    if (pi < 0 || pi >= (int)r.m.parts.size()) return -1;
    const Partition& p = r.m.parts[pi];
    uint64_t end = p.offset + p.length;
    uint64_t room = unusedAfterEnd(r, p.disk, end) + (1ull << 20);
    int best = -1;
    for (size_t i = 0; i < r.m.parts.size(); ++i) {
        const Partition& q = r.m.parts[i];
        if ((int)i == pi || q.disk != p.disk || q.offset < end) continue;
        if (best < 0 || q.offset < r.m.parts[best].offset) best = (int)i;
    }
    if (best >= 0 && r.m.parts[best].offset - end > room) return -1;  // something else is in between
    return best;
}

uint64_t unusedAfter(const Report& r, int pi) {
    if (pi < 0 || pi >= (int)r.m.parts.size()) return 0;
    const Partition& p = r.m.parts[pi];
    return unusedAfterEnd(r, p.disk, p.offset + p.length);
}

static wstr whyProtected(const Report& r, int pi) {
    const Partition& p = r.m.parts[pi];
    for (const Install& in : r.installs)
        if (in.running && (in.part == pi || in.sysPart == pi))
            return in.part == pi ? T(L"This is the drive the running Windows is on.") : T(L"The running Windows starts from this partition.");
    if (sameDrive(g_sys.exeDir, p)) return T(L"This program runs from this drive.");
    if (!g_protectedFolder.empty() && sameDrive(g_protectedFolder, p)) return T(L"The backups are kept on this drive. Choose another backup folder first.");
    return L"";
}

static wstr whyDiskProtected(const Report& r, int disk) {
    const Disk* d = diskByNumber(r, disk);
    if (!d) return T(L"The disk was not found.");
    for (int i : d->parts) {
        wstr w = whyProtected(r, i);
        if (!w.empty()) return T(L"A partition on this disk is in use: ") + w;
    }
    return L"";
}

wstr workingDiskReason(const Report& r, int disk) {
    for (const Install& in : r.installs)
        if (in.running) {
            if (in.part >= 0 && r.m.parts[in.part].disk == disk) return T(L"The Windows that is running now is on this disk.");
            if (in.sysPart >= 0 && r.m.parts[in.sysPart].disk == disk) return T(L"The Windows that is running now starts from this disk.");
        }
    for (const Partition& p : r.m.parts)
        if (p.disk == disk && sameDrive(g_sys.exeDir, p)) return T(L"This program runs from this disk.");
    return L"";
}

wstr workingDiskRefusal(const Report& r, int disk) {
    wstr w = workingDiskReason(r, disk);
    if (w.empty()) return w;
    return w + T(L" Disks in use are not changed: Windows keeps writing to them while a change runs, which can damage them. Start the PC from the Boot Doctor rescue USB stick (Tools > Make a USB rescue disk) and do it from there.");
}

// Changes to the partitions or the partition table: refused on a disk in use
static bool changesLayout(int k) {
    switch (k) {
        case DO_SURFACE:
        case DO_UNLOCK: case DO_FIND_LOST: case DO_RECOVER_FILES: case DO_LABEL: case DO_LETTER: case DO_REMOVE_LETTER: case DO_CHKDSK:
        case DO_ONLINE: case DO_CLEAR_READONLY: case DO_BITLOCKER_OFF: case DO_BITLOCKER_SUSPEND:
            return false;
    }
    return true;
}

static std::vector<std::pair<int, wstr>> diskOpsForRaw(const Report& r, int sel, int disk, int pi, int gap);

std::vector<std::pair<int, wstr>> diskOpsFor(const Report& r, int sel, int disk, int pi, int gap) {
    auto out = diskOpsForRaw(r, sel, disk, pi, gap);
    wstr refuse = workingDiskRefusal(r, disk);
    if (!refuse.empty())
        for (auto& op : out)
            if (changesLayout(op.first) && op.second.empty()) op.second = refuse;
    return out;
}

static std::vector<std::pair<int, wstr>> diskOpsForRaw(const Report& r, int sel, int disk, int pi, int gap) {
    std::vector<std::pair<int, wstr>> out;
    const Disk* d = diskByNumber(r, disk);
    if (!d) return out;
    auto add = [&](int k, const wstr& why = wstr()) { out.push_back({k, why}); };
    if (sel == SEL_GAP) {
        int primaries = 0;
        bool ext = false;
        for (int i : d->parts) {
            if (r.m.parts[i].role == R_EXTENDED) ext = true;
            if (r.m.parts[i].number <= 4) ++primaries;
        }
        add(DO_FIND_LOST);
        add(DO_CREATE, d->style == 1 && primaries >= 4 ? T(L"This MBR disk already has four main partitions, the most it can hold.") : d->style == 1 && primaries >= 3 && ext ? T(L"Unused space inside an existing extended partition cannot be used from here; use Disk Management.") : L"");
    } else if (sel == SEL_PART && pi >= 0) {
        const Partition& p = r.m.parts[pi];
        wstr prot = whyProtected(r, pi);
        bool runningOs = false;
        for (const Install& in : r.installs)
            if (in.running && in.part == pi) runningOs = true;
        bool sys = p.role == R_ESP || p.role == R_MSR || p.role == R_RECOVERY;
        wstr holds;  // a system installed here
        for (const Install& in : r.installs)
            if (in.part == pi || in.bootPart == pi) holds = in.label();
        wstr typeBlock = !prot.empty() ? prot : !holds.empty() ? holds + T(L" is installed here; changing the partition's type would hide it.") : wstr();
        wstr noBde = findSystemTool(L"manage-bde.exe").empty() ? T(L"The BitLocker tool (manage-bde.exe) is not part of this system.") : wstr();
        if (p.locked) add(DO_UNLOCK, noBde);
        if (p.bitlocker && !p.locked) {
            add(DO_BITLOCKER_SUSPEND, noBde);
            add(DO_BITLOCKER_OFF, noBde);
        }
        if (p.role != R_MSR && p.role != R_EXTENDED) add(DO_RECOVER_FILES, p.locked ? T(L"The drive is locked by BitLocker. Unlock it first.") : L"");
        if (!p.volume.empty() && p.role != R_MSR) add(DO_LETTER, runningOs ? T(L"The drive the running Windows is on cannot change its letter.") : L"");
        if (!p.letter.empty()) add(DO_REMOVE_LETTER, prot);
        if (p.readable && !p.volume.empty()) add(DO_LABEL);
        if (p.role != R_MSR) add(DO_FORMAT, !prot.empty() ? prot : p.locked ? T(L"The drive is locked by BitLocker. Unlock it first.") : L"");
        add(DO_DELETE, prot);
        bool gapAfter = false;
        for (const auto& g : d->gaps)
            if (g.first >= p.offset + p.length && g.first - (p.offset + p.length) < (1ull << 20)) gapAfter = true;
        if (p.role == R_DATA || p.role == R_SYSTEM)
            add(DO_EXTEND, !gapAfter ? T(L"There is no unused space right after it.") : (p.readable && p.fs != L"NTFS") ? T(L"Only NTFS partitions can be made bigger here.") : L"");
        if (p.role == R_DATA || p.role == R_SYSTEM) {
            int nx = nextPartition(r, pi);
            wstr why = nx < 0 ? T(L"There is no partition right after it.")
                       : (p.readable && p.fs != L"NTFS") ? T(L"Only NTFS partitions can be made bigger here.")
                       : !whyProtected(r, nx).empty() ? T(L"The partition after it is in use: ") + whyProtected(r, nx)
                       : r.m.parts[nx].role == R_ESP || r.m.parts[nx].role == R_MSR ? T(L"The partition after it is a start-up or reserved partition, which must stay.")
                       : r.m.parts[nx].role == R_EXTENDED ? T(L"The space after it is an extended partition; use Disk Management.")
                       : wstr();
            for (const Install& in : r.installs)
                if (nx >= 0 && why.empty() && (in.part == nx || in.bootPart == nx)) why = in.label() + T(L" is installed on the partition after it.");
            add(DO_MERGE_NEXT, why);
        }
        if (p.role == R_DATA || p.role == R_SYSTEM)
            add(DO_SHRINK, p.fs != L"NTFS" || !p.readable ? T(L"Only readable NTFS partitions can be made smaller here.") : p.freeBytes < (200ull << 20) ? T(L"It has almost no free space to give up.") : L"");
        if (d->style == 1 && p.number <= 4 && p.role != R_EXTENDED) {
            if (!p.active) add(DO_ACTIVE);
            else add(DO_INACTIVE, prot);
        }
        if (p.role != R_DATA && p.role != R_SYSTEM) add(DO_TYPE_DATA, prot);
        if (p.role != R_ESP) add(DO_TYPE_ESP, !typeBlock.empty() ? typeBlock : (p.readable && p.fs != L"FAT32") ? T(L"A start-up partition must use FAT32. Format it as FAT32 first.") : L"");
        if (p.role != R_RECOVERY && !sys) add(DO_TYPE_RECOVERY, typeBlock);
        if (p.readable) add(DO_CHKDSK);
    } else if (sel == SEL_DISK) {
        wstr prot = whyDiskProtected(r, disk);
        if (d->style == 0) {
            add(DO_INIT_GPT);
            add(DO_INIT_MBR);
        } else if (d->parts.empty()) {
            add(d->style == 1 ? DO_CONVERT_GPT : DO_CONVERT_MBR);
        } else if (d->style == 1) {
            bool win = false;
            for (const Install& in : r.installs)
                if (!in.linux && r.m.parts[in.part].disk == disk) win = true;
            if (win) {
                wstr tool = findSystemTool(L"mbr2gpt.exe");
                add(DO_MBR2GPT, tool.empty() ? T(L"The MBR2GPT tool is part of Windows 10 (version 1703) and newer; it is not on this system.") : L"");
            }
        }
        add(DO_FIND_LOST);
        add(DO_SURFACE);
        {
            bool win = false;
            for (const Install& in : r.installs)
                if (!in.linux && in.atRoot && r.m.parts[in.part].disk == disk) win = true;
            if (win) add(DO_LAYOUT);
        }
        add(DO_CLEAN, prot);
        add(DO_ONLINE);
        add(DO_OFFLINE, prot);
        add(DO_CLEAR_READONLY);
    }
    return out;
}

static wstr cleanLabel(wstr l, int fs) {
    l.erase(std::remove_if(l.begin(), l.end(), [](wchar_t c) { return c == L'"' || c == L'\'' || c < 32; }), l.end());
    size_t max = fs == 0 ? 32 : 11;
    if (l.size() > max) l.resize(max);
    return l;
}

wstr diskpartScript(const DiskOpArgs& a, const Report& r) {
    const Disk* d = diskByNumber(r, a.disk);
    if (!d) return L"";
    const Partition* p = a.part >= 0 && a.part < (int)r.m.parts.size() ? &r.m.parts[a.part] : nullptr;
    wstr s = L"select disk " + std::to_wstring(d->number) + L"\n";
    wstr selp = p ? L"select partition " + std::to_wstring(p->number) + L"\n" : wstr();
    auto format = [&](int fs) {
        if (fs >= 3) return wstr();
        wstr lbl = cleanLabel(a.label, fs);
        return L"format fs=" + toLower(fsName(fs)) + (lbl.empty() ? wstr() : L" label=\"" + lbl + L"\"") + (a.quick ? L" quick" : L"") + L" override\n";
    };
    switch (a.kind) {
        case DO_CREATE: {
            wstr where = L" size=" + std::to_wstring(a.sizeMB) + L" offset=" + std::to_wstring(a.offset / 1024);
            int fs = a.fs;
            if (d->style == 2) {
                if (a.type == 1) { s += L"create partition efi" + where + L"\n"; fs = 1; }
                else if (a.type == 3) { s += L"create partition msr size=16 offset=" + std::to_wstring(a.offset / 1024) + L"\n"; fs = 3; }
                else s += L"create partition primary" + where + L"\n";
                if (a.type == 2) s += L"set id=de94bba4-06d1-4d40-a16a-bfd50179d6ac override\ngpt attributes=0x8000000000000001\n";
            } else {
                s += L"create partition primary" + where + L"\n";
                if (a.type == 1) { s += L"set id=ef override\n"; fs = 1; }
                if (a.type == 2) s += L"set id=27 override\n";
            }
            s += format(fs);
            if (!a.letter.empty() && a.type == 0 && fs < 3) s += L"assign letter=" + a.letter.substr(0, 1) + L"\n";
            break;
        }
        case DO_DELETE: s += selp + L"delete partition override\n"; break;
        case DO_FORMAT: s += selp + format(a.fs); break;
        case DO_EXTEND: s += selp + L"extend size=" + std::to_wstring(a.sizeMB) + L"\n"; break;
        case DO_MERGE_NEXT: {
            const Partition* n = a.part2 >= 0 && a.part2 < (int)r.m.parts.size() ? &r.m.parts[a.part2] : nullptr;
            if (!n || !p) return L"";
            s += L"select partition " + std::to_wstring(n->number) + L"\ndelete partition override\n" + selp + L"extend\n";
            break;
        }
        case DO_SHRINK: s += selp + L"shrink desired=" + std::to_wstring(a.sizeMB) + L" minimum=" + std::to_wstring(a.sizeMB) + L"\n"; break;
        case DO_LETTER:
            s += selp + (p && !p->letter.empty() ? L"remove letter=" + p->letter.substr(0, 1) + L" noerr\n" : wstr()) + L"assign letter=" + a.letter.substr(0, 1) + L"\n";
            break;
        case DO_REMOVE_LETTER: s += selp + L"remove letter=" + (p ? p->letter.substr(0, 1) : wstr()) + L"\n"; break;
        case DO_ACTIVE: s += selp + L"active\n"; break;
        case DO_INACTIVE: s += selp + L"inactive\n"; break;
        case DO_TYPE_DATA:
            s += selp + (d->style == 2 ? L"set id=ebd0a0a2-b9e5-4433-87c0-68b6b72699c7 override\ngpt attributes=0x0000000000000000\n" : L"set id=07 override\n");
            break;
        case DO_TYPE_ESP: s += selp + (d->style == 2 ? L"set id=c12a7328-f81f-11d2-ba4b-00a0c93ec93b override\n" : L"set id=ef override\n"); break;
        case DO_TYPE_RECOVERY:
            s += selp + (d->style == 2 ? L"set id=de94bba4-06d1-4d40-a16a-bfd50179d6ac override\ngpt attributes=0x8000000000000001\n" : L"set id=27 override\n");
            break;
        case DO_INIT_GPT: s += L"online disk noerr\nattributes disk clear readonly noerr\nconvert gpt\n"; break;
        case DO_INIT_MBR: s += L"online disk noerr\nattributes disk clear readonly noerr\nconvert mbr\n"; break;
        case DO_CONVERT_GPT: s += L"convert gpt\n"; break;
        case DO_CONVERT_MBR: s += L"convert mbr\n"; break;
        case DO_CLEAN: s += L"clean\n"; break;
        case DO_ONLINE: s += L"online disk\n"; break;
        case DO_OFFLINE: s += L"offline disk\n"; break;
        case DO_CLEAR_READONLY: s += L"attributes disk clear readonly\n"; break;
        default: return L"";
    }
    return s;
}

static ToolResult runDiskpart(const wstr& script, volatile bool* cancel) {
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    wstr file = wstr(tmp) + L"PATioBD-disk.txt";
    std::string u = wideToUtf8(script + L"exit\n");
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD wr = 0;
    if (h != INVALID_HANDLE_VALUE) {
        WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr);
        CloseHandle(h);
    }
    ToolResult t = runTool(findSystemTool(L"diskpart.exe"), L"/s \"" + file + L"\"", cancel);
    DeleteFileW(file.c_str());
    return t;
}

FixOutcome runDiskOp(const DiskOpArgs& a, Report& r, Journal& j, volatile bool* cancel) {
    FixOutcome o;
    DiskOpInfo info = diskOpInfo(a.kind);
    const Disk* d = diskByNumber(r, a.disk);
    const Partition* p = a.part >= 0 && a.part < (int)r.m.parts.size() ? &r.m.parts[a.part] : nullptr;
    if (!d) {
        o.result = T(L"The disk was not found. Scan again.");
        return o;
    }
    // Checked again here, whatever asked for it: the same rules as the menus (running Windows, backup drive, ...)
    if (a.kind != DO_CREATE) {
        bool listed = false;
        wstr why;
        for (const auto& op : diskOpsFor(r, p ? SEL_PART : SEL_DISK, a.disk, a.part, -1))
            if (op.first == a.kind) {
                listed = true;
                why = op.second;
            }
        if (!listed || !why.empty()) {
            o.result = T(L"Nothing was changed: ") + (why.empty() ? wstr(T(L"this is not possible for this partition or disk.")) : why);
            return o;
        }
    }
    if (a.kind == DO_CHKDSK) {  // the disk check is the same as the repair
        Fix f = makeFix(FX_CHKDSK);
        f.part = a.part;
        f.disk = a.disk;
        return runFix(f, r, j, cancel, nullptr);
    }
    wstr target = p ? p->name() + L" (" + p->where() + L")" : d->name();
    wstr act = j.begin(info.title, target);
    wstr err;
    if (a.kind == DO_BITLOCKER_OFF || a.kind == DO_BITLOCKER_SUSPEND) {
        wstr bde = findSystemTool(L"manage-bde.exe");
        wstr drv = p ? p->letter : wstr();
        if (drv.empty()) {
            o.result = T(L"The drive needs a drive letter for this. Give it one first.");
        } else if (a.kind == DO_BITLOCKER_OFF) {
            ToolResult t = runTool(bde, L"-off " + drv, cancel);
            o.log = L"> manage-bde -off " + drv + L"\r\n" + t.output;
            o.ok = t.started && t.code == 0;
            o.result = o.ok ? T(L"Decrypting has started. It goes on in the background; keep the PC on until it is finished.")
                            : T(L"BitLocker could not be turned off. ") + t.output.substr(t.output.size() > 400 ? t.output.size() - 400 : 0);
        } else {
            ToolResult t = runTool(bde, L"-protectors -disable " + drv + L" -RebootCount 0", cancel);
            if (!(t.started && t.code == 0)) t = runTool(bde, L"-protectors -disable " + drv, cancel);  // Windows 7 has no RebootCount
            o.log = L"> manage-bde -protectors -disable " + drv + L"\r\n" + t.output;
            o.ok = t.started && t.code == 0;
            o.result = o.ok ? T(L"BitLocker is paused: the PC no longer asks for the recovery key at start-up. Resume it afterwards with manage-bde -protectors -enable ") + drv + L"."
                            : T(L"BitLocker could not be paused. ") + t.output.substr(t.output.size() > 400 ? t.output.size() - 400 : 0);
        }
        j.end(act, o.ok, o.result, o.log, false);
        o.actionDir = act;
        o.undoable = false;
        return o;
    }
    bool tableOp = a.kind != DO_LABEL && a.kind != DO_LETTER && a.kind != DO_REMOVE_LETTER && a.kind != DO_ONLINE && a.kind != DO_OFFLINE && a.kind != DO_CLEAR_READONLY;
    if (tableOp && !j.saveDiskTables(act, *d, err)) {
        o.result = T(L"Nothing was changed, because the backup failed. ") + err;
        j.end(act, false, o.result, L"", false);
        return o;
    }
    if (a.kind == DO_LABEL) {
        wstr old = p ? p->label : wstr();
        wstr lbl = cleanLabel(a.label, p && p->fs == L"NTFS" ? 0 : 1);
        j.recordLabel(act, *p, old);
        o.ok = p && !p->volume.empty() && SetVolumeLabelW(p->volume.c_str(), lbl.c_str());
        o.result = o.ok ? T(L"The drive is now called \"") + lbl + L"\"." : T(L"The name could not be changed (") + errorText(GetLastError()) + L").";
    } else if (a.kind == DO_MBR2GPT) {
        wstr tool = findSystemTool(L"mbr2gpt.exe");
        wstr args = L"/convert /disk:" + std::to_wstring(a.disk) + (r.hostPE ? L"" : L" /allowFullOS");
        o.log = L"> mbr2gpt " + args + L"\r\n";
        ToolResult t = runTool(tool, args, cancel);
        o.log += t.output;
        o.ok = t.started && t.code == 0;
        o.result = o.ok ? T(L"The disk is now GPT. Before Windows can start again, switch the PC to UEFI mode (CSM / Legacy off) in its firmware settings.")
                        : T(L"The disk could not be changed. MBR2GPT said: ") + t.output.substr(t.output.size() > 400 ? t.output.size() - 400 : 0);
    } else {
        wstr script = diskpartScript(a, r);
        if (a.kind == DO_LETTER && p) {
            wstr undo = L"select disk " + std::to_wstring(d->number) + L"\nselect partition " + std::to_wstring(p->number) + L"\nremove letter=" + a.letter.substr(0, 1) + L" noerr\n";
            if (!p->letter.empty()) undo += L"assign letter=" + p->letter.substr(0, 1) + L"\n";
            j.recordScript(act, undo);
        } else if (a.kind == DO_REMOVE_LETTER && p)
            j.recordScript(act, L"select disk " + std::to_wstring(d->number) + L"\nselect partition " + std::to_wstring(p->number) + L"\nassign letter=" + p->letter.substr(0, 1) + L"\n");
        else if (a.kind == DO_OFFLINE)
            j.recordScript(act, L"select disk " + std::to_wstring(d->number) + L"\nonline disk\n");
        else if (a.kind == DO_ONLINE)
            j.recordScript(act, L"select disk " + std::to_wstring(d->number) + L"\noffline disk\n");
        o.log = T(L"diskpart script:\r\n") + script + L"\r\n";
        ToolResult t = runDiskpart(script, cancel);
        o.log += t.output;
        o.ok = t.started && t.code == 0;
        if (o.ok) {
            switch (a.kind) {
                case DO_CREATE: o.result = T(L"The new partition was made") + (a.letter.empty() ? wstr(L".") : T(L" and is drive ") + a.letter + L"."); break;
                case DO_DELETE: o.result = T(L"The partition was deleted. Its space is now unused."); break;
                case DO_FORMAT: o.result = T(L"The partition was formatted as ") + wstr(fsName(a.fs)) + L"."; break;
                case DO_EXTEND: o.result = T(L"The partition is ") + std::to_wstring(a.sizeMB) + T(L" MB bigger."); break;
                case DO_MERGE_NEXT: o.result = T(L"The partition after it was deleted and its space added."); break;
                case DO_SHRINK: o.result = T(L"The partition is ") + std::to_wstring(a.sizeMB) + T(L" MB smaller. The space is now unused."); break;
                case DO_LETTER: o.result = T(L"The partition is now drive ") + a.letter + L"."; break;
                default: o.result = info.title + T(L": done."); break;
            }
        } else {
            wstr tail = t.output.substr(t.output.size() > 500 ? t.output.size() - 500 : 0);
            o.result = T(L"Windows did not allow it. Diskpart said:\n") + tail;
        }
    }
    o.actionDir = act;
    o.undoable = info.undoable && o.ok;
    j.end(act, o.ok, o.result, o.log, o.undoable);
    return o;
}

}  // namespace bd

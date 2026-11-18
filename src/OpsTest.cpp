// --opstest: real disk operations (create, rename, letter, delete, wipe, and
// their undo / redo) on a throw-away 256 MB virtual disk file. The PC's own
// disks are never touched. Needs administrator rights.
#include "Layout.h"
#include "Recover.h"
#include "WinRepair.h"
#include "SelfTest.h"
#include "DiskTool.h"
#include "Repair.h"
#include "Rescue.h"
#include "core/Platform.h"

using namespace bd;

namespace {

struct Log {
    wstr text;
    int passed = 0, failed = 0;
    void line(const wstr& s) { text += s + L"\r\n"; }
    void check(bool ok, const wstr& what) {
        (ok ? passed : failed)++;
        line((ok ? L"  ok    " : L"  FAIL  ") + what);
    }
};

ToolResult diskpart(const wstr& folder, const wstr& script) {
    wstr file = folder + L"\\opstest-dp.txt";
    std::string u = wideToUtf8(script + L"exit\n");
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD wr = 0;
    if (h != INVALID_HANDLE_VALUE) {
        WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr);
        CloseHandle(h);
    }
    ToolResult t = runTool(findSystemTool(L"diskpart.exe"), L"/s \"" + file + L"\"");
    DeleteFileW(file.c_str());
    return t;
}

}  // namespace

int runRescueTest(const wstr& folderIn) {
    wchar_t full[MAX_PATH];
    GetFullPathNameW(folderIn.c_str(), MAX_PATH, full, nullptr);
    wstr folder = full;
    while (!folder.empty() && folder.back() == L'\\') folder.pop_back();
    Log log;
    log.line(L"Rescue stick on a virtual disk");
    wstr vhd = folder + L"\\rescuetest.vhdx";
    diskpart(folder, L"select vdisk file=\"" + vhd + L"\"\ndetach vdisk noerr\n");
    DeleteFileW(vhd.c_str());
    ToolResult t = diskpart(folder, L"create vdisk file=\"" + vhd + L"\" maximum=2048 type=expandable\nselect vdisk file=\"" + vhd + L"\"\nattach vdisk\n");
    log.check(t.code == 0, L"2 GB virtual disk created and attached");
    Sleep(1500);
    Report r = scan(nullptr);
    int disk = -1;
    for (int d : rescueTargets(r, true))
        for (const Disk& x : r.m.disks)
            if (x.number == d && x.size == (2048ull << 20)) disk = d;
    log.check(disk >= 0, L"The virtual disk is offered as a target");
    std::vector<RescueSource> src = rescueSources(r);
    log.check(!src.empty(), L"A recovery image was found: " + (src.empty() ? wstr(L"none") : src[0].label + L" (" + sizeText(src[0].size) + L")"));
    bool sysOffered = false;
    for (int d : rescueTargets(r, true))
        for (const Install& in : r.installs)
            if (in.running && r.m.parts[in.part].disk == d) sysOffered = true;
    log.check(!sysOffered, L"The disk of the running Windows is never offered");
    if (disk >= 0 && !src.empty()) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        FixOutcome o = makeRescueStick(r, disk, src[0], exe, folder, [&](const wstr& s, float) { log.line(L"        " + s); }, nullptr);
        log.check(o.ok, L"Stick made: " + o.result);
        if (!o.ok) log.line(o.log.substr(o.log.size() > 3000 ? o.log.size() - 3000 : 0));
        r = scan(nullptr);
        wstr root;
        for (const Partition& p : r.m.parts)
            if (p.disk == disk && !p.root.empty()) root = p.root;
        log.check(!root.empty() && GetFileAttributesW((root + L"sources\\boot.wim").c_str()) != INVALID_FILE_ATTRIBUTES, L"boot.wim is on the stick");
        if (!root.empty()) {
            ToolResult e = runTool(bcdeditPath(), L"/store \"" + root + L"EFI\\Microsoft\\Boot\\BCD\" /enum all");
            log.check(e.code == 0 && icontains(e.output, L"boot.wim") && icontains(e.output, L"winload.efi"), L"UEFI start-up settings start boot.wim");
            ToolResult b = runTool(bcdeditPath(), L"/store \"" + root + L"Boot\\BCD\" /enum all");
            log.check(b.code == 0 && icontains(b.output, L"winload.exe"), L"Legacy BIOS start-up settings are there too");
            // The image itself starts this program
            wstr mnt = folder + L"\\rescuetest-mount";
            CreateDirectoryW(mnt.c_str(), nullptr);
            ToolResult m = runTool(findSystemTool(L"dism.exe"), L"/Mount-Image /ImageFile:\"" + root + L"sources\\boot.wim\" /Index:1 /MountDir:\"" + mnt + L"\" /ReadOnly");
            std::string ini;
            readFileText(mnt + L"\\Windows\\System32\\winpeshl.ini", ini);
            log.check(m.code == 0 && ini.find("PATioBootDoctor_x64.exe") != std::string::npos &&
                          GetFileAttributesW((mnt + L"\\PATio\\PatSuite\\PATio Boot Doctor\\PATioBootDoctor_x64.exe").c_str()) != INVALID_FILE_ATTRIBUTES,
                      L"The image starts PATio Boot Doctor by itself");
            runTool(findSystemTool(L"dism.exe"), L"/Unmount-Image /MountDir:\"" + mnt + L"\" /Discard");
            RemoveDirectoryW(mnt.c_str());
        }
    }
    diskpart(folder, L"select vdisk file=\"" + vhd + L"\"\ndetach vdisk\n");
    DeleteFileW(vhd.c_str());
    log.line(L"");
    log.line(std::to_wstring(log.passed) + L" passed, " + std::to_wstring(log.failed) + L" failed");
    std::string u = "\xEF\xBB\xBF" + wideToUtf8(log.text);
    HANDLE h = CreateFileW((folder + L"\\rescuetest.log").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD wr = 0;
        WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr);
        CloseHandle(h);
    }
    return log.failed ? 1 : 0;
}

// Moving a partition, the surface test, lost partitions, deleted files and the disk image, for
// real, on a throw-away 512 MB virtual disk. Needs administrator rights.
int runMoveTest(const wstr& folderIn) {
    wchar_t full[MAX_PATH];
    GetFullPathNameW(folderIn.c_str(), MAX_PATH, full, nullptr);
    wstr folder = full;
    while (!folder.empty() && folder.back() == L'\\') folder.pop_back();
    Log log;
    log.line(L"Moving, recovering and imaging on a virtual disk");
    wstr vhd = folder + L"\\movetest.vhdx";
    diskpart(folder, L"select vdisk file=\"" + vhd + L"\"\ndetach vdisk noerr\n");
    DeleteFileW(vhd.c_str());
    ToolResult t = diskpart(folder, L"create vdisk file=\"" + vhd + L"\" maximum=512 type=expandable\nselect vdisk file=\"" + vhd + L"\"\nattach vdisk\n");
    if (t.code != 0) {
        // the disk service may still be starting: say what diskpart answered and try once more
        log.line(L"  first try (code " + std::to_wstring(t.code) + L"): " + t.output);
        Sleep(3000);
        diskpart(folder, L"select vdisk file=\"" + vhd + L"\"\ndetach vdisk noerr\n");
        DeleteFileW(vhd.c_str());
        t = diskpart(folder, L"create vdisk file=\"" + vhd + L"\" maximum=512 type=expandable\nselect vdisk file=\"" + vhd + L"\"\nattach vdisk\n");
    }
    log.check(t.started && t.code == 0, L"512 MB virtual disk created and attached");
    if (t.code != 0) log.line(L"  code " + std::to_wstring(t.code) + L": " + t.output);
    Sleep(1500);
    Report r;
    int disk = -1;
    auto rescan = [&]() {
        Sleep(1500);
        r = scan(nullptr);
        for (const Disk& d : r.m.disks)
            if (d.size == (512ull << 20) && (d.bus == L"Virtual" || d.number == disk)) disk = d.number;
    };
    auto part = [&]() -> const Partition* {
        for (const Partition& p : r.m.parts)
            if (p.disk == disk && p.label == L"MOVE") return &p;
        return nullptr;
    };
    // a file whose every part can be told apart
    std::string content(2 << 20, '\0');
    for (size_t i = 0; i < content.size(); ++i) content[i] = (char)((i * 131 + (i >> 9)) & 0xFF);
    auto fileOk = [&](const wstr& path) {
        std::string b;
        return readFileText(path, b) && b == content;
    };
    auto write = [&](const wstr& path) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD wr = 0;
        bool ok = h != INVALID_HANDLE_VALUE && WriteFile(h, content.data(), (DWORD)content.size(), &wr, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(h);
            CloseHandle(h);
        }
        return ok;
    };
    rescan();
    log.check(disk >= 0, L"The virtual disk is found");
    Journal j;
    wstr err;
    j.open(folder + L"\\movetest-backups", L"move test", err);
    if (disk >= 0) {
        wstr sel = L"select disk " + std::to_wstring(disk) + L"\n";
        t = diskpart(folder, sel + L"convert gpt noerr\ncreate partition primary size=100\nformat fs=ntfs quick label=MOVE\n");
        rescan();
        const Partition* p = part();
        // Windows puts its own reserved partition first on a new GPT disk, so the place is whatever it gave
        uint64_t at = p ? p->offset : 0;
        log.check(t.code == 0 && p && p->length >= (99ull << 20) && !p->root.empty(), L"A 100 MB NTFS partition" + (p ? L" at " + std::to_wstring(at >> 20) + L" MB" : wstr()));
        if (t.code != 0 || !p) {
            log.line(L"  code " + std::to_wstring(t.code) + L": " + t.output);
            for (const Partition& x : r.m.parts)
                if (x.disk == disk) log.line(L"  partition " + std::to_wstring(x.number) + L" at " + std::to_wstring(x.offset) + L", " + std::to_wstring(x.length) + L" bytes, label \"" + x.label + L"\"");
        }
        if (p) {
            log.check(write(p->root + L"keep.bin") && fileOk(p->root + L"keep.bin"), L"A 2 MB file is written to it");
            // ---- move towards the end, then back towards the start
            wstr mlog;
            Partition before = *p;
            bool ok = movePartition(disk, before, before.offset + (64ull << 20), folder, nullptr, err, mlog);
            rescan();
            p = part();
            log.check(ok && p && p->offset == at + (64ull << 20), L"Moved 64 MB towards the end" + (ok ? wstr() : L": " + err));
            log.check(p && fileOk(p->root + L"keep.bin"), L"The file is whole after the move");
            if (p) {
                before = *p;
                ok = movePartition(disk, before, before.offset - (32ull << 20), folder, nullptr, err, mlog);
                rescan();
                p = part();
                log.check(ok && p && p->offset == at + (32ull << 20) && fileOk(p->root + L"keep.bin"), L"Moved 32 MB back towards the start (overlapping), file whole" + (ok ? wstr() : L": " + err));
            }
            wstr what;
            log.check(!pendingMove(folder, what), L"No unfinished move is left behind");
            // ---- surface test
            SurfaceResult sr = surfaceTest(disk, nullptr, nullptr);
            log.check(sr.finished && sr.bad == 0 && sr.tested == (512ull << 20), L"Surface test: every sector read, none bad");
            // ---- deleted file
            p = part();
            if (p) {
                write(p->root + L"gone.bin");
                DeleteFileW((p->root + L"gone.bin").c_str());
                Sleep(1500);
                FileScan fs = scanDeletedFiles(*p, false, nullptr, nullptr);
                int idx = -1;
                for (size_t i = 0; i < fs.files.size(); ++i)
                    if (fs.files[i].name == L"gone.bin") idx = (int)i;
                log.check(idx >= 0 && fs.files[idx].size == content.size(), L"A deleted file is found by the quick search");
                if (idx < 0) {
                    log.line(L"  " + std::to_wstring(fs.files.size()) + L" deleted files found, " + std::to_wstring(fs.scanned) + L" records read, NTFS " + (fs.ntfs ? L"yes" : L"no") + L". " + fs.note);
                    for (size_t i = 0; i < fs.files.size() && i < 10; ++i) log.line(L"  " + fs.files[i].name + L", " + std::to_wstring(fs.files[i].size) + L" bytes");
                }
                wstr outDir = folder + L"\\movetest-recovered";
                deleteFolderTree(outDir);
                int failed = 0;
                wstr rlog;
                int got = idx >= 0 ? recoverFiles(*p, fs, {idx}, outDir, nullptr, nullptr, failed, rlog) : 0;
                log.check(got == 1 && failed == 0 && fileOk(outDir + L"\\gone.bin"), L"It is recovered whole to another folder");
                deleteFolderTree(outDir);
            }
            // ---- lost partition
            p = part();
            if (p) {
                uint64_t off = p->offset, len = p->length;
                t = diskpart(folder, sel + L"select partition " + std::to_wstring(p->number) + L"\ndelete partition override\n");
                rescan();
                log.check(t.code == 0 && !part(), L"The partition is deleted");
                std::vector<LostPartition> lost = findLostPartitions(r, disk, false, nullptr, nullptr);
                const LostPartition* lp = nullptr;
                for (const LostPartition& x : lost)
                    if (x.offset == off) lp = &x;
                log.check(lp && lp->fs == L"NTFS" && lp->length == len && lp->restorable, L"The quick search finds it where it was, with its size");
                if (!lp || lp->length != len || !lp->restorable) {
                    log.line(L"  it was at " + std::to_wstring(off) + L", " + std::to_wstring(len) + L" bytes; found:");
                    for (const LostPartition& x : lost)
                        log.line(L"  " + x.fs + L" at " + std::to_wstring(x.offset) + L", " + std::to_wstring(x.length) + L" bytes" + (x.restorable ? wstr() : L", cannot be put back: " + x.why));
                }
                if (lp) {
                    FixOutcome o = restorePartition(*lp, disk, r, j);
                    rescan();
                    p = part();
                    log.check(o.ok && p && p->offset == off && fileOk(p->root + L"keep.bin"), L"Put back: the partition and its file are there again" + (o.ok ? wstr() : L": " + o.result));
                }
            }
            // ---- files on unreadable sectors: treat the whole partition as unreadable, the file must be named
            p = part();
            if (p) {
                wstr note;
                std::vector<FileOnBad> files = filesOnBadRanges(*p, {{p->offset, p->length}}, note, nullptr, nullptr);
                bool keep = false, mftCritical = false;
                for (const FileOnBad& f : files) {
                    keep |= iequals(f.path, L"keep.bin") && !f.critical;
                    mftCritical |= iequals(f.path, L"$MFT") && f.critical;
                }
                log.check(keep && mftCritical, L"Unreadable areas are traced to the files on them ($MFT as start-up critical, keep.bin as a plain file)" + (note.empty() ? wstr() : L": " + note));
            }
            // ---- disk image
            wstr img = folder + L"\\movetest-image.vhd";
            ImageResult ir = imageDisk(disk, img, nullptr, nullptr);
            WIN32_FILE_ATTRIBUTE_DATA fa{};
            GetFileAttributesExW(img.c_str(), GetFileExInfoStandard, &fa);
            uint64_t isz = (uint64_t)fa.nFileSizeHigh << 32 | fa.nFileSizeLow;
            log.check(ir.ok && ir.unreadable == 0 && isz == (512ull << 20) + 512, L"Disk image: the whole disk plus the .vhd footer");
            DeleteFileW(img.c_str());
        }
    }
    diskpart(folder, L"select vdisk file=\"" + vhd + L"\"\ndetach vdisk\n");
    DeleteFileW(vhd.c_str());
    log.line(L"");
    log.line(std::to_wstring(log.passed) + L" passed, " + std::to_wstring(log.failed) + L" failed");
    std::string u = "\xEF\xBB\xBF" + wideToUtf8(log.text);
    HANDLE h = CreateFileW((folder + L"\\movetest.log").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD wr = 0;
        WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr);
        CloseHandle(h);
    }
    return log.failed ? 1 : 0;
}

int runOpsTest(const wstr& folderIn) {
    wchar_t full[MAX_PATH];
    GetFullPathNameW(folderIn.c_str(), MAX_PATH, full, nullptr);
    wstr folder = full;
    while (!folder.empty() && folder.back() == L'\\') folder.pop_back();
    Log log;
    log.line(L"Disk operations on a virtual disk");
    wstr vhd = folder + L"\\opstest.vhdx";
    diskpart(folder, L"select vdisk file=\"" + vhd + L"\"\ndetach vdisk noerr\n");
    DeleteFileW(vhd.c_str());
    ToolResult t = diskpart(folder, L"create vdisk file=\"" + vhd + L"\" maximum=256 type=expandable\nselect vdisk file=\"" + vhd + L"\"\nattach vdisk\n");
    log.check(t.started && t.code == 0, L"Virtual disk created and attached");
    if (t.code != 0) log.line(t.output);
    Sleep(1500);
    Report r;
    int disk = -1;
    auto rescan = [&]() -> const Disk* {
        r = scan(nullptr);
        checkTools(r);
        for (const Disk& d : r.m.disks)
            if (d.size == (256ull << 20) && (d.bus == L"Virtual" || d.number == disk)) return &d;
        return nullptr;
    };
    const Disk* d = rescan();
    log.check(d != nullptr, L"Virtual disk found: " + (d ? d->name() : wstr(L"none")));
    Journal j;
    wstr err;
    j.open(folder + L"\\opstest-backups", L"ops test", err);
    auto part = [&](const wstr& label) -> int {
        for (size_t i = 0; i < r.m.parts.size(); ++i)
            if (r.m.parts[i].disk == disk && r.m.parts[i].role == R_DATA && (label.empty() || r.m.parts[i].label == label)) return (int)i;
        return -1;
    };
    auto undo = [&](const wstr& dir, wstr* next = nullptr) {
        wstr ulog;
        bool ok = undoAction(readAction(dir), r.m, &j, ulog, next);
        if (!ok) log.line(L"        " + ulog);
        Sleep(1500);
        return ok;
    };
    if (d) {
        disk = d->number;
        DiskOpArgs a;
        a.disk = disk;
        a.kind = DO_INIT_GPT;
        FixOutcome o = runDiskOp(a, r, j, nullptr);
        d = rescan();
        log.check(o.ok && d && d->style == 2, L"Set up as GPT: " + o.result);
        // Create, with a name and a drive letter
        a = DiskOpArgs();
        a.kind = DO_CREATE;
        a.disk = disk;
        if (d && !d->gaps.empty()) a.offset = (d->gaps.back().first + (1ull << 20) - 1) / (1ull << 20) * (1ull << 20);
        a.sizeMB = 100;
        a.fs = 0;
        a.label = L"OpsTest";
        std::vector<wstr> letters = freeLetters();
        if (!letters.empty()) a.letter = letters.back();
        o = runDiskOp(a, r, j, nullptr);
        Sleep(1500);
        rescan();
        int pi = part(L"OpsTest");
        log.check(o.ok && pi >= 0, L"Partition created and formatted: " + o.result);
        log.check(pi >= 0 && r.m.parts[pi].letter == a.letter, L"It got the drive letter " + a.letter);
        // Rename, undo, redo
        if (pi >= 0) {
            DiskOpArgs l;
            l.kind = DO_LABEL;
            l.disk = disk;
            l.part = pi;
            l.label = L"Renamed";
            o = runDiskOp(l, r, j, nullptr);
            rescan();
            log.check(o.ok && part(L"Renamed") >= 0, L"Renamed");
            wstr next;
            bool ok = undo(o.actionDir, &next);
            rescan();
            log.check(ok && part(L"OpsTest") >= 0, L"Rename undone");
            ok = undo(next);
            rescan();
            log.check(ok && part(L"Renamed") >= 0, L"Redo (undo of the undo) renames it again");
        }
        // Change the letter, undo
        pi = part(L"");
        if (pi >= 0 && letters.size() > 1) {
            DiskOpArgs l;
            l.kind = DO_LETTER;
            l.disk = disk;
            l.part = pi;
            l.letter = letters[letters.size() - 2];
            o = runDiskOp(l, r, j, nullptr);
            Sleep(1000);
            rescan();
            pi = part(L"");
            log.check(o.ok && pi >= 0 && r.m.parts[pi].letter == l.letter, L"Drive letter changed to " + l.letter);
            undo(o.actionDir);
            rescan();
            pi = part(L"");
            log.check(pi >= 0 && r.m.parts[pi].letter == a.letter, L"Letter change undone");
        }
        // Delete, undo: the partition comes back with its files
        pi = part(L"");
        if (pi >= 0 && !r.m.parts[pi].root.empty()) {
            HANDLE h = CreateFileW((r.m.parts[pi].root + L"keep.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            DWORD wr = 0;
            if (h != INVALID_HANDLE_VALUE) {
                WriteFile(h, "kept", 4, &wr, nullptr);
                FlushFileBuffers(h);
                CloseHandle(h);
            }
            DiskOpArgs del;
            del.kind = DO_DELETE;
            del.disk = disk;
            del.part = pi;
            o = runDiskOp(del, r, j, nullptr);
            rescan();
            log.check(o.ok && part(L"") < 0, L"Partition deleted");
            bool ok = undo(o.actionDir);
            rescan();
            pi = part(L"");
            log.check(ok && pi >= 0, L"Delete undone: the partition is back");
            log.check(pi >= 0 && !r.m.parts[pi].root.empty() && GetFileAttributesW((r.m.parts[pi].root + L"keep.txt").c_str()) != INVALID_FILE_ATTRIBUTES,
                      L"Its files are back too");
        }
        // Wipe, undo
        DiskOpArgs c;
        c.kind = DO_CLEAN;
        c.disk = disk;
        o = runDiskOp(c, r, j, nullptr);
        d = rescan();
        log.check(o.ok && d && d->style == 0, L"Disk wiped");
        bool ok = undo(o.actionDir);
        d = rescan();
        log.check(ok && d && d->style == 2 && part(L"") >= 0, L"Wipe undone: partition table and partition are back");
        // Protection: the running Windows' own partition is never offered for delete or format
        int sysPart = -1;
        for (const Install& in : r.installs)
            if (in.running) sysPart = in.part;
        if (sysPart >= 0) {
            bool blocked = true;
            for (const auto& op : diskOpsFor(r, SEL_PART, r.m.parts[sysPart].disk, sysPart, -1))
                if ((op.first == DO_DELETE || op.first == DO_FORMAT) && op.second.empty()) blocked = false;
            log.check(blocked, L"The running Windows' drive cannot be deleted or formatted");
        }
    }
    diskpart(folder, L"select vdisk file=\"" + vhd + L"\"\ndetach vdisk\n");
    DeleteFileW(vhd.c_str());
    deleteFolderTree(folder + L"\\opstest-backups");
    log.line(L"");
    log.line(std::to_wstring(log.passed) + L" passed, " + std::to_wstring(log.failed) + L" failed");
    std::string u = "\xEF\xBB\xBF" + wideToUtf8(log.text);
    HANDLE h = CreateFileW((folder + L"\\opstest.log").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD wr = 0;
        WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr);
        CloseHandle(h);
    }
    return log.failed ? 1 : 0;
}

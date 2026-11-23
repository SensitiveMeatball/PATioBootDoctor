#include "Repair.h"
#include "Lang.h"
#include "Hive.h"
#include "core/Platform.h"
#include "Space.h"
#include "DiskTool.h"
#include "Keys.h"
#include "StorageDrivers.h"

namespace bd {

static bool exists(const wstr& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

wstr findSystemTool(const wchar_t* name) {
    wchar_t win[MAX_PATH] = {};
    GetWindowsDirectoryW(win, MAX_PATH);
    wstr native = wstr(win) + L"\\Sysnative\\" + name;
    if (exists(native)) return native;
    wstr sys = wstr(win) + L"\\System32\\" + name;
    return exists(sys) ? sys : wstr();
}

static wstr nativeArch() {
    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: return L"x64";
        case PROCESSOR_ARCHITECTURE_INTEL: return L"x86";
        case 12: return L"ARM64";
    }
    return L"";
}

// A drive letter for a partition while a tool runs; removed again afterwards if it was added.
struct TempLetter {
    wstr letter;
    bool made = false;
    bool get(const Partition& p, wstr& err) {
        if (!p.letter.empty()) {
            letter = p.letter;
            return true;
        }
        if (p.volume.empty()) {
            err = T(L"Windows has no volume for ") + p.where() + T(L", so it cannot be opened.");
            return false;
        }
        letter = freeLetter();
        if (letter.empty()) {
            err = T(L"No free drive letter is left.");
            return false;
        }
        made = mountAt(p.volume, letter, err);
        return made;
    }
    ~TempLetter() {
        if (made) unmount(letter);
    }
};

static wstr relOf(const Partition& p, const wstr& full) { return istartsWith(full, p.root) ? full.substr(p.root.size()) : full; }

// The newest bcdboot that can run here: this system's, or the one of the Windows being repaired.
static wstr pickBcdboot(const Install* in, int& major, int& minor) {
    wstr best = findSystemTool(L"bcdboot.exe");
    int b = 0, x = 0, y = 0;
    major = minor = 0;
    if (!best.empty() && fileVersion(best, x, y, b)) {
        major = x;
        minor = y;
    }
    int bestBuild = b;
    if (in) {
        wstr own = in->winDir + L"\\System32\\bcdboot.exe";
        if (exists(own) && peArch(own) == nativeArch() && fileVersion(own, x, y, b) && (best.empty() || b > bestBuild)) {
            best = own;
            major = x;
            minor = y;
        }
    }
    return best;
}

static int targetSysPart(const Report& r, const Install& in) {
    if (in.sysPart >= 0) return in.sysPart;
    const Partition& P = r.m.parts[in.part];
    if (r.m.fw.uefi) {
        for (size_t i = 0; i < r.m.parts.size(); ++i)
            if (isEsp(r.m.parts[i]) && r.m.parts[i].disk == P.disk) return (int)i;
        for (size_t i = 0; i < r.m.parts.size(); ++i)
            if (isEsp(r.m.parts[i])) return (int)i;
        return -1;
    }
    for (size_t i = 0; i < r.m.parts.size(); ++i)
        if (r.m.parts[i].disk == P.disk && r.m.parts[i].active) return (int)i;
    return in.part;  // legacy BIOS: Windows' own partition can hold the start-up files
}

static const Disk* diskOf(const Report& r, int number) {
    for (const Disk& d : r.m.disks)
        if (d.number == number) return &d;
    return nullptr;
}

static bool backupBootFiles(const Report& r, Journal& j, const wstr& act, int sysPart, wstr& err) {
    const Partition& S = r.m.parts[sysPart];
    if (S.root.empty() || !S.readable) return true;  // nothing readable to save
    for (const BcdStore& s : r.stores)
        if (s.part == sysPart && s.live && !j.saveLiveBcd(act, err)) return false;
    if (r.m.fw.uefi) {
        if (!j.saveDir(act, S, L"EFI\\Microsoft", err)) return false;
        if (!j.saveDir(act, S, L"EFI\\Boot", err)) return false;
    } else {
        if (!j.saveFile(act, S, L"bootmgr", err)) return false;
        if (!j.saveDir(act, S, L"Boot", err)) return false;
    }
    return true;
}

static FixOutcome rebuild(const Fix& f, Report& r, Journal& j, const wstr& act, int sysPart, bool addLast) {
    FixOutcome o;
    Install& in = r.installs[f.install];
    const Partition& P = r.m.parts[in.part];
    if (sysPart < 0) {
        o.result = T(L"There is no start-up partition to put the files on. Create one first.");
        return o;
    }
    const Partition& S = r.m.parts[sysPart];
    wstr err;
    if (!backupBootFiles(r, j, act, sysPart, err)) {
        o.result = T(L"Nothing was changed, because the backup failed. ") + err;
        return o;
    }
    const Disk* sd = diskOf(r, S.disk);
    bool bios = !r.m.fw.uefi;
    if (bios && sd && sd->style == 1 && !S.active) {
        if (!j.saveDiskTables(act, *sd, err)) {
            o.result = T(L"Nothing was changed, because the backup failed. ") + err;
            return o;
        }
        if (!setActivePartition(S.disk, S.number, err)) {
            o.result = T(L"The start-up partition could not be marked active. ") + err;
            return o;
        }
        o.log += T(L"Marked ") + S.where() + L" active.\r\n";
    }
    TempLetter sl, wl;
    if (!sl.get(S, err) || !wl.get(P, err)) {
        o.result = err;
        return o;
    }
    int ma = 0, mi = 0;
    wstr tool = pickBcdboot(&in, ma, mi);
    if (tool.empty()) {
        o.result = T(L"bcdboot.exe, the Windows tool that writes start-up files, was not found here.");
        return o;
    }
    bool modern = ma > 6 || (ma == 6 && mi >= 2);
    wstr winDir = wl.letter + L"\\" + in.folder + L"Windows";
    wstr args = L"\"" + winDir + L"\" /s " + sl.letter;
    if (modern) args += r.m.fw.uefi ? L" /f UEFI" : L" /f BIOS";
    if (addLast && modern) args += L" /addlast";
    o.log += L"> " + tool + L" " + args + L"\r\n";
    ToolResult t = runTool(tool, args);
    o.log += t.output;
    o.ok = t.started && t.code == 0;
    if (o.ok) {
        o.result = addLast ? T(L"This Windows was added to the start-up menu.") : T(L"Fresh start-up files were written and this Windows now starts first.");
        if (addLast && !modern) o.result += L" (This version of the tool also made it start first.)";
    } else
        o.result = T(L"The start-up files could not be written. The tool said: ") + t.output.substr(0, 300);
    return o;
}

static FixOutcome bcdedit(const Fix& f, Report& r, Journal& j, const wstr& act, const wstr& what, const wstr& done) {
    FixOutcome o;
    if (f.store < 0 || f.store >= (int)r.stores.size()) {
        o.result = T(L"The start-up settings were not found.");
        return o;
    }
    const BcdStore& s = r.stores[f.store];
    const Partition& S = r.m.parts[s.part];
    wstr err, storeArg;
    TempLetter tl;
    if (s.live) {
        if (!j.saveLiveBcd(act, err)) {
            o.result = err;
            return o;
        }
    } else {
        if (!j.saveFile(act, S, relOf(S, s.file), err)) {
            o.result = T(L"Nothing was changed, because the backup failed. ") + err;
            return o;
        }
        if (!tl.get(S, err)) {
            o.result = err;
            return o;
        }
        storeArg = L"/store \"" + tl.letter + L"\\" + relOf(S, s.file) + L"\" ";
    }
    bool ok = true;
    for (size_t a = 0, b; a < what.size(); a = b + 1) {
        b = what.find(L'\n', a);
        if (b == wstr::npos) b = what.size();
        wstr cmd = storeArg + what.substr(a, b - a);
        o.log += L"> bcdedit " + cmd + L"\r\n";
        ToolResult t = runTool(bcdeditPath(), cmd);
        o.log += t.output;
        ok = ok && t.started && t.code == 0;
    }
    o.ok = ok;
    o.result = ok ? done : T(L"The start-up settings could not be changed. ") + o.log.substr(0, 300);
    return o;
}

static FixOutcome editSystemHive(const Fix& f, Report& r, Journal& j, const wstr& act, bool drivers) {
    FixOutcome o;
    Install& in = r.installs[f.install];
    const Partition& P = r.m.parts[in.part];
    wstr rel = in.folder + L"Windows\\System32\\config\\SYSTEM", err;
    for (const wchar_t* ext : {L"", L".LOG", L".LOG1", L".LOG2"})
        if (!j.saveFile(act, P, rel + ext, err)) {
            o.result = T(L"Nothing was changed, because the backup failed. ") + err;
            return o;
        }
    StorageCheck plan;  // worked out on a copy of the file, before the file itself is opened for changing
    if (drivers && f.arg.empty()) {
        const Disk* d0 = diskOf(r, P.disk);
        plan = checkStorage(in, P, d0 ? d0->bus : wstr());
    }
    LoadedHive h;
    if (!h.load(P.root + rel, false, err)) {
        o.result = T(L"Windows' settings file could not be opened. ") + err;
        return o;
    }
    if (drivers) {
        // Which drivers: those named by the fix (the storage check's plan), else the plan for the
        // controller found on this PC now, plus the core disk drivers.
        std::vector<wstr> names = {L"disk", L"volmgr", L"partmgr", L"Ntfs"};
        auto addName = [&](const wstr& n) {
            for (const wstr& x : names)
                if (iequals(x, n)) return;
            if (!n.empty()) names.push_back(n);
        };
        std::vector<wstr> cddb;
        if (!f.arg.empty()) {
            for (size_t a = 0; a <= f.arg.size();) {
                size_t b = f.arg.find(L',', a);
                if (b == wstr::npos) b = f.arg.size();
                addName(f.arg.substr(a, b - a));
                a = b + 1;
            }
            for (size_t a = 0; !f.arg2.empty() && a <= f.arg2.size();) {
                size_t b = f.arg2.find(L';', a);
                if (b == wstr::npos) b = f.arg2.size();
                if (b > a) cddb.push_back(f.arg2.substr(a, b - a));
                a = b + 1;
            }
        } else {
            const Disk* d = diskOf(r, P.disk);
            for (const wstr& n : plan.turnOn) addName(n);
            cddb = plan.cddb;
            if (!plan.ok || plan.mode == SM_UNKNOWN) {  // no controller seen: the drivers for the disk's bus, as before
                if (d && d->bus == L"NVMe") addName(L"stornvme");
                if (d && d->bus == L"SATA") {
                    addName(L"storahci");
                    addName(L"msahci");
                }
            }
        }
        int changed = 0;
        for (const wstr& cs : regSubkeys(h.root(), L""))
            if (istartsWith(cs, L"ControlSet")) {
                for (const wstr& n : names) {
                    DWORD st = 0;
                    wstr key = cs + L"\\Services\\" + n;
                    if (regDword(h.root(), key, L"Start", st) && st != 0 && regSetDword(h.root(), key, L"Start", 0)) {
                        ++changed;
                        o.log += T(L"Turned on ") + n + L" in " + cs + L" (was " + std::to_wstring(st) + L")\r\n";
                    }
                }
                for (const wstr& e : cddb) {  // Windows 7: "pci#ven_8086&dev_2822&cc_0104=iaStorV"
                    size_t eq = e.find(L'=');
                    if (eq == wstr::npos) continue;
                    wstr id = e.substr(0, eq), svc = e.substr(eq + 1);
                    bool raid = id.find(L"cc_0104") != wstr::npos || id.find(L"cc_0108") != wstr::npos;
                    wstr key = cs + L"\\Control\\CriticalDeviceDatabase\\" + id;
                    if (regEnsureKey(h.root(), key) && regSetString(h.root(), key, L"Service", svc) &&
                        regSetString(h.root(), key, L"ClassGUID", raid ? L"{4D36E97B-E325-11CE-BFC1-08002BE10318}" : L"{4D36E96A-E325-11CE-BFC1-08002BE10318}")) {
                        ++changed;
                        o.log += T(L"Listed the controller ") + id + L" -> " + svc + L" in " + cs + L"\r\n";
                    }
                }
            }
        o.ok = true;
        o.result = changed ? T(L"The disk drivers are turned on again.") : T(L"The disk drivers were already on.");
    } else {
        bool ok = regSetDword(h.root(), L"Setup", L"SystemSetupInProgress", 0) && regSetDword(h.root(), L"Setup", L"SetupType", 0);
        regSetDword(h.root(), L"Setup", L"OOBEInProgress", 0);
        regSetString(h.root(), L"Setup", L"CmdLine", L"");
        o.ok = ok;
        o.result = ok ? T(L"The unfinished set-up mark is cleared.") : T(L"The set-up mark could not be changed.");
    }
    h.unload();
    return o;
}

static ToolResult diskpartRun(const wstr& script, volatile bool* cancel, wstr& log) {
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    wstr file = wstr(tmp) + L"PATioBD-diskpart.txt";
    std::string u = wideToUtf8(script + L"exit\r\n");
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD wr = 0;
    if (h != INVALID_HANDLE_VALUE) {
        WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr);
        CloseHandle(h);
    }
    log += T(L"diskpart script:\r\n") + script + L"\r\n";
    ToolResult t = runTool(findSystemTool(L"diskpart.exe"), L"/s \"" + file + L"\"", cancel);
    DeleteFileW(file.c_str());
    log += t.output + L"\r\n";
    return t;
}

static bool copyBig(const wstr& from, const wstr& to, wstr& err) {
    if (CopyFileExW(from.c_str(), to.c_str(), nullptr, nullptr, nullptr, 0)) return true;
    err = T(L"Could not copy ") + from + L": " + errorText(GetLastError());
    return false;
}

// Copies a folder and everything under it (used to set other makers' boot folders
// aside while the EFI partition is reformatted, and to put them back afterwards).
static bool copyFolderTree(const wstr& from, const wstr& to, wstr& err) {
    CreateDirectoryW(to.c_str(), nullptr);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((from + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return true;
    bool ok = true;
    do {
        wstr name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        wstr src = from + L"\\" + name, dst = to + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            ok = copyFolderTree(src, dst, err) && ok;
        else {
            SetFileAttributesW(dst.c_str(), FILE_ATTRIBUTE_NORMAL);
            ok = copyBig(src, dst, err) && ok;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}

// Where a copy of Windows' repair tools (Winre.wim) can be taken from: this Windows, another
// Windows, a recovery partition, or a Windows install stick (inside its install.wim).
// Where a copy of Windows' repair tools (Winre.wim) can come from, best first.
enum WinreKind { WK_FILE, WK_INSTALL_WIM, WK_INSTALL_ESD, WK_STICK_IMAGE };
struct WinreSource {
    wstr path;
    int kind = WK_FILE;
};

static std::vector<WinreSource> findWinreSources(const Report& r, const Install& in) {
    std::vector<WinreSource> out;
    auto add = [&](const wstr& p, int k) {
        for (const WinreSource& x : out)
            if (iequals(x.path, p)) return;
        out.push_back(WinreSource{p, k});
    };
    // 1. this Windows' own copy (where Windows keeps it while the troubleshoot options are off)
    if (protectedFileExists(in.winDir + L"\\System32\\Recovery\\Winre.wim")) add(in.winDir + L"\\System32\\Recovery\\Winre.wim", WK_FILE);
    // 2. the clean copy a rescue stick carries
    wstr stick = stickFolder();
    if (!stick.empty() && exists(stick + L"\\Winre.wim")) add(stick + L"\\Winre.wim", WK_FILE);
    for (const Partition& q : r.m.parts)
        if (!q.root.empty() && exists(q.root + L"PATioBootDoctor.stick") && exists(q.root + L"PATioAppData\\PATioBootDoctorData\\Winre.wim")) add(q.root + L"PATioAppData\\PATioBootDoctorData\\Winre.wim", WK_FILE);
    // 3. recovery partitions on this PC (open to the system account only: read with the backup permission)
    for (const Partition& q : r.m.parts)
        if (!q.root.empty() && protectedFileExists(q.root + L"Recovery\\WindowsRE\\Winre.wim")) add(q.root + L"Recovery\\WindowsRE\\Winre.wim", WK_FILE);
    // 4. another Windows of the same kind
    for (const Install& o : r.installs)
        if (!o.linux && protectedFileExists(o.winDir + L"\\System32\\Recovery\\Winre.wim") && peArch(o.winDir + L"\\System32\\ntoskrnl.exe") == peArch(in.winDir + L"\\System32\\ntoskrnl.exe"))
            add(o.winDir + L"\\System32\\Recovery\\Winre.wim", WK_FILE);
    // 5. Windows install media: install.wim, or install.esd (Media Creation Tool)
    for (const Partition& q : r.m.parts)
        if (!q.root.empty()) {
            if (exists(q.root + L"sources\\install.wim")) add(q.root + L"sources\\install.wim", WK_INSTALL_WIM);
            else if (exists(q.root + L"sources\\install.esd")) add(q.root + L"sources\\install.esd", WK_INSTALL_ESD);
        }
    // 6. the rescue stick's own image: a Winre.wim with this program added, which can be taken out again
    for (const Partition& q : r.m.parts)
        if (!q.root.empty() && exists(q.root + L"PATioBootDoctor.stick") && exists(q.root + L"sources\\boot.wim")) add(q.root + L"sources\\boot.wim", WK_STICK_IMAGE);
    return out;
}

// A folder with room for images (Windows PE's own temp folder is in memory)
static wstr roomyFolder(const Report& r, uint64_t need) {
    const Partition* best = nullptr;
    for (const Partition& p : r.m.parts)
        if (p.readable && !p.letter.empty() && p.fs == L"NTFS" && p.freeBytes > need && (!best || p.freeBytes > best->freeBytes)) best = &p;
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    wstr dir = best ? best->letter + L"\\PATioBD-work" : wstr(tmp) + L"PATioBD-work";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

// Gets a usable Winre.wim from the source into work\Winre.wim (or returns the source itself).
static wstr takeWinre(const Report& r, const WinreSource& s, wstr& work, volatile bool* cancel, const StepFn& step, wstr& log) {
    if (s.kind == WK_FILE) return s.path;
    wstr dism = findSystemTool(L"dism.exe");
    if (dism.empty()) return L"";
    work = roomyFolder(r, 6ull << 30);
    wstr mount = work + L"\\mount", out = work + L"\\Winre.wim", err;
    CreateDirectoryW(mount.c_str(), nullptr);
    runTool(dism, L"/Unmount-Image /MountDir:\"" + mount + L"\" /Discard");
    wstr image = s.path;
    if (s.kind == WK_INSTALL_ESD) {
        // A compressed .esd cannot be opened directly: its first edition is turned into a .wim first
        if (step) step(T(L"Unpacking the Windows install image (install.esd); this takes several minutes"));
        image = work + L"\\install.wim";
        DeleteFileW(image.c_str());
        ToolResult e = runTool(dism, L"/Export-Image /SourceImageFile:\"" + s.path + L"\" /SourceIndex:1 /DestinationImageFile:\"" + image + L"\" /Compress:max", cancel);
        log += e.output;
        if (e.code != 0) return L"";
    }
    if (s.kind == WK_STICK_IMAGE) {
        // The rescue stick's image is Winre.wim plus this program: take a copy and remove what was added
        if (step) step(T(L"Taking the repair tools from the rescue stick"));
        if (!copyProtectedFile(s.path, out, err)) return L"";
        ToolResult m = runTool(dism, L"/Mount-Image /ImageFile:\"" + out + L"\" /Index:1 /MountDir:\"" + mount + L"\"", cancel);
        log += m.output;
        if (m.code != 0) return L"";
        deleteFolderTree(mount + L"\\PATio");
        deleteFolderTree(mount + L"\\PATioBootDoctor");
        std::string ini = "[LaunchApps]\r\n%SYSTEMDRIVE%\\sources\\recovery\\recenv.exe\r\n";
        HANDLE h = CreateFileW((mount + L"\\Windows\\System32\\winpeshl.ini").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD wr = 0;
        if (h != INVALID_HANDLE_VALUE) {
            WriteFile(h, ini.data(), (DWORD)ini.size(), &wr, nullptr);
            CloseHandle(h);
        }
        ToolResult c = runTool(dism, L"/Unmount-Image /MountDir:\"" + mount + L"\" /Commit", cancel);
        log += c.output;
        RemoveDirectoryW(mount.c_str());
        return c.code == 0 ? out : wstr();
    }
    if (step) step(T(L"Taking the repair tools from the Windows install image"));
    ToolResult m = runTool(dism, L"/Mount-Image /ImageFile:\"" + image + L"\" /Index:1 /MountDir:\"" + mount + L"\" /ReadOnly", cancel);
    log += m.output;
    bool ok = m.code == 0 && copyProtectedFile(mount + L"\\Windows\\System32\\Recovery\\Winre.wim", out, err);
    runTool(dism, L"/Unmount-Image /MountDir:\"" + mount + L"\" /Discard");
    RemoveDirectoryW(mount.c_str());
    if (s.kind == WK_INSTALL_ESD) DeleteFileW(image.c_str());
    return ok ? out : wstr();
}

static FixOutcome rebuildWinre(const Fix& f, Report& r, Journal& j, const wstr& act, volatile bool* cancel, const StepFn& step) {
    FixOutcome o;
    Install* in = f.install >= 0 && f.install < (int)r.installs.size() ? &r.installs[f.install] : nullptr;
    if (!in) { o.result = T(L"No Windows was chosen."); return o; }
    const Partition& P = r.m.parts[in->part];
    const Disk* d = nullptr;
    for (const Disk& x : r.m.disks)
        if (x.number == P.disk) d = &x;
    if (!d) { o.result = T(L"The disk was not found."); return o; }
    wstr err, src, work;
    for (const WinreSource& ws : findWinreSources(r, *in)) {
        src = takeWinre(r, ws, work, cancel, step, o.log);
        if (!src.empty()) break;
    }
    if (src.empty()) {
        o.result = T(L"No copy of Windows' repair tools (Winre.wim) was found: this Windows has none of its own, and there is no recovery partition, other Windows, Windows install media or rescue stick with one.\n\nAttach Windows install media (a USB stick, or an ISO file as a DVD drive in a virtual machine; install.wim and install.esd both work), scan again (Ctrl+R) and try once more.");
        return o;
    }
    if (!j.saveDiskTables(act, *d, err)) {
        o.result = T(L"Nothing was changed, because the backup failed. ") + err;
        return o;
    }
    // Where to put it: an empty recovery partition, unused space, or 1 GB from the end of Windows
    int reuse = -1;
    for (int i : d->parts) {
        const Partition& q = r.m.parts[i];
        if (q.role == R_RECOVERY && q.length >= (600ull << 20) && !q.locked && !(q.root.size() && exists(q.root + L"Recovery\\WindowsRE\\Winre.wim"))) reuse = i;
    }
    uint64_t gapAt = 0;
    if (reuse < 0)
        for (const auto& g : d->gaps)
            if (g.second >= (1100ull << 20) && !gapAt) gapAt = (g.first + (1ull << 20) - 1) / (1ull << 20) * (1ull << 20);
    wstr letter = freeLetter();
    if (letter.empty()) { o.result = T(L"No free drive letter is left."); return o; }
    wstr L1 = letter.substr(0, 1);
    wstr sc = L"select disk " + std::to_wstring(d->number) + L"\r\n";
    if (reuse >= 0) {
        sc += L"select partition " + std::to_wstring(r.m.parts[reuse].number) + L"\r\n";
    } else if (gapAt) {
        sc += L"create partition primary size=1024 offset=" + std::to_wstring(gapAt / 1024) + L"\r\n";
    } else {
        if (!P.readable || P.fs != L"NTFS" || P.freeBytes < (4ull << 30)) {
            o.result = T(L"There is no room for a recovery partition: no unused space on the disk, and the Windows partition has less than 4 GB free to give up 1 GB. Free up space first.") + T(L"\n\nIf the recovery partition cannot be made from here, start the PC from the Boot Doctor rescue USB stick (Tools > Make a USB rescue disk) and do it from there: from the stick, nothing on the Windows drive is in use.");
            return o;
        }
        if (d->style == 1) {
            int primaries = 0;
            for (int i : d->parts) primaries += r.m.parts[i].number <= 4 ? 1 : 0;
            if (primaries >= 4) { o.result = T(L"This MBR disk already has four partitions, the most it can hold."); return o; }
        }
        uint64_t newEnd = P.offset + P.length - (1024ull << 20);
        sc += L"select partition " + std::to_wstring(P.number) + L"\r\nshrink desired=1024 minimum=1024\r\n";
        sc += L"create partition primary size=1000 offset=" + std::to_wstring(newEnd / 1024) + L"\r\n";
    }
    if (step) step(T(L"Making the recovery partition"));
    sc += L"format quick fs=ntfs label=\"Recovery\" override\r\nassign letter=" + L1 + L"\r\n";
    sc += d->style == 2 ? L"set id=de94bba4-06d1-4d40-a16a-bfd50179d6ac override\r\ngpt attributes=0x8000000000000001\r\n" : L"set id=27 override\r\n";
    ToolResult t = diskpartRun(sc, cancel, o.log);
    if (!t.started || !exists(letter + L"\\")) {
        o.result = T(L"The recovery partition could not be made. ") + t.output.substr(t.output.size() > 400 ? t.output.size() - 400 : 0) + T(L"\n\nIf the recovery partition cannot be made from here, start the PC from the Boot Doctor rescue USB stick (Tools > Make a USB rescue disk) and do it from there: from the stick, nothing on the Windows drive is in use.");
        if (!work.empty()) deleteFolderTree(work);
        return o;
    }
    if (step) step(T(L"Copying the repair tools"));
    wstr dir = letter + L"\\Recovery\\WindowsRE";
    CreateDirectoryW((letter + L"\\Recovery").c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    bool copied = copyProtectedFile(src, dir + L"\\Winre.wim", err);
    if (!work.empty()) deleteFolderTree(work);
    if (!copied) o.log += err + L"\r\n";
    // Tell Windows where its repair tools are
    wstr reagent = findSystemTool(L"reagentc.exe");
    if (exists(in->winDir + L"\\System32\\reagentc.exe") && !in->running) reagent = in->winDir + L"\\System32\\reagentc.exe";
    bool registered = false;
    if (copied && !reagent.empty()) {
        if (step) step(T(L"Switching the troubleshoot options on"));
        if (in->running) {
            runTool(reagent, L"/disable", cancel);
            ToolResult a = runTool(reagent, L"/setreimage /path " + dir, cancel);
            ToolResult b = runTool(reagent, L"/enable", cancel);
            o.log += L"> reagentc /setreimage /path " + dir + L"\r\n" + a.output + L"> reagentc /enable\r\n" + b.output;
            registered = a.code == 0 && b.code == 0;
        } else {
            TempLetter wl;
            if (wl.get(P, err)) {
                wstr target = wl.letter + L"\\" + in->folder + L"Windows";
                ToolResult a = runTool(reagent, L"/setreimage /path " + dir + L" /target " + target, cancel);
                o.log += L"> reagentc /setreimage /path " + dir + L" /target " + target + L"\r\n" + a.output;
                registered = a.code == 0;
                if (registered && !in->entryId.empty()) {
                    ToolResult b = runTool(reagent, L"/enable /osguid " + in->entryId, cancel);
                    o.log += L"> reagentc /enable /osguid " + in->entryId + L"\r\n" + b.output;
                }
            }
        }
    }
    diskpartRun(L"select volume " + L1 + L"\r\nremove letter=" + L1 + L" noerr\r\n", nullptr, o.log);
    o.ok = copied;
    o.result = !copied ? T(L"The recovery partition was made, but the repair tools could not be copied onto it. ") + err
               : registered ? T(L"The recovery partition was recreated and the troubleshoot options are switched on.")
                            : T(L"The recovery partition was recreated with the repair tools on it. Windows could not be told about it from here: in Windows, open the command prompt (admin) and type: reagentc /enable");
    return o;
}

static FixOutcome renameSysFolders(const Fix& f, Report& r, Journal& j, const wstr& act) {
    FixOutcome o;
    if (f.part < 0) { o.result = T(L"The partition was not found."); return o; }
    const Partition& P = r.m.parts[f.part];
    wstr winRel = f.arg;  // e.g. "Win_old" or "Backup\Win"
    wstr parentRel = winRel.find(L'\\') == wstr::npos ? wstr() : winRel.substr(0, winRel.find_last_of(L'\\') + 1);
    wstr winName = winRel.substr(parentRel.size());
    int done = 0;
    auto mv = [&](const wstr& fromRel, const wstr& toRel) {
        if (iequals(fromRel, toRel)) return true;
        if (exists(P.root + toRel)) {
            o.log += T(L"Not renamed, because it already exists: ") + toRel + L"\r\n";
            return false;
        }
        // A rename that only changes upper and lower case needs a step in between
        if (MoveFileExW((P.root + fromRel).c_str(), (P.root + toRel).c_str(), 0)) {
            j.recordMove(act, P, fromRel, toRel);
            o.log += T(L"Renamed ") + fromRel + L" -> " + toRel + L"\r\n";
            ++done;
            return true;
        }
        o.log += T(L"Could not rename ") + fromRel + L": " + errorText(GetLastError()) + L"\r\n";
        return false;
    };
    bool ok = true;
    // innermost first, so the outer paths stay valid
    if (!f.arg3.empty() && !iequals(f.arg3, L"config")) ok &= mv(winRel + L"\\" + f.arg2 + L"\\" + f.arg3, winRel + L"\\" + f.arg2 + L"\\config");
    if (!iequals(f.arg2, L"System32")) ok &= mv(winRel + L"\\" + f.arg2, winRel + L"\\System32");
    if (!iequals(winName, L"Windows")) ok &= mv(winRel, parentRel + L"Windows");
    o.ok = ok && exists(P.root + parentRel + L"Windows\\System32\\config");
    o.result = o.ok ? T(L"Windows' folders have their own names again. Scan again to check this Windows.")
                    : T(L"Not every folder could be renamed. See the details.");
    return o;
}

// Sets a driver's Start value in every control set of a Windows that is not running.
static FixOutcome setDriverStart(const Fix& f, Report& r, Journal& j, const wstr& act) {
    FixOutcome o;
    Install& in = r.installs[f.install];
    const Partition& P = r.m.parts[in.part];
    wstr rel = in.folder + L"Windows\\System32\\config\\SYSTEM", err;
    for (const wchar_t* ext : {L"", L".LOG", L".LOG1", L".LOG2"})
        if (!j.saveFile(act, P, rel + ext, err)) {
            o.result = T(L"Nothing was changed, because the backup failed. ") + err;
            return o;
        }
    LoadedHive h;
    if (!h.load(P.root + rel, false, err)) {
        o.result = T(L"Windows' settings file could not be opened. ") + err;
        return o;
    }
    DWORD want = (DWORD)_wtoi(f.arg2.c_str());
    int changed = 0;
    for (const wstr& cs : regSubkeys(h.root(), L""))
        if (istartsWith(cs, L"ControlSet")) {
            DWORD st = 0;
            wstr key = cs + L"\\Services\\" + f.arg;
            if (regDword(h.root(), key, L"Start", st) && regSetDword(h.root(), key, L"Start", want)) {
                ++changed;
                o.log += cs + L"\\Services\\" + f.arg + L": Start " + std::to_wstring(st) + L" -> " + std::to_wstring(want) + L"\r\n";
            }
        }
    h.unload();
    o.ok = changed > 0;
    o.result = o.ok ? (want == 4 ? TF(L"The driver {0} is switched off: Windows will not load it at the next start.", f.arg) : TF(L"The driver {0} is switched on again.", f.arg))
                    : TF(L"The driver {0} was not found in Windows' settings.", f.arg);
    return o;
}

FixOutcome runFix(const Fix& fx, Report& r, Journal& j, volatile bool* cancel, const StepFn& step) {
    Fix f = fx;
    FixOutcome o;
    if (!f.available) {
        o.result = f.notAvailable;
        return o;
    }
    Install* in = f.install >= 0 && f.install < (int)r.installs.size() ? &r.installs[f.install] : nullptr;
    const Partition* part = f.part >= 0 ? &r.m.parts[f.part] : nullptr;
    wstr target = in ? in->label() + L" in " + in->winDir : part ? part->name() : f.disk >= 0 ? T(L"Disk ") + std::to_wstring(f.disk) : wstr();
    if (step) step(f.title);
    if (fixChangesPartitions(f.kind)) {
        int disk = f.disk >= 0 ? f.disk : part ? part->disk : (in && in->part >= 0 ? r.m.parts[in->part].disk : -1);
        wstr refuse = disk >= 0 ? workingDiskRefusal(r, disk) : wstr();
        if (!refuse.empty()) {
            o.result = T(L"Nothing was changed. ") + refuse;
            return o;
        }
    }
    wstr act = j.begin(f.title, target);
    wstr err;
    auto fail = [&](const wstr& why) {
        o.ok = false;
        o.result = why;
    };
    switch (f.kind) {
        case FX_REBUILD_BOOT:
        case FX_ADD_ENTRY: {
            if (!in) { fail(T(L"No Windows was chosen.")); break; }
            o = rebuild(f, r, j, act, targetSysPart(r, *in), f.kind == FX_ADD_ENTRY);
            break;
        }
        case FX_SHOW_IN_MENU:
            o = bcdedit(f, r, j, act, L"/displayorder " + f.entry + L" /addlast", T(L"This Windows is shown in the start-up menu again."));
            break;
        case FX_MAKE_DEFAULT:
            o = bcdedit(f, r, j, act, L"/default " + f.entry, T(L"This Windows now starts first."));
            break;
        case FX_STOP_AUTOREPAIR:
            o = bcdedit(f, r, j, act, L"/set " + f.entry + L" recoveryenabled No\n/set " + f.entry + L" bootstatuspolicy IgnoreAllFailures",
                        T(L"Automatic Repair is off for this Windows. Start the PC and note the error message it shows; that tells what to fix next."));
            break;
        case FX_ADD_DRIVER: {
            if (!in || f.arg.empty()) { fail(T(L"No driver was chosen.")); break; }
            wstr dism = findSystemTool(L"dism.exe");
            wstr target;
            TempLetter tl;
            if (in->running) target = L"/Online";
            else {
                if (!tl.get(r.m.parts[in->part], err)) { fail(err); break; }
                target = L"/Image:" + tl.letter + L"\\" + in->folder;
            }
            wstr args = target + L" /Add-Driver /Driver:\"" + f.arg + L"\"";
            if (step) step(T(L"Adding the driver to Windows"));
            o.log += L"> dism " + args + L"\r\n";
            ToolResult t = runTool(dism, args, cancel);
            o.log += t.output;
            o.ok = t.started && (t.code == 0 || t.code == 3010);
            o.result = o.ok ? TF(L"The driver {0} is in this Windows now. It loads it at the next start.", f.arg.substr(f.arg.find_last_of(L'\\') + 1))
                            : T(L"The driver could not be added. It must be a 64-bit driver for this Windows version, with its .sys and .cat files next to the .inf. DISM said: ") +
                                  t.output.substr(t.output.size() > 400 ? t.output.size() - 400 : 0);
            break;
        }
        case FX_DRIVER_START: {
            if (!in || f.arg.empty()) { fail(T(L"No driver was chosen.")); break; }
            if (in->running) { fail(T(L"For the Windows that is running, use Device Manager, or start from the rescue stick.")); break; }
            o = setDriverStart(f, r, j, act);
            break;
        }
        case FX_BOOT_OPTION: {
            // arg: bcdedit commands with {id} for this Windows' entry, one per line; arg2: what to say afterwards
            wstr cmds = f.arg;
            for (size_t p; (p = cmds.find(L"{id}")) != wstr::npos;) cmds.replace(p, 4, f.entry);
            o = bcdedit(f, r, j, act, cmds, f.arg2);
            break;
        }
        case FX_SAFE_MODE: {
            bool on = f.arg != L"off";
            o = bcdedit(f, r, j, act, on ? L"/set " + f.entry + L" safeboot minimal" : L"/deletevalue " + f.entry + L" safeboot",
                        on ? T(L"This Windows starts in Safe Mode from the next start on. Switch it back with \"Normal start again\" when you are done.")
                           : T(L"This Windows starts normally again."));
            break;
        }
        case FX_DISM_HEAL: {
            if (!in) { fail(T(L"No Windows was chosen.")); break; }
            wstr dism = findSystemTool(L"dism.exe");
            wstr args;
            TempLetter tl;
            if (in->running) args = L"/Online /Cleanup-Image /RestoreHealth";
            else {
                if (!tl.get(r.m.parts[in->part], err)) { fail(err); break; }
                // good copies come from install media of the same Windows
                wstr source;
                for (const Partition& q : r.m.parts)
                    if (!q.root.empty() && source.empty()) {
                        if (exists(q.root + L"sources\\install.wim")) source = L"WIM:" + q.root + L"sources\\install.wim:1";
                        else if (exists(q.root + L"sources\\install.esd")) source = L"ESD:" + q.root + L"sources\\install.esd:1";
                    }
                if (source.empty()) {
                    fail(T(L"A Windows that does not start can only be repaired from Windows install media of the same version. Attach it (a USB stick, or an ISO file as a DVD drive in a virtual machine), scan again and try once more."));
                    break;
                }
                // Windows PE's own scratch space is small and in memory: use the Windows drive's
                wstr scratch = tl.letter + L"\\PATioBD-scratch";
                CreateDirectoryW(scratch.c_str(), nullptr);
                args = L"/Image:" + tl.letter + L"\\" + in->folder + L" /Cleanup-Image /RestoreHealth /Source:\"" + source + L"\" /LimitAccess /ScratchDir:\"" + scratch + L"\"";
            }
            if (step) step(T(L"Repairing the Windows image. This takes 15 to 60 minutes"));
            o.log += L"> dism " + args + L"\r\n";
            ToolResult t = runTool(dism, args, cancel);
            o.log += t.output;
            if (!in->running) deleteFolderTree(tl.letter + L"\\PATioBD-scratch");
            o.ok = t.started && (t.code == 0 || t.code == 3010);
            o.result = o.ok ? T(L"The Windows image was checked and repaired where needed. Now run \"Repair Windows system files\".")
                            : T(L"The Windows image could not be repaired. The install media must be the same Windows version and edition. DISM said: ") +
                                  t.output.substr(t.output.size() > 400 ? t.output.size() - 400 : 0);
            break;
        }
        case FX_REMOVE_UPDATE: {
            if (!in || f.arg.empty()) { fail(T(L"No update was chosen.")); break; }
            wstr dism = findSystemTool(L"dism.exe");
            wstr target;
            TempLetter tl;
            if (in->running) target = L"/Online";
            else {
                if (!tl.get(r.m.parts[in->part], err)) { fail(err); break; }
                target = L"/Image:" + tl.letter + L"\\" + in->folder;
            }
            wstr args = target + L" /Remove-Package /PackageName:" + f.arg + L" /NoRestart";
            if (step) step(T(L"Removing the update"));
            o.log += L"> dism " + args + L"\r\n";
            ToolResult t = runTool(dism, args, cancel);
            o.log += t.output;
            o.ok = t.started && (t.code == 0 || t.code == 3010);
            o.result = o.ok ? TF(L"The update {0} was removed. Restart Windows to finish.", f.arg)
                            : T(L"The update could not be removed (some updates cannot be). DISM said: ") + t.output.substr(t.output.size() > 400 ? t.output.size() - 400 : 0);
            break;
        }
        case FX_RENAME_ENTRY: {
            wstr name = f.arg;
            name.erase(std::remove(name.begin(), name.end(), L'"'), name.end());
            if (name.empty()) { fail(T(L"No name was given.")); break; }
            o = bcdedit(f, r, j, act, L"/set " + f.entry + L" description \"" + name + L"\"", TF(L"It is now called \"{0}\" in the start-up menu.", name));
            break;
        }
        case FX_SET_TIMEOUT:
            o = bcdedit(f, r, j, act, L"/timeout " + std::to_wstring(_wtoi(f.arg.c_str())), TF(L"The start-up menu now waits {0} seconds.", std::to_wstring(_wtoi(f.arg.c_str()))));
            break;
        case FX_DELETE_BCD_ENTRY:
            o = bcdedit(f, r, j, act, L"/delete " + f.entry + L" /cleanup", T(L"The entry was removed from the start-up settings and the menu."));
            break;
        case FX_REMOVE_FROM_MENU:
            o = bcdedit(f, r, j, act, L"/displayorder " + f.entry + L" /remove", T(L"The entry was taken off the start-up menu."));
            break;
        case FX_FIRMWARE_SETTINGS:
            o.ok = restartIntoFirmware(err);
            o.result = o.ok ? T(L"The PC is restarting into its firmware settings.") : err;
            break;
        case FX_ADD_FW_ENTRY: {
            if (!part || !part->gpt || f.arg.empty()) { fail(T(L"The start-up partition was not found.")); break; }
            const Disk* d = diskOf(r, part->disk);
            uint32_t sector = d ? d->sector : 512;
            int num = -1;
            for (int n = 0; n < 0x1000 && num < 0; ++n) {
                wchar_t nm[16];
                swprintf(nm, 16, L"Boot%04X", n);
                std::string tmp;
                if (!readFwVar(nm, tmp)) num = n;
            }
            if (num < 0) { fail(T(L"The PC's start-up list is full.")); break; }
            wchar_t name[16];
            swprintf(name, 16, L"Boot%04X", num);
            if (!j.saveFwVar(act, name, err) || !j.saveFwVar(act, L"BootOrder", err)) { fail(L"Nothing was changed, because the backup failed. " + err); break; }
            // EFI_LOAD_OPTION: attributes, path length, description, then the device path
            auto put16 = [](std::string& s, uint16_t v) { s += (char)(v & 255); s += (char)(v >> 8); };
            auto put32 = [&](std::string& s, uint32_t v) { put16(s, (uint16_t)v); put16(s, (uint16_t)(v >> 16)); };
            auto put64 = [&](std::string& s, uint64_t v) { put32(s, (uint32_t)v); put32(s, (uint32_t)(v >> 32)); };
            std::string dp;
            dp += '\x04'; dp += '\x01'; put16(dp, 42);  // hard drive
            put32(dp, (uint32_t)part->number);
            put64(dp, part->offset / sector);
            put64(dp, part->length / sector);
            dp.append((const char*)&part->id, 16);
            dp += '\x02'; dp += '\x02';  // GPT, GUID signature
            wstr path = f.arg;
            std::string fp;
            for (wchar_t c : path) put16(fp, (uint16_t)c);
            put16(fp, 0);
            dp += '\x04'; dp += '\x04'; put16(dp, (uint16_t)(4 + fp.size()));
            dp += fp;
            dp += '\x7F'; dp += '\xFF'; put16(dp, 4);
            std::string opt;
            put32(opt, 1);  // active
            put16(opt, (uint16_t)dp.size());
            wstr desc = f.entry.empty() ? wstr(T(L"Linux")) : f.entry;
            for (wchar_t c : desc) put16(opt, (uint16_t)c);
            put16(opt, 0);
            opt += dp;
            if (!writeFwVar(name, opt)) { fail(L"The PC did not allow a new start-up entry (error " + std::to_wstring(GetLastError()) + L")."); break; }
            std::string order;
            readFwVar(L"BootOrder", order);
            put16(order, (uint16_t)num);
            o.ok = writeFwVar(L"BootOrder", order);
            o.log = T(L"Wrote ") + wstr(name) + T(L" for ") + path + T(L" and added it to BootOrder.\r\n");
            o.result = o.ok ? T(L"Linux was added to the PC's start-up list. Pick it in the PC's boot menu, or use \"Make Linux start first\".")
                            : T(L"The entry was written, but it could not be added to the start-up order.");
            break;
        }
        case FX_FW_FIRST: {
            if (f.fwNum < 0) { fail(T(L"The entry was not found.")); break; }
            if (!j.saveFwVar(act, L"BootOrder", err)) { fail(L"Nothing was changed, because the backup failed. " + err); break; }
            std::string order, out;
            readFwVar(L"BootOrder", order);
            out += (char)(f.fwNum & 255);
            out += (char)(f.fwNum >> 8);
            for (size_t i = 0; i + 1 < order.size(); i += 2)
                if (((uint8_t)order[i] | ((uint8_t)order[i + 1] << 8)) != f.fwNum) out += order.substr(i, 2);
            o.ok = writeFwVar(L"BootOrder", out);
            o.result = o.ok ? (f.entry.empty() ? wstr(L"It") : f.entry) + T(L" now starts first. Its menu usually lets you choose Windows too.")
                            : T(L"The PC did not allow its start-up order to be changed (error ") + std::to_wstring(GetLastError()) + L").";
            break;
        }
        case FX_DELETE_EFI_DIR: {
            if (!part || part->root.empty() || f.arg.empty()) { fail(T(L"The start-up partition was not found.")); break; }
            if (!j.saveDir(act, *part, f.arg, err)) { fail(L"Nothing was changed, because the backup failed. " + err); break; }
            std::string order;
            bool haveOrder = r.m.fw.varsReadable && readFwVar(L"BootOrder", order);
            std::string kept = order;
            if (haveOrder) {
                j.saveFwVar(act, L"BootOrder", err);
                for (const FirmwareEntry& e : r.m.fw.entries) {
                    if (!e.hasPart || !part->gpt || !IsEqualGUID(e.part, part->id) || !icontains(e.path, L"\\" + f.arg + L"\\")) continue;
                    wchar_t nm[16];
                    swprintf(nm, 16, L"Boot%04X", e.num);
                    j.saveFwVar(act, nm, err);
                    std::string next;
                    for (size_t i = 0; i + 1 < kept.size(); i += 2)
                        if (((uint8_t)kept[i] | ((uint8_t)kept[i + 1] << 8)) != e.num) next += kept.substr(i, 2);
                    kept = next;
                    writeFwVar(nm, std::string());
                    o.log += T(L"Removed ") + wstr(nm) + L" (" + e.description + L")\r\n";
                }
                if (kept != order) writeFwVar(L"BootOrder", kept);
            }
            o.ok = deleteFolderTree(part->root + f.arg) || GetFileAttributesW((part->root + f.arg).c_str()) == INVALID_FILE_ATTRIBUTES;
            o.log += L"Deleted \\" + f.arg + L"\r\n";
            o.result = o.ok ? T(L"The leftover boot loader was removed.") : T(L"Some files of the boot loader could not be deleted.");
            break;
        }
        case FX_DELETE_FW_ENTRY: {
            if (f.fwNum < 0) { fail(T(L"The entry was not found.")); break; }
            wchar_t name[16];
            swprintf(name, 16, L"Boot%04X", f.fwNum);
            if (!j.saveFwVar(act, name, err) || !j.saveFwVar(act, L"BootOrder", err)) { fail(L"Nothing was changed, because the backup failed. " + err); break; }
            std::string order, kept;
            if (readFwVar(L"BootOrder", order)) {
                for (size_t i = 0; i + 1 < order.size(); i += 2)
                    if (((uint8_t)order[i] | ((uint8_t)order[i + 1] << 8)) != f.fwNum) kept += order.substr(i, 2);
                if (kept != order && !writeFwVar(L"BootOrder", kept)) {
                    fail(T(L"The PC did not allow its start-up order to be changed (error ") + std::to_wstring(GetLastError()) + L").");
                    break;
                }
            }
            o.ok = writeFwVar(name, std::string());
            o.log = T(L"Removed ") + wstr(name) + T(L" and took it out of BootOrder.\r\n");
            o.result = o.ok ? T(L"The entry was removed from the PC's start-up list.")
                            : T(L"The entry was taken out of the start-up order, but the PC did not allow the entry itself to be deleted (error ") + std::to_wstring(GetLastError()) + L").";
            if (!o.ok && kept != order) { o.ok = true; }  // out of the order is what matters
            break;
        }
        case FX_MARK_ACTIVE: {
            const Disk* d = diskOf(r, part ? part->disk : f.disk);
            if (!d || !part) { fail(T(L"The partition was not found.")); break; }
            if (!j.saveDiskTables(act, *d, err)) { fail(T(L"Nothing was changed, because the backup failed. ") + err); break; }
            o.ok = setActivePartition(d->number, part->number, err);
            o.result = o.ok ? part->where() + T(L" is now the active start-up partition.") : err;
            break;
        }
        case FX_FIX_GPT: {
            const Disk* d = diskOf(r, f.disk);
            if (!d) { fail(T(L"The disk was not found.")); break; }
            if (!j.saveDiskTables(act, *d, err)) { fail(T(L"Nothing was changed, because the backup failed. ") + err); break; }
            o.ok = rewritePartitionTable(d->number, err);
            o.result = o.ok ? T(L"Both copies of the partition table were written again.") : err;
            break;
        }
        case FX_MBR_CODE: {
            const Disk* d = diskOf(r, f.disk);
            int sp = part ? f.part : in ? targetSysPart(r, *in) : -1;
            if (!d || sp < 0) { fail(T(L"The disk or its start-up partition was not found.")); break; }
            const Partition& S = r.m.parts[sp];
            wstr tool = f.arg.empty() ? findSystemTool(L"bootsect.exe") : f.arg;
            if (tool.empty()) { fail(T(L"bootsect.exe is not available here.")); break; }
            if (!j.saveSectors(act, *d, 0, d->sector, err) || !j.saveSectors(act, *d, S.offset, 16 * d->sector, err)) {
                fail(T(L"Nothing was changed, because the backup failed. ") + err);
                break;
            }
            TempLetter tl;
            if (!tl.get(S, err)) { fail(err); break; }
            wstr args = L"/nt60 " + tl.letter + L" /mbr /force";
            o.log += L"> bootsect " + args + L"\r\n";
            ToolResult t = runTool(tool, args, cancel);
            o.log += t.output;
            o.ok = t.started && t.code == 0;
            o.result = o.ok ? T(L"Fresh start-up code was written to the disk and the start-up partition.") : T(L"The start-up code could not be written. ") + t.output.substr(0, 300);
            break;
        }
        case FX_CREATE_SYSPART: {
            if (!in) { fail(T(L"No Windows was chosen.")); break; }
            const Partition& P = r.m.parts[in->part];
            const Disk* d = diskOf(r, P.disk);
            if (!d) { fail(T(L"The disk was not found.")); break; }
            if (!j.saveDiskTables(act, *d, err)) { fail(T(L"Nothing was changed, because the backup failed. ") + err); break; }
            wstr letter = freeLetter();
            if (letter.empty()) { fail(T(L"No free drive letter is left.")); break; }
            wstr L1 = letter.substr(0, 1);
            bool uefi = r.m.fw.uefi;
            wstr s = L"select disk " + std::to_wstring(d->number) + L"\r\n";
            if (d->largestGap < (320ull << 20)) s += L"select partition " + std::to_wstring(P.number) + L"\r\nshrink desired=300 minimum=300\r\n";
            bool hasMsr = false;
            for (int i : d->parts) hasMsr |= r.m.parts[i].role == R_MSR;
            if (d->style == 2) s += L"create partition efi size=260\r\nformat quick fs=fat32 label=System\r\n";
            else if (uefi) s += L"create partition primary size=300\r\nformat quick fs=fat32 label=System\r\nactive\r\n";
            else s += L"create partition primary size=300\r\nformat quick fs=ntfs label=\"System Reserved\"\r\nactive\r\n";
            s += L"assign letter=" + L1 + L"\r\n";
            if (d->style == 2 && !hasMsr) s += L"create partition msr size=16\r\n";  // Windows expects one on GPT disks
            s += L"exit\r\n";
            wchar_t tmp[MAX_PATH] = {};
            GetTempPathW(MAX_PATH, tmp);
            wstr script = wstr(tmp) + L"PATioBD-diskpart.txt";
            HANDLE h = CreateFileW(script.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            std::string a = wideToUtf8(s);
            DWORD wr = 0;
            if (h != INVALID_HANDLE_VALUE) {
                WriteFile(h, a.data(), (DWORD)a.size(), &wr, nullptr);
                CloseHandle(h);
            }
            o.log += T(L"diskpart script:\r\n") + s;
            ToolResult t = runTool(findSystemTool(L"diskpart.exe"), L"/s \"" + script + L"\"", cancel);
            DeleteFileW(script.c_str());
            o.log += t.output;
            if (!t.started || t.code != 0 || !exists(letter + L"\\")) {
                fail(T(L"The new start-up partition could not be created. ") + t.output.substr(t.output.size() > 400 ? t.output.size() - 400 : 0));
                break;
            }
            TempLetter wl;
            if (!wl.get(P, err)) { fail(err); break; }
            int ma = 0, mi = 0;
            wstr tool = pickBcdboot(in, ma, mi);
            bool modern = ma > 6 || (ma == 6 && mi >= 2);
            wstr args = L"\"" + wl.letter + L"\\" + in->folder + L"Windows\" /s " + letter + (modern ? (uefi ? L" /f UEFI" : L" /f BIOS") : L"");
            o.log += T(L"\r\n> bcdboot ") + args + L"\r\n";
            ToolResult b = runTool(tool, args);
            o.log += b.output;
            unmount(letter);
            o.ok = b.started && b.code == 0;
            o.result = o.ok ? T(L"A new start-up partition was created and fresh start-up files were put on it.")
                            : T(L"The partition was created, but the start-up files could not be written. Try \"Rebuild the start-up files\" after scanning again.");
            break;
        }
        case FX_REBUILD_ESP: {
            if (!in) { fail(T(L"No Windows was chosen.")); break; }
            int sp = f.part >= 0 ? f.part : targetSysPart(r, *in);
            if (sp < 0) { fail(T(L"There is no start-up partition to rebuild. Create one first.")); break; }
            const Partition& S = r.m.parts[sp];
            const Partition& P = r.m.parts[in->part];
            const Disk* d = diskOf(r, S.disk);
            if (!d) { fail(T(L"The disk was not found.")); break; }
            if (!r.m.fw.uefi) { fail(T(L"Rebuilding the EFI partition is for UEFI PCs. On an older (BIOS) PC use \"Write new start-up code to the disk\".")); break; }
            // 1. A full image of the partition, so the change can be undone exactly.
            bool imaged = false;
            if (S.length && S.length <= 0x7FFFFFFFull) {
                if (!j.saveSectors(act, *d, S.offset, (uint32_t)S.length, err)) { fail(T(L"Nothing was changed, because the backup failed. ") + err); break; }
                imaged = true;
            }
            // 2. Set other makers' boot folders (HP, Dell, Lenovo, Linux GRUB) aside; Microsoft and the
            //    fallback Boot folder are written fresh by bcdboot.
            wstr L = S.letter.empty() ? freeLetter() : S.letter;
            if (L.empty()) { fail(T(L"No free drive letter is left.")); break; }
            wstr L1 = L.substr(0, 1);
            wstr selp = L"select disk " + std::to_wstring(d->number) + L"\r\nselect partition " + std::to_wstring(S.number) + L"\r\n";
            if (S.letter.empty()) { diskpartRun(selp + L"assign letter=" + L1 + L"\r\n", cancel, o.log); Sleep(800); }
            wchar_t tmp[MAX_PATH] = {};
            GetTempPathW(MAX_PATH, tmp);
            wstr saveDir = wstr(tmp) + L"PATioBD-esp-" + std::to_wstring(GetTickCount64());
            CreateDirectoryW(saveDir.c_str(), nullptr);
            std::vector<wstr> preserved;
            if (exists(L + L"\\EFI")) {
                WIN32_FIND_DATAW fd;
                HANDLE h = FindFirstFileW((L + L"\\EFI\\*").c_str(), &fd);
                if (h != INVALID_HANDLE_VALUE) {
                    do {
                        wstr name = fd.cFileName;
                        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || name == L"." || name == L".." || iequals(name, L"Microsoft") || iequals(name, L"Boot")) continue;
                        wstr e2;
                        if (copyFolderTree(L + L"\\EFI\\" + name, saveDir + L"\\" + name, e2)) {
                            preserved.push_back(name);
                            o.log += T(L"Kept other boot folder aside: \\EFI\\") + name + L"\r\n";
                        }
                    } while (FindNextFileW(h, &fd));
                    FindClose(h);
                }
            }
            // 3. BitLocker on the Windows drive: pause it when Windows is live, otherwise warn.
            wstr bitNote;
            if (P.bitlocker) {
                if (in->running && !P.letter.empty()) {
                    runTool(findSystemTool(L"manage-bde.exe"), L"-protectors -disable " + P.letter + L" -RebootCount 1", cancel);
                    bitNote = T(L" BitLocker on the Windows drive was paused for the next restart.");
                } else
                    bitNote = T(L" The Windows drive is encrypted with BitLocker: have its 48-digit recovery key ready, as BitLocker may ask for it once at the next start.");
            }
            // 4. Make a fresh, empty FAT32 file system (the partition and its type stay), keep the letter.
            ToolResult t = diskpartRun(selp + L"format fs=fat32 quick label=System\r\nassign letter=" + L1 + L"\r\n", cancel, o.log);
            Sleep(1000);
            if (t.code != 0 || !exists(L + L"\\")) {
                deleteFolderTree(saveDir);
                fail(T(L"The start-up partition could not be reformatted. ") + t.output.substr(t.output.size() > 300 ? t.output.size() - 300 : 0));
                break;
            }
            // 5. Fresh start-up files for this Windows, then put the kept boot folders back.
            TempLetter wl;
            if (!wl.get(P, err)) { deleteFolderTree(saveDir); fail(err); break; }
            int ma = 0, mi = 0;
            wstr tool = pickBcdboot(in, ma, mi);
            if (tool.empty()) { deleteFolderTree(saveDir); fail(T(L"bcdboot.exe, the Windows tool that writes start-up files, was not found here.")); break; }
            wstr args = L"\"" + wl.letter + L"\\" + in->folder + L"Windows\" /s " + L + L" /f UEFI";
            o.log += L"> bcdboot " + args + L"\r\n";
            ToolResult b = runTool(tool, args);
            o.log += b.output;
            for (const wstr& name : preserved) {
                wstr e2;
                copyFolderTree(saveDir + L"\\" + name, L + L"\\EFI\\" + name, e2);
            }
            deleteFolderTree(saveDir);
            if (S.letter.empty()) diskpartRun(L"select volume " + L1 + L"\r\nremove letter=" + L1 + L" noerr\r\n", cancel, o.log);
            o.ok = b.started && b.code == 0;
            o.undoable = imaged;
            o.result = o.ok ? T(L"The start-up partition was given a fresh FAT32 file system and new start-up files, and a \"Windows Boot Manager\" entry was written to the PC's firmware.") +
                                  (preserved.empty() ? wstr() : TF(L" {0} other boot folder(s) were kept and put back.", std::to_wstring(preserved.size()))) + bitNote +
                                  T(L" Scan again to confirm.")
                            : T(L"The partition was reformatted, but the start-up files could not be written. The tool said: ") + b.output.substr(0, 300) + bitNote;
            break;
        }
        case FX_CHKDSK: {
            if (!part) { fail(T(L"The partition was not found.")); break; }
            TempLetter tl;
            if (!tl.get(*part, err)) { fail(err); break; }
            ToolResult t = runTool(findSystemTool(L"chkdsk.exe"), tl.letter + L" /f /x", cancel, "Y\r\nY\r\n");
            o.log = t.output;
            o.ok = t.started && (t.code == 0 || t.code == 1 || t.code == 2);
            bool scheduled = icontains(t.output, L"restart") || icontains(t.output, L"next time");
            o.result = !o.ok ? T(L"The disk check could not finish. ") + t.output.substr(t.output.size() > 300 ? t.output.size() - 300 : 0)
                       : scheduled ? T(L"The drive is in use, so the check runs the next time Windows starts.")
                       : t.code == 1 ? T(L"The disk check found and repaired errors.") : T(L"The disk check finished. No errors needed repair.");
            break;
        }
        case FX_UNLOCK: {
            if (!part) { fail(T(L"The partition was not found.")); break; }
            wstr tool = findSystemTool(L"manage-bde.exe");
            TempLetter tl;
            if (!tl.get(*part, err)) { fail(err); break; }
            ToolResult t = runTool(tool, L"-unlock " + tl.letter + L" -RecoveryPassword " + f.arg, cancel);
            o.log = t.output;
            o.ok = t.started && t.code == 0;
            tl.made = false;  // keep the letter so the drive stays reachable
            o.result = o.ok ? T(L"The drive is unlocked. Scan again to check it.") : T(L"The drive could not be unlocked. Check the recovery key and try again.");
            break;
        }
        case FX_RESTORE_HIVE: {
            if (!in) { fail(T(L"No Windows was chosen.")); break; }
            const Partition& P = r.m.parts[in->part];
            wstr cfgRel = in->folder + L"Windows\\System32\\config\\";
            int done = 0;
            bool ok = true;
            for (const wchar_t* name : {L"SYSTEM", L"SOFTWARE"}) {
                wstr why;
                if (hiveFileHealth(P.root + cfgRel + name, why) != Health::Bad) continue;
                if (hiveFileHealth(P.root + cfgRel + L"RegBack\\" + name, why) != Health::Ok) continue;
                for (const wchar_t* ext : {L"", L".LOG", L".LOG1", L".LOG2"})
                    if (!j.saveFile(act, P, cfgRel + name + ext, err)) ok = false;
                if (!ok) break;
                wstr dst = P.root + cfgRel + name;
                SetFileAttributesW(dst.c_str(), FILE_ATTRIBUTE_NORMAL);
                if (CopyFileW((P.root + cfgRel + L"RegBack\\" + name).c_str(), dst.c_str(), FALSE)) {
                    for (const wchar_t* ext : {L".LOG", L".LOG1", L".LOG2"}) {
                        SetFileAttributesW((dst + ext).c_str(), FILE_ATTRIBUTE_NORMAL);
                        DeleteFileW((dst + ext).c_str());
                    }
                    ++done;
                    o.log += T(L"Put back the backup copy of ") + wstr(name) + L"\r\n";
                } else {
                    ok = false;
                    err = errorText(GetLastError());
                }
            }
            o.ok = ok && done > 0;
            o.result = o.ok ? T(L"The registry's backup copy was put back.") : T(L"The backup copy could not be put back. ") + err;
            break;
        }
        case FX_SFC: {
            if (!in) { fail(T(L"No Windows was chosen.")); break; }
            wstr tool = findSystemTool(L"sfc.exe");
            wstr args;
            TempLetter tl;
            if (in->running) args = L"/scannow";
            else {
                if (!tl.get(r.m.parts[in->part], err)) { fail(err); break; }
                args = L"/scannow /offbootdir=" + tl.letter + L"\\ /offwindir=" + tl.letter + L"\\" + in->folder + L"Windows";
            }
            if (step) step(T(L"Repairing Windows system files. This takes 10 to 30 minutes"));
            o.log += L"> sfc " + args + L"\r\n";
            ToolResult t = runTool(tool, args, cancel);
            o.log += t.output;
            o.ok = t.started && t.code == 0;
            bool repaired = icontains(t.output, L"successfully repaired");
            bool couldNot = icontains(t.output, L"unable to fix");
            o.result = !o.ok ? T(L"The file check did not finish. It may need the same Windows version to run it.") : couldNot ? T(L"Some damaged files could not be repaired.")
                       : repaired ? T(L"Damaged Windows files were repaired.") : T(L"The file check finished.");
            break;
        }
        case FX_ENABLE_DRIVERS:
        case FX_CLEAR_SETUP:
            if (!in) { fail(T(L"No Windows was chosen.")); break; }
            o = editSystemHive(f, r, j, act, f.kind == FX_ENABLE_DRIVERS);
            break;
        case FX_REVERT_UPDATES: {
            if (!in) { fail(T(L"No Windows was chosen.")); break; }
            const Partition& P = r.m.parts[in->part];
            if (!j.saveFile(act, P, in->folder + L"Windows\\WinSxS\\pending.xml", err)) { fail(L"Nothing was changed, because the backup failed. " + err); break; }
            TempLetter tl;
            if (!tl.get(P, err)) { fail(err); break; }
            wstr args = L"/Image:" + tl.letter + L"\\" + in->folder + L" /Cleanup-Image /RevertPendingActions";
            if (step) step(T(L"Undoing unfinished updates"));
            o.log += L"> dism " + args + L"\r\n";
            ToolResult t = runTool(findSystemTool(L"dism.exe"), args, cancel);
            o.log += t.output;
            o.ok = t.started && (t.code == 0 || t.code == 3010);
            o.result = o.ok ? T(L"The unfinished updates were undone.") : T(L"The updates could not be undone. ") + t.output.substr(t.output.size() > 300 ? t.output.size() - 300 : 0);
            break;
        }
        case FX_MOVE_TO_ROOT: {
            if (!in || in->folder.empty()) { fail(T(L"This Windows is already at the top of the drive.")); break; }
            const Partition& P = r.m.parts[in->part];
            wstr from = P.root + in->folder;
            WIN32_FIND_DATAW fd{};
            HANDLE h = FindFirstFileW((from + L"*").c_str(), &fd);
            int moved = 0, skipped = 0;
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    wstr n = fd.cFileName;
                    if (n == L"." || n == L"..") continue;
                    if (exists(P.root + n)) {
                        ++skipped;
                        o.log += T(L"Left in place (the top of the drive already has it): ") + n + L"\r\n";
                        continue;
                    }
                    if (MoveFileExW((from + n).c_str(), (P.root + n).c_str(), 0)) {
                        j.recordMove(act, P, in->folder + n, n);
                        ++moved;
                        o.log += T(L"Moved ") + n + L"\r\n";
                    } else {
                        ++skipped;
                        o.log += T(L"Could not move ") + n + L": " + errorText(GetLastError()) + L"\r\n";
                    }
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
            o.ok = exists(P.root + L"Windows\\System32");
            if (o.ok) {
                wstr folder = in->folder;  // remove the now empty folders
                while (!folder.empty()) {
                    RemoveDirectoryW((P.root + folder).c_str());
                    folder.pop_back();
                    size_t s = folder.find_last_of(L'\\');
                    folder = s == wstr::npos ? wstr() : folder.substr(0, s + 1);
                }
                in->folder.clear();
                in->winDir = P.root + L"Windows";
                in->systemRoot = L"\\Windows";
                in->atRoot = true;
                in->sysPart = targetSysPart(r, *in);
            }
            o.result = o.ok ? T(L"Windows was moved back to the top of the drive (") + std::to_wstring(moved) + T(L" items") +
                                  (skipped ? L", " + std::to_wstring(skipped) + T(L" left in place") : wstr()) + L")."
                            : T(L"Windows could not be moved back.");
            break;
        }
        case FX_ENABLE_WINRE: {
            if (!in) { fail(T(L"No Windows was chosen.")); break; }
            wstr reagent = findSystemTool(L"reagentc.exe");
            if (!in->running && exists(in->winDir + L"\\System32\\reagentc.exe")) reagent = in->winDir + L"\\System32\\reagentc.exe";
            if (in->running) {
                ToolResult t = runTool(reagent, L"/enable", cancel);
                o.log = L"> reagentc /enable\r\n" + t.output;
                o.ok = t.started && t.code == 0;
            } else {
                const Partition& P = r.m.parts[in->part];
                TempLetter wl;
                if (!wl.get(P, err)) { fail(err); break; }
                wstr winTarget = wl.letter + L"\\" + in->folder + L"Windows";
                wstr path = winTarget + L"\\System32\\Recovery";
                ToolResult t = runTool(reagent, L"/setreimage /path " + path + L" /target " + winTarget, cancel);
                o.log = L"> reagentc /setreimage /path " + path + L" /target " + winTarget + L"\r\n" + t.output;
                o.ok = t.started && t.code == 0;
                if (o.ok && !in->entryId.empty()) {
                    ToolResult b = runTool(reagent, L"/enable /osguid " + in->entryId, cancel);
                    o.log += L"> reagentc /enable /osguid " + in->entryId + L"\r\n" + b.output;
                }
            }
            o.result = o.ok ? T(L"The troubleshoot options are switched on. They appear by themselves when Windows cannot start (or hold Shift while choosing Restart).")
                            : T(L"Windows' tool did not switch them on. ") + o.log.substr(o.log.size() > 300 ? o.log.size() - 300 : 0);
            break;
        }
        case FX_REBUILD_WINRE: {
            o = rebuildWinre(f, r, j, act, cancel, step);
            break;
        }
        case FX_CLEAN_SPACE: {
            if (!in) { fail(T(L"No Windows was chosen.")); break; }
            const Partition& P = r.m.parts[in->part];
            wstr root = P.root + in->folder;
            std::vector<CleanSpot> spots = cleanupSpots();
            for (size_t k = 0; k < spots.size(); ++k) spots[k].on = k < f.arg.size() ? f.arg[k] == L'1' : !spots[k].browser;
            uint64_t before = freeSpace(P.root);
            uint32_t removed = 0, skipped = 0;
            uint64_t freed = cleanSpots(root, spots, cancel, [&](const wstr& t, float) { if (step) step(t); }, removed, skipped);
            uint64_t after = freeSpace(P.root);
            o.ok = true;
            o.log = std::to_wstring(removed) + T(L" files removed, ") + std::to_wstring(skipped) + T(L" in use and left.");
            o.result = TF(L"{0} freed ({1} files). {2} is free now.", sizeText(after > before ? after - before : freed), std::to_wstring(removed), sizeText(after));
            break;
        }
        case FX_RENAME_SYSFOLDERS: {
            o = renameSysFolders(f, r, j, act);
            break;
        }
        case FX_CREATE_MSR: {
            const Partition* wp = in ? &r.m.parts[in->part] : part;
            const Disk* d = wp ? diskOf(r, wp->disk) : nullptr;
            if (!d || d->style != 2) { fail(T(L"This is only for GPT disks.")); break; }
            if (!j.saveDiskTables(act, *d, err)) { fail(T(L"Nothing was changed, because the backup failed. ") + err); break; }
            // Natural place: right after the start-up partition
            uint64_t at = 0;
            for (int i : d->parts)
                if (r.m.parts[i].role == R_ESP) {
                    uint64_t end = (r.m.parts[i].offset + r.m.parts[i].length + (1ull << 20) - 1) / (1ull << 20) * (1ull << 20);
                    for (const auto& g : d->gaps)
                        if (g.first <= end && g.first + g.second >= end + (16ull << 20)) at = end;
                }
            if (!at)
                for (const auto& g : d->gaps)
                    if (!at && g.second >= (17ull << 20)) at = (g.first + (1ull << 20) - 1) / (1ull << 20) * (1ull << 20);
            wstr sc = L"select disk " + std::to_wstring(d->number) + L"\r\n";
            if (at) sc += L"create partition msr size=16 offset=" + std::to_wstring(at / 1024) + L"\r\n";
            else {
                if (!wp->readable || wp->fs != L"NTFS") { fail(T(L"There is no unused space, and the Windows partition cannot be made smaller.")); break; }
                sc += L"select partition " + std::to_wstring(wp->number) + L"\r\nshrink desired=16 minimum=16\r\ncreate partition msr size=16\r\n";
            }
            ToolResult t = diskpartRun(sc, cancel, o.log);
            o.ok = t.started && t.code == 0;
            o.result = o.ok ? T(L"The reserved partition (MSR) was created.") : T(L"The reserved partition could not be created. ") + t.output.substr(t.output.size() > 400 ? t.output.size() - 400 : 0);
            break;
        }
        case FX_RESTORE_WINDOWS_OLD: {
            if (!in || !in->windowsOld) { fail(T(L"No Windows.old was chosen.")); break; }
            const Partition& P = r.m.parts[in->part];
            wstr oldRel = in->folder;  // "Windows.old\"
            SYSTEMTIME st;
            GetLocalTime(&st);
            wchar_t keep[64];
            swprintf(keep, 64, L"Windows.replaced %04d-%02d-%02d %02d-%02d\\", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
            wstr keepRel = keep;
            CreateDirectoryW((P.root + keepRel).c_str(), nullptr);
            static const wchar_t* top[] = {L"Windows", L"Program Files", L"Program Files (x86)", L"ProgramData", L"Users", L"Documents and Settings"};
            bool ok = true;
            int moved = 0;
            // 1. the current Windows out of the way
            for (const wchar_t* n : top) {
                if (!exists(P.root + n) || !exists(P.root + oldRel + n)) continue;
                if (MoveFileExW((P.root + n).c_str(), (P.root + keepRel + n).c_str(), 0)) {
                    j.recordMove(act, P, n, keepRel + n);
                    o.log += T(L"Moved aside: ") + n + L"\r\n";
                } else {
                    ok = false;
                    o.log += T(L"Could not move ") + n + L": " + errorText(GetLastError()) + L"\r\n";
                    break;
                }
            }
            // 2. the earlier one back in place
            if (ok)
                for (const wchar_t* n : top) {
                    if (!exists(P.root + oldRel + n) || exists(P.root + n)) continue;
                    if (MoveFileExW((P.root + oldRel + n).c_str(), (P.root + n).c_str(), 0)) {
                        j.recordMove(act, P, oldRel + n, n);
                        ++moved;
                        o.log += T(L"Put back: ") + n + L"\r\n";
                    } else {
                        ok = false;
                        o.log += T(L"Could not move ") + n + L": " + errorText(GetLastError()) + L"\r\n";
                    }
                }
            o.ok = ok && exists(P.root + L"Windows\\System32\\config\\SYSTEM");
            if (o.ok) {
                in->folder.clear();
                in->winDir = P.root + L"Windows";
                in->systemRoot = L"\\Windows";
                in->atRoot = true;
                in->windowsOld = false;
                in->sysPart = targetSysPart(r, *in);
                if (step) step(T(L"Writing start-up files for it"));
                FixOutcome b = rebuild(f, r, j, act, in->sysPart, false);
                o.log += b.log;
                o.result = b.ok ? T(L"The earlier Windows is back in place and has fresh start-up files. The replaced one is kept in the folder ") + keepRel.substr(0, keepRel.size() - 1) + L"."
                                : T(L"The earlier Windows is back in place, but its start-up files could not be written: use \"Rebuild the start-up files\" after scanning again.");
            } else o.result = T(L"It could not be put back completely; Undo moves everything back. See the details.");
            break;
        }
        default: fail(T(L"This fix is not known."));
    }
    j.end(act, o.ok, o.result, o.log, f.undoable);
    o.actionDir = act;
    o.undoable = f.undoable;
    return o;
}

wstr fixCommands(const Fix& f, const Report& r) {
    const Install* in = f.install >= 0 && f.install < (int)r.installs.size() ? &r.installs[f.install] : nullptr;
    wstr win = in ? (r.m.parts[in->part].letter.empty() ? wstr(L"X:") : r.m.parts[in->part].letter) + L"\\" + in->folder + L"Windows" : wstr(L"X:\\Windows");
    wstr drive = win.substr(0, 2);
    bool running = in && in->running;
    wstr store = f.store >= 0 && f.store < (int)r.stores.size() && !r.stores[f.store].live ? L"bcdedit /store \"" + r.stores[f.store].file + L"\" " : wstr(L"bcdedit ");
    wstr id = f.entry.empty() ? wstr(L"{id}") : f.entry;
    wstr image = running ? wstr(L"/Online") : L"/Image:" + drive + L"\\";
    switch (f.kind) {
        case FX_REBUILD_BOOT: return L"bcdboot " + win + L" /s S: /f " + (r.m.fw.uefi ? L"UEFI" : L"BIOS");
        case FX_ADD_ENTRY: return L"bcdboot " + win + L" /s S: /addlast";
        case FX_SHOW_IN_MENU: return store + L"/displayorder " + id + L" /addlast";
        case FX_MAKE_DEFAULT: return store + L"/default " + id;
        case FX_STOP_AUTOREPAIR: return store + L"/set " + id + L" recoveryenabled No";
        case FX_DELETE_BCD_ENTRY: return store + L"/delete " + id + L" /cleanup";
        case FX_REMOVE_FROM_MENU: return store + L"/displayorder " + id + L" /remove";
        case FX_RENAME_ENTRY: return store + L"/set " + id + L" description \"...\"";
        case FX_SET_TIMEOUT: return store + L"/timeout " + f.arg;
        case FX_SAFE_MODE: return store + (f.arg == L"off" ? L"/deletevalue " + id + L" safeboot" : L"/set " + id + L" safeboot minimal");
        case FX_BOOT_OPTION: {
            wstr c = f.arg, out;
            for (size_t p; (p = c.find(L"{id}")) != wstr::npos;) c.replace(p, 4, id);
            for (size_t a = 0; a <= c.size();) {
                size_t b = c.find(L'\n', a);
                if (b == wstr::npos) b = c.size();
                out += (out.empty() ? L"" : L"\n") + store + c.substr(a, b - a);
                a = b + 1;
            }
            return out;
        }
        case FX_MARK_ACTIVE: return L"diskpart: select disk N, select partition N, active";
        case FX_MBR_CODE: return L"bootsect /nt60 S: /mbr /force";
        case FX_CREATE_SYSPART: return L"diskpart: shrink, create partition efi size=260, format fs=fat32" + wstr(r.m.fw.uefi ? L", create partition msr size=16" : L"") + L"\nbcdboot " + win + L" /s S:";
        case FX_REBUILD_ESP: return L"diskpart: select the EFI partition, format fs=fat32 quick\nbcdboot " + win + L" /s S: /f UEFI";
        case FX_CREATE_MSR: return L"diskpart: create partition msr size=16";
        case FX_FIX_GPT: return L"IOCTL_DISK_SET_DRIVE_LAYOUT_EX (writes both copies of the partition table again)";
        case FX_CHKDSK: return L"chkdsk " + drive + L" /f /x";
        case FX_UNLOCK: return L"manage-bde -unlock " + drive + L" -RecoveryPassword ******-...";
        case FX_RESTORE_HIVE: return L"copy " + win + L"\\System32\\config\\RegBack\\* " + win + L"\\System32\\config\\";
        case FX_SFC: return running ? wstr(L"sfc /scannow") : L"sfc /scannow /offbootdir=" + drive + L"\\ /offwindir=" + win;
        case FX_DISM_HEAL: return L"dism " + image + L" /Cleanup-Image /RestoreHealth" + (running ? L"" : L" /Source:WIM:<install.wim>:1 /LimitAccess");
        case FX_REVERT_UPDATES: return L"dism " + image + L" /Cleanup-Image /RevertPendingActions";
        case FX_REMOVE_UPDATE: return L"dism " + image + L" /Remove-Package /PackageName:" + (f.arg.empty() ? wstr(L"<package>") : f.arg);
        case FX_ADD_DRIVER: return L"dism " + image + L" /Add-Driver /Driver:\"" + f.arg + L"\"";
        case FX_ENABLE_DRIVERS:
            return L"reg: " + win + L"\\System32\\config\\SYSTEM, ControlSet*\\Services\\{disk,volmgr,partmgr,Ntfs" +
                   (f.arg.empty() ? wstr(L", + the controller's driver") : L"," + f.arg) + L"}\\Start = 0" +
                   (f.arg2.empty() ? wstr() : L"\nreg: ControlSet*\\Control\\CriticalDeviceDatabase\\" + f.arg2);
        case FX_DRIVER_START: return L"reg: " + win + L"\\System32\\config\\SYSTEM, ControlSet*\\Services\\" + f.arg + L"\\Start = " + f.arg2;
        case FX_CLEAR_SETUP: return L"reg: SYSTEM\\Setup: SystemSetupInProgress = 0, SetupType = 0, CmdLine = \"\"";
        case FX_ENABLE_WINRE: return running ? wstr(L"reagentc /enable") : L"reagentc /setreimage /path " + win + L"\\System32\\Recovery /target " + win;
        case FX_REBUILD_WINRE: return L"diskpart: create partition primary size=1024, format fs=ntfs, set id=de94bba4-... (recovery)\ncopy Winre.wim R:\\Recovery\\WindowsRE\nreagentc /setreimage /path R:\\Recovery\\WindowsRE /target " + win;
        case FX_MOVE_TO_ROOT: return L"move " + drive + L"\\" + (in ? in->folder : wstr()) + L"* " + drive + L"\\";
        case FX_DELETE_FW_ENTRY: return L"SetFirmwareEnvironmentVariable(Boot####, empty), BootOrder without it";
        case FX_ADD_FW_ENTRY: return L"SetFirmwareEnvironmentVariable(Boot####, EFI_LOAD_OPTION), BootOrder";
        case FX_FW_FIRST: return L"SetFirmwareEnvironmentVariable(BootOrder, this entry first)";
        case FX_FIRMWARE_SETTINGS: return L"SetFirmwareEnvironmentVariable(OsIndications |= 1), restart";
    }
    return L"";
}

bool fixChangesPartitions(int kind) {
    switch (kind) {
        case FX_CREATE_SYSPART: case FX_REBUILD_ESP: case FX_REBUILD_WINRE: case FX_FIX_GPT: case FX_MBR_CODE: case FX_MARK_ACTIVE: case FX_GROW_WINDOWS: case FX_CREATE_MSR: case FX_LAYOUT:
            return true;
    }
    return false;
}

void checkTools(Report& r) {
    wstr bootsect = findSystemTool(L"bootsect.exe");
    if (bootsect.empty())  // a Windows install USB stick has it in \boot
        for (const Partition& p : r.m.parts)
            if (!p.root.empty() && exists(p.root + L"boot\\bootsect.exe") && peArch(p.root + L"boot\\bootsect.exe") == nativeArch()) bootsect = p.root + L"boot\\bootsect.exe";
    bool manageBde = !findSystemTool(L"manage-bde.exe").empty();
    bool dism = !findSystemTool(L"dism.exe").empty();
    bool sfc = !findSystemTool(L"sfc.exe").empty();
    bool diskpart = !findSystemTool(L"diskpart.exe").empty();
    int ma = 0, mi = 0;
    bool bcdboot = !pickBcdboot(nullptr, ma, mi).empty();
    auto check = [&](Fix& f, const Install* in) {
        if (!f.available) return;
        auto no = [&](const wstr& why) {
            f.available = false;
            f.notAvailable = why;
        };
        const wstr usb = T(L" Start the PC from a Windows USB stick or Windows' recovery tools and run this program there.");
        // Fixes that change partitions are never done on the disk in use
        if (fixChangesPartitions(f.kind)) {
            int disk = f.disk >= 0 ? f.disk : f.part >= 0 ? r.m.parts[f.part].disk : (in && in->part >= 0 ? r.m.parts[in->part].disk : -1);
            wstr refuse = disk >= 0 ? workingDiskRefusal(r, disk) : wstr();
            if (!refuse.empty()) {
                no(refuse);
                return;
            }
        }
        switch (f.kind) {
            case FX_MBR_CODE:
                if (bootsect.empty()) no(T(L"The tool for this (bootsect.exe) is only part of Windows' recovery tools and install USB sticks.") + usb);
                else f.arg = bootsect;
                break;
            case FX_FIRMWARE_SETTINGS:
                if (!r.m.fw.uefi || !r.m.fw.canOpenSettings)
                    no(T(L"This PC cannot be asked to open its firmware settings from here. Restart it and press its settings key (often Delete, F2, F10, F1 or Esc) right after switching it on."));
                break;
            case FX_ADD_FW_ENTRY:
            case FX_FW_FIRST:
            case FX_DELETE_FW_ENTRY:
                if (!r.m.fw.uefi || !r.m.fw.varsReadable) no(T(L"This program cannot change the PC's start-up list here. Remove the entry in the PC's firmware settings (often under \"Boot\")."));
                break;
            case FX_DELETE_BCD_ENTRY:
            case FX_REMOVE_FROM_MENU:
                if (findSystemTool(L"bcdedit.exe").empty()) no(L"bcdedit.exe is not part of this system." + usb);
                break;
            case FX_ENABLE_WINRE:
                if (findSystemTool(L"reagentc.exe").empty() && !(in && exists(in->winDir + L"\\System32\\reagentc.exe")))
                    no(T(L"Windows' tool for this (reagentc.exe) is not part of this system.") + usb);
                break;
            case FX_REBUILD_WINRE:
            case FX_CREATE_MSR:
                if (!diskpart) no(T(L"diskpart.exe is not part of this system.") + usb);
                break;
            case FX_RESTORE_WINDOWS_OLD:
                if (in == nullptr && f.install >= 0 && f.install < (int)r.installs.size()) {
                    const Install& w = r.installs[f.install];
                    for (const Install& o2 : r.installs)
                        if (o2.running && o2.part == w.part) no(T(L"The Windows that is running now is on this drive, and its folders cannot be moved while it runs.") + usb);
                }
                break;
            case FX_UNLOCK: if (!manageBde) no(T(L"The BitLocker tool (manage-bde.exe) is not part of this system.") + usb); break;
            case FX_REVERT_UPDATES: if (!dism) no(T(L"DISM is not part of this system.") + usb); break;
            case FX_ADD_DRIVER:
            case FX_DISM_HEAL:
            case FX_REMOVE_UPDATE:
                if (!dism) no(T(L"DISM is not part of this system.") + usb);
                break;
            case FX_BOOT_OPTION:
            case FX_SAFE_MODE:
                if (f.entry.empty() || f.store < 0) no(T(L"This Windows has no start-up entry to change. Rebuild its start-up files first."));
                break;
            case FX_SFC: if (!sfc) no(T(L"The file checker (sfc.exe) is not part of this system.") + usb); break;
            case FX_REBUILD_ESP:
                if (!diskpart) no(T(L"diskpart.exe is not part of this system.") + usb);
                else if (in && in->running) no(T(L"This is the Windows that is running now, so its start-up partition cannot be reformatted.") + usb);
                [[fallthrough]];
            case FX_CREATE_SYSPART:
                if (f.kind == FX_CREATE_SYSPART && !diskpart) no(T(L"diskpart.exe is not part of this system.") + usb);
                else if (f.kind == FX_CREATE_SYSPART && in) {
                    const Partition& P = r.m.parts[in->part];
                    const Disk* d = diskOf(r, P.disk);
                    int primaries = 0;
                    if (d)
                        for (int i : d->parts)
                            if (r.m.parts[i].number <= 4 || r.m.parts[i].gpt) ++primaries;
                    if (d && d->style == 1 && primaries >= 4) no(T(L"This MBR disk already has four partitions, the most it can hold."));
                    else if (d && d->largestGap < (320ull << 20) && P.freeBytes < (2ull << 30))
                        no(T(L"There is not enough free space: the Windows drive needs at least 2 GB free to give up 300 MB."));
                    else if (in->running) no(T(L"This is the Windows that is running now.") + usb);
                }
                [[fallthrough]];
            case FX_REBUILD_BOOT:
            case FX_ADD_ENTRY:
                if (f.available && !bcdboot && !(in && exists(in->winDir + L"\\System32\\bcdboot.exe")))
                    no(T(L"bcdboot.exe, the Windows tool that writes start-up files, is not part of this system.") + usb);
                break;
        }
    };
    for (Install& in : r.installs)
        for (Fix& f : in.plan) check(f, &in);
    for (Finding& fd : r.pc)
        for (Fix& f : fd.fixes) check(f, nullptr);
}

bool snapshotBootSetup(const Report& r, Journal& j, wstr& log, const wstr& title) {
    wstr act = j.begin(title, T(L"All disks"));
    bool ok = true, liveDone = false;
    int n = 0;
    for (const BcdStore& s : r.stores) {
        wstr err;
        const Partition& p = r.m.parts[s.part];
        if (s.live) {
            if (liveDone) continue;
            liveDone = true;
            if (!j.saveLiveBcd(act, err)) ok = false;
        } else if (!j.saveFile(act, p, istartsWith(s.file, p.root) ? s.file.substr(p.root.size()) : s.file, err))
            ok = false;
        if (!err.empty()) log += err + L"\r\n";
        ++n;
    }
    for (const Disk& d : r.m.disks) {
        if (d.removable || !d.readable || d.style == 0) continue;
        wstr err;
        if (!j.saveDiskTables(act, d, err)) {
            ok = false;
            log += err + L"\r\n";
        }
    }
    j.end(act, ok, T(L"Saved ") + std::to_wstring(n) + (n == 1 ? T(L" start-up settings file") : T(L" start-up settings files")) + T(L" and the partition tables of every disk."),
          log, true);
    return ok;
}

}  // namespace bd

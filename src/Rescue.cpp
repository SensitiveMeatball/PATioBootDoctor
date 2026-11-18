#include "Rescue.h"
#include "Lang.h"
#include "core/Platform.h"
#include "Keys.h"
#include "core/Settings.h"

namespace bd {

static bool exists(const wstr& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

static uint64_t fileBytes(const wstr& p) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d)) return 0;
    return ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}

static void makeDirs(const wstr& path) {
    for (size_t p = path.find(L'\\', 3); p != wstr::npos; p = path.find(L'\\', p + 1)) CreateDirectoryW(path.substr(0, p).c_str(), nullptr);
    CreateDirectoryW(path.c_str(), nullptr);
}

StickMarker parseStickMarker(const std::string& text) {
    StickMarker m;
    m.found = true;
    wstr t = utf8ToWide(text);
    size_t a = 0;
    while (a <= t.size()) {
        size_t b = t.find(L'\n', a);
        if (b == wstr::npos) b = t.size();
        wstr line = t.substr(a, b - a);
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();
        if (istartsWith(line, L"name=")) m.name = line.substr(5);
        else if (m.version.empty() && !line.empty()) m.version = line;
        a = b + 1;
    }
    return m;
}

StickMarker readStickMarker(const wstr& root) {
    std::string s;
    if (!readFileText(root + L"PATioBootDoctor.stick", s)) return StickMarker();
    return parseStickMarker(s);
}

bool writeStickMarker(const wstr& root, const wstr& version, const wstr& name) {
    wstr clean;
    for (wchar_t c : name)
        if (c != L'\r' && c != L'\n') clean += c;
    std::string u = wideToUtf8(version + (clean.empty() ? wstr() : L"\r\nname=" + clean) + L"\r\n");
    wstr path = root + L"PATioBootDoctor.stick";
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    HANDLE m = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (m == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    bool ok = WriteFile(m, u.data(), (DWORD)u.size(), &wr, nullptr) && wr == u.size();
    CloseHandle(m);
    return ok;
}

wstr stickNameOnDisk(const Report& r, int disk) {
    for (const Disk& d : r.m.disks)
        if (d.number == disk)
            for (int i : d.parts) {
                const Partition& p = r.m.parts[i];
                if (p.root.empty()) continue;
                StickMarker m = readStickMarker(p.root);
                if (m.found && !m.name.empty()) return m.name;
            }
    return L"";
}

wstr findCommander(const wstr& exe) {
    // PATio Commander lives one level above the PatSuite folder this program is in
    wstr dir = exe.substr(0, exe.find_last_of(L'\\'));
    for (int up = 0; up < 3 && !dir.empty(); ++up) {
        wstr leaf = dir.substr(dir.find_last_of(L'\\') + 1);
        wstr parent = dir.find_last_of(L'\\') == wstr::npos ? wstr() : dir.substr(0, dir.find_last_of(L'\\'));
        if (iequals(leaf, L"PatSuite") && exists(parent + L"\\PATioCommander_x64.exe")) return parent + L"\\PATioCommander_x64.exe";
        if (exists(dir + L"\\PATioCommander_x64.exe")) return dir + L"\\PATioCommander_x64.exe";
        dir = parent;
    }
    return L"";
}

// "Drivers": the storage drivers a technician keeps on the stick for "Load a storage driver"
static const wchar_t* const kStickData[] = {L"PATioAppData", L"PATioBootDoctorData", L"PATioBootDoctor", L"PATioBootDoctor Backups", L"Drivers"};

RescueNeeds rescueNeeds(const Report& r, const RescueSource& src, const wstr& exe, int targetDisk) {
    RescueNeeds n;
    const uint64_t MB = 1ull << 20;
    auto fsize = [](const wstr& p) { return (uint64_t)fileBytes(p); };
    wstr exeDir = exe.substr(0, exe.find_last_of(L'\\'));
    uint64_t programs = fsize(exe) + (exists(exeDir + L"\\lang") ? folderBytes(exeDir + L"\\lang") : 0);
    wstr cmdr = findCommander(exe);
    if (!cmdr.empty()) {
        wstr cdir = cmdr.substr(0, cmdr.find_last_of(L'\\'));
        programs += fsize(cmdr) + (exists(cdir + L"\\lang") ? folderBytes(cdir + L"\\lang") : 0);
    }
    // the image grows by about what is added to it (it is compressed again, so this is generous)
    n.image = src.size + programs + 8 * MB;
    bool fromWinre = src.path.size() >= 9 && iequals(src.path.substr(src.path.size() - 9), L"Winre.wim");
    n.repairCopy = fromWinre ? src.size : 0;
    n.bootFiles = 40 * MB;
    n.data = fsize(keyStorePath());
    if (targetDisk >= 0)
        for (const Disk& d : r.m.disks)
            if (d.number == targetDisk)
                for (int i : d.parts) {
                    const Partition& p = r.m.parts[i];
                    if (p.root.empty()) continue;
                    for (const wchar_t* f : kStickData)
                        if (exists(p.root + f)) n.data += folderBytes(p.root + f);
                }
    uint64_t total = n.image + n.repairCopy + n.data + n.bootFiles;
    n.onStick = total + total / 10 + 64 * MB;  // 10 % and a little more to spare
    // sticks are sold as 1, 2, 4, 8, 16, 32 ... GB, and hold a bit less than that
    for (uint64_t gb = 1; gb <= 2048; gb *= 2)
        if ((gb << 30) * 93 / 100 >= n.onStick) {
            n.stickSize = gb << 30;
            break;
        }
    // on this PC while it is made: a copy of the image, the image opened (about 1.5 times its size),
    // the start-up files, and the stick's own data kept safe
    n.work = src.size * 3 + programs + 300 * MB + (n.data > fsize(keyStorePath()) ? n.data : 0);
    uint64_t vhd = std::max<uint64_t>(1024 * MB, (n.onStick / MB * 13 / 10 + 400 + 255) / 256 * 256 * MB);
    n.imageFiles = vhd * 2;
    return n;
}

std::vector<RescueSource> rescueSources(const Report& r) {
    std::vector<RescueSource> out;
    auto add = [&](const wstr& path, const wstr& label) {
        if (!protectedFileExists(path)) return;  // the recovery partition's folder is open to the system account only
        for (const RescueSource& s : out)
            if (iequals(s.path, path)) return;
        uint64_t size = fileBytes(path);
        if (!size) {
            HANDLE h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            LARGE_INTEGER li{};
            if (h != INVALID_HANDLE_VALUE) {
                if (GetFileSizeEx(h, &li)) size = (uint64_t)li.QuadPart;
                CloseHandle(h);
            }
        }
        out.push_back({path, label, size});
    };
    for (const Partition& p : r.m.parts) {
        if (p.root.empty() || !p.readable) continue;
        add(p.root + L"Recovery\\WindowsRE\\Winre.wim", T(L"Windows Recovery Environment on ") + p.where());
        add(p.root + L"sources\\boot.wim", T(L"Windows install or rescue media on ") + p.name());
    }
    for (const Install& in : r.installs)
        if (!in.linux) add(in.winDir + L"\\System32\\Recovery\\Winre.wim", T(L"Windows Recovery Environment of ") + in.label());
    return out;
}

std::vector<int> rescueTargets(const Report& r, bool allowVirtual) {
    std::vector<int> out;
    for (const Disk& d : r.m.disks) {
        bool usb = d.removable || d.bus == L"USB";
        bool virt = allowVirtual && d.bus == L"Virtual";
        if ((usb || virt) && d.size >= (900ull << 20)) {
            bool system = false;  // never the disk the running system or this program is on
            for (int i : d.parts)
                for (const Install& in : r.installs)
                    if (in.running && in.part == i) system = true;
            if (!system) out.push_back(d.number);
        }
    }
    return out;
}

static ToolResult diskpart(const wstr& work, const wstr& script) {
    wstr file = work + L"\\dp.txt";
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

static bool copyFile(const wstr& from, const wstr& to, wstr& log) {
    size_t s = to.find_last_of(L'\\');
    if (s != wstr::npos) makeDirs(to.substr(0, s));
    SetFileAttributesW(to.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (CopyFileW(from.c_str(), to.c_str(), FALSE)) {
        SetFileAttributesW(to.c_str(), FILE_ATTRIBUTE_NORMAL);
        return true;
    }
    log += T(L"Could not copy ") + from + L": " + errorText(GetLastError()) + L"\r\n";
    return false;
}

static void copyTree(const wstr& from, const wstr& to) {
    makeDirs(to);
    WIN32_FIND_DATAW fd{};
    HANDLE f = FindFirstFileW((from + L"\\*").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        wstr n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) copyTree(from + L"\\" + n, to + L"\\" + n);
        else CopyFileW((from + L"\\" + n).c_str(), (to + L"\\" + n).c_str(), FALSE);
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

// Copies a folder tree; false when anything could not be copied.
static bool copyTreeChecked(const wstr& from, const wstr& to, wstr& log) {
    makeDirs(to);
    WIN32_FIND_DATAW fd{};
    HANDLE f = FindFirstFileW((from + L"\\*").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    bool ok = true;
    do {
        wstr n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ok = copyTreeChecked(from + L"\\" + n, to + L"\\" + n, log) && ok;
        else {
            SetFileAttributesW((to + L"\\" + n).c_str(), FILE_ATTRIBUTE_NORMAL);
            if (!CopyFileW((from + L"\\" + n).c_str(), (to + L"\\" + n).c_str(), FALSE)) {
                log += T(L"Could not copy ") + from + L"\\" + n + L": " + errorText(GetLastError()) + L"\r\n";
                ok = false;
            }
        }
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return ok;
}

// This program's data on a stick that is about to be erased: its folder (keys, settings,
// repair tools) and backups kept on it.

static wstr guidIn(const wstr& text) {
    size_t a = text.find(L'{'), b = a == wstr::npos ? wstr::npos : text.find(L'}', a);
    return b == wstr::npos ? wstr() : text.substr(a, b - a + 1);
}

// A BCD store that starts the image from the stick as a RAM disk.
static bool makeStore(const wstr& store, bool efi, const wstr& loaderPath, wstr& log) {
    wstr bcd = bcdeditPath();
    DeleteFileW(store.c_str());
    auto run = [&](const wstr& args) {
        ToolResult t = runTool(bcd, L"/store \"" + store + L"\" " + args);
        log += L"> bcdedit " + args + L"\r\n" + t.output;
        return t;
    };
    ToolResult t = runTool(bcd, L"/createstore \"" + store + L"\"");
    log += t.output;
    if (t.code != 0) return false;
    bool ok = run(L"/create {bootmgr} /d \"PATio Boot Doctor\"").code == 0;
    ok = ok && run(L"/set {bootmgr} device boot").code == 0;
    run(L"/set {bootmgr} timeout 0");
    ok = ok && run(L"/create {ramdiskoptions} /d \"PATio Boot Doctor\"").code == 0;
    ok = ok && run(L"/set {ramdiskoptions} ramdisksdidevice boot").code == 0;
    ok = ok && run(L"/set {ramdiskoptions} ramdisksdipath \\Boot\\boot.sdi").code == 0;
    ToolResult c = run(L"/create /d \"PATio Boot Doctor\" /application osloader");
    wstr id = guidIn(c.output);
    if (!ok || id.empty()) return false;
    wstr dev = L"ramdisk=[boot]\\sources\\boot.wim,{ramdiskoptions}";
    ok = run(L"/set " + id + L" device " + dev).code == 0;
    ok = ok && run(L"/set " + id + L" osdevice " + dev).code == 0;
    ok = ok && run(L"/set " + id + L" path " + loaderPath + (efi ? L".efi" : L".exe")).code == 0;
    ok = ok && run(L"/set " + id + L" systemroot \\windows").code == 0;
    run(L"/set " + id + L" winpe yes");
    run(L"/set " + id + L" detecthal yes");
    ok = ok && run(L"/set {bootmgr} default " + id).code == 0;
    ok = ok && run(L"/set {bootmgr} displayorder " + id).code == 0;
    return ok;
}

FixOutcome makeRescueStick(const Report& r, int diskNum, const RescueSource& src, const wstr& exe, const wstr& workIn,
                           const std::function<void(const wstr&, float)>& progress, volatile bool* cancel, const RescueExtras& extras) {
    FixOutcome o;
    auto step = [&](const wstr& s, float f) {
        if (progress) progress(s, f);
        o.log += L"\r\n== " + s + L"\r\n";
    };
    const Disk* disk = nullptr;
    for (const Disk& d : r.m.disks)
        if (d.number == diskNum) disk = &d;
    if (!disk) {
        o.result = T(L"The USB stick was not found. Plug it in again and scan again.");
        return o;
    }
    wstr work = workIn + L"\\PATioBD-rescue";
    wstr mount = work + L"\\mount", wim = work + L"\\boot.wim";
    // A mount left behind by an earlier attempt
    runTool(findSystemTool(L"dism.exe"), L"/Unmount-Image /MountDir:\"" + mount + L"\" /Discard");
    deleteFolderTree(work);
    makeDirs(mount);
    wstr dism = findSystemTool(L"dism.exe");
    if (dism.empty()) {
        o.result = T(L"DISM, the Windows tool that opens system images, is not part of this system.");
        return o;
    }
    // 1. Copy the image and put this program into it
    step(T(L"Copying the recovery image"), 0.05f);
    wstr copyErr;
    if (!copyProtectedFile(src.path, wim, copyErr)) {
        o.log += copyErr + L"\r\n";
        o.result = T(L"The recovery image could not be copied. Is there 1 GB free on ") + workIn + L"?";
        return o;
    }
    step(T(L"Opening the image"), 0.15f);
    ToolResult t = runTool(dism, L"/Mount-Image /ImageFile:\"" + wim + L"\" /Index:1 /MountDir:\"" + mount + L"\"", cancel);
    o.log += t.output;
    if (t.code != 0) {
        o.result = T(L"The image could not be opened. DISM said: ") + t.output.substr(t.output.size() > 300 ? t.output.size() - 300 : 0);
        deleteFolderTree(work);
        return o;
    }
    bool ok = true;
    auto discard = [&] { runTool(dism, L"/Unmount-Image /MountDir:\"" + mount + L"\" /Discard"); };
    if (cancel && *cancel) {
        discard();
        o.result = T(L"Stopped. Nothing was written to the USB stick.");
        return o;
    }
    // Both programs, laid out as PatSuite lays them out, so each one's PatSuite menu finds the other:
    //   X:\PATio\PATioCommander_x64.exe
    //   X:\PATio\PatSuite\PATio Boot Doctor\PATioBootDoctor_x64.exe
    wstr commander = findCommander(exe);
    step(commander.empty() ? T(L"Adding PATio Boot Doctor to the image") : T(L"Adding PATio Boot Doctor and PATio Commander to the image"), 0.35f);
    wstr suiteDir = mount + L"\\PATio";
    wstr appDir = suiteDir + L"\\PatSuite\\PATio Boot Doctor";
    makeDirs(appDir);
    ok = copyFile(exe, appDir + L"\\PATioBootDoctor_x64.exe", o.log) && ok;
    {
        wstr exeDir = exe.substr(0, exe.find_last_of(L'\\'));
        if (exists(exeDir + L"\\lang")) copyTree(exeDir + L"\\lang", appDir + L"\\lang");
    }
    if (!commander.empty()) {
        ok = copyFile(commander, suiteDir + L"\\PATioCommander_x64.exe", o.log) && ok;
        wstr cdir = commander.substr(0, commander.find_last_of(L'\\'));
        if (exists(cdir + L"\\lang")) copyTree(cdir + L"\\lang", suiteDir + L"\\lang");
    }
    // What starts when the stick starts: Windows PE's own set-up (drives, network), PATio Commander
    // in the background, then PATio Boot Doctor; a command prompt stays open after it, so closing
    // Boot Doctor does not restart the PC while Commander is still in use.
    std::string shell = "[LaunchApps]\r\n";
    if (exists(mount + L"\\Windows\\System32\\wpeinit.exe")) shell += "%WINDIR%\\System32\\wpeinit.exe\r\n";
    if (!commander.empty()) shell += "%WINDIR%\\System32\\cmd.exe, /c start \"\" \"%SYSTEMDRIVE%\\PATio\\PATioCommander_x64.exe\"\r\n";
    // (the folder name has spaces: started through cmd, which handles the quotes, and waited for)
    shell += "%WINDIR%\\System32\\cmd.exe, /c start \"\" /wait \"%SYSTEMDRIVE%\\PATio\\PatSuite\\PATio Boot Doctor\\PATioBootDoctor_x64.exe\"\r\n%WINDIR%\\System32\\cmd.exe\r\n";
    {
        wstr ini = mount + L"\\Windows\\System32\\winpeshl.ini";
        SetFileAttributesW(ini.c_str(), FILE_ATTRIBUTE_NORMAL);
        HANDLE h = CreateFileW(ini.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD wr = 0;
        ok = h != INVALID_HANDLE_VALUE && WriteFile(h, shell.data(), (DWORD)shell.size(), &wr, nullptr) && ok;
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    // Start-up files from the image itself (they match it), else from this Windows
    wchar_t win[MAX_PATH] = {};
    GetWindowsDirectoryW(win, MAX_PATH);
    auto pick = [&](const wstr& rel) {
        wstr a = mount + L"\\Windows\\" + rel, b = wstr(win) + L"\\" + rel;
        return exists(a) ? a : exists(b) ? b : wstr();
    };
    wstr bootmgfw = pick(L"Boot\\EFI\\bootmgfw.efi"), bootmgr = pick(L"Boot\\PCAT\\bootmgr"), sdi = pick(L"Boot\\DVD\\EFI\\boot.sdi");
    if (sdi.empty()) sdi = pick(L"Boot\\DVD\\PCAT\\boot.sdi");
    wstr fonts = pick(L"Boot\\Fonts");
    wstr loader = exists(mount + L"\\Windows\\System32\\boot\\winload.efi") ? L"\\windows\\system32\\boot\\winload" : L"\\windows\\system32\\winload";
    if (bootmgfw.empty() || sdi.empty()) {
        discard();
        o.result = T(L"The start-up files needed for the stick (bootmgfw.efi, boot.sdi) were not found in the image or in this Windows.");
        return o;
    }
    std::vector<std::pair<wstr, wstr>> files = {{bootmgfw, L"EFI\\Boot\\bootx64.efi"}, {sdi, L"Boot\\boot.sdi"}};
    wstr staged = work + L"\\stick";
    for (auto& f : files) ok = copyFile(f.first, staged + L"\\" + f.second, o.log) && ok;
    if (!bootmgr.empty()) copyFile(bootmgr, staged + L"\\bootmgr", o.log);
    if (!fonts.empty()) {
        copyTree(fonts, staged + L"\\EFI\\Microsoft\\Boot\\Fonts");
        copyTree(fonts, staged + L"\\Boot\\Fonts");
    }
    step(T(L"Saving the image (this takes a few minutes)"), 0.45f);
    t = runTool(dism, L"/Unmount-Image /MountDir:\"" + mount + L"\" /Commit", cancel);
    o.log += t.output;
    if (t.code != 0 || !ok) {
        discard();
        o.result = T(L"The image could not be saved. ") + t.output.substr(t.output.size() > 300 ? t.output.size() - 300 : 0);
        return o;
    }
    // 2. Start-up settings for both kinds of PC
    step(T(L"Writing the start-up settings"), 0.65f);
    makeDirs(staged + L"\\EFI\\Microsoft\\Boot");
    makeDirs(staged + L"\\Boot");
    if (!makeStore(staged + L"\\EFI\\Microsoft\\Boot\\BCD", true, loader, o.log) || !makeStore(staged + L"\\Boot\\BCD", false, loader, o.log)) {
        o.result = T(L"The start-up settings for the stick could not be written.");
        return o;
    }
    if (cancel && *cancel) {
        o.result = T(L"Stopped. Nothing was written to the USB stick.");
        deleteFolderTree(work);
        return o;
    }
    // 3. Keep this program's data that is on the stick now (an earlier rescue stick): copied to
    // this PC first, and the stick is only erased when that worked.
    wstr keep = workIn + L"\\PATio Boot Doctor stick data (kept)";
    std::vector<wstr> kept;
    for (int i : disk->parts) {
        const Partition& p = r.m.parts[i];
        if (p.root.empty()) continue;
        for (const wchar_t* d : kStickData) {
            if (!exists(p.root + d)) continue;
            step(T(L"Keeping the data of the earlier rescue stick (keys, settings, backups)"), 0.7f);
            if (!copyTreeChecked(p.root + d, keep + L"\\" + d, o.log)) {
                o.result = TF(L"The data on the USB stick ({0}) could not be kept safe on this PC, so the stick was not erased. Is there room on {1}?", wstr(d), workIn);
                return o;
            }
            kept.push_back(d);
        }
    }
    // 4. Prepare the stick: only now is it erased
    step(T(L"Preparing the USB stick (everything on it is erased)"), 0.72f);
    wstr letter = freeLetter();
    if (letter.empty()) {
        o.result = T(L"No free drive letter is left for the USB stick.");
        return o;
    }
    uint64_t sizeMB = std::min<uint64_t>(disk->size >> 20, 30000) - 8;  // FAT32 made by Windows: up to 32 GB
    t = diskpart(work, L"select disk " + std::to_wstring(diskNum) + L"\nclean\nconvert mbr\ncreate partition primary size=" + std::to_wstring(sizeMB) +
                           L"\nformat fs=fat32 quick label=BOOTDOCTOR\nactive\nassign letter=" + letter.substr(0, 1) + L"\n");
    o.log += t.output;
    Sleep(1500);
    if (t.code != 0 || !exists(letter + L"\\")) {
        o.result = T(L"The USB stick could not be prepared. Diskpart said: ") + t.output.substr(t.output.size() > 300 ? t.output.size() - 300 : 0);
        if (!kept.empty()) o.result += L"\n\n" + TF(L"The data of the earlier rescue stick is kept safe in {0}.", keep);
        return o;
    }
    wstr root = letter + L"\\";
    step(T(L"Copying to the USB stick"), 0.8f);
    copyTree(staged, root.substr(0, 2));
    ok = copyFile(wim, root + L"sources\\boot.wim", o.log);
    // The stick keeps the saved BitLocker keys (they are locked with their password) and is marked as ours,
    // so keys saved while running from it are kept on the stick, not in memory.
    CreateDirectoryW((root + L"PATioAppData").c_str(), nullptr);
    CreateDirectoryW((root + L"PATioAppData\\PATioBootDoctorData").c_str(), nullptr);
    // A clean copy of Windows' repair tools, for "Recreate the recovery partition" when started from the stick
    if (src.path.size() >= 9 && iequals(src.path.substr(src.path.size() - 9), L"Winre.wim")) {
        step(T(L"Keeping a clean copy of the repair tools on the stick"), 0.9f);
        wstr e2;
        if (!copyProtectedFile(src.path, root + L"PATioAppData\\PATioBootDoctorData\\Winre.wim", e2)) o.log += e2 + L"\r\n";
    }
    // The data kept from the earlier stick goes back
    wstr keptNote;
    bool restoreOk = true;
    for (const wstr& d : kept) {
        step(T(L"Putting the kept data back on the stick"), 0.92f);
        // the fresh repair tools stay: an older copy does not replace them
        // the fresh repair tools stay; data of an earlier version ("PATioBootDoctor") goes into the data folder
        wstr fresh = root + L"PATioAppData\\PATioBootDoctorData\\Winre.wim";
        bool data = d == L"PATioBootDoctorData" || d == L"PATioBootDoctor";  // where earlier versions kept it
        wstr saved = keep + L"\\" + d + (d == L"PATioAppData" ? L"\\PATioBootDoctorData" : L"") + L"\\Winre.wim";
        if ((data || d == L"PATioAppData") && exists(fresh)) DeleteFileW(saved.c_str());
        restoreOk = copyTreeChecked(keep + L"\\" + d, root + (data ? wstr(L"PATioAppData\\PATioBootDoctorData") : d), o.log) && restoreOk;
        keptNote += (keptNote.empty() ? L"" : L", ") + d;
    }
    // This PC's saved keys and settings go on the stick too
    wstr stickKeys = root + L"PATioAppData\\PATioBootDoctorData\\bitlocker-keys.dat";
    if (GetFileAttributesW(keyStorePath().c_str()) != INVALID_FILE_ATTRIBUTES) {
        if (!exists(stickKeys)) CopyFileW(keyStorePath().c_str(), stickKeys.c_str(), FALSE);
        else {
            int merged = extras.keysPassword.empty() ? -1 : mergeKeyFiles(stickKeys, keyStorePath(), extras.keysPassword);
            if (merged >= 0) keptNote += (keptNote.empty() ? L"" : L"; ") + TF(L"{0} keys of this PC added to the stick's keys", std::to_wstring(merged));
            else {
                // different passwords (or none given): both are kept, nothing is lost
                CopyFileW(keyStorePath().c_str(), (root + L"PATioAppData\\PATioBootDoctorData\\bitlocker-keys (this PC).dat").c_str(), FALSE);
                keptNote += (keptNote.empty() ? L"" : L"; ") + wstr(T(L"this PC's keys kept beside the stick's as \"bitlocker-keys (this PC).dat\""));
            }
        }
    }
    {
        // PATio Commander's settings of this PC, unless the stick has its own
        wstr from = patioAppData() + L"\\PATioCommanderData", to = root + L"PATioAppData\\PATioCommanderData";
        if (exists(from) && !exists(to)) copyTreeChecked(from, to, o.log);
    }
    if (!extras.settingsFile.empty() && exists(extras.settingsFile) && !exists(root + L"PATioAppData\\PATioBootDoctorData\\PATioBootDoctor.ini"))
        CopyFileW(extras.settingsFile.c_str(), (root + L"PATioAppData\\PATioBootDoctorData\\PATioBootDoctor.ini").c_str(), FALSE);
    if (!restoreOk) {
        o.log += TF(L"Not everything kept from the earlier stick could be put back. The copy stays in {0}.", keep) + L"\r\n";
        keptNote += L" " + TF(L"(not all of it could be put back; the copy stays in {0})", keep);
    } else if (!kept.empty())
        deleteFolderTree(keep);
    {
        int ma = 0, mi = 0, bu = 0;  // which version made it, so an outdated stick can be told, and its name
        fileVersion(exe, ma, mi, bu);
        writeStickMarker(root, std::to_wstring(ma) + L"." + std::to_wstring(mi) + L"." + std::to_wstring(bu), extras.stickName);
    }
    {
        std::string readme =
            "PATio Boot Doctor rescue stick" + (extras.stickName.empty() ? std::string() : ": " + wideToUtf8(extras.stickName)) + "\r\n\r\n"
            "To use it: plug it in, switch the PC on and open the PC's boot menu (often F12, F11, F8 or Esc right after switching on).\r\n"
            "Choose this USB stick. PATio Boot Doctor starts by itself. Closing it opens a command prompt.\r\n"
            "It works with Secure Boot on, on UEFI PCs and on older (legacy BIOS) PCs.\r\n";
        HANDLE h = CreateFileW((root + L"README.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD wr = 0;
        if (h != INVALID_HANDLE_VALUE) {
            WriteFile(h, readme.data(), (DWORD)readme.size(), &wr, nullptr);
            CloseHandle(h);
        }
    }
    step(T(L"Checking the stick"), 0.95f);
    ok = ok && exists(root + L"EFI\\Boot\\bootx64.efi") && exists(root + L"EFI\\Microsoft\\Boot\\BCD") && exists(root + L"Boot\\BCD") &&
         exists(root + L"Boot\\boot.sdi") && fileBytes(root + L"sources\\boot.wim") == fileBytes(wim);
    deleteFolderTree(work);
    o.ok = ok;
    o.result = ok ? T(L"The rescue USB stick is ready (drive ") + letter + T(L"). To use it, plug it in, switch the PC on, open the PC's boot menu (often F12, F11, F8 or Esc) and choose the stick. PATio Boot Doctor starts by itself.")
                  : T(L"The stick was made, but some files are missing on it. Try again, or use another stick.");
    if (!keptNote.empty()) o.result += L"\n\n" + TF(L"Kept on the stick: {0}.", keptNote);
    return o;
}

// A rescue disk image: the same rescue stick, made on a virtual disk file (.vhd), and copied
// to a plain image (.img) that tools such as Rufus or balenaEtcher write to a stick.
FixOutcome makeRescueImage(const RescueSource& src, const wstr& exe, const wstr& outFolderIn, const wstr& workFolder,
                           const std::function<void(const wstr&, float)>& progress, volatile bool* cancel, const RescueExtras& extras) {
    FixOutcome o;
    wstr outFolder = outFolderIn;
    while (!outFolder.empty() && outFolder.back() == L'\\') outFolder.pop_back();
    wstr vhd = outFolder + L"\\PATio Boot Doctor rescue.vhd", img = outFolder + L"\\PATio Boot Doctor rescue.img";
    Report none;
    RescueNeeds need = rescueNeeds(none, src, exe);
    uint64_t mbSize = std::max<uint64_t>(1024, ((need.onStick >> 20) * 13 / 10 + 400 + 255) / 256 * 256);
    if (progress) progress(T(L"Making the disk image file"), 0.02f);
    wstr work = workFolder;
    CreateDirectoryW(work.c_str(), nullptr);
    diskpart(work, L"select vdisk file=\"" + vhd + L"\"\ndetach vdisk noerr\n");
    DeleteFileW(vhd.c_str());
    ToolResult t = diskpart(work, L"create vdisk file=\"" + vhd + L"\" maximum=" + std::to_wstring(mbSize) + L" type=fixed\nselect vdisk file=\"" + vhd + L"\"\nattach vdisk\n");
    o.log += t.output;
    if (t.code != 0) {
        o.result = T(L"The image file could not be made (is there room on that drive?). Diskpart said: ") + t.output.substr(t.output.size() > 300 ? t.output.size() - 300 : 0);
        return o;
    }
    Sleep(2000);
    Report r = scan(nullptr);
    int disk = -1;
    for (const Disk& d : r.m.disks)
        if (d.size == (mbSize << 20) && d.parts.empty() && (d.bus == L"Virtual" || d.bus.empty())) disk = d.number;
    if (disk < 0) {
        diskpart(work, L"select vdisk file=\"" + vhd + L"\"\ndetach vdisk noerr\n");
        o.result = T(L"The new image file was not seen as a disk, so nothing was written to it.");
        return o;
    }
    o = makeRescueStick(r, disk, src, exe, work, [&](const wstr& s, float f) {
        if (progress) progress(s, 0.05f + f * 0.8f);
    }, cancel, extras);
    diskpart(work, L"select vdisk file=\"" + vhd + L"\"\ndetach vdisk\n");
    if (!o.ok) return o;
    // .img: a fixed .vhd is the disk itself plus a 512-byte footer
    if (progress) progress(T(L"Writing the plain image (.img)"), 0.9f);
    HANDLE in = CreateFileW(vhd.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    HANDLE out = CreateFileW(img.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    bool imgOk = in != INVALID_HANDLE_VALUE && out != INVALID_HANDLE_VALUE;
    if (imgOk) {
        LARGE_INTEGER sz{};
        GetFileSizeEx(in, &sz);
        uint64_t left = sz.QuadPart > 512 ? (uint64_t)sz.QuadPart - 512 : 0;
        std::vector<char> buf(4 << 20);
        while (left && imgOk) {
            DWORD n = (DWORD)std::min<uint64_t>(left, buf.size()), rd = 0, wr = 0;
            imgOk = ReadFile(in, buf.data(), n, &rd, nullptr) && rd == n && WriteFile(out, buf.data(), n, &wr, nullptr) && wr == n;
            left -= n;
        }
    }
    if (in != INVALID_HANDLE_VALUE) CloseHandle(in);
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (!imgOk) DeleteFileW(img.c_str());
    o.result = TF(L"The rescue disk image is ready:\n{0}\n(a virtual disk: start it in a virtual machine, or attach it in Disk Management)", vhd) +
               (imgOk ? L"\n" + TF(L"{0}\n(a plain image: write it to a USB stick of {1} or more with a tool such as Rufus or balenaEtcher)", img, sizeText(mbSize << 20)) : wstr());
    return o;
}

}  // namespace bd

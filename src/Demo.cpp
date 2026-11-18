#include "Demo.h"
#include "Lang.h"

namespace bd {

static Partition part(int disk, int number, uint64_t off, uint64_t len, int role, const wchar_t* letter, const wchar_t* fs, const wchar_t* label,
                      uint64_t freeB) {
    Partition p;
    p.disk = disk;
    p.number = number;
    p.offset = off;
    p.length = len;
    p.role = role;
    p.letter = letter;
    p.fs = fs;
    p.rawFs = fs;
    p.label = label;
    p.readable = *fs != 0;
    p.totalBytes = len;
    p.freeBytes = freeB;
    p.root = *letter ? wstr(letter) + L"\\" : wstr();
    p.gpt = disk != 1;
    p.active = disk == 1 && number == 1;
    p.key = L"demo@" + std::to_wstring(disk) + L"-" + std::to_wstring(number);
    return p;
}

static Link link(const char* icon, const wchar_t* name, Health h, const wstr& plain, const wstr& tech, std::vector<int> fixes = {}) {
    Link l;
    l.icon = icon;
    l.name = name;
    l.health = h;
    l.plain = plain;
    l.tech = tech;
    l.fixes = fixes;
    return l;
}

Report demoReport() {
    const uint64_t MB = 1ull << 20, GB = 1ull << 30;
    Report r;
    r.demo = true;
    GetLocalTime(&r.when);
    r.host = T(L"Windows PE (build 26100)");
    r.hostPE = true;
    r.m.fw.uefi = true;
    r.m.fw.varsReadable = true;
    r.m.fw.secureBootKnown = r.m.fw.secureBoot = true;
    r.m.fw.canOpenSettings = true;
    r.m.fw.maker = L"LENOVO";
    r.m.fw.model = T(L"ThinkCentre M70q");

    Disk d0;
    d0.number = 0;
    d0.size = 500 * GB;
    d0.style = 2;
    d0.model = T(L"Samsung SSD 980");
    d0.bus = L"NVMe";
    d0.readable = d0.gptPrimaryOk = d0.gptBackupOk = d0.sig55aa = true;
    Disk d1;
    d1.number = 1;
    d1.size = 1000 * GB;
    d1.style = 1;
    d1.model = T(L"WDC WD10EZEX");
    d1.bus = L"SATA";
    d1.readable = d1.sig55aa = d1.bootCode = true;
    Disk d2;
    d2.number = 2;
    d2.size = 32 * GB;
    d2.style = 2;
    d2.model = T(L"SanDisk Ultra");
    d2.bus = L"USB";
    d2.removable = true;
    d2.readable = d2.gptPrimaryOk = true;
    d2.problems.push_back(T(L"The backup copy of the partition table is missing or damaged."));

    auto add = [&](Disk& d, const Partition& p) {
        d.parts.push_back((int)r.m.parts.size());
        r.m.parts.push_back(p);
    };
    add(d0, part(0, 1, MB, 100 * MB, R_ESP, L"", L"FAT32", L"", 68 * MB));
    add(d0, part(0, 2, 101 * MB, 16 * MB, R_MSR, L"", L"", L"", 0));
    add(d0, part(0, 3, 117 * MB, 498 * GB, R_DATA, L"C:", L"NTFS", L"Windows", 212 * GB));
    add(d0, part(0, 4, 498 * GB + 117 * MB, 750 * MB, R_RECOVERY, L"", L"NTFS", L"", 320 * MB));
    add(d1, part(1, 1, MB, 465 * GB, R_SYSTEM, L"D:", L"NTFS", L"", 61 * GB));
    add(d1, part(1, 2, 465 * GB + MB, 465 * GB, R_DATA, L"E:", L"NTFS", L"Backup", 140 * GB));
    add(d2, part(2, 1, MB, 32 * GB - 2 * MB, R_DATA, L"F:", L"exFAT", L"USB stick", 9 * GB));
    add(d1, part(1, 3, 930 * GB + MB, 69 * GB, R_LINUX, L"", L"", L"", 0));
    r.m.parts[7].rawFs = L"ext4";
    d1.gaps.push_back({999 * GB + MB, GB - 2 * MB});
    d2.gaps.clear();
    for (Disk* dk : {&d0, &d1, &d2}) {  // what kind of storage each one is
        dk->busType = dk->bus == L"NVMe" ? 17 : dk->bus == L"SATA" ? 11 : dk->bus == L"USB" ? 7 : 0;
        dk->removableMedia = dk->removable;
        if (dk->busType == 11) dk->rpm = 7200, dk->formFactor = L"3.5\"";
        classifyDisk(*dk);
    }
    r.m.parts[7].fsUuid = L"6f1c2a4e-93b0-4d7e-8a51-2c0f7b3e9d12";
    r.m.parts[0].root = L"\\\\?\\Volume{demo}\\";
    r.m.disks = {d0, d1, d2};

    BcdStore s;
    s.file = L"\\\\?\\Volume{demo}\\EFI\\Microsoft\\Boot\\BCD";
    s.part = 0;
    s.loaded = true;
    s.health = Health::Ok;
    s.hasBootmgr = true;
    s.timeout = 30;
    r.stores.push_back(s);

    // Windows 11, running
    Install a;
    a.part = 2;
    a.root = L"C:\\";
    a.winDir = L"C:\\Windows";
    a.systemRoot = L"\\Windows";
    a.name = L"Windows 11";
    a.edition = T(L"Professional");
    a.major = 10;
    a.build = 26100;
    a.arch = L"x64";
    a.running = true;
    a.sysPart = 0;
    a.store = 0;
    a.entryId = L"{current}";
    a.inMenu = a.isDefault = a.active = true;
    a.where = T(L"C: · Disk 0, partition 3");
    a.verdict = Health::Ok;
    a.verdictTitle = T(L"Running now");
    a.verdictText = T(L"This is the Windows you are using now. Everything it needs to start is in place.");
    a.chain = {
        link("bd_chip", N(L"PC firmware"), Health::Ok, L"The PC starts in UEFI mode, the modern way of starting.", L"UEFI firmware, Secure Boot on"),
        link("disk_hw", N(L"Disk"), Health::Ok, L"The disk with this Windows (Disk 0 · Samsung SSD 980 · 500 GB) can be read and its partition table is fine.", L"GPT, NVMe"),
        link("partition", N(L"Start-up partition"), Health::Ok, L"The start-up partition is there and can be read.", L"Disk 0, partition 1 (100 MB, FAT32)"),
        link("bd_key", N(L"Boot manager"), Health::Ok, L"Windows Boot Manager is on the start-up partition.", L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi"),
        link("bd_list", N(L"Start-up settings"), Health::Ok, L"The start-up settings list this Windows as \"Windows 11\", and it starts first.", L"BCD entry {current}"),
        link("drive", N(L"Windows partition"), Health::Ok, L"The partition with Windows can be read.", L"Disk 0, partition 3 (498 GB, NTFS, C:)"),
        link("bd_rocket", N(L"Windows loader"), Health::Ok, L"Windows' loader, the file that starts Windows itself, is in place.", L"C:\\Windows\\System32\\winload.efi"),
        link("file_app", N(L"Core system files"), Health::Ok, L"The Windows core and the drivers it needs to read its disk are in place.", L"ntoskrnl.exe, hal.dll, ci.dll"),
        link("settings", N(L"System settings"), Health::Ok, L"Windows' settings file (registry) looks fine.", L"config\\SYSTEM, SOFTWARE"),
        link("restart", N(L"Starting up"), Health::Ok, L"This Windows is running now, so it started fine.", L""),
    };

    // Windows 10 on the second disk: not in the menu, and looping
    Install b;
    b.part = 4;
    b.root = L"D:\\";
    b.winDir = L"D:\\Windows";
    b.systemRoot = L"\\Windows";
    b.name = L"Windows 10";
    b.major = 10;
    b.build = 19045;
    b.arch = L"x64";
    b.sysPart = 0;
    b.where = T(L"D: · Disk 1, partition 1");
    b.loop = true;
    b.verdict = Health::Bad;
    b.verdictTitle = T(L"Stuck in a restart loop");
    b.verdictText = T(L"This Windows keeps failing to start.");
    b.loopSigns = {T(L"Automatic Repair ran on 9 Nov 2026 because Windows could not start."), T(L"Windows crashed with a blue screen 3 times since 8 Nov 2026."),
                   T(L"Windows updates are waiting to finish installing.")};
    Fix f1 = makeFix(FX_REVERT_UPDATES), f2 = makeFix(FX_ADD_ENTRY), f3 = makeFix(FX_STOP_AUTOREPAIR);
    for (Fix* f : {&f1, &f2, &f3}) {
        f->install = 1;
        f->part = 4;
        f->disk = 1;
    }
    b.plan = {f1, f2, f3};
    b.chain = {
        link("bd_chip", N(L"PC firmware"), Health::Ok, L"The PC starts in UEFI mode, the modern way of starting.", L"UEFI firmware"),
        link("disk_hw", N(L"Disk"), Health::Ok, L"The disk with this Windows (Disk 1 · WDC WD10EZEX · 1000 GB) can be read and its partition table is fine.", L"MBR, SATA"),
        link("partition", N(L"Start-up partition"), Health::Ok,
             T(L"The start-up partition is there and can be read. It is on another disk (Disk 0), so this Windows only starts while that disk is connected."),
             T(L"Disk 0, partition 1 (100 MB, FAT32)")),
        link("bd_key", N(L"Boot manager"), Health::Ok, L"Windows Boot Manager is on the start-up partition.", L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi"),
        link("bd_list", N(L"Start-up settings"), Health::Warn,
             T(L"This Windows is not in the start-up menu, so it never starts (it is inactive). The menu only lists the other Windows."),
             T(L"No entry with osdevice Disk 1, partition 1 and systemroot \\Windows"), {1}),
        link("drive", N(L"Windows partition"), Health::Ok, L"The partition with Windows can be read.", L"Disk 1, partition 1 (465 GB, NTFS, D:)"),
        link("bd_rocket", N(L"Windows loader"), Health::Ok, L"Windows' loader, the file that starts Windows itself, is in place.", L"D:\\Windows\\System32\\winload.efi"),
        link("file_app", N(L"Core system files"), Health::Ok, L"The Windows core and the drivers it needs to read its disk are in place.", L""),
        link("settings", N(L"System settings"), Health::Ok, L"Windows' settings file (registry) looks fine.", L""),
        link("restart", N(L"Starting up"), Health::Bad, L"Windows seems to be stuck in a loop: it starts, fails and restarts, or keeps going into Automatic Repair.",
             b.loopSigns[0] + L"\n" + b.loopSigns[1] + L"\n" + b.loopSigns[2], {0, 2}),
    };

    // Windows 7 moved into a folder
    Install c;
    c.part = 5;
    c.root = L"E:\\";
    c.folder = L"Old PC\\";
    c.winDir = L"E:\\Old PC\\Windows";
    c.systemRoot = L"\\Old PC\\Windows";
    c.atRoot = false;
    c.name = L"Windows 7";
    c.major = 6;
    c.minor = 1;
    c.build = 7601;
    c.arch = L"x64";
    c.where = T(L"E: · Disk 1, partition 2");
    c.verdict = Health::Bad;
    c.verdictTitle = T(L"Moved into a folder");
    c.verdictText = T(L"This Windows sits inside the folder \"Old PC\\\", so it cannot start. It was probably copied or moved there.");
    Fix m1 = makeFix(FX_MOVE_TO_ROOT), m2 = makeFix(FX_REBUILD_BOOT);
    m1.install = m2.install = 2;
    m1.part = 5;
    m2.what = T(L"After the move, copies fresh start-up files and writes a start-up entry for this Windows.");
    c.plan = {m1, m2};
    c.chain = {link("folder", N(L"Location"), Health::Bad, c.verdictText + L" To start it again, its folders must go back to the top of the drive.", c.winDir, {0, 1})};

    // Ubuntu on the second disk: fine, but Windows starts first
    Install u;
    u.linux = true;
    u.part = 7;
    u.name = T(L"Ubuntu 24.04.1 LTS");
    u.distroId = L"ubuntu";
    u.kernels = {T(L"6.8.0-45-generic"), T(L"6.8.0-41-generic")};
    u.edition = T(L"Linux kernel 6.8.0-45-generic");
    u.winDir = T(L"Disk 1, partition 3 (ext4)");
    u.where = T(L"Disk 1, partition 3");
    u.sysPart = 0;
    u.active = u.inMenu = true;
    u.verdict = Health::Info;
    u.verdictTitle = T(L"Starts from the boot menu");
    u.verdictText = T(L"This Linux is fine. Another system starts first; to start Linux, pick it in the PC's boot menu, or make it start first.");
    Fix uf = makeFix(FX_FW_FIRST);
    uf.install = 3;
    uf.fwNum = 2;
    uf.entry = u.name;
    u.plan = {uf};
    u.chain = {
        link("bd_chip", N(L"PC firmware"), Health::Ok, L"The PC starts in UEFI mode, the modern way of starting.", L""),
        link("disk_hw", N(L"Disk"), Health::Ok, L"The disk with this Linux (Disk 1 · WDC WD10EZEX · 1000 GB) can be read and its partition table is fine.", L"MBR"),
        link("partition", N(L"Start-up partition"), Health::Ok,
             T(L"The start-up partition with the Linux boot loader is there and can be read. It is on another disk (Disk 0), so Linux only starts while that disk is connected."),
             T(L"Disk 0, partition 1 (100 MB, FAT32)")),
        link("bd_list", N(L"PC's start-up list"), Health::Info,
             T(L"The PC's start-up list has this Linux (\"ubuntu\"), but \"Windows Boot Manager\" starts first. To start Linux, pick it in the PC's boot menu (often F12, F11, F8 or Esc while the PC starts), or make it start first. When GRUB starts first, it usually shows a menu with Windows in it too."),
             L"Boot0002, \\EFI\\ubuntu\\shimx64.efi", {0}),
        link("bd_key", N(L"Linux boot loader"), Health::Ok, L"The Linux boot loader is in place (with \"shim\", which lets it start while Secure Boot is on).", L"\\EFI\\ubuntu\\shimx64.efi"),
        link("bd_list", N(L"GRUB settings"), Health::Ok, L"The boot loader looks for Linux on the right partition (Disk 1, partition 3).", L"search --fs-uuid 6f1c2a4e-93b0-4d7e-8a51-2c0f7b3e9d12"),
        link("drive", N(L"Linux partition"), Health::Ok, L"The partition with Linux is there and this program can look inside it.", L"Disk 1, partition 3 (69.0 GB, ext4)"),
        link("file_app", N(L"Linux kernel"), Health::Ok, L"The Linux kernel is in place, plus 1 older one(s) to fall back on.", L"6.8.0-45-generic, 6.8.0-41-generic"),
        link("bd_list", N(L"GRUB menu"), Health::Ok, L"The menu file the boot loader shows (with the Linux kernels to choose from) is in place.", L"/boot/grub/grub.cfg"),
        link("settings", N(L"Linux settings"), Health::Ok, L"Every partition Linux expects to use (in /etc/fstab) is there.", L"/  UUID=6f1c2a4e-...\n/boot/efi  UUID=1A2B-3C4D"),
    };
    r.installs = {a, b, c, u};

    Finding usb;
    usb.health = Health::Warn;
    usb.title = r.m.disks[2].name();
    usb.text = r.m.disks[2].problems[0];
    Fix g = makeFix(FX_FIX_GPT);
    g.disk = 2;
    usb.fixes.push_back(g);
    usb.lesson = T(kLessonPartitionTable);
    Finding orphan;
    orphan.health = Health::Warn;
    orphan.title = T(L"A start-up menu entry leads nowhere: \"Windows 7\"");
    orphan.text = T(L"The start-up menu on Disk 0, partition 1 lists \"Windows 7\", but the partition it points to is not on any disk now. If someone picks it in the start-up menu, the PC shows an error. It also makes the menu confusing.");
    orphan.lesson = T(kLessonMenuEntry);
    Fix x1 = makeFix(FX_DELETE_BCD_ENTRY), x2 = makeFix(FX_REMOVE_FROM_MENU);
    x1.store = x2.store = 0;
    x1.entry = x2.entry = L"{demo-win7}";
    orphan.fixes = {x1, x2};
    Finding fw;
    fw.health = Health::Warn;
    fw.title = T(L"The PC's start-up list has an entry that leads nowhere: \"fedora\"");
    fw.text = T(L"\"fedora\" (Fedora (Linux)) is in the start-up list kept inside the PC, but its start-up file (\\EFI\\fedora\\shimx64.efi) is no longer on Disk 0, partition 1. The system it used to start (Fedora (Linux)) was probably deleted or uninstalled, but its line stayed behind. The PC tries it every time it starts, which can slow starting down or show an error before Windows appears.\n\nFor a technician: Boot0003, \\EFI\\fedora\\shimx64.efi");
    fw.lesson = T(kLessonFirmwareList);
    Fix x3 = makeFix(FX_DELETE_FW_ENTRY);
    x3.fwNum = 3;
    fw.fixes = {x3};
    r.pc = {fw, orphan, usb};
    for (Install& in : r.installs) {
        applyLessons(in);
        addCauses(in);
    }
    buildPcChain(r);
    r.env.winPE = true;
    fillEnvironmentText(r.env);
    return r;
}

std::vector<SessionInfo> demoSessions() {
    std::vector<SessionInfo> out;
    auto act = [](const wstr& time, const wstr& title, const wstr& target, const wstr& result, bool ok, bool canUndo, bool undone) {
        ActionInfo a;
        a.time = time;
        a.title = title;
        a.target = target;
        a.result = result;
        a.ok = ok;
        a.finished = true;
        a.canUndo = canUndo;
        a.undone = undone;
        return a;
    };
    SessionInfo s1;
    s1.name = L"2026-11-12 14:03";
    s1.host = T(L"Windows PE (build 26100)");
    s1.bytes = 412 * 1024;
    s1.actions = {act(L"2026-11-12 14:03:58", T(L"Start-up setup when the session began"), T(L"All disks"),
                      T(L"Saved 1 start-up settings file and the partition tables of every disk."), true, true, false),
                  act(L"2026-11-12 14:06:12", T(L"Undo unfinished updates"), T(L"Windows 10 (64-bit) in D:\\Windows"), T(L"The unfinished updates were undone."), true, false, false),
                  act(L"2026-11-12 14:09:40", T(L"Add it to the start-up menu"), T(L"Windows 10 (64-bit) in D:\\Windows"), T(L"This Windows was added to the start-up menu."), true, true, false)};
    SessionInfo s2;
    s2.name = L"2026-11-10 09:41";
    s2.host = T(L"Windows 11 (build 26100)");
    s2.bytes = 188 * 1024;
    s2.actions = {act(L"2026-11-10 09:41:20", T(L"Start-up setup when the session began"), T(L"All disks"), T(L"Saved 1 start-up settings file and the partition tables of every disk."), true, true, false),
                  act(L"2026-11-10 09:44:02", T(L"Make it start first"), T(L"Windows 11 (64-bit) in C:\\Windows"), T(L"This Windows now starts first."), true, true, true)};
    out = {s1, s2};
    return out;
}

}  // namespace bd

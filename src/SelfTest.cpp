#include "SelfTest.h"
#include "Diagnose.h"
#include "Backup.h"
#include "Hive.h"
#include "Demo.h"
#include "Lang.h"
#include "Linux.h"
#include "Keys.h"
#include "Recover.h"
#include "Space.h"
#include "Suggest.h"
#include "Repair.h"
#include "Layout.h"
#include "Migrate.h"
#include "Rescue.h"
#include "Update.h"
#include "WinRepair.h"
#include "EfiCheck.h"
#include "Accounts.h"
#include "Capture.h"
#include "SecureBoot.h"
#include "StorageDrivers.h"
#include "HardwareInfo.h"
#include "Drivers.h"
#include "Pdf.h"
#include "DiskTool.h"
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

void makeDirs(const wstr& path) {
    for (size_t p = path.find(L'\\', 3); p != wstr::npos; p = path.find(L'\\', p + 1)) CreateDirectoryW(path.substr(0, p).c_str(), nullptr);
    CreateDirectoryW(path.c_str(), nullptr);
}

bool put(const wstr& path, const std::string& data) {
    size_t s = path.find_last_of(L'\\');
    if (s != wstr::npos) makeDirs(path.substr(0, s));
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    BOOL ok = data.empty() || WriteFile(h, data.data(), (DWORD)data.size(), &wr, nullptr);
    CloseHandle(h);
    return ok != 0;
}

std::string get(const wstr& path) {
    std::string s;
    readFileText(path, s);
    return s;
}

// A tiny x64 program header: enough for "looks like a program" and the architecture check.
std::string program() {
    std::string s(512, '\0');
    s[0] = 'M';
    s[1] = 'Z';
    s[0x3C] = (char)0x80;
    memcpy(&s[0x80], "PE\0\0", 4);
    s[0x84] = (char)0x64;
    s[0x85] = (char)0x86;
    return s;
}

std::string hiveHead(bool goodSum, uint32_t seq1, uint32_t seq2) {
    std::string h(4096, '\0');
    memcpy(&h[0], "regf", 4);
    memcpy(&h[4], &seq1, 4);
    memcpy(&h[8], &seq2, 4);
    uint32_t sum = 0;
    for (int i = 0; i < 127; ++i) {
        uint32_t v;
        memcpy(&v, &h[i * 4], 4);
        sum ^= v;
    }
    if (!goodSum) sum ^= 0x1234;
    memcpy(&h[0x1FC], &sum, 4);
    return h;
}

void fakeWindows(const wstr& winDir, bool withWinload, bool uefi) {
    std::string p = program();
    for (const wchar_t* f : {L"ntoskrnl.exe", L"hal.dll", L"ci.dll", L"drivers\\ntfs.sys", L"drivers\\disk.sys", L"drivers\\classpnp.sys",
                             L"drivers\\volmgr.sys", L"drivers\\partmgr.sys", L"drivers\\pci.sys", L"drivers\\acpi.sys"})
        put(winDir + L"\\System32\\" + f, p);
    if (withWinload) put(winDir + (uefi ? L"\\System32\\winload.efi" : L"\\System32\\winload.exe"), p);
    put(winDir + L"\\System32\\config\\SYSTEM", hiveHead(true, 5, 5));
    put(winDir + L"\\System32\\config\\SOFTWARE", hiveHead(true, 9, 9));
    put(winDir + L"\\Boot\\EFI\\bootmgfw.efi", p);
    put(winDir + L"\\Boot\\PCAT\\bootmgr", p);
}

const Link* linkNamed(const Install& in, const wchar_t* name) {
    for (const Link& l : in.chain)
        if (l.name == name) return &l;
    return nullptr;
}

bool hasFix(const Install& in, int kind) {
    for (const Fix& f : in.plan)
        if (f.kind == kind) return true;
    return false;
}

}  // namespace

int runSelfTest(const wstr& folderIn, const wchar_t* appName, const wchar_t* version) {
    wstr folder = folderIn;
    while (!folder.empty() && (folder.back() == L'\\' || folder.back() == L'/')) folder.pop_back();
    wchar_t full[MAX_PATH];
    GetFullPathNameW(folder.c_str(), MAX_PATH, full, nullptr);
    folder = full;
    wstr work = folder + L"\\selftest-work";
    deleteFolderTree(work);
    makeDirs(work);
    Log log;
    log.line(wstr(appName) + L" " + version + L" self-test on " + g_sys.describe());

    log.line(L"Basics");
    log.check(crc32("123456789", 9) == 0xCBF43926u, L"CRC32 of \"123456789\"");
    GUID g;
    log.check(parseGuid(L"{C12A7328-F81F-11D2-BA4B-00A0C93EC93B}", g) && guidText(g) == L"{C12A7328-F81F-11D2-BA4B-00A0C93EC93B}", L"GUID text round trip");
    log.check(sizeText(500ull << 30) == L"500 GB" && sizeText(100ull << 20) == L"100 MB", L"Sizes in plain units");
    log.check(osName(10, 0, 26100) == L"Windows 11" && osName(10, 0, 19045) == L"Windows 10" && osName(6, 1, 7601) == L"Windows 7", L"Windows names from versions");
    log.check(worse(Health::Ok, Health::Bad) == Health::Bad && worse(Health::Warn, Health::Info) == Health::Warn, L"Worst health wins");

    log.line(L"File systems");
    std::string s(4096, '\0');
    memcpy(&s[3], "NTFS    ", 8);
    log.check(sniffFs(s) == L"NTFS", L"NTFS boot sector");
    s.assign(4096, '\0');
    memcpy(&s[0x52], "FAT32   ", 8);
    log.check(sniffFs(s) == L"FAT32", L"FAT32 boot sector");
    s.assign(4096, '\0');
    memcpy(&s[3], "-FVE-FS-", 8);
    log.check(sniffFs(s) == L"BitLocker", L"BitLocker volume");
    s.assign(4096, '\0');
    log.check(sniffFs(s).empty(), L"Blank sector has no file system");

    log.line(L"Registry files");
    wstr why;
    log.check(hiveBytesHealth(hiveHead(true, 3, 3), why) == Health::Ok, L"Clean registry file");
    log.check(hiveBytesHealth(hiveHead(true, 3, 4), why) == Health::Warn, L"Registry file not saved cleanly");
    log.check(hiveBytesHealth(hiveHead(false, 3, 3), why) == Health::Bad, L"Registry file with a broken header");
    log.check(hiveBytesHealth(std::string(4096, 'x'), why) == Health::Bad, L"Not a registry file");

    log.line(L"EFI partition check");
    {
        // A tiny FAT32 image built in memory: boot sector + backup, two FAT copies, a directory tree
        // (\EFI\Microsoft\Boot\{bootmgfw.efi,BCD} and \EFI\Boot\bootx64.efi), and the data clusters.
        struct Fat32 {
            enum : uint32_t { bps = 512, spc = 1, rsc = 32, nFat = 2, fatSz = 1, totalSec = 98, rootClus = 2 };
            static uint32_t countClus() { return (totalSec - (rsc + nFat * fatSz)) / spc; }  // 64
            std::vector<uint32_t> fat;
            std::vector<std::string> data;
            uint32_t nextFree = 3;  // cluster 2 is kept for the root directory
            Fat32() : fat(66, 0), data(66) { fat[0] = 0x0FFFFF00; fat[1] = 0x0FFFFFFF; }
            uint32_t alloc(std::string content) {
                uint32_t nc = std::max<uint32_t>(1, (uint32_t)((content.size() + bps - 1) / bps));
                content.resize((size_t)nc * bps, '\0');
                uint32_t first = nextFree, prev = 0;
                for (uint32_t i = 0; i < nc; ++i) {
                    uint32_t c = nextFree++;
                    data[c] = content.substr((size_t)i * bps, bps);
                    if (prev) fat[prev] = c;
                    prev = c;
                }
                fat[prev] = 0x0FFFFFFF;
                return first;
            }
            static void p16(std::string& e, size_t o, uint16_t v) { e[o] = (char)(v & 0xFF); e[o + 1] = (char)(v >> 8); }
            static void p32(std::string& e, size_t o, uint32_t v) { p16(e, o, (uint16_t)v); p16(e, o + 2, (uint16_t)(v >> 16)); }
            static std::string shortEntry(const char* n8, const char* ext3, uint8_t attr, uint32_t clus, uint32_t size) {
                std::string e(32, '\0'), nm = n8, ex = ext3;
                nm.resize(8, ' ');
                ex.resize(3, ' ');
                for (int i = 0; i < 8; ++i) e[i] = nm[i];
                for (int i = 0; i < 3; ++i) e[8 + i] = ex[i];
                e[0x0B] = (char)attr;
                p16(e, 0x14, (uint16_t)(clus >> 16));
                p16(e, 0x1A, (uint16_t)(clus & 0xFFFF));
                p32(e, 0x1C, size);
                return e;
            }
            static std::string lfnEntry(const std::wstring& name) {
                std::string e(32, '\0');
                e[0] = 0x41;     // sequence 1, marked last
                e[0x0B] = 0x0F;  // long-name attribute
                size_t idx = 0;
                auto emit = [&](size_t o, int n) {
                    for (int i = 0; i < n; ++i) {
                        uint16_t ch = idx < name.size() ? (uint16_t)name[idx] : (idx == name.size() ? 0 : 0xFFFF);
                        p16(e, o + i * 2, ch);
                        ++idx;
                    }
                };
                emit(0x01, 5);
                emit(0x0E, 6);
                emit(0x1C, 2);
                return e;
            }
            std::string image() const {
                std::string img((size_t)totalSec * bps, '\0'), boot(512, '\0');
                p16(boot, 0x0B, (uint16_t)bps);
                boot[0x0D] = (char)spc;
                p16(boot, 0x0E, (uint16_t)rsc);
                boot[0x10] = (char)nFat;
                p32(boot, 0x20, totalSec);
                p32(boot, 0x24, fatSz);
                p32(boot, 0x2C, rootClus);
                p16(boot, 0x30, 1);
                p16(boot, 0x32, 6);
                memcpy(&boot[0x52], "FAT32   ", 8);
                boot[0x1FE] = (char)0x55;
                boot[0x1FF] = (char)0xAA;
                memcpy(&img[0], boot.data(), 512);
                memcpy(&img[(size_t)6 * bps], boot.data(), 512);
                std::string fatb((size_t)fatSz * bps, '\0');
                for (size_t c = 0; c < fat.size(); ++c) p32(fatb, c * 4, fat[c]);
                memcpy(&img[(size_t)rsc * bps], fatb.data(), fatb.size());
                memcpy(&img[(size_t)(rsc + fatSz) * bps], fatb.data(), fatb.size());
                uint32_t dataSec = rsc + nFat * fatSz;
                for (uint32_t c = 2; c < data.size(); ++c)
                    if (!data[c].empty())
                        memcpy(&img[((size_t)dataSec + (size_t)(c - 2) * spc) * bps], data[c].data(), std::min<size_t>(data[c].size(), bps));
                return img;
            }
        };
        std::string pe = program(), bcd = hiveHead(true, 3, 3);
        auto build = [&](Fat32& fs) -> uint32_t {  // returns bootmgfw.efi's first cluster
            uint32_t cBmgr = fs.alloc(pe), cBcd = fs.alloc(bcd);
            uint32_t cMsBoot = fs.alloc(Fat32::shortEntry("BOOTMGFW", "EFI", 0x20, cBmgr, (uint32_t)pe.size()) +
                                        Fat32::shortEntry("BCD", "", 0x20, cBcd, (uint32_t)bcd.size()));
            uint32_t cBx64 = fs.alloc(pe);
            uint32_t cEfiBoot = fs.alloc(Fat32::shortEntry("BOOTX64", "EFI", 0x20, cBx64, (uint32_t)pe.size()));
            uint32_t cMs = fs.alloc(Fat32::shortEntry("BOOT", "", 0x10, cMsBoot, 0));
            uint32_t cEfi = fs.alloc(Fat32::lfnEntry(L"Microsoft") + Fat32::shortEntry("MICROS~1", "", 0x10, cMs, 0) +
                                     Fat32::shortEntry("BOOT", "", 0x10, cEfiBoot, 0));
            fs.data[2] = Fat32::shortEntry("EFI", "", 0x10, cEfi, 0);
            fs.data[2].resize(Fat32::bps, '\0');
            fs.fat[2] = 0x0FFFFFFF;
            return cBmgr;
        };
        auto reader = [](const std::string& img) {
            return ReadFn([&img](uint64_t o, uint32_t n, std::string& out) {
                if (o + n > img.size()) return false;
                out = img.substr((size_t)o, n);
                return true;
            });
        };
        {
            Fat32 fs;
            build(fs);
            std::string img = fs.image();
            EspCheck c = checkEsp(reader(img), img.size());
            log.check(c.ran && c.fat32 && c.fault == EspFault::None, L"Sound EFI partition passes the check");
        }
        {
            Fat32 fs;
            build(fs);
            std::string img = fs.image();
            img[(size_t)(Fat32::rsc + Fat32::fatSz) * Fat32::bps + 8] ^= (char)0xFF;  // damage the second FAT copy
            EspCheck c = checkEsp(reader(img), img.size());
            log.check(c.fault == EspFault::FatCopies, L"A mismatched second FAT copy is caught");
        }
        {
            Fat32 fs;
            uint32_t cBmgr = build(fs);
            fs.fat[cBmgr] = 9000;  // point bootmgfw.efi's chain off the end (both FAT copies agree)
            std::string img = fs.image();
            EspCheck c = checkEsp(reader(img), img.size());
            log.check(c.fault == EspFault::ClusterChain, L"A broken cluster chain is caught");
        }
    }

    log.line(L"Start-up settings devices");
    {
        Machine m;
        Disk d0, d1;
        d0.number = 0;
        d0.style = 2;
        d1.number = 1;
        d1.style = 1;
        d1.signature = 0xA1B2C3D4;
        Partition gp, mp;
        gp.disk = 0;
        gp.gpt = true;
        parseGuid(L"{11111111-2222-3333-4444-555566667777}", gp.id);
        mp.disk = 1;
        mp.offset = 1048576;
        m.parts = {gp, mp};
        m.disks = {d0, d1};
        std::string blob(88, '\0');
        memcpy(&blob[0x20], &gp.id, 16);
        log.check(matchDevice(blob, m) == 0, L"GPT partition found in a device element");
        blob.assign(88, '\0');
        uint64_t off = 1048576;
        memcpy(&blob[0x20], &off, 8);
        memcpy(&blob[0x38], &d1.signature, 4);
        log.check(matchDevice(blob, m) == 1, L"MBR partition found by disk signature and offset");
        blob.assign(88, '\0');
        log.check(matchDevice(blob, m) == -1, L"Missing partition is reported as missing");
        std::string wim;
        for (wchar_t c : wstr(L"\\sources\\boot.wim")) {
            wim += (char)(c & 255);
            wim += (char)(c >> 8);
        }
        log.check(deviceIsFile(std::string(40, '\0') + wim), L"A .wim device is recognised as a file");
    }

    log.line(L"Finding Windows");
    wstr esp = work + L"\\esp\\", win = work + L"\\c\\", data = work + L"\\d\\";
    makeDirs(esp);
    fakeWindows(win + L"Windows", true, true);
    fakeWindows(data + L"Backup\\Old PC\\Windows", true, true);
    fakeWindows(data + L"Windows.old\\Windows", true, true);
    put(esp + L"EFI\\Microsoft\\Boot\\bootmgfw.efi", program());
    {
        std::vector<Install> found;
        findInstalls(win, 2, found);
        findInstalls(data, 3, found);
        log.check(found.size() == 3, L"Three installations found (" + std::to_wstring(found.size()) + L")");
        int root = 0, moved = 0, old = 0;
        for (const Install& i : found) {
            root += i.atRoot;
            moved += !i.atRoot && !i.windowsOld;
            old += i.windowsOld;
        }
        log.check(root == 1 && moved == 1 && old == 1, L"One at the top, one moved into a folder, one Windows.old");
        for (const Install& i : found)
            if (!i.atRoot && !i.windowsOld) log.check(i.systemRoot == L"\\Backup\\Old PC\\Windows", L"Moved Windows keeps its folder: " + i.systemRoot);
        log.check(peArch(win + L"Windows\\System32\\ntoskrnl.exe") == L"x64", L"Architecture read from the program header");
    }

    log.line(L"Boot chain");
    {
        Report r;
        r.m.fw.uefi = true;
        Disk d;
        d.number = 0;
        d.style = 2;
        d.size = 100ull << 30;
        d.readable = d.gptPrimaryOk = d.gptBackupOk = true;
        Partition pe, pw, pd;
        pe.disk = pw.disk = pd.disk = 0;
        pe.number = 1;
        pw.number = 2;
        pd.number = 3;
        pe.role = R_ESP;
        pw.role = pd.role = R_DATA;
        pe.gpt = pw.gpt = pd.gpt = true;
        pe.fs = L"FAT32";
        pw.fs = pd.fs = L"NTFS";
        pe.readable = pw.readable = pd.readable = true;
        pe.root = esp;
        pw.root = win;
        pd.root = data;
        pe.freeBytes = 60ull << 20;
        pw.freeBytes = pd.freeBytes = 20ull << 30;
        pw.totalBytes = pd.totalBytes = 40ull << 30;
        pe.length = 100ull << 20;
        pw.length = pd.length = 40ull << 30;
        d.parts = {0, 1, 2};
        r.m.parts = {pe, pw, pd};
        r.m.disks = {d};
        findInstalls(win, 1, r.installs);
        findInstalls(data, 2, r.installs);
        diagnose(r);
        const Install* main = nullptr;
        for (const Install& i : r.installs)
            if (i.atRoot) main = &i;
        log.check(main != nullptr, L"The installation at the top was diagnosed");
        if (main) {
            const Link* sp = linkNamed(*main, L"Start-up partition");
            const Link* bm = linkNamed(*main, L"Boot manager");
            const Link* st = linkNamed(*main, L"Start-up settings");
            log.check(main->chain.size() == 11, L"Eleven steps in the chain");
            log.check(sp && sp->health == Health::Ok, L"EFI partition found and readable");
            log.check(bm && bm->health == Health::Ok, L"Boot manager found");
            log.check(st && st->health == Health::Bad, L"Missing start-up settings file is a problem");
            log.check(main->verdict == Health::Bad, L"Verdict: will not start");
            log.check(hasFix(*main, FX_REBUILD_BOOT), L"Plan: rebuild the start-up files");
            log.check(main->sysPart == 0, L"The EFI partition is the planned start-up partition");
        }
        const Install* moved = nullptr;
        for (const Install& i : r.installs)
            if (!i.atRoot && !i.windowsOld) moved = &i;
        log.check(moved && moved->verdict == Health::Bad && hasFix(*moved, FX_MOVE_TO_ROOT), L"Moved Windows: plan to move it back");
        bool foundNoWin = false;
        for (const Finding& f : r.pc) foundNoWin |= f.title == L"No Windows was found";
        log.check(!foundNoWin, L"No false \"no Windows\" finding");

        // Missing loader, missing EFI partition
        DeleteFileW((win + L"Windows\\System32\\winload.efi").c_str());
        Report r2 = r;
        r2.installs.clear();
        r2.pc.clear();
        r2.m.parts[0].role = R_DATA;  // no EFI partition any more
        findInstalls(win, 1, r2.installs);
        diagnose(r2);
        if (!r2.installs.empty()) {
            const Install& i = r2.installs[0];
            const Link* wl = linkNamed(i, L"Windows loader");
            const Link* sp = linkNamed(i, L"Start-up partition");
            log.check(wl && wl->health == Health::Bad, L"Missing Windows loader is a problem");
            log.check(sp && sp->health == Health::Bad, L"Missing EFI partition is a problem");
            log.check(hasFix(i, FX_CREATE_SYSPART) && hasFix(i, FX_SFC), L"Plan: create a start-up partition and repair system files");
            bool order = false;
            for (size_t k = 0; k + 1 < i.plan.size(); ++k)
                if (i.plan[k].kind == FX_CREATE_SYSPART) order = true;
            log.check(!hasFix(i, FX_REBUILD_BOOT), L"Creating the partition replaces the separate rebuild");
            (void)order;
        }
        // Legacy BIOS without an active partition
        Report r3 = r;
        r3.installs.clear();
        r3.pc.clear();
        r3.m.fw.uefi = false;
        r3.m.disks[0].style = 1;
        r3.m.disks[0].bootCode = true;
        for (Partition& p : r3.m.parts) p.gpt = false;
        put(win + L"Windows\\System32\\winload.exe", program());
        findInstalls(win, 1, r3.installs);
        diagnose(r3);
        if (!r3.installs.empty()) {
            const Link* sp = linkNamed(r3.installs[0], L"Start-up partition");
            log.check(sp && sp->health == Health::Bad, L"Legacy: no active partition is a problem");
            log.check(hasFix(r3.installs[0], FX_MARK_ACTIVE), L"Legacy: plan marks a partition active");
        }
        // Restart loop signs
        put(win + L"Windows\\System32\\LogFiles\\Srt\\SrtTrail.txt", "Boot critical file is corrupt");
        put(win + L"Windows\\Minidump\\a.dmp", "x");
        put(win + L"Windows\\Minidump\\b.dmp", "x");
        put(win + L"Windows\\WinSxS\\pending.xml", "<xml/>");
        Report r4 = r;
        r4.installs.clear();
        r4.pc.clear();
        findInstalls(win, 1, r4.installs);
        diagnose(r4);
        if (!r4.installs.empty()) {
            const Install& i = r4.installs[0];
            log.check(i.loop, L"Restart loop detected from repair log, crashes and pending updates");
            log.check(i.loopSigns.size() >= 3, L"Loop signs explained (" + std::to_wstring(i.loopSigns.size()) + L")");
            log.check(hasFix(i, FX_REVERT_UPDATES), L"Plan: undo unfinished updates");
        }
        // Broken registry with a backup copy
        put(win + L"Windows\\System32\\config\\SYSTEM", std::string(8192, 'z'));
        put(win + L"Windows\\System32\\config\\RegBack\\SYSTEM", hiveHead(true, 2, 2) + std::string(4096, '\0'));
        Report r5 = r;
        r5.installs.clear();
        r5.pc.clear();
        findInstalls(win, 1, r5.installs);
        diagnose(r5);
        if (!r5.installs.empty()) {
            const Link* reg = linkNamed(r5.installs[0], L"System settings");
            log.check(reg && reg->health == Health::Bad, L"Damaged registry file is a problem");
            log.check(hasFix(r5.installs[0], FX_RESTORE_HIVE), L"Plan: put back the registry's backup copy");
        }
        wstr text = reportText(r5);
        log.check(text.find(L"What it will take") != wstr::npos && text.find(L"SYSTEMS FOUND") != wstr::npos, L"Report text has every section");
    }

    log.line(L"Start-up entries that lead nowhere");
    {
        Report r;
        r.m.fw.uefi = r.m.fw.varsReadable = true;
        Partition pe, pw;
        pe.disk = pw.disk = 0;
        pe.number = 1;
        pw.number = 2;
        pe.gpt = pw.gpt = true;
        pe.role = R_ESP;
        pw.role = R_DATA;
        pe.readable = pw.readable = true;
        pe.root = esp;
        pw.root = win;
        parseGuid(L"{AAAAAAAA-0000-0000-0000-000000000001}", pe.id);
        parseGuid(L"{AAAAAAAA-0000-0000-0000-000000000002}", pw.id);
        r.m.parts = {pe, pw};
        auto fwe = [&](int num, const wchar_t* desc, const wchar_t* path, const wchar_t* guid) {
            FirmwareEntry e;
            e.num = num;
            e.description = desc;
            e.path = path;
            e.inOrder = true;
            if (guid) {
                e.hasPart = true;
                parseGuid(guid, e.part);
            }
            return e;
        };
        r.m.fw.entries = {fwe(0, L"Windows Boot Manager", L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi", L"{AAAAAAAA-0000-0000-0000-000000000001}"),
                          fwe(1, L"ubuntu", L"\\EFI\\ubuntu\\shimx64.efi", L"{AAAAAAAA-0000-0000-0000-000000000001}"),
                          fwe(2, L"Old disk", L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi", L"{BBBBBBBB-0000-0000-0000-000000000009}"),
                          fwe(3, L"UEFI: Network", L"", nullptr)};
        BcdStore st;
        st.part = 0;
        st.loaded = true;
        auto ent = [&](const wchar_t* id, const wchar_t* desc, int osPart, const wchar_t* root, bool menu) {
            BcdEntry e;
            e.id = id;
            e.app = APP_LOADER;
            e.description = desc;
            e.hasOsDevice = true;
            e.osPart = osPart;
            e.systemRoot = root;
            e.inMenu = menu;
            return e;
        };
        st.entries = {ent(L"{good}", L"Windows 11", 1, L"\\Windows", true), ent(L"{gone}", L"Windows 10", 1, L"\\OldWindows", true),
                     ent(L"{nodisk}", L"Windows 7", -1, L"\\Windows", false)};
        st.order = {L"{good}", L"{gone}", L"{missing}"};
        r.stores = {st};
        findDeadEntries(r);
        int fwDead = 0, bcdDead = 0, menuOnly = 0;
        bool ubuntuNamed = false;
        for (const Finding& f : r.pc) {
            for (const Fix& x : f.fixes) {
                if (x.kind == FX_DELETE_FW_ENTRY) {
                    ++fwDead;
                    ubuntuNamed |= f.text.find(L"Ubuntu (Linux)") != wstr::npos;
                }
                if (x.kind == FX_DELETE_BCD_ENTRY) ++bcdDead;
            }
            if (f.fixes.size() == 1 && f.fixes[0].kind == FX_REMOVE_FROM_MENU) ++menuOnly;
            log.check(!f.lesson.empty(), L"Explained in plain words: " + f.title);
        }
        log.check(fwDead == 2, L"Two dead entries in the PC's own list: deleted Linux and a missing disk (" + std::to_wstring(fwDead) + L")");
        log.check(ubuntuNamed, L"The dead Linux entry is named as Ubuntu (Linux)");
        log.check(bcdDead == 2, L"Two dead start-up menu entries: Windows gone from its folder, and partition gone (" + std::to_wstring(bcdDead) + L")");
        log.check(menuOnly == 1, L"A menu line with no settings behind it is found");
        log.check(systemKindFromPath(L"\\EFI\\fedora\\shimx64.efi") == L"Fedora (Linux)" && systemKindFromPath(L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi") == L"Windows",
                  L"Systems named from their start-up file");
        log.check(!lessonFor(L"Start-up settings").empty() && !lessonFor(L"PC firmware").empty() && !lessonFor(L"Starting up").empty(), L"Every step has a lesson");

        // Start-up settings left empty (their last entry removed) are a leftover, not damage, and can be removed.
        Report r2;
        r2.m.parts = r.m.parts;
        BcdStore empty;
        empty.part = 0;
        empty.loaded = empty.hasBootmgr = empty.empty = true;
        empty.health = Health::Warn;
        empty.file = L"X:\\EFI\\Microsoft\\Boot\\BCD";
        r2.stores = {empty};
        findStoreProblems(r2);
        bool leftover = r2.pc.size() == 1 && r2.pc[0].title.find(L"Leftover Windows start-up files") == 0 && r2.pc[0].health == Health::Info &&
                        r2.pc[0].fixes.size() == 1 && r2.pc[0].fixes[0].kind == FX_DELETE_EFI_DIR && r2.pc[0].fixes[0].arg == L"EFI\\Microsoft" && r2.pc[0].fixes[0].part == 0;
        log.check(leftover, L"Empty start-up settings nobody uses: called a leftover, with a way to remove them");
        Install uses;
        uses.part = 1;
        uses.sysPart = 0;
        uses.name = L"Windows 11";
        r2.installs = {uses};
        r2.pc.clear();
        findStoreProblems(r2);
        log.check(r2.pc.size() == 1 && r2.pc[0].fixes.empty() && r2.pc[0].text.find(L"Rebuild the start-up files") != wstr::npos,
                  L"Empty start-up settings a Windows needs: never offered for removal, pointed to the rebuild");
        r2.stores[0].live = true;
        r2.installs.clear();
        r2.pc.clear();
        findStoreProblems(r2);
        log.check(r2.pc.size() == 1 && r2.pc[0].fixes.empty(), L"The running system's own start-up files are never offered for removal");
        BcdStore broken;
        broken.part = 0;
        broken.health = Health::Bad;
        broken.problem = L"The start-up settings file is damaged: test";
        broken.file = L"X:\\EFI\\Microsoft\\Boot\\BCD";
        r2.stores = {broken};
        r2.pc.clear();
        findStoreProblems(r2);
        log.check(r2.pc.size() == 1 && r2.pc[0].title.find(L"Damaged") == 0 && r2.pc[0].fixes.size() == 1, L"Damaged leftover settings come with a solution too");
        // A dead entry that is the only one in a store nobody uses: offer to remove the whole leftover first.
        Report r3;
        r3.m.parts = r.m.parts;
        BcdStore lone;
        lone.part = 0;
        lone.loaded = lone.hasBootmgr = true;
        lone.file = L"X:\\EFI\\Microsoft\\Boot\\BCD";
        lone.entries = {ent(L"{nodisk}", L"Windows 10", -1, L"\\Windows", true)};
        lone.order = {L"{nodisk}"};
        r3.stores = {lone};
        findDeadEntries(r3);
        bool whole = false;
        for (const Finding& f : r3.pc)
            if (f.fixes.size() >= 2 && f.fixes[0].kind == FX_DELETE_EFI_DIR && f.fixes[1].kind == FX_DELETE_BCD_ENTRY) whole = true;
        log.check(whole, L"The last entry of unused start-up settings: removing all the leftover files is offered first");
    }

    log.line(L"Linux");
    {
        // A tiny ext4-style file system in memory: 1 KB blocks, one group, extents and block maps.
        struct Img {
            std::string b = std::string(128 * 1024, '\0');
            uint32_t next = 12;
            void p16(size_t at, uint32_t v) { b[at] = (char)(v & 255); b[at + 1] = (char)((v >> 8) & 255); }
            void p32(size_t at, uint32_t v) { p16(at, v & 0xFFFF); p16(at + 2, v >> 16); }
            Img() {
                size_t s = 1024;
                p32(s + 0x00, 64); p32(s + 0x04, 128); p32(s + 0x14, 1); p32(s + 0x18, 0); p32(s + 0x20, 8192); p32(s + 0x28, 64);
                p16(s + 0x38, 0xEF53); p32(s + 0x4C, 1); p32(s + 0x54, 11); p16(s + 0x58, 128); p32(s + 0x60, 0x42);
                for (int i = 0; i < 16; ++i) b[s + 0x68 + i] = (char)(0x10 + i);
                p32(2048 + 8, 3);  // inode table at block 3
            }
            void inode(uint32_t ino, uint32_t mode, size_t size, uint32_t flags, const std::string& ib) {
                size_t at = 3 * 1024 + (ino - 1) * 128;
                p16(at, mode); p32(at + 4, (uint32_t)size); p32(at + 0x20, flags);
                memcpy(&b[at + 0x28], ib.data(), std::min<size_t>(60, ib.size()));
            }
            uint32_t put(const std::string& d) {
                uint32_t n = (uint32_t)std::max<size_t>(1, (d.size() + 1023) / 1024), start = next;
                next += n;
                memcpy(&b[start * 1024], d.data(), d.size());
                return start;
            }
            std::string map(uint32_t start, uint32_t n) {
                std::string s(60, '\0');
                for (uint32_t i = 0; i < n && i < 12; ++i) { uint32_t v = start + i; memcpy(&s[i * 4], &v, 4); }
                return s;
            }
            std::string ext(uint32_t start, uint32_t n) {
                std::string s(60, '\0');
                uint16_t h[] = {0xF30A, 1, 4, 0};
                memcpy(&s[0], h, 8);
                uint32_t lb = 0;
                uint16_t len = (uint16_t)n, hi = 0;
                memcpy(&s[12], &lb, 4); memcpy(&s[16], &len, 2); memcpy(&s[18], &hi, 2); memcpy(&s[20], &start, 4);
                return s;
            }
            void file(uint32_t ino, const std::string& d, bool extents) {
                uint32_t n = (uint32_t)std::max<size_t>(1, (d.size() + 1023) / 1024), start = put(d);
                inode(ino, 0x81A4, d.size(), extents ? 0x80000 : 0, extents ? ext(start, n) : map(start, n));
            }
            void dir(uint32_t ino, const std::vector<std::pair<std::string, uint32_t>>& e) {
                std::string blk(1024, '\0');
                size_t pos = 0;
                for (size_t i = 0; i < e.size(); ++i) {
                    uint16_t rec = (uint16_t)(i + 1 == e.size() ? 1024 - pos : ((8 + e[i].first.size() + 3) & ~(size_t)3));
                    memcpy(&blk[pos], &e[i].second, 4);
                    memcpy(&blk[pos + 4], &rec, 2);
                    blk[pos + 6] = (char)e[i].first.size();
                    memcpy(&blk[pos + 8], e[i].first.data(), e[i].first.size());
                    pos += rec;
                }
                inode(ino, 0x41ED, 1024, 0, map(put(blk), 1));
            }
            void link(uint32_t ino, const std::string& target) { inode(ino, 0xA1FF, target.size(), 0, target + std::string(60 - target.size(), '\0')); }
        } img;
        wstr linuxId = L"10111213-1415-1617-1819-1a1b1c1d1e1f";
        img.dir(2, {{".", 2}, {"..", 2}, {"etc", 12}, {"boot", 13}, {"usr", 14}});
        img.dir(12, {{".", 12}, {"..", 2}, {"os-release", 17}, {"fstab", 18}});
        img.dir(14, {{".", 14}, {"..", 2}, {"lib", 15}});
        img.dir(15, {{".", 15}, {"..", 14}, {"os-release", 16}});
        img.dir(13, {{".", 13}, {"..", 2}, {"vmlinuz-6.8.0-45-generic", 19}, {"grub", 20}});
        img.dir(20, {{".", 20}, {"..", 13}, {"grub.cfg", 21}});
        img.file(16, "NAME=\"Ubuntu\"\nPRETTY_NAME=\"Ubuntu 24.04 LTS\"\nID=ubuntu\n", true);
        img.link(17, "../usr/lib/os-release");
        img.file(18, "# fstab\nUUID=" + wideToUtf8(linuxId) + " / ext4 defaults 0 1\nUUID=AAAA-BBBB /boot/efi vfat umask=0077 0 1\n"
                     "UUID=deadbeef-0000-0000-0000-000000000000 /home ext4 defaults 0 2\n", false);
        img.file(19, std::string(3000, 'k'), false);
        img.file(21, "menuentry 'Ubuntu' {}\n", true);
        ReadFn mem = [&img](uint64_t off, uint32_t n, std::string& out) {
            if (off + n > img.b.size()) return false;
            out = img.b.substr((size_t)off, n);
            return true;
        };
        ExtReader x;
        wstr err;
        log.check(x.open(mem, err), L"ext file system opened");
        std::string osr, fst;
        log.check(x.readFile("/etc/os-release", osr) && parseOsRelease(osr)["PRETTY_NAME"] == "Ubuntu 24.04 LTS", L"os-release read through a symbolic link (extents)");
        std::vector<std::string> boot;
        log.check(x.list("/boot", boot) && std::find(boot.begin(), boot.end(), "vmlinuz-6.8.0-45-generic") != boot.end(), L"Kernel listed in /boot");
        log.check(x.exists("/boot/grub/grub.cfg") && !x.exists("/boot/nothing"), L"Files found, missing files not found");
        log.check(x.readFile("/etc/fstab", fst) && parseFstab(fst).size() == 3, L"fstab read (block map) and parsed");
        log.check(grubUuid("search.fs_uuid 6F1C2A4E-93B0-4D7E-8A51-2C0F7B3E9D12 root\n") == "6f1c2a4e-93b0-4d7e-8a51-2c0f7b3e9d12" &&
                      grubUuid("search --no-floppy --fs-uuid --set=dev abcd-1234\n") == "abcd-1234" && grubPrefix("set prefix=($root)'/boot/grub'") == "/boot/grub",
                  L"GRUB settings parsed");
        std::string sb = img.b.substr(0, 4096);
        log.check(sniffFs(sb) == L"ext4" && fsUuidFrom(L"ext4", sb, std::string()) == linuxId, L"ext4 and its id recognised from the first sector");

        // A PC with Ubuntu (fine apart from a missing /home) and a leftover Fedora boot loader
        wstr esp2 = work + L"\\esp2\\";
        put(esp2 + L"EFI\\ubuntu\\shimx64.efi", program());
        put(esp2 + L"EFI\\ubuntu\\grub.cfg", "search.fs_uuid " + wideToUtf8(linuxId) + " root\nset prefix=($root)'/boot/grub'\nconfigfile $prefix/grub.cfg\n");
        put(esp2 + L"EFI\\fedora\\grubx64.efi", program());
        put(esp2 + L"EFI\\fedora\\grub.cfg", "search --no-floppy --fs-uuid --set=dev 99999999-0000-0000-0000-000000000000\n");
        auto pcWith = [&](bool entry) {
            Report r;
            r.m.fw.uefi = r.m.fw.varsReadable = true;
            Disk d;
            d.number = 0;
            d.style = 2;
            d.readable = d.gptPrimaryOk = d.gptBackupOk = true;
            d.parts = {0, 1};
            r.m.disks = {d};
            Partition e, l;
            e.disk = l.disk = 0;
            e.number = 1;
            l.number = 2;
            e.gpt = l.gpt = true;
            e.role = R_ESP;
            l.role = R_LINUX;
            e.readable = true;
            e.fs = L"FAT32";
            e.root = esp2;
            e.fsUuid = L"aaaa-bbbb";
            l.rawFs = L"ext4";
            l.fsUuid = linuxId;
            parseGuid(L"{CCCCCCCC-0000-0000-0000-000000000001}", e.id);
            r.m.parts = {e, l};
            FirmwareEntry w, u;
            w.num = 0;
            w.description = L"Windows Boot Manager";
            w.path = L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi";
            u.num = 1;
            u.description = L"ubuntu";
            u.path = L"\\EFI\\ubuntu\\shimx64.efi";
            w.hasPart = u.hasPart = w.inOrder = u.inOrder = true;
            w.part = u.part = e.id;
            r.m.fw.entries = entry ? std::vector<FirmwareEntry>{w, u} : std::vector<FirmwareEntry>{w};
            r.m.fw.order = entry ? std::vector<int>{0, 1} : std::vector<int>{0};
            findLinuxWith(r, [&](int i) { return i == 1 ? mem : ReadFn(); });
            diagnose(r);
            return r;
        };
        Report r = pcWith(true);
        const Install* lin = nullptr;
        for (const Install& i : r.installs)
            if (i.linux) lin = &i;
        log.check(lin && lin->name == L"Ubuntu 24.04 LTS", L"Ubuntu found on its ext4 partition");
        if (lin) {
            log.check(lin->kernels.size() == 1 && lin->loader >= 0, L"Its kernel and its boot loader found");
            log.check(lin->chain.size() == 10, L"Ten steps in the Linux chain");
            const Link* gs = linkNamed(*lin, L"GRUB settings");
            const Link* fl = linkNamed(*lin, L"PC's start-up list");
            const Link* ls = linkNamed(*lin, L"Linux settings");
            log.check(gs && gs->health == Health::Ok, L"GRUB points to the right partition");
            log.check(fl && fl->health == Health::Info && hasFix(*lin, FX_FW_FIRST), L"Windows starts first: offer to make Linux start first");
            log.check(ls && ls->health == Health::Warn, L"A missing /home partition is a warning");
            bool usbOff = false;
            for (const Fix& f : lin->plan) usbOff |= f.kind == FX_LINUX_USB && !f.available && !f.notAvailable.empty();
            log.check(usbOff, L"Linux-side repairs are explained, not attempted from Windows");
            bool lessons = true;
            for (const Link& l : lin->chain) lessons &= !l.lesson.empty();
            log.check(lessons, L"Every Linux step has a lesson");
        }
        bool leftover = false;
        for (const Finding& f : r.pc)
            for (const Fix& fx : f.fixes) leftover |= fx.kind == FX_DELETE_EFI_DIR && fx.arg == L"EFI\\fedora";
        log.check(leftover, L"Leftover Fedora boot loader found, with an offer to remove it");

        // GRUB pointing nowhere, and Linux missing from the PC's list
        put(esp2 + L"EFI\\ubuntu\\grub.cfg", "search.fs_uuid 12345678-0000-0000-0000-000000000000 root\n");
        Report r2 = pcWith(false);
        const Install* l2 = nullptr;
        for (const Install& i : r2.installs)
            if (i.linux) l2 = &i;
        log.check(l2 != nullptr, L"Ubuntu still found when its boot loader is broken");
        if (l2) {
            const Link* gs = linkNamed(*l2, L"GRUB settings");
            log.check(gs && gs->health == Health::Bad, L"GRUB pointing to a missing partition is a problem");
            log.check(hasFix(*l2, FX_ADD_FW_ENTRY) && l2->verdict == Health::Bad, L"Plan: add Linux to the PC's start-up list");
        }
    }

    log.line(L"Languages");
    {
        std::string pack = "\xEF\xBB\xBF# test\n@code tr\n@name T\xC3\xBCrk\xC3\xA7" "e\n@rtl no\n\n"
                           "msgid \"Scan again\"\nmsgstr \"Yeniden tara\"\n\n"
                           "msgid \"Found {0} on {1}\"\nmsgstr \"{1} \xC3\xBCzerinde {0} bulundu\"\n\n"
                           "msgid \"Line one\\nLine \\\"two\\\"\"\nmsgstr \"Bir\\nIki \\\"iki\\\"\"\n\n"
                           "msgid \"Long \"\n\"text\"\nmsgstr \"Uzun \"\n\"metin\"\n\n"
                           "msgid \"Untranslated\"\nmsgstr \"\"\n";
        i18n::Pack p;
        log.check(i18n::parsePack(pack, p) && p.code == L"tr" && p.name == L"T\u00fcrk\u00e7e" && !p.rtl, L"Pack header read (code, name in its own language)");
        log.check(p.entries.size() == 4, L"Translated texts read, empty ones skipped (" + std::to_wstring(p.entries.size()) + L")");
        bool esc = false, joined = false;
        for (auto& e : p.entries) {
            if (e.first == L"Line one\nLine \"two\"" && e.second == L"Bir\nIki \"iki\"") esc = true;
            if (e.first == L"Long text" && e.second == L"Uzun metin") joined = true;
        }
        log.check(esc, L"New lines and quotes in a pack");
        log.check(joined, L"Texts continued on the next line");
        log.check(i18n::trf(L"Found {0} on {1}", {L"3", L"Disk 0"}) == L"Found 3 on Disk 0", L"Placeholders filled in");
        log.check(i18n::tr(L"Some text no pack has") == L"Some text no pack has", L"Missing translations fall back to English");
        i18n::Pack bad;
        log.check(!i18n::parsePack("msgid \"a\"\nmsgstr \"b\"\n", bad), L"A file without @code is refused");
    }

    log.line(L"Backups and undo");
    {
        Partition p;
        p.root = data;
        p.key = L"test@1";
        p.readable = true;
        Machine m;
        m.parts = {p};
        put(data + L"Boot\\BCD", "original settings");
        put(data + L"bootmgr", "original manager");
        Journal j;
        wstr err;
        log.check(j.open(work + L"\\backups", L"self-test", err), L"Session folder created");
        wstr act = j.begin(L"Test change", L"test");
        log.check(j.saveFile(act, p, L"bootmgr", err), L"File backed up");
        log.check(j.saveDir(act, p, L"Boot", err), L"Folder backed up");
        log.check(j.saveFile(act, p, L"newfile.txt", err), L"A file that does not exist yet is recorded");
        put(data + L"bootmgr", "changed");
        put(data + L"Boot\\BCD", "changed");
        put(data + L"newfile.txt", "new");
        CreateDirectoryW((data + L"Moved").c_str(), nullptr);
        put(data + L"Moved\\x.txt", "x");
        MoveFileExW((data + L"Moved").c_str(), (data + L"MovedHere").c_str(), 0);
        j.recordMove(act, p, L"Moved", L"MovedHere");
        j.end(act, true, L"changed things", L"log", true);
        std::vector<SessionInfo> ss = listSessions(work + L"\\backups");
        log.check(ss.size() == 1 && ss[0].actions.size() == 1, L"History lists the session and its change");
        if (!ss.empty() && !ss[0].actions.empty()) {
            ActionInfo a = ss[0].actions[0];
            log.check(a.canUndo && a.items.size() == 4, L"The change can be undone and lists 4 items");
            wstr ulog;
            log.check(undoAction(a, m, &j, ulog), L"Undo ran");
            log.check(get(data + L"bootmgr") == "original manager", L"File put back");
            log.check(get(data + L"Boot\\BCD") == "original settings", L"Folder put back");
            log.check(GetFileAttributesW((data + L"newfile.txt").c_str()) == INVALID_FILE_ATTRIBUTES, L"New file removed");
            log.check(GetFileAttributesW((data + L"Moved\\x.txt").c_str()) != INVALID_FILE_ATTRIBUTES, L"Move undone");
            log.check(readAction(a.dir).undone, L"The change is marked as undone");
            ss = listSessions(work + L"\\backups");
            log.check(!ss.empty() && ss[0].actions.size() == 2, L"The undo itself is in the history too");
        }
    }

    log.line(L"BitLocker keys");
    {
        int caret = 7;
        wstr t = keyInputFormat(L"", L"1234567", caret);
        log.check(t == L"123456-7" && caret == 8, L"Dashes are added while typing a key");
        caret = 6;
        t = keyInputFormat(L"123456-7", L"1234567", caret);
        log.check(t == L"123457" && caret == 5, L"Backspace over a dash removes the digit before it");
        caret = 0;
        t = keyInputFormat(L"", wstr(60, L'1'), caret);
        log.check(keyDigits(t).size() == 48, L"A key is cut at 48 digits");
        log.check(validRecoveryKey(L"111111-222222-333333-444444-555555-666666-000000-123453"), L"A well-formed recovery key is accepted");
        log.check(!validRecoveryKey(L"111111-222222-333333-444444-555555-666666-000000-123454"), L"A mistyped group is caught (multiple of 11)");
        log.check(!validRecoveryKey(L"777777-222222-333333-444444-555555-666666-000000-123453"), L"A group above 720896 is refused");
        log.check(!validRecoveryKey(L"1111-2222"), L"A short key is refused");
        std::string enc, dec;
        log.check(protectBytes("secret key", enc) && enc != "secret key" && unprotectBytes(enc, dec) && dec == "secret key", L"Keys of the earlier version can still be read (DPAPI)");
        std::string blob, back;
        bool sealed = encryptBlob("111111-222222", L"pass word", blob, 2000);
        log.check(sealed && blob.find("111111") == std::string::npos && decryptBlob(blob, L"pass word", back) && back == "111111-222222", L"Saved keys are locked with the password and read back");
        log.check(!decryptBlob(blob, L"pass Word", back), L"A wrong password opens nothing");
        std::string bad = blob;
        bad[bad.size() / 2] ^= 1;
        log.check(!decryptBlob(bad, L"pass word", back), L"A changed file is noticed");
    }

    log.line(L"Recovery");
    {
        std::string b(4096, '\0');
        memcpy(&b[3], "NTFS    ", 8);
        b[0x0B] = 0; b[0x0C] = 2; b[0x0D] = 8;
        uint64_t total = 2047999;
        memcpy(&b[0x28], &total, 8);
        wstr fs, label;
        uint64_t size = 0;
        log.check(parseBootSector(b, fs, size, label) && fs == L"NTFS" && size == 2048000ull * 512, L"NTFS first sector: size");
        b.assign(4096, '\0');
        memcpy(&b[0x52], "FAT32   ", 8);
        b[0x0B] = 0; b[0x0C] = 2;
        uint32_t t32 = 204800;
        memcpy(&b[0x20], &t32, 4);
        memcpy(&b[0x47], "PHOTOS     ", 11);
        b[0x1FE] = 0x55; b[0x1FF] = (char)0xAA;
        log.check(parseBootSector(b, fs, size, label) && fs == L"FAT32" && size == 100ull << 20 && label == L"PHOTOS", L"FAT32 first sector: size and name");
        b.assign(4096, '\0');
        b[1024 + 0x38] = 0x53; b[1024 + 0x39] = (char)0xEF;
        uint32_t blocks = 25600, logb = 2, incompat = 0x40;
        memcpy(&b[1024 + 4], &blocks, 4);
        memcpy(&b[1024 + 0x18], &logb, 4);
        memcpy(&b[1024 + 0x60], &incompat, 4);
        memcpy(&b[1024 + 0x78], "root", 4);
        log.check(parseBootSector(b, fs, size, label) && fs == L"ext4" && size == 100ull << 20 && label == L"root", L"Linux ext4: size and name");
        b.assign(4096, '\0');
        log.check(!parseBootSector(b, fs, size, label), L"An empty sector is no file system");

        const uint8_t runs[] = {0x21, 0x02, 0x10, 0x00, 0x11, 0x01, 0xF0, 0x00};
        auto rl = ntfsRuns(runs, sizeof runs);
        log.check(rl.size() == 2 && rl[0].first == 16 && rl[0].second == 2 && rl[1].first == 0 && rl[1].second == 1, L"NTFS cluster runs, also going backwards");

        // A deleted file's record: "test.txt", 5000 bytes, in two pieces
        std::string rec(1024, '\0');
        memcpy(&rec[0], "FILE", 4);
        rec[4] = 0x30; rec[6] = 3;
        rec[0x30] = 1; rec[0x32] = (char)0xAB; rec[0x33] = (char)0xCD; rec[0x34] = (char)0xEF; rec[0x35] = 0x12;
        rec[510] = 1; rec[1022] = 1;
        rec[0x14] = 0x38;
        size_t a = 0x38;
        rec[a] = 0x30; rec[a + 4] = 0x70;
        rec[a + 0x10] = 0x52; rec[a + 0x14] = 0x18;
        size_t c = a + 0x18;
        uint64_t parent = 5;
        memcpy(&rec[c], &parent, 8);
        uint64_t fnSize = 1234;
        memcpy(&rec[c + 0x30], &fnSize, 8);
        rec[c + 0x40] = 8; rec[c + 0x41] = 1;
        const wchar_t* nm = L"test.txt";
        memcpy(&rec[c + 0x42], nm, 16);
        a += 0x70;
        rec[a] = (char)0x80; rec[a + 4] = 0x48; rec[a + 8] = 1;
        rec[a + 0x20] = 0x40;
        uint64_t real = 5000;
        memcpy(&rec[a + 0x30], &real, 8);
        memcpy(&rec[a + 0x40], runs, sizeof runs);
        a += 0x48;
        memset(&rec[a], 0xFF, 4);
        bool fix = ntfsApplyFixups(rec, 512);
        FoundFile f;
        uint64_t par = 0;
        bool inUse = true, isDir = true, isBase = false;
        log.check(fix && (uint8_t)rec[510] == 0xAB && (uint8_t)rec[1022] == 0xEF, L"NTFS record: sector ends put back (fix-ups)");
        log.check(ntfsParseRecord(rec, f, par, inUse, isDir, isBase) && !inUse && !isDir && isBase && f.name == L"test.txt" && par == 5 && f.size == 5000 &&
                      f.runs.size() == 2 && !f.resident,
                  L"NTFS record: a deleted file with its name, folder, size and pieces");
        log.check(fileKindOf(L"IMG_0001.JPG") == FK_PICTURE && fileKindOf(L"a.docx") == FK_DOCUMENT && fileKindOf(L"x.mp4") == FK_MEDIA && fileKindOf(L"y.dll") == FK_OTHER,
                  L"Kinds of files from their names");
    }

    log.line(L"BitLocker first");
    {
        Report r;
        Partition pe, pl;
        pe.disk = pl.disk = 0;
        pe.number = 1;
        pl.number = 2;
        pe.role = R_ESP;
        pl.role = R_DATA;
        pe.readable = true;
        pe.root = esp;
        pl.locked = pl.bitlocker = true;
        r.m.parts = {pe, pl};
        BcdStore st;
        st.part = 0;
        st.loaded = true;
        BcdEntry e;
        e.id = L"{inside}";
        e.app = APP_LOADER;
        e.description = L"Windows 11";
        e.hasOsDevice = true;
        e.osPart = 1;
        e.systemRoot = L"\\Windows";
        e.inMenu = e.isDefault = true;
        BcdEntry g = e;
        g.id = L"{nodisk}";
        g.osPart = -1;
        g.isDefault = false;
        st.entries = {e, g};
        st.order = {L"{inside}", L"{nodisk}"};
        r.stores = {st};
        findDeadEntries(r);
        const Finding* locked = nullptr;
        const Finding* gone = nullptr;
        for (const Finding& f : r.pc) {
            if (f.title.find(L"locked by BitLocker") != wstr::npos) locked = &f;
            else if (f.title.find(L"leads nowhere") != wstr::npos) gone = &f;
        }
        bool onlyUnlock = locked && !locked->fixes.empty();
        if (locked)
            for (const Fix& x : locked->fixes) onlyUnlock &= x.kind == FX_UNLOCK;
        log.check(onlyUnlock, L"An entry pointing into a locked drive is not called broken: only Unlock is offered");
        log.check(gone && gone->text.find(L"locked by BitLocker") != wstr::npos, L"Other dead entries warn to unlock the locked drive first");
    }

    log.line(L"Free space");
    {
        wstr root = work + L"\\clean";
        put(root + L"\\Users\\Ann\\AppData\\Local\\Temp\\a.tmp", std::string(100, 'x'));
        put(root + L"\\Users\\Bob\\AppData\\Local\\Temp\\sub\\b.tmp", std::string(50, 'x'));
        put(root + L"\\Windows\\Temp\\c.tmp", std::string(10, 'x'));
        put(root + L"\\Users\\Ann\\Documents\\keep.txt", std::string(10, 'x'));
        log.check(expandPattern(root, L"Users\\*\\AppData\\Local\\Temp").size() == 2, L"Folder patterns find every user's folder");
        std::vector<CleanSpot> spots = cleanupSpots();
        measureSpots(root, spots, nullptr, nullptr);
        log.check(spots[0].bytes == 160 && spots[0].files == 3, L"Temporary files are measured");
        for (size_t k = 0; k < spots.size(); ++k) spots[k].on = k == 0;
        uint32_t removed = 0, skipped = 0;
        uint64_t freed = cleanSpots(root, spots, nullptr, nullptr, removed, skipped);
        log.check(freed == 160 && removed == 3 && GetFileAttributesW((root + L"\\Users\\Ann\\AppData\\Local\\Temp").c_str()) != INVALID_FILE_ATTRIBUTES &&
                      GetFileAttributesW((root + L"\\Users\\Ann\\Documents\\keep.txt").c_str()) != INVALID_FILE_ATTRIBUTES,
                  L"Clean-up empties only the chosen folders and keeps them");
        log.check(lowSpace(1ull << 30, 500ull << 30) && !lowSpace(50ull << 30, 500ull << 30) && lowSpace(4ull << 30, 1000ull << 30) && !lowSpace(4ull << 30, 100ull << 30),
                  L"\"Almost full\" means under 2 GB, or under 1 %");
    }

    log.line(L"Renamed system folders");
    {
        wstr root = work + L"\\renamed\\";
        put(root + L"OldWin\\Sys\\ntoskrnl.exe", program());
        put(root + L"OldWin\\Sys\\winload.efi", program());
        put(root + L"OldWin\\Sys\\cfg\\SYSTEM", "regf");
        put(root + L"OldWin\\Sys\\cfg\\SOFTWARE", "regf");
        put(root + L"Data\\photo.jpg", "x");
        auto found = findRenamedWindows(root, 0, 5000);
        log.check(found.size() == 1 && found[0].windowsRel == L"OldWin" && found[0].systemName == L"Sys" && found[0].configName == L"cfg",
                  L"Windows, System32 and config are found under other names");
        Report r;
        Partition p;
        p.root = root;
        p.readable = true;
        r.m.parts.push_back(p);
        Journal j;
        wstr err;
        bool open = j.open(work + L"\\renamed-backups", L"self-test", err);
        Fix x = makeFix(FX_RENAME_SYSFOLDERS);
        x.part = 0;
        x.arg = L"OldWin";
        x.arg2 = L"Sys";
        x.arg3 = L"cfg";
        FixOutcome o = open ? runFix(x, r, j, nullptr, nullptr) : FixOutcome();
        log.check(o.ok && GetFileAttributesW((root + L"Windows\\System32\\config\\SYSTEM").c_str()) != INVALID_FILE_ATTRIBUTES, L"They are given their names back");
        std::vector<Install> ins;
        findInstalls(root, 0, ins);
        log.check(ins.size() == 1 && ins[0].atRoot, L"Afterwards the Windows is found at the top of the drive");
    }

    log.line(L"Partition order");
    {
        const uint64_t MB = 1ull << 20, GB = 1ull << 30;
        auto makeReport = [&](bool msr, uint64_t recLen, bool running) {
            Report r;
            Disk d;
            d.number = 7;
            d.style = 2;
            d.size = 100 * GB;
            auto add = [&](int role, uint64_t off, uint64_t len, const wchar_t* fs) {
                Partition q;
                q.disk = 7;
                q.number = (int)r.m.parts.size() + 1;
                q.offset = off;
                q.length = len;
                q.role = role;
                q.fs = fs;
                q.gpt = true;
                d.parts.push_back((int)r.m.parts.size());
                r.m.parts.push_back(q);
                return (int)r.m.parts.size() - 1;
            };
            uint64_t o = MB;
            add(R_ESP, o, 260 * MB, L"FAT32");
            o += 260 * MB;
            if (msr) {
                add(R_MSR, o, 16 * MB, L"");
                o += 16 * MB;
            }
            uint64_t recOff = d.size - MB - recLen;
            int w = add(R_DATA, o, recOff - o, L"NTFS");
            r.m.parts[w].readable = true;
            r.m.parts[w].totalBytes = r.m.parts[w].length;
            r.m.parts[w].freeBytes = r.m.parts[w].length / 2;
            add(R_RECOVERY, recOff, recLen, L"NTFS");
            r.m.disks.push_back(d);
            Install in;
            in.part = w;
            in.atRoot = true;
            in.running = running;
            in.winDir = work + L"\\nowhere\\Windows";
            r.installs.push_back(in);
            return r;
        };
        Report a = makeReport(false, 500 * MB, false);
        LayoutPlan pa = planLayout(a, 0, true, false);
        auto has = [&](const LayoutPlan& pl, int kind) {
            for (const LayoutStep& st : pl.steps)
                if (st.kind == kind) return true;
            return false;
        };
        log.check(pa.possible && !pa.steps.empty() && pa.steps[0].kind == ST_DELETE, L"Too small recovery partition: it is removed first");
        log.check(has(pa, ST_SHRINK) && has(pa, ST_MOVE) && has(pa, ST_CREATE_MSR) && has(pa, ST_CREATE_RECOVERY) && has(pa, ST_BOOTFILES) && has(pa, ST_WINRE),
                  L"Missing MSR: Windows is made smaller, moved, the MSR and a new recovery partition are made, then start-up files and repair tools");
        bool order = pa.target.size() == 4 && pa.target[0].kind == SK_ESP && pa.target[1].kind == SK_MSR && pa.target[2].kind == SK_WIN && pa.target[3].kind == SK_RECOVERY &&
                     pa.target[1].off == pa.target[0].off + pa.target[0].len && pa.target[2].off == pa.target[1].off + 16 * MB && pa.target[3].len == 1024 * MB &&
                     pa.target[3].off + pa.target[3].len == 100 * GB - MB;
        log.check(order, L"UEFI order: EFI, MSR, Windows, recovery (1 GB) at the end");
        Report b = makeReport(true, 1024 * MB, false);
        LayoutPlan pb = planLayout(b, 0, true, false);
        log.check(pb.possible && !has(pb, ST_MOVE) && !has(pb, ST_DELETE) && !has(pb, ST_SHRINK), L"Already in order: nothing is moved or removed");
        Report c = makeReport(true, 1024 * MB, true);
        LayoutPlan pc = planLayout(c, 0, true, false);
        log.check(!pc.possible && pc.why.find(L"rescue USB") != wstr::npos, L"The disk in use is refused (start from the rescue stick instead)");
        LayoutPlan pd = planLayout(b, 0, false, false);
        log.check(!pd.possible, L"A legacy BIOS order is refused on a GPT disk");
        log.check(!workingDiskRefusal(c, 7).empty() && workingDiskRefusal(b, 7).empty(), L"The disk of the running Windows counts as in use");
        bool refused = false;
        for (const auto& op : diskOpsFor(c, SEL_PART, 7, 2, -1))
            if (op.first == DO_SHRINK || op.first == DO_DELETE) refused = !op.second.empty();
        log.check(refused, L"Disk changes on it are refused");
    }

    log.line(L"Windows.old");
    {
        wstr root = work + L"\\old\\";
        put(root + L"Windows.old\\Windows\\System32\\ntoskrnl.exe", program());
        put(root + L"Windows.old\\Windows\\System32\\winload.exe", program());
        put(root + L"Windows.old\\Windows\\System32\\config\\SYSTEM", std::string(300 * 1024, 'x'));
        put(root + L"Windows.old\\Windows\\System32\\config\\SOFTWARE", std::string(1024, 'x'));
        put(root + L"Windows.old\\Program Files\\a.txt", "x");
        put(root + L"Windows.old\\Users\\Ann\\a.txt", "x");
        std::vector<Install> ins;
        findInstalls(root, 0, ins);
        bool found = ins.size() == 1 && ins[0].windowsOld;
        log.check(found, L"Windows.old is found");
        Report r;
        Partition q;
        q.root = root;
        q.readable = true;
        r.m.parts.push_back(q);
        r.installs = ins;
        diagnose(r);
        bool can = false;
        for (const Finding& f : r.pc)
            for (const Fix& x : f.fixes) can |= x.kind == FX_RESTORE_WINDOWS_OLD;
        log.check(can, L"A whole Windows.old can be brought back");
        DeleteFileW((root + L"Windows.old\\Windows\\System32\\config\\SYSTEM").c_str());
        Report r2;
        r2.m.parts.push_back(q);
        r2.installs = ins;
        diagnose(r2);
        bool cannot = true;
        for (const Finding& f : r2.pc)
            for (const Fix& x : f.fixes) cannot &= x.kind != FX_RESTORE_WINDOWS_OLD;
        log.check(cannot, L"Without its settings it is not offered");
    }

    log.line(L"Disk health");
    {
        uint8_t attrs[512] = {}, th[512] = {};
        auto attr = [&](int k, int id, int value, uint64_t raw) {
            uint8_t* a = attrs + 2 + k * 12;
            a[0] = (uint8_t)id;
            a[3] = (uint8_t)value;
            for (int i = 0; i < 6; ++i) a[5 + i] = (uint8_t)(raw >> (8 * i));
        };
        auto thr = [&](int k, int id, int limit) {
            th[2 + k * 12] = (uint8_t)id;
            th[2 + k * 12 + 1] = (uint8_t)limit;
        };
        attr(0, 9, 90, 12345);
        attr(1, 194, 100, 38);
        attr(2, 5, 100, 0);
        thr(0, 5, 36);
        DiskHealth good;
        parseAtaSmart(attrs, th, good);
        log.check(good.known && good.level == 0 && good.hours == 12345 && good.tempC == 38, L"A healthy SATA disk: hours and temperature, no warning");
        attr(2, 5, 100, 12);
        attr(3, 197, 100, 3);
        DiskHealth worn;
        parseAtaSmart(attrs, th, worn);
        log.check(worn.level == 2 && worn.reallocated == 12 && worn.pending == 3 && worn.problems.size() >= 2, L"Replaced and unreadable sectors: failing");
        attr(3, 197, 100, 0);
        attr(2, 5, 30, 12);
        DiskHealth past;
        parseAtaSmart(attrs, th, past);
        log.check(past.level == 2, L"A value at or below the disk's own limit: failing");
        uint8_t nv[512] = {};
        nv[1] = (uint8_t)(310 & 0xFF);
        nv[2] = (uint8_t)(310 >> 8);
        nv[3] = 100;
        nv[4] = 10;
        nv[5] = 3;
        nv[128] = 0x10;
        DiskHealth n1;
        parseNvmeLog(nv, n1);
        log.check(n1.known && n1.level == 0 && n1.tempC == 37 && n1.percentUsed == 3 && n1.hours == 16, L"A healthy NVMe SSD");
        nv[0] = 0x04;
        nv[5] = 95;
        DiskHealth n2;
        parseNvmeLog(nv, n2);
        log.check(n2.level == 2 && n2.problems.size() >= 2, L"An NVMe SSD that reports it is no longer reliable: failing");
    }

    log.line(L"Moving Windows to another drive");
    {
        const uint64_t MB = 1ull << 20, GB = 1ull << 30;
        auto build = [&](uint64_t targetSize, bool running) {
            Report r;
            Disk a, b;
            a.number = 0;
            a.style = 2;
            a.size = 500 * GB;
            a.readable = true;
            b.number = 1;
            b.style = 1;
            b.size = targetSize;
            b.readable = true;
            Partition w;
            w.disk = 0;
            w.number = 3;
            w.offset = 300 * MB;
            w.length = 400 * GB;
            w.fs = L"NTFS";
            w.readable = true;
            w.role = R_DATA;
            w.totalBytes = w.length;
            w.freeBytes = 300 * GB;
            a.parts.push_back(0);
            r.m.parts.push_back(w);
            r.m.disks = {a, b};
            Install in;
            in.part = 0;
            in.atRoot = true;
            in.running = running;
            r.installs.push_back(in);
            return r;
        };
        Report big = build(1000 * GB, false);
        MigratePlan p1 = planMigration(big, 0, 1, true);
        log.check(p1.possible && p1.shrinkBy == 0 && p1.copyLen == 400 * GB && p1.winLen > 400 * GB && p1.recLen == 1024 * MB && p1.msrLen == 16 * MB,
                  L"To a bigger disk: all of Windows is copied, the new partition is bigger, with EFI, MSR and recovery");
        Report small = build(256 * GB, false);
        MigratePlan p2 = planMigration(small, 0, 1, true);
        log.check(p2.possible && p2.shrinkBy > 0 && p2.copyLen <= p2.winLen, L"To a smaller disk (but big enough for the files): Windows gives up space first");
        Report tiny = build(64 * GB, false);
        log.check(!planMigration(tiny, 0, 1, true).possible, L"To a disk too small for the files: refused");
        log.check(shrinkQueryMB(L"The maximum number of reclaimable bytes is:  152 GB (155648MB)\r\n") == 155648 && shrinkQueryMB(L"Geri kazanilabilir en fazla bayt sayisi: 9 GB (9216 MB)") == 9216,
                  L"How much a partition can give up is read from Windows' answer, in any language");
        Report run = build(1000 * GB, true);
        MigratePlan p4 = planMigration(run, 0, 1, true);
        log.check(!p4.possible && p4.why.find(L"rescue USB") != wstr::npos, L"The running Windows is not copied (start from the rescue stick)");
        big.m.disks[1].health.known = true;
        big.m.disks[1].health.level = 2;
        log.check(!planMigration(big, 0, 1, true).possible, L"Never onto a disk that reports it is failing");
    }

    log.line(L"Keys never shown, PATio Commander on the stick");
    {
        wstr seen;
        setToolLogger([&](const wstr& line) { seen += line; });
        toolLog(L"    Password:\r\n      111111-222222-333333-444444-555555-666666-000000-123453\r\n");
        setToolLogger(nullptr);
        log.check(seen.find(L"111111") == wstr::npos && seen.find(L"******-******") != wstr::npos, L"A recovery key in a tool's output is hidden in the console");
        wstr suite = work + L"\\cmdr";
        put(suite + L"\\PATioCommander_x64.exe", program());
        put(suite + L"\\PatSuite\\PATio Boot Doctor\\PATioBootDoctor_x64.exe", program());
        log.check(iequals(findCommander(suite + L"\\PatSuite\\PATio Boot Doctor\\PATioBootDoctor_x64.exe"), suite + L"\\PATioCommander_x64.exe"),
                  L"PATio Commander is found above the PatSuite folder, to go on the rescue stick");
        log.check(findCommander(work + L"\\nowhere\\PATioBootDoctor_x64.exe").empty(), L"Without it, the stick has Boot Doctor alone");
    }

    log.line(L"Updates");
    {
        log.check(compareVersions(L"0.3.0", L"0.2.9") > 0 && compareVersions(L"0.10.0", L"0.9.1") > 0 && compareVersions(L"1.0", L"1.0.0") == 0 && compareVersions(L"0.2.0", L"0.3.0") < 0,
                  L"Versions compare number by number");
        UpdateInfo u = parseUpdateFile("version=0.4.1\r\nurl=https://example.org/PATioBootDoctor_x64.exe\r\nsha256=ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef0123456789\r\nnotes=Faster scans\r\n");
        // with a key built in, an update file without a signature must be refused
        log.check(u.ok == !updateKeyBuiltIn() && u.version == L"0.4.1" && u.notes == L"Faster scans" && u.sha256.size() == 64 && u.sha256[0] == L'a',
                  updateKeyBuiltIn() ? L"The update file is read, and refused because it is not signed" : L"The update file is read");
        log.check(!parseUpdateFile("version=0.4.1\nurl=http://example.org/a.exe\nsha256=00\n").ok, L"An update file without https or a full checksum is refused");
        log.check(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", L"SHA-256 checksum");
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        wstr v, err;
        bool own = checkProgramFile(exe, L"", v, err) && compareVersions(v, version) == 0;
        log.check(own, L"This program passes the update check, with its own version (" + v + L")");
        {
            wstr priv = work + L"\\k.key", pub = work + L"\\k.pub", e2;
            std::string pubHex;
            bool made = makeUpdateKey(priv, pub, e2) && readFileText(pub, pubHex);
            wstr sha = L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
            wstr sig = made ? signChecksum(sha, priv, e2) : wstr();
            wstr other = sha;
            other[0] = L'c';
            log.check(made && sig.size() == 128 && verifyChecksum(sha, sig, pubHex), L"An update's checksum is signed and the signature checks out");
            log.check(made && !verifyChecksum(other, sig, pubHex) && !verifyChecksum(sha, wstr(128, L'0'), pubHex), L"Another checksum or a made-up signature is refused");
            log.check(!makeUpdateKey(priv, pub, e2), L"An existing private key is never overwritten");
        }
        put(work + L"\\fake.exe", program() + std::string(8192, 'x'));
        log.check(!checkProgramFile(work + L"\\fake.exe", L"", v, err), L"Another program is refused");
    }

    log.line(L"Local accounts (SAM)");
    {
        auto p32 = [](std::string& s, size_t o, uint32_t v) {
            for (int i = 0; i < 4; ++i) s[o + i] = (char)(v >> (8 * i) & 0xFF);
        };
        // an account's F value: control flags at 0x38 = normal + disabled
        std::string f(0x50, '\0');
        f[0x38] = (char)(0x10 | 0x01);
        uint16_t acb = 0;
        log.check(samParseF(f, acb) && acb == 0x11, L"Account flags are read (normal, disabled)");
        samSetAcb(f, acb & ~(uint16_t)0x0001);
        acb = 0;
        log.check(samParseF(f, acb) && acb == 0x10 && !(acb & 0x0001), L"Enabling clears only the disabled flag");
        samSetAcb(f, 0x10 | 0x0400);
        samParseF(f, acb);
        samSetAcb(f, acb & ~(uint16_t)0x0400);
        samParseF(f, acb);
        log.check(!(acb & 0x0400) && (acb & 0x10), L"Unlocking clears only the auto-lock flag");
        // an account's V value: name, full name, and a present NT hash
        std::string nameB, fullB;
        for (wchar_t c : wstr(L"Ann")) {
            nameB += (char)(c & 0xFF);
            nameB += (char)(c >> 8);
        }
        for (wchar_t c : wstr(L"Ann Smith")) {
            fullB += (char)(c & 0xFF);
            fullB += (char)(c >> 8);
        }
        std::string data = nameB + fullB + std::string(20, 'h');  // name, full name, 20-byte NT hash
        std::string v(0xCC, '\0');
        p32(v, 0x0C, 0);                                 // name offset
        p32(v, 0x10, (uint32_t)nameB.size());            // name length
        p32(v, 0x18, (uint32_t)nameB.size());            // full name offset
        p32(v, 0x1C, (uint32_t)fullB.size());            // full name length
        p32(v, 0x9C, (uint32_t)(nameB.size() + fullB.size()));  // LM hash offset
        p32(v, 0xA0, 0);                                 // LM hash length (none)
        p32(v, 0xA8, (uint32_t)(nameB.size() + fullB.size()));  // NT hash offset
        p32(v, 0xAC, 20);                                // NT hash length (present)
        v += data;
        wstr nm, fn;
        uint32_t lm = 1, nt = 0;
        log.check(samParseV(v, nm, fn, lm, nt) && nm == L"Ann" && fn == L"Ann Smith" && lm == 0 && nt == 20, L"Account name, full name and that a password is set are read");
        log.check(samBlankV(v) && samParseV(v, nm, fn, lm, nt) && nt == 0 && lm == 0 && nm == L"Ann", L"Clearing the password zeroes both hash lengths, nothing else");
        std::string tiny(4, '\0');
        log.check(!samParseF(tiny, acb) && !samBlankV(tiny), L"A too-short structure is refused, not misread");
    }

    log.line(L"Screenshot and recording");
    {
        log.check(crc32Of("123456789") == 0xCBF43926, L"CRC-32 matches the known value");
        const int cw = 20, ch = 12;
        std::vector<uint32_t> img((size_t)cw * ch);
        for (int y = 0; y < ch; ++y)
            for (int x = 0; x < cw; ++x) img[y * cw + x] = ((x * 12) << 16) | ((y * 20) << 8) | (x + y);
        wstr png = work + L"\\cap.png";
        std::string back;
        int pw = 0, ph = 0;
        bool wrote = writePng(png, img.data(), cw, ch) && readFileText(png, back);
        log.check(wrote && pngLooksValid(back, pw, ph) && pw == cw && ph == ch, L"A screenshot PNG is written and reads back as a valid PNG of the right size");
        std::string jpg = encodeJpeg(img.data(), cw, ch, 80);
        log.check(!jpg.empty() && jpegLooksValid(jpg), L"A frame encodes to a well-formed JPEG (markers and byte-stuffing)");
        std::string odd = encodeJpeg(img.data(), 17, 7, 60);  // sizes not a multiple of 8
        log.check(!odd.empty() && jpegLooksValid(odd), L"A frame whose size is not a multiple of 8 still encodes cleanly");
        wstr avi = work + L"\\cap.avi";
        AviWriter mv;
        bool mok = mv.open(avi, cw, ch, 8) && mv.addFrame(jpg) && mv.addFrame(jpg) && mv.addFrame(jpg);
        int fr = mv.frames();
        mok = mv.close() && mok;
        std::string av;
        readFileText(avi, av);
        bool riff = av.size() > 256 && memcmp(av.data(), "RIFF", 4) == 0 && memcmp(av.data() + 8, "AVI ", 4) == 0 && av.find("movi") != std::string::npos && av.find("idx1") != std::string::npos;
        log.check(mok && fr == 3 && riff, L"A Motion-JPEG .avi is written with its frames and an index");
        if (Mp4Writer::available()) {
            wstr mp4 = work + L"\\cap.mp4";
            int mfr = 0;
            wstr e4;
            bool m4 = recordTestMp4(mp4, mfr, e4);
            std::string mb;
            readFileText(mp4, mb);
            log.check(m4 && mfr == 30 && mp4LooksValid(mb), L"An MP4 is recorded through Media Foundation");
            DeleteFileW(mp4.c_str());
        } else
            log.check(true, L"Media Foundation is not here: recording falls back to .avi (MP4 skipped)");
        DeleteFileW(png.c_str());
        DeleteFileW(avi.c_str());
    }

    log.line(L"Secure Boot certificates (2023 change)");
    {
        auto le32 = [](uint32_t v) {
            std::string s(4, '\0');
            for (int i = 0; i < 4; ++i) s[i] = (char)(v >> (8 * i) & 0xFF);
            return s;
        };
        // SHA-256 list: two revoked hashes
        std::string shaGuid = {(char)0x26, (char)0x16, (char)0xC4, (char)0xC1, (char)0x4C, (char)0x50, (char)0x92, (char)0x40, (char)0xAC, (char)0xA9, (char)0x41, (char)0xF9, (char)0x36, (char)0x93, (char)0x43, (char)0x28};
        std::string shaList = shaGuid + le32(28 + 2 * 48) + le32(0) + le32(48) + std::string(48, 'a') + std::string(48, 'b');
        // X.509 list: one certificate whose common name is "Windows UEFI CA 2023"
        std::string der = std::string("\x06\x03\x55\x04\x03\x13\x14", 7) + "Windows UEFI CA 2023";
        std::string x509Guid = {(char)0xA1, (char)0x59, (char)0xC0, (char)0xA5, (char)0xE4, (char)0x94, (char)0xA7, (char)0x4A, (char)0x87, (char)0xB5, (char)0xAB, (char)0x15, (char)0x5C, (char)0x2B, (char)0xF0, (char)0x72};
        uint32_t sigSize = 16 + (uint32_t)der.size();
        std::string x509List = x509Guid + le32(28 + sigSize) + le32(0) + le32(sigSize) + std::string(16, '\0') + der;
        std::vector<std::string> ders;
        int hashes = 0;
        efiSigListCerts(shaList + x509List, ders, hashes);
        log.check(hashes == 2 && ders.size() == 1, L"A signature list is parsed: two revoked hashes and one certificate");
        log.check(!ders.empty() && certCommonName(ders[0]) == L"Windows UEFI CA 2023", L"A certificate's common name is read from the signature list");

        Firmware fw;
        fw.uefi = true;
        SecureBootCerts c;
        c.read = true;
        c.dbHas2011 = true;
        c.dbxRevokes2011 = true;
        c.dbxHashes = 200;
        wstr note1 = secureBoot2023Note(c, fw);
        log.check(icontains(note1, L"does not yet trust the 2023") && icontains(note1, L"Secure Boot off"), L"Revoked 2011 without 2023: the recovery steps are given");
        c.dbHas2023 = true;
        wstr note2 = secureBoot2023Note(c, fw);
        log.check(icontains(note2, L"rebuild the start-up files") && note2 != note1, L"Revoked 2011 with 2023 present: rebuild the boot files instead");
        log.check(secureBoot2023Note(c, Firmware{}).empty(), L"On a legacy-BIOS PC there is nothing to say");
    }

    log.line(L"Disk controller and storage drivers");
    {
        log.check(modeFromHardwareId(L"PCI\\VEN_8086&DEV_1C02&CC_010601") == SM_AHCI && modeFromHardwareId(L"PCI\\VEN_8086&DEV_2822&CC_0104") == SM_RAID &&
                      modeFromHardwareId(L"PCI\\VEN_8086&DEV_1C00&CC_0101") == SM_IDE && modeFromHardwareId(L"PCI\\VEN_144D&DEV_A808&CC_010802") == SM_NVME &&
                      modeFromHardwareId(L"PCI\\VEN_8086&DEV_1C02") == SM_UNKNOWN,
                  L"The controller's mode is read from its class code (AHCI, RAID, IDE, NVMe)");
        auto drv = [](const wchar_t* s, int mode, bool inbox, bool present, DWORD start) {
            DriverState d;
            d.service = s;
            d.mode = mode;
            d.inbox = inbox;
            d.present = present;
            d.fileThere = present;
            d.start = present ? start : 0xFFFFFFFF;
            return d;
        };
        // Windows 7 on a 2nd-generation Intel PC, set up for AHCI; the firmware now runs the controller as RAID
        StorageCheck c;
        c.ok = true;
        c.win7 = true;
        StorageController k;
        k.mode = SM_RAID;
        k.venDev = L"VEN_8086&DEV_2822";
        k.cc = L"0104";
        c.controllers = {k};
        c.mode = SM_RAID;
        c.drivers = {drv(L"disk", SM_CORE, true, true, 0), drv(L"msahci", SM_AHCI, true, true, 0), drv(L"iaStorV", SM_RAID, true, true, 3),
                     drv(L"pciide", SM_IDE, true, true, 3), drv(L"storahci", SM_AHCI, true, false, 0)};
        c.cddbHave = {L"pci#cc_010601"};
        judgeStorage(c);
        bool iaOn = false;
        for (const wstr& s : c.turnOn) iaOn |= s == L"iaStorV";
        log.check(c.needPresent && !c.needOn && iaOn && c.setUpFor.size() == 1 && c.setUpFor[0] == SM_AHCI, L"RAID mode, RAID driver installed but off: the fix turns on iaStorV (not the AHCI drivers)");
        log.check(c.cddb.size() == 1 && c.cddb[0] == L"pci#ven_8086&dev_2822&cc_0104=iaStorV", L"Windows 7: the RAID controller is added to the critical-device database");
        // the same Windows with the controller back in AHCI: nothing to do
        c.mode = SM_AHCI;
        c.controllers[0].mode = SM_AHCI;
        c.controllers[0].cc = L"0106";
        judgeStorage(c);
        log.check(c.needOn && c.turnOn.empty() && c.cddb.empty(), L"Controller in the mode Windows was set up for: the driver is not the problem");
        // planning a change to RAID before flipping the firmware
        judgeStorage(c, SM_RAID);
        log.check(c.turnOn.size() == 1 && c.turnOn[0] == L"iaStorV", L"Preparing for RAID mode turns on the RAID driver first");
        // VMD with no Intel driver
        c.drivers.push_back(drv(L"iaStorVD", SM_VMD, false, false, 0));
        judgeStorage(c, SM_VMD);
        log.check(!c.needPresent && c.turnOn.empty() && c.advice.find(L"iaStorVD") != wstr::npos, L"No driver for the mode: says which driver to add, changes nothing");
        std::vector<wstr> all = allInboxStorageDrivers(c);
        bool hasPci = false, hasMs = false;
        for (const wstr& s : all) {
            hasPci |= s == L"pciide";
            hasMs |= s == L"msahci";
        }
        log.check(hasPci && !hasMs, L"\"Start in any SATA mode\" turns on the built-in drivers that are off, and only those");
        // Windows 10 on an Intel PC in AHCI mode, run by Intel Rapid Storage (iaStorAC) with storahci off: that is fine
        StorageCheck r;
        r.ok = true;
        r.mode = SM_AHCI;
        DriverState rst = drv(L"iaStorAC", SM_RAID, false, true, 0);
        rst.mode2 = SM_AHCI;
        r.drivers = {drv(L"disk", SM_CORE, true, true, 0), drv(L"storahci", SM_AHCI, true, true, 3), rst};
        judgeStorage(r);
        log.check(r.needOn && r.turnOn.empty(), L"An AHCI controller run by Intel Rapid Storage counts as having its driver (nothing to change)");
    }

    log.line(L"Files on unreadable sectors");
    {
        log.check(isStartupCritical(L"Windows\\System32\\config\\SYSTEM") && isStartupCritical(L"Backup\\Windows\\System32\\drivers\\iaStorV.sys") &&
                      isStartupCritical(L"Windows\\System32\\winload.efi") && isStartupCritical(L"$MFT") && isStartupCritical(L"bootmgr") &&
                      !isStartupCritical(L"Users\\Ann\\Pictures\\holiday.jpg") && !isStartupCritical(L"Windows\\System32\\notepad.exe"),
                  L"Files Windows needs to start are told apart from the rest");
        for (const StorageController& k : liveStorageControllers())
            log.line(L"  this PC: " + k.name + L" · " + storageModeName(k.mode) + L" · " + k.venDev + L" · driver " + (k.liveService.empty() ? wstr(L"-") : k.liveService));
    }

    log.line(L"Hardware details");
    {
        std::string t;
        auto bytes = [&](std::initializer_list<int> b) {
            for (int v : b) t += (char)v;
        };
        auto zeros = [&](int n) { t.append((size_t)n, '\0'); };
        // type 1: system (length 0x1B): maker 1, product 2, version 0, serial 3, blank UUID, SKU 0, family 0
        bytes({1, 0x1B, 0, 0, 1, 2, 0, 3});
        zeros(16);
        bytes({6, 0, 0});
        t += std::string("Dell Inc.\0Latitude E6420\0 7XYZ12 \0\0", 35);
        // type 17: an 8 GB DDR3 module (length 0x28)
        size_t m = t.size();
        bytes({17, 0x28, 1, 0});
        zeros(0x28 - 4);
        t[m + 0x0C] = 0x00;
        t[m + 0x0D] = 0x20;  // 8192 MB
        t[m + 0x10] = 1;     // locator
        t[m + 0x11] = 2;     // bank
        t[m + 0x12] = 0x18;  // DDR3
        t[m + 0x15] = (char)(1333 & 0xFF);
        t[m + 0x16] = (char)(1333 >> 8);
        t[m + 0x17] = 3;     // maker
        t[m + 0x1A] = 4;     // part number
        t[m + 0x20] = (char)(1600 & 0xFF);
        t[m + 0x21] = (char)(1600 >> 8);
        t += std::string("DIMM A\0BANK 0\0Samsung\0M471B1G73\0\0", 33);
        // type 17: an empty slot (size 0), no strings
        bytes({17, 0x28, 2, 0});
        zeros(0x28 - 4);
        bytes({0, 0});
        bytes({127, 4, 3, 0, 0, 0});
        SmbiosInfo s;
        log.check(parseSmbios(t, s) && s.maker == L"Dell Inc." && s.model == L"Latitude E6420" && s.serial == L"7XYZ12" && s.uuid.empty(),
                  L"The PC's maker, model and serial number are read from the SMBIOS table");
        log.check(s.mems.size() == 1 && s.mems[0].mb == 8192 && s.mems[0].type == L"DDR3" && s.mems[0].mhz == 1600 && s.mems[0].slot == L"DIMM A" && s.mems[0].part == L"M471B1G73",
                  L"Memory modules are read (size, type, speed, slot, part number); empty slots are left out");
        log.check(hardwareTitle(s, L"x") == L"Dell Inc. Latitude E6420 (serial 7XYZ12)", L"A collection is filed under maker, model and serial number");
        SmbiosInfo live = readSmbios();
        log.check(live.found, L"This PC's own SMBIOS tables are read");
        log.line(L"  this PC: " + hardwareTitle(live, L"?") + L" · " + std::to_wstring(live.mems.size()) + L" memory modules");
    }

    log.line(L"Rescue stick names");
    {
        StickMarker old = parseStickMarker("0.3.0");
        log.check(old.found && old.version == L"0.3.0" && old.name.empty(), L"A marker of an earlier version (version only) is read");
        StickMarker named = parseStickMarker("0.4.0\r\nname=Workshop 1\r\n");
        log.check(named.version == L"0.4.0" && named.name == L"Workshop 1", L"A named stick's version and name are read");
        wstr root = work + L"\\stick\\";
        CreateDirectoryW((work + L"\\stick").c_str(), nullptr);
        bool wrote = writeStickMarker(root, L"0.4.0", L"Van kit\r\nname=injected");
        StickMarker back = readStickMarker(root);
        log.check(wrote && back.found && back.version == L"0.4.0" && back.name == L"Van kitname=injected", L"A name is written on one line (line breaks in it are dropped) and read back");
    }

    log.line(L"Memory (RAM) test result");
    {
        wstr pass = L"Event[0]:\r\n  Log Name: System\r\n  Source: Microsoft-Windows-MemoryDiagnostics-Results\r\n  Date: 2024-06-01T12:34:56.789\r\n  Event ID: 1201\r\n  Level: Information\r\n  Description:\r\nThe Windows Memory Diagnostic tested the computer's memory and detected no errors.\r\n";
        MemTestResult r;
        log.check(parseMemoryEvent(pass, r) && r.found && !r.errors && r.when == L"2024-06-01 12:34" && !r.detail.empty(), L"A clean memory-test result is read, with its date");
        wstr fail = L"Event[0]:\r\n  Source: Microsoft-Windows-MemoryDiagnostics-Results\r\n  Date: 2023-11-20T08:00:00.000\r\n  Event ID: 1101\r\n  Description:\r\nThe Windows Memory Diagnostic detected hardware errors.\r\n";
        MemTestResult r2;
        log.check(parseMemoryEvent(fail, r2) && r2.found && r2.errors, L"A result that found errors is read as errors");
        MemTestResult r3;
        log.check(!parseMemoryEvent(L"No events were found.", r3) && !r3.found, L"No result on record is not mistaken for a pass");
    }

    log.line(L"Repair tools record");
    {
        wstr win = work + L"\\reagent\\Windows";
        put(win + L"\\System32\\Recovery\\ReAgent.xml",
            "<?xml version='1.0' encoding='utf-8'?>\r\n<WindowsRE version=\"2.0\">\r\n  <WinreLocation path=\"\\Recovery\\WindowsRE\" id=\"0\" offset=\"238983053312\" guid=\"{0f062a9a}\"/>\r\n  <InstallState state=\"1\"/>\r\n</WindowsRE>\r\n");
        ReAgentInfo ra = readReAgent(win);
        log.check(ra.found && ra.enabled && ra.offset == 238983053312ull, L"Windows' record: troubleshoot options on, and on which partition");
        put(win + L"\\System32\\Recovery\\ReAgent.xml", "<WindowsRE><WinreLocation path=\"\" id=\"0\" offset=\"0\"/><InstallState state=\"0\"/></WindowsRE>");
        ReAgentInfo off = readReAgent(win);
        log.check(off.found && !off.enabled && off.offset == 0, L"Windows' record: switched off");
        log.check(!readReAgent(work + L"\\nothing\\Windows").found, L"No record: nothing assumed");
    }

    log.line(L"Keys of an earlier stick and of this PC");
    {
        auto key = [](const wchar_t* id, const wchar_t* when, const wchar_t* owner) {
            SavedKey k;
            k.id = id;
            k.name = id;
            k.when = when;
            k.owner = owner;
            k.key = L"111111-222222-333333-444444-555555-666666-000000-123453";
            return k;
        };
        wstr stick = work + L"\\stick-keys.dat", pc = work + L"\\pc-keys.dat", err;
        bool ok = storeKeysFile(stick, {key(L"A", L"2026-11-01 10:00", L"Ann's laptop"), key(L"B", L"2026-11-01 10:00", L"")}, L"same pass", err) &&
                  storeKeysFile(pc, {key(L"B", L"2026-11-13 09:00", L"Office"), key(L"C", L"2026-11-13 09:00", L"")}, L"same pass", err);
        int added = ok ? mergeKeyFiles(stick, pc, L"same pass") : -2;
        std::vector<SavedKey> all;
        loadKeysFile(stick, L"same pass", all);
        const SavedKey* b = findKey(all, L"B");
        log.check(added == 1 && all.size() == 3 && b && b->when == L"2026-11-13 09:00" && b->owner == L"Office", L"Same password: the keys are merged, the newer one wins");
        storeKeysFile(pc, {key(L"D", L"2026-11-13 09:00", L"")}, L"other pass", err);
        log.check(mergeKeyFiles(stick, pc, L"same pass") == -1, L"Different passwords: not merged (both files are kept)");
    }

    log.line(L"Rescue stick size");
    {
        Report none;
        RescueSource re;
        re.path = L"D:\\Recovery\\WindowsRE\\Winre.wim";
        re.size = 600ull << 20;
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        RescueNeeds n = rescueNeeds(none, re, exe);
        log.check(n.image > re.size && n.repairCopy == re.size && n.onStick > n.image + n.repairCopy && n.stickSize == (2ull << 30),
                  L"From a 600 MB Winre.wim: about " + sizeText(n.onStick) + L" on the stick, a 2 GB stick suggested");
        RescueSource boot = re;
        boot.path = L"E:\\sources\\boot.wim";
        RescueNeeds b = rescueNeeds(none, boot, exe);
        log.check(b.repairCopy == 0 && b.stickSize == (1ull << 30), L"From an install stick's boot.wim (no repair-tools copy): a 1 GB stick is enough");
        log.check(n.work > re.size * 2 && n.imageFiles >= 2 * n.onStick, L"Room on the PC while it is made, and for the image files, is estimated too");
    }

    log.line(L"Windows Repair");
    {
        std::string dump(0x80, '\0');
        memcpy(&dump[0], "PAGEDU64", 8);
        dump[0x38] = 0x7B;
        wstr name, meaning;
        describeStopCode(dumpStopCode(dump), name, meaning);
        log.check(dumpStopCode(dump) == 0x7B && name == L"INACCESSIBLE_BOOT_DEVICE" && !meaning.empty(), L"A crash file's stop code is read and explained");
        {
            // 64-bit: parameter 1 holds an NTSTATUS
            std::string d64(0x80, '\0');
            memcpy(&d64[0], "PAGEDU64", 8);
            d64[0x38] = 0x7B;
            uint32_t st = 0xC0000032;
            memcpy(&d64[0x40], &st, 4);
            uint32_t code = 0;
            uint64_t p[4];
            log.check(dumpBugCheck(d64, code, p) && code == 0x7B && p[0] == 0xC0000032ull, L"A 64-bit dump's stop code and parameters are read");
            // 32-bit: the stop code is at 0x28 (0x20 is the machine type)
            std::string d32(0x80, '\0');
            memcpy(&d32[0], "PAGEDUMP", 8);
            uint32_t mt = 0x14C, c32 = 0x7B, s32 = 0xC0000034;
            memcpy(&d32[0x20], &mt, 4);
            memcpy(&d32[0x28], &c32, 4);
            memcpy(&d32[0x2C], &s32, 4);
            log.check(dumpBugCheck(d32, code, p) && code == 0x7B && p[0] == 0xC0000034ull, L"A 32-bit dump's stop code is read from the right place");
            // the verdicts
            auto ft = [](uint64_t v) {
                FILETIME f;
                f.dwLowDateTime = (DWORD)v;
                f.dwHighDateTime = (DWORD)(v >> 32);
                return f;
            };
            const uint64_t day = 864000000000ULL, now = 133000000000000000ULL;
            BootStage b;
            b.ran = true;
            b.lastRun = ft(now - 3 * day);
            b.dump = true;
            b.dumpTime = ft(now);
            b.code = 0x7B;
            b.codeName = L"INACCESSIBLE_BOOT_DEVICE";
            b.param[0] = 0xC0000032;
            judgeBootStage(b);
            log.check(b.stage == BS_KERNEL_NO_DISK && b.diskSuspect && !b.driverSuspect && !b.dumpStale, L"0x7B with a disk-error status: the kernel ran and lost its disk; the disk is the suspect");
            b.param[0] = 0xFFFFA00012345678ull;  // a pointer, not a status
            b.param[1] = 0xC0000034;
            judgeBootStage(b);
            log.check(b.stage == BS_KERNEL_NO_DISK && b.driverSuspect && !b.diskSuspect, L"0x7B with \"device not found\": the storage driver / controller mode is the suspect");
            b.dumpTime = ft(now - 30 * day);
            b.lastRun = ft(now - 1 * day);
            judgeBootStage(b);
            log.check(b.dumpStale && b.stage == BS_NO_SIGN, L"A dump older than the last normal run is called out as an earlier problem");
            BootStage none;
            judgeBootStage(none);
            log.check(none.stage == BS_NO_SIGN && !none.detail.empty(), L"No dump, boot log or repair log: says how to find out");
            wstr srt = L"Startup Repair diagnosis and repair log\r\n---\r\nRoot cause found: \r\n---------------------------\r\nBoot critical file c:\\windows\\system32\\drivers\\iastorv.sys is corrupt.\r\n\r\nRepair action: File repair\r\nResult: Failed.\r\n";
            log.check(srtRootCause(srt) == L"Boot critical file c:\\windows\\system32\\drivers\\iastorv.sys is corrupt.", L"Automatic Repair's root cause is read from its log");
        }
        wstr table = L"Package Identity | State | Release Type | Install Time\r\n"
                     L"--------------------------------------------\r\n"
                     L"Package_for_KB5040001~31bf3856ad364e35~amd64~~26100.1.1 | Installed | Security Update | 10/10/2026 9:00 AM\r\n"
                     L"Microsoft-Windows-Foundation-Package~31bf3856ad364e35~amd64~~10.0.26100.1 | Installed | Foundation | 1/1/2026 1:00 AM\r\n"
                     L"Package_for_RollupFix~31bf3856ad364e35~amd64~~26100.2033.1.8 | Installed | Update | 11/12/2026 8:00 PM\r\n";
        auto ups = parsePackageTable(table);
        log.check(ups.size() == 2 && ups[0].package.find(L"RollupFix") != wstr::npos && ups[0].when == L"11/12/2026 8:00 PM", L"Installed updates are listed, newest first, without Windows' own base parts");
        std::string f = vhdFooter(1ull << 30);
        uint32_t sum = 0;
        for (size_t i = 0; i < 512; ++i)
            if (i < 64 || i >= 68) sum += (uint8_t)f[i];
        uint32_t stored = (uint32_t)(uint8_t)f[64] << 24 | (uint8_t)f[65] << 16 | (uint8_t)f[66] << 8 | (uint8_t)f[67];
        log.check(f.size() == 512 && !memcmp(f.data(), "conectix", 8) && stored == ~sum && f[63] == 2, L"Disk images get a valid fixed .vhd footer");
        std::string hive(4096, '\0');
        memcpy(&hive[0], "regf", 4);
        const wchar_t* path = L"\\??\\C:\\Users\\Ann\\ntuser.dat";
        memcpy(&hive[0x30], path, wcslen(path) * 2);
        log.check(hiveNameFromHeader(hive) == L"ntuser.dat", L"A registry file found by its contents is named from its own header");
        FoundFile a, b, c, d;
        a.name = L"SOFTWARE";
        a.folder = L"Windows\\System32\\config";
        a.kind = fileKindOf(a.name);
        b.name = L"1b2c3d4e-0000";
        b.folder = L"Users\\Ann\\AppData\\Roaming\\Microsoft\\Protect\\S-1-5-21";
        c.name = L"holiday.jpg";
        c.folder = L"Users\\Ann\\Downloads";
        d.name = L"Outlook.pst";
        d.folder = L"Users\\Ann\\Documents\\Outlook Files";
        log.check(isWindowsEssential(a) && a.kind == FK_REGISTRY && isWindowsEssential(b) && isWindowsEssential(d) && !isWindowsEssential(c),
                  L"Windows essentials: registry, password keys and mail, not any download");
    }

    log.line(L"Technician tools");
    {
        auto disk = [](int bus, int rpm, bool removableMedia, const wchar_t* model, const wchar_t* ff) {
            Disk d;
            d.busType = bus;
            d.rpm = rpm;
            d.removableMedia = d.removable = removableMedia;
            d.model = model;
            d.formFactor = ff;
            classifyDisk(d);
            return d;
        };
        Disk a = disk(17, 0, false, L"Samsung SSD 980", L""), b = disk(11, 7200, false, L"WDC WD10EZEX", L"3.5\""), c = disk(11, 1, false, L"Crucial MX500", L"2.5\""),
             e = disk(7, 0, true, L"SanDisk Ultra", L""), f = disk(12, 0, true, L"SD card", L""), g = disk(15, 0, false, L"Msft Virtual Disk", L""), h = disk(9, 0, false, L"NAS LUN", L"");
        log.check(a.group == DG_FIXED && a.typeText.find(L"NVMe") != wstr::npos && b.typeText.find(L"7200") != wstr::npos && b.typeText.find(L"3.5") != wstr::npos && c.typeText.find(L"SATA SSD") != wstr::npos,
                  L"Fixed disks: NVMe SSD, hard disk with its speed and size, SATA SSD");
        log.check(e.group == DG_REMOVABLE && f.group == DG_REMOVABLE && f.typeText.find(L"SD") != wstr::npos && g.group == DG_ENTERPRISE && h.group == DG_ENTERPRISE,
                  L"Removable devices, memory cards, and virtual and network storage are grouped apart");
        wstr prov;
        std::string inf = "[Version]\r\nSignature=\"$WINDOWS NT$\"\r\nClass = SCSIAdapter\r\nClassGuid={4D36E97B-E325-11CE-BFC1-08002BE10318}\r\nProvider=%INTEL%\r\n[Strings]\r\nINTEL = \"Intel Corporation\"\r\n";
        log.check(infIsStorageDriver(inf, prov) && prov == L"Intel Corporation", L"A storage driver's .inf is recognised, with its maker");
        log.check(!infIsStorageDriver("[Version]\r\nClass=Net\r\n", prov), L"Other drivers are not offered");
        std::string packed = packBits(std::string(300, '\xFF') + "abc");
        log.check(packed.size() < 20 && (uint8_t)packed.back() == 128, L"The job sheet's page picture is packed (run-length)");
        std::string u16("\xFF\xFE", 2);
        const wchar_t* w = L"Root cause found";
        u16.append((const char*)w, wcslen(w) * 2);
        log.check(decodeText(u16) == L"Root cause found" && decodeText("plain") == L"plain", L"Logs are read in UTF-16 and UTF-8");
        Install none;
        none.winDir = work + L"\\nolog\\Windows";
        none.root = work + L"\\nolog\\";
        put(none.winDir + L"\\System32\\LogFiles\\Srt\\SrtTrail.txt", u16);
        auto logs = windowsLogs(none);
        wstr err;
        auto lines = logs.empty() ? std::vector<wstr>() : readLogTail(logs[0], 50, err);
        log.check(logs.size() == 1 && lines.size() == 1 && lines[0] == L"Root cause found", L"Automatic Repair's log is found and read");
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        wstr maker = fileCompany(exe);
        log.check(maker.find(L"registry") == wstr::npos && maker.size() < 60, L"A program file's maker is read from its version information: " + (maker.empty() ? wstr(L"(none)") : maker));
    }

    log.line(L"Make a suggestion");
    {
        // The percent-encoder used to build the GitHub / mailto links.
        log.check(urlEncode(L"abc-_.~ABC123") == "abc-_.~ABC123", L"Unreserved characters are left as-is");
        log.check(urlEncode(L"a b") == "a%20b", L"A space becomes %20, not '+'");
        log.check(urlEncode(L"line1\nline2") == "line1%0Aline2", L"A newline is encoded");
        log.check(urlEncode(L"say \"hi\"") == "say%20%22hi%22", L"Quotes are encoded");
        log.check(urlEncode(L"caf\u00e9") == "caf%C3%A9", L"Non-ASCII (\u00e9) is UTF-8 then %XX");

        // The one-line suggestions.txt record: fields in order, newlines escaped.
        wstr rec = suggestionRecord(L"2026-11-23 10:40", L"An idea", L"0.5.0", L"Pat", L"line one\nline two");
        log.check(rec == L"2026-11-23 10:40 | An idea | app 0.5.0 | Pat | line one\\nline two", L"Record has fields in order, newline escaped");
        log.check(rec.find(L'\n') == wstr::npos, L"The record is a single line");
        wstr anon = suggestionRecord(L"2026-11-23 10:40", L"What worked", L"0.5.0", L"", L"path C:\\\\x\r\ny");
        log.check(anon == L"2026-11-23 10:40 | What worked | app 0.5.0 | - | path C:\\\\\\\\x\\ny", L"Empty contact is '-', backslash and CRLF escaped");
    }

    log.line(L"Demo data");
    {
        Report d = demoReport();
        log.check(d.installs.size() == 4 && d.installs[3].linux && d.installs[1].loop && !d.installs[2].atRoot, L"Demo PC has a running, a looping and a moved Windows");
        for (const Install& i : d.installs)
            for (const Link& l : i.chain)
                for (int f : l.fixes) log.check(f >= 0 && f < (int)i.plan.size(), L"Demo fix index valid: " + i.name + L" / " + l.name);
    }

    deleteFolderTree(work);
    log.line(L"");
    log.line(std::to_wstring(log.passed) + L" passed, " + std::to_wstring(log.failed) + L" failed");
    std::string u = "\xEF\xBB\xBF" + wideToUtf8(log.text);
    HANDLE h = CreateFileW((folder + L"\\selftest.log").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD wr = 0;
        WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr);
        CloseHandle(h);
    }
    return log.failed ? 1 : 0;
}

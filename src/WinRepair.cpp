#include "WinRepair.h"
#include "Lang.h"
#include "Repair.h"
#include "core/Platform.h"
#include <winioctl.h>
#include <ctime>

namespace bd {

namespace {
bool exists(const wstr& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

wstr fileTimeText(const FILETIME& ft) {
    FILETIME local;
    SYSTEMTIME st;
    FileTimeToLocalFileTime(&ft, &local);
    FileTimeToSystemTime(&local, &st);
    wchar_t b[32];
    swprintf(b, 32, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return b;
}
}  // namespace

// ================================================================== crashes
// The dump header: 64-bit ("PAGEDU64") stop code at 0x38, its parameters (8 bytes each) from 0x40;
// 32-bit ("PAGEDUMP") stop code at 0x28, its parameters (4 bytes each) from 0x2C.
bool dumpBugCheck(const std::string& h, uint32_t& code, uint64_t p[4]) {
    auto u32 = [&](size_t o) -> uint32_t { return o + 4 <= h.size() ? (uint8_t)h[o] | (uint8_t)h[o + 1] << 8 | (uint8_t)h[o + 2] << 16 | (uint32_t)(uint8_t)h[o + 3] << 24 : 0; };
    code = 0;
    for (int i = 0; i < 4; ++i) p[i] = 0;
    if (h.size() >= 0x60 && !memcmp(h.data(), "PAGEDU64", 8)) {
        code = u32(0x38);
        for (int i = 0; i < 4; ++i) p[i] = (uint64_t)u32(0x40 + i * 8) | (uint64_t)u32(0x44 + i * 8) << 32;
        return true;
    }
    if (h.size() >= 0x3C && !memcmp(h.data(), "PAGEDUMP", 8)) {
        code = u32(0x28);
        for (int i = 0; i < 4; ++i) p[i] = u32(0x2C + i * 4);
        return true;
    }
    return false;
}

uint32_t dumpStopCode(const std::string& h) {
    uint32_t code = 0;
    uint64_t p[4];
    dumpBugCheck(h, code, p);
    return code;
}

void describeStopCode(uint32_t code, wstr& name, wstr& meaning) {
    struct S {
        uint32_t code;
        const wchar_t* name;
        const wchar_t* meaning;
    };
    static const S k[] = {
        {0x7B, L"INACCESSIBLE_BOOT_DEVICE", N(L"Windows could not read its own disk while starting: a storage driver is off or missing (often after the SATA mode was changed between AHCI, RAID and VMD in the firmware, or after moving the disk to another PC), or the disk itself has problems. Try \"Turn the disk drivers back on\" and check the SATA mode.")},
        {0xED, L"UNMOUNTABLE_BOOT_VOLUME", N(L"The Windows drive could not be opened: its file system is damaged. Run \"Check the drive\".")},
        {0xEF, L"CRITICAL_PROCESS_DIED", N(L"A process Windows needs stopped: often damaged system files or a failed update. Try \"Repair Windows system files\", then \"Repair the Windows image\", or remove the last update.")},
        {0xC000021A, L"STATUS_SYSTEM_PROCESS_TERMINATED", N(L"A core part of Windows stopped while starting: usually damaged system files, registry or a failed update. Try \"Undo unfinished updates\", \"Put back the registry's backup\" and \"Repair Windows system files\".")},
        {0x50, L"PAGE_FAULT_IN_NONPAGED_AREA", N(L"A driver or the memory used memory it should not: often a faulty driver, antivirus or a memory fault. Start in Safe Mode and remove recent drivers or programs; test the memory.")},
        {0x0A, L"IRQL_NOT_LESS_OR_EQUAL", N(L"A driver misbehaved: usually a recently installed or updated driver. Start in Safe Mode and remove or roll it back.")},
        {0xD1, L"DRIVER_IRQL_NOT_LESS_OR_EQUAL", N(L"A driver misbehaved (often network, graphics or antivirus drivers). Start in Safe Mode and update or remove it.")},
        {0x3B, L"SYSTEM_SERVICE_EXCEPTION", N(L"A driver or Windows component failed: often graphics drivers or damaged system files. Try \"Repair Windows system files\" and update the graphics driver.")},
        {0x1E, L"KMODE_EXCEPTION_NOT_HANDLED", N(L"A driver failed: usually a recent driver. Start in Safe Mode and remove it.")},
        {0x7E, L"SYSTEM_THREAD_EXCEPTION_NOT_HANDLED", N(L"A driver failed while Windows was starting or running: often graphics or storage drivers. Start in Safe Mode and update or remove it.")},
        {0x133, L"DPC_WATCHDOG_VIOLATION", N(L"A driver or the disk did not answer in time: often old SSD firmware or storage drivers. Update the SSD firmware and storage drivers.")},
        {0x124, L"WHEA_UNCORRECTABLE_ERROR", N(L"The hardware reported an error it could not correct: overheating, overclocking, the processor, memory or power supply. Check the hardware.")},
        {0x9F, L"DRIVER_POWER_STATE_FAILURE", N(L"A driver failed going to or coming back from sleep. Update the drivers (often graphics, Wi-Fi or USB).")},
        {0x139, L"KERNEL_SECURITY_CHECK_FAILURE", N(L"Windows found damaged data: often a faulty driver, damaged system files or memory. Repair the system files and test the memory.")},
        {0x1A, L"MEMORY_MANAGEMENT", N(L"Memory went wrong: often a memory module that is failing. Test the memory (Windows Memory Diagnostic).")},
        {0x154, L"UNEXPECTED_STORE_EXCEPTION", N(L"Windows' memory compression store failed: often the disk (check its health) or antivirus.")},
        {0x19, L"BAD_POOL_HEADER", N(L"A driver damaged memory. Start in Safe Mode and remove recent drivers or antivirus.")},
        {0xC2, L"BAD_POOL_CALLER", N(L"A driver used memory wrongly. Start in Safe Mode and remove recent drivers.")},
        {0xF4, L"CRITICAL_OBJECT_TERMINATION", N(L"A core part of Windows stopped: often the disk failing or a loose cable. Check the disk's health and its cable.")},
        {0x7A, L"KERNEL_DATA_INPAGE_ERROR", N(L"Windows could not read data back from the disk: a failing disk, a bad cable or a damaged page file. Check the disk's health.")},
        {0x77, L"KERNEL_STACK_INPAGE_ERROR", N(L"Windows could not read from the disk: a failing disk or cable. Check the disk's health.")},
        {0xE2, L"MANUALLY_INITIATED_CRASH", N(L"The crash was started on purpose (a key combination or a tool). Nothing to repair.")},
    };
    for (const S& s : k)
        if (s.code == code) {
            name = s.name;
            meaning = T(s.meaning);
            return;
        }
    wchar_t b[24];
    swprintf(b, 24, L"0x%08X", code);
    name = code ? b : L"?";
    meaning = code ? T(L"Look this stop code up together with the name of the PC's model; a recently changed driver or update is the usual cause.") : T(L"The crash file could not be read.");
}

std::vector<CrashInfo> recentCrashes(const wstr& winDir, size_t max) {
    struct F {
        wstr path;
        FILETIME t;
    };
    std::vector<F> files;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((winDir + L"\\Minidump\\*.dmp").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do files.push_back({winDir + L"\\Minidump\\" + fd.cFileName, fd.ftLastWriteTime});
        while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (GetFileAttributesExW((winDir + L"\\MEMORY.DMP").c_str(), GetFileExInfoStandard, &a)) files.push_back({winDir + L"\\MEMORY.DMP", a.ftLastWriteTime});
    std::sort(files.begin(), files.end(), [](const F& x, const F& y) { return CompareFileTime(&x.t, &y.t) > 0; });
    std::vector<CrashInfo> out;
    for (const F& f : files) {
        if (out.size() >= max) break;
        CrashInfo c;
        c.file = f.path;
        c.when = fileTimeText(f.t);
        HANDLE fh = CreateFileW(f.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (fh != INVALID_HANDLE_VALUE) {
            std::string head(0x80, '\0');
            DWORD rd = 0;
            if (ReadFile(fh, &head[0], (DWORD)head.size(), &rd, nullptr)) {
                head.resize(rd);
                c.code = dumpStopCode(head);
            }
            CloseHandle(fh);
        }
        describeStopCode(c.code, c.name, c.meaning);
        out.push_back(c);
    }
    return out;
}

// ================================================================== how far the last start got
wstr ntStatusMeaning(uint32_t s, bool& disk, bool& driver) {
    struct S {
        uint32_t code;
        int side;  // 1 the disk itself, 2 the storage driver / controller
        const wchar_t* text;
    };
    static const S k[] = {
        {0xC0000032, 1, N(L"STATUS_DISK_CORRUPT_ERROR: the disk returned damaged data — bad sectors or a damaged file system")},
        {0xC0000185, 1, N(L"STATUS_IO_DEVICE_ERROR: the disk did not answer reads properly — a failing disk, cable or port")},
        {0xC000009C, 1, N(L"STATUS_DEVICE_DATA_ERROR: the disk reported unreadable sectors")},
        {0xC0000102, 1, N(L"STATUS_FILE_CORRUPT_ERROR: a file Windows needs is damaged (often it sits on bad sectors)")},
        {0xC000014F, 1, N(L"STATUS_UNRECOGNIZED_VOLUME: the Windows partition's file system was not recognised — damaged, or locked by BitLocker")},
        {0xC000016A, 1, N(L"STATUS_DISK_OPERATION_FAILED: a read from the disk failed")},
        {0xC00000B5, 1, N(L"STATUS_IO_TIMEOUT: the disk stopped answering")},
        {0xC0000034, 2, N(L"STATUS_OBJECT_NAME_NOT_FOUND: the device Windows starts from was not found — its storage driver did not load, or the controller mode changed")},
        {0xC000000E, 2, N(L"STATUS_NO_SUCH_DEVICE: no driver found the disk controller")},
        {0xC0000225, 2, N(L"STATUS_NOT_FOUND: the start-up device was not found")},
    };
    for (const S& e : k)
        if (e.code == s) {
            (e.side == 1 ? disk : driver) = true;
            return T(e.text);
        }
    return L"";
}

wstr srtRootCause(const wstr& t) {
    wstr last;
    size_t at = 0;
    while ((at = t.find(L"Root cause found", at)) != wstr::npos) {
        size_t a = t.find(L'\n', at);
        wstr cause;
        while (a != wstr::npos && a < t.size()) {
            size_t b = t.find(L'\n', a + 1);
            wstr line = t.substr(a + 1, (b == wstr::npos ? t.size() : b) - a - 1);
            while (!line.empty() && (line.back() == L'\r' || line.back() == L' ' || line.back() == L'\t')) line.pop_back();
            size_t s = line.find_first_not_of(L" \t");
            line = s == wstr::npos ? wstr() : line.substr(s);
            a = b;
            if (line.empty()) {
                if (cause.empty()) continue;  // the blank line before the text
                break;
            }
            if (line.find_first_not_of(L'-') == wstr::npos) continue;  // the ---- underline
            if (istartsWith(line, L"Repair action") || istartsWith(line, L"Result:")) break;
            cause += (cause.empty() ? L"" : L" ") + line;
        }
        if (!cause.empty()) last = cause;
        at += 16;
    }
    return last;
}

static bool fileWriteTime(const wstr& path, FILETIME& t) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) return false;
    t = a.ftLastWriteTime;
    return true;
}

static uint64_t ft64(const FILETIME& t) { return (uint64_t)t.dwHighDateTime << 32 | t.dwLowDateTime; }

void judgeBootStage(BootStage& b) {
    const uint64_t day = 864000000000ULL;  // 100-ns units
    b.status = 0;
    b.statusText.clear();
    b.diskSuspect = b.driverSuspect = false;
    // a dump from long before Windows last ran normally is about an old problem, not today's
    b.dumpStale = b.dump && b.ran && ft64(b.dumpTime) + 2 * day < ft64(b.lastRun);
    if (b.dump && !b.dumpStale) {
        for (int i = 0; i < 4 && !b.status; ++i) {
            uint64_t p = b.param[i];
            if ((p >> 32) == 0 && (p & 0xF0000000u) == 0xC0000000u) {
                bool d = false, r = false;
                wstr m = ntStatusMeaning((uint32_t)p, d, r);
                if (!m.empty()) {
                    b.status = (uint32_t)p;
                    b.statusText = m;
                    b.diskSuspect = d;
                    b.driverSuspect = r;
                }
            }
        }
        bool noDisk = b.code == 0x7B || b.code == 0xED;
        b.stage = noDisk ? BS_KERNEL_NO_DISK : BS_KERNEL_STOPPED;
        if (noDisk) {
            b.verdict = TF(L"Windows got as far as its kernel, which then could not reach its own disk ({0}).", b.codeName);
            if (b.diskSuspect)
                b.detail = b.statusText + L". " + T(L"This points at the disk itself, not at the start-up files or drivers: back up first, check the disk's health, run a surface test, then check the drive for errors — or move Windows to a healthy disk.");
            else if (b.driverSuspect)
                b.detail = b.statusText + L". " + T(L"This points at the storage driver or the controller mode (AHCI / RAID) in the firmware settings: check which mode Windows was installed in and turn its storage drivers on.");
            else
                b.detail = T(L"It is either the disk itself (bad sectors, damaged file system) or the storage driver / controller mode. Check the disk's health first, then the storage drivers.");
        } else {
            b.verdict = TF(L"Windows got as far as its kernel, then stopped with {0}.", b.codeName);
            b.detail = b.codeMeaning;
        }
    } else if (b.srt && !b.srtCause.empty()) {
        b.stage = BS_UNKNOWN;
        b.verdict = T(L"Automatic Repair found a cause it could not fix:");
        b.detail = b.srtCause;
    } else if (b.bootLog && (!b.ran || ft64(b.bootLogTime) + day / 24 >= ft64(b.lastRun))) {
        b.stage = BS_KERNEL_RAN;
        b.verdict = T(L"The boot log shows that Windows' kernel started and loaded drivers at its last start.");
        b.detail = T(L"The trouble comes later in the start: read the boot log under Logs — the driver after the last one loaded is the suspect.");
    } else {
        b.stage = BS_NO_SIGN;
        b.verdict = T(L"Nothing shows that Windows started since it last worked.");
        b.detail = T(L"There is no newer crash dump, boot log or repair log. Either the start fails before Windows loads (firmware, start-up files, disk not found), or Windows could not write to its disk — a stop with INACCESSIBLE_BOOT_DEVICE often leaves no dump at all. To find out, switch on Boot logging and Show drivers while starting (Start-up options), start the PC once, and check again.");
    }
    if (b.dumpStale)
        b.detail += (b.detail.empty() ? L"" : L"\n") + TF(L"The newest crash dump ({0}) is older than the last time Windows ran normally, so it is about an earlier problem.", b.codeName);
}

BootStage readBootStage(const wstr& winDir) {
    BootStage b;
    b.ran = fileWriteTime(winDir + L"\\System32\\config\\SYSTEM", b.lastRun);
    std::vector<CrashInfo> c = recentCrashes(winDir, 1);
    if (!c.empty()) {
        b.dump = true;
        fileWriteTime(c[0].file, b.dumpTime);
        HANDLE fh = CreateFileW(c[0].file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (fh != INVALID_HANDLE_VALUE) {
            std::string head(0x80, '\0');
            DWORD rd = 0;
            if (ReadFile(fh, &head[0], (DWORD)head.size(), &rd, nullptr)) {
                head.resize(rd);
                dumpBugCheck(head, b.code, b.param);
            }
            CloseHandle(fh);
        }
        describeStopCode(b.code, b.codeName, b.codeMeaning);
        if (b.codeName.empty()) {
            wchar_t hx[16];
            swprintf(hx, 16, L"0x%X", b.code);
            b.codeName = hx;
        }
    }
    b.bootLog = fileWriteTime(winDir + L"\\ntbtlog.txt", b.bootLogTime);
    wstr srt = winDir + L"\\System32\\LogFiles\\Srt\\SrtTrail.txt";
    if (fileWriteTime(srt, b.srtTime)) {
        b.srt = true;
        std::string raw;
        if (readFileText(srt, raw)) b.srtCause = srtRootCause(decodeText(raw));
    }
    judgeBootStage(b);
    if (b.ran) b.evidence.push_back({T(L"Windows last saved its settings (it ran)"), fileTimeText(b.lastRun), L""});
    if (b.dump) b.evidence.push_back({T(L"Newest crash dump"), fileTimeText(b.dumpTime), b.codeName + (b.status ? L" · " + b.statusText.substr(0, b.statusText.find(L':')) : wstr())});
    if (b.bootLog) b.evidence.push_back({T(L"Boot log (ntbtlog.txt)"), fileTimeText(b.bootLogTime), L""});
    if (b.srt) b.evidence.push_back({T(L"Automatic Repair log (SrtTrail.txt)"), fileTimeText(b.srtTime), b.srtCause});
    return b;
}

// ================================================================== installed updates
std::vector<UpdateEntry> parsePackageTable(const wstr& o) {
    // "Package Identity | State | Release Type | Install Time" (the words are translated; the bars are not)
    std::vector<UpdateEntry> out;
    size_t a = 0;
    while (a < o.size()) {
        size_t b = o.find(L'\n', a);
        if (b == wstr::npos) b = o.size();
        wstr line = o.substr(a, b - a);
        a = b + 1;
        if (line.find(L'~') == wstr::npos) continue;  // package identities have ~ in them, headers do not
        std::vector<wstr> col;
        size_t p = 0;
        for (;;) {
            size_t q = line.find(L'|', p);
            wstr c = line.substr(p, q == wstr::npos ? wstr::npos : q - p);
            while (!c.empty() && iswspace(c.back())) c.pop_back();
            while (!c.empty() && iswspace(c.front())) c.erase(0, 1);
            col.push_back(c);
            if (q == wstr::npos) break;
            p = q + 1;
        }
        if (col.size() < 4) continue;
        // only updates: Microsoft's own feature and language packs are left out
        if (col[0].find(L"Package_for_") == wstr::npos && !icontains(col[2], L"update") && !icontains(col[2], L"security")) continue;
        out.push_back(UpdateEntry{col[0], col[3], col[2]});
    }
    // newest first (the time is as DISM writes it; most recent are at the bottom of its list)
    std::reverse(out.begin(), out.end());
    return out;
}

std::vector<UpdateEntry> listUpdates(const Install& in, const Partition& P, wstr& err) {
    wstr dism = findSystemTool(L"dism.exe");
    if (dism.empty()) {
        err = T(L"DISM is not part of this system.");
        return {};
    }
    wstr target;
    if (in.running) target = L"/Online";
    else {
        wstr root = P.letter.empty() ? P.root : P.letter + L"\\";
        if (root.empty()) {
            err = T(L"The Windows drive cannot be opened.");
            return {};
        }
        target = L"/Image:\"" + root + in.folder + L"\"";
    }
    ToolResult t = runTool(dism, target + L" /English /Get-Packages /Format:Table");
    if (t.code != 0) {
        err = T(L"DISM could not list the updates. ") + t.output.substr(t.output.size() > 300 ? t.output.size() - 300 : 0);
        return {};
    }
    return parsePackageTable(t.output);
}

// ================================================================== disk image
std::string vhdFooter(uint64_t size) {
    std::string f(512, '\0');
    auto be32 = [&](size_t o, uint32_t v) {
        for (int i = 0; i < 4; ++i) f[o + i] = (char)(v >> (24 - 8 * i));
    };
    auto be64 = [&](size_t o, uint64_t v) {
        for (int i = 0; i < 8; ++i) f[o + i] = (char)(v >> (56 - 8 * i));
    };
    memcpy(&f[0], "conectix", 8);
    be32(8, 2);                    // features: reserved bit
    be32(12, 0x00010000);          // format version
    be64(16, 0xFFFFFFFFFFFFFFFFull);  // data offset: none (fixed disk)
    be32(24, (uint32_t)(time(nullptr) - 946684800));  // seconds since 2000-01-01
    memcpy(&f[28], "pbd ", 4);     // creator application
    be32(32, 0x00010000);
    memcpy(&f[36], "Wi2k", 4);     // creator host OS
    be64(40, size);
    be64(48, size);
    // geometry (cylinders, heads, sectors per track) as the VHD specification works it out
    uint64_t sectors = std::min<uint64_t>(size / 512, 65535ull * 16 * 255);
    uint32_t spt, heads, cth;
    if (sectors >= 65535ull * 16 * 63) {
        spt = 255;
        heads = 16;
        cth = (uint32_t)(sectors / spt);
    } else {
        spt = 17;
        cth = (uint32_t)(sectors / spt);
        heads = (cth + 1023) / 1024;
        if (heads < 4) heads = 4;
        if (cth >= heads * 1024 || heads > 16) {
            spt = 31;
            heads = 16;
            cth = (uint32_t)(sectors / spt);
        }
        if (cth >= heads * 1024) {
            spt = 63;
            heads = 16;
            cth = (uint32_t)(sectors / spt);
        }
    }
    uint32_t cyl = cth / heads;
    f[56] = (char)(cyl >> 8);
    f[57] = (char)cyl;
    f[58] = (char)heads;
    f[59] = (char)spt;
    be32(60, 2);  // disk type: fixed
    for (int i = 0; i < 16; ++i) f[68 + i] = (char)(GetTickCount() * (i + 13) ^ (i * 151));  // unique id
    uint32_t sum = 0;
    for (size_t i = 0; i < 512; ++i)
        if (i < 64 || i >= 68) sum += (uint8_t)f[i];
    be32(64, ~sum);
    return f;
}

ImageResult imageDisk(int disk, const wstr& outFile, const std::function<void(const wstr&, float)>& progress, volatile bool* cancel) {
    ImageResult r;
    HANDLE src = openDiskHandle(disk, false);
    if (src == INVALID_HANDLE_VALUE) {
        r.result = T(L"The disk could not be opened.");
        return r;
    }
    GET_LENGTH_INFORMATION gl{};
    DWORD n = 0;
    DeviceIoControl(src, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0, &gl, sizeof gl, &n, nullptr);
    uint64_t size = (uint64_t)gl.Length.QuadPart;
    uint32_t sector = diskSectorSize(src);
    HANDLE out = CreateFileW(outFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (!size || out == INVALID_HANDLE_VALUE) {
        CloseHandle(src);
        if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
        r.result = !size ? T(L"The disk's size could not be read.") : T(L"The image file could not be made there.");
        return r;
    }
    toolLog(L"[image] Disk " + std::to_wstring(disk) + L" -> " + outFile);
    const uint32_t chunk = 8u << 20;
    auto t0 = GetTickCount64();
    bool ok = true;
    uint64_t badStart = 0, badLen = 0;
    auto flushBad = [&]() {
        if (!badLen) return;
        r.badRanges.push_back({badStart, badLen});
        r.badList += std::to_wstring(badStart) + L" + " + std::to_wstring(badLen) + L" bytes\r\n";
        badLen = 0;
    };
    auto put = [&](const std::string& b) {
        DWORD wr = 0;
        if (!WriteFile(out, b.data(), (DWORD)b.size(), &wr, nullptr) || wr != b.size()) ok = false;
    };
    for (uint64_t at = 0; at < size && ok;) {
        if (cancel && *cancel) {
            r.result = T(L"Stopped. The image is incomplete.");
            ok = false;
            break;
        }
        uint32_t len = (uint32_t)std::min<uint64_t>(chunk, size - at);
        std::string b;
        if (readDiskAt(src, at, len, b)) {
            flushBad();
            put(b);
        } else {
            // a damaged area: go sector by sector, so as much as possible is kept
            for (uint32_t o = 0; o < len && ok; o += sector) {
                std::string sb;
                if (readDiskAt(src, at + o, sector, sb)) {
                    flushBad();
                    put(sb);
                } else {
                    if (!badLen) badStart = at + o;
                    badLen += sector;
                    r.unreadable += sector;
                    put(std::string(sector, '\0'));
                }
            }
        }
        at += len;
        r.copied = at;
        uint64_t ms = GetTickCount64() - t0;
        if (progress && ((at / len) % 8 == 0 || at == size)) {
            wstr left = ms > 5000 ? L" · " + std::to_wstring((uint64_t)((double)ms / at * (size - at) / 60000)) + T(L" min left") : wstr();
            progress(TF(L"Copying Disk {0}: {1} of {2}", std::to_wstring(disk), sizeText(at), sizeText(size)) + left, (float)at / size);
        }
    }
    flushBad();
    if (ok) put(vhdFooter(size));
    CloseHandle(out);
    CloseHandle(src);
    if (ok && !r.badList.empty()) {
        HANDLE l = CreateFileW((outFile + L".unreadable.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        std::string u = wideToUtf8(T(L"Areas of the disk that could not be read (start in bytes + length); they are zeros in the image:\r\n") + r.badList);
        DWORD wr = 0;
        if (l != INVALID_HANDLE_VALUE) {
            WriteFile(l, u.data(), (DWORD)u.size(), &wr, nullptr);
            CloseHandle(l);
        }
    }
    r.ok = ok;
    if (ok)
        r.result = TF(L"The whole disk ({0}) was copied to {1}.", sizeText(size), outFile) +
                   (r.unreadable ? L" " + TF(L"{0} could not be read and are zeros in the copy (listed in {1}.unreadable.txt).", sizeText(r.unreadable), outFile) : wstr(L" " + T(L"Everything could be read.")));
    else if (r.result.empty())
        r.result = T(L"The image could not be written (is the target drive full, or FAT32, which cannot hold files over 4 GB?).");
    return r;
}

static ToolResult diskpartAt(const wstr& script) {
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    wstr file = wstr(tmp) + L"PATioBD-vhd.txt";
    std::string u = wideToUtf8(script + L"exit\r\n");
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

bool attachImageReadOnly(const wstr& vhd, wstr& err) {
    ToolResult t = diskpartAt(L"select vdisk file=\"" + vhd + L"\"\r\nattach vdisk readonly\r\n");
    if (t.code != 0) err = T(L"The image could not be attached. Diskpart said: ") + t.output.substr(t.output.size() > 300 ? t.output.size() - 300 : 0);
    return t.code == 0;
}

bool detachImage(const wstr& vhd, wstr& err) {
    ToolResult t = diskpartAt(L"select vdisk file=\"" + vhd + L"\"\r\ndetach vdisk\r\n");
    if (t.code != 0) err = t.output;
    return t.code == 0;
}

// ================================================================== logs
wstr decodeText(const std::string& b) {
    if (b.size() >= 2 && (uint8_t)b[0] == 0xFF && (uint8_t)b[1] == 0xFE) return wstr((const wchar_t*)(b.data() + 2), (b.size() - 2) / 2);
    if (b.size() >= 3 && (uint8_t)b[0] == 0xEF && (uint8_t)b[1] == 0xBB && (uint8_t)b[2] == 0xBF) return utf8ToWide(b.substr(3));
    // UTF-16 without a mark: every second byte is zero
    size_t zeros = 0, n = std::min<size_t>(b.size(), 400);
    for (size_t i = 1; i < n; i += 2) zeros += b[i] == 0;
    if (n >= 8 && zeros > n / 3) return wstr((const wchar_t*)b.data(), b.size() / 2);
    return utf8ToWide(b);
}

std::vector<LogFile> windowsLogs(const Install& in) {
    std::vector<LogFile> out;
    auto add = [&](const wstr& title, const wstr& path, const wstr& what, bool ev = false) {
        if (exists(path)) out.push_back(LogFile{title, path, what, ev});
    };
    const wstr& w = in.winDir;
    add(T(L"Automatic Repair (SrtTrail.txt)"), w + L"\\System32\\LogFiles\\Srt\\SrtTrail.txt", T(L"What Windows' own start-up repair tested and what it found wrong. The last lines name the root cause."));
    add(T(L"Boot log (ntbtlog.txt)"), w + L"\\ntbtlog.txt", T(L"Every driver Windows loaded at the last start with boot logging on. The driver after the last loaded one is the suspect."));
    add(T(L"System events: errors (System.evtx)"), w + L"\\System32\\winevt\\Logs\\System.evtx", T(L"Errors and critical events Windows recorded: disk and file-system errors, crashes, failed services, sudden power loss."), true);
    add(T(L"Set-up errors (setuperr.log)"), w + L"\\Panther\\setuperr.log", T(L"Errors of the last Windows installation or upgrade."));
    add(T(L"Upgrade errors (setuperr.log in $WINDOWS.~BT)"), in.root + L"$WINDOWS.~BT\\Sources\\Panther\\setuperr.log", T(L"Errors of an upgrade that failed or was rolled back."));
    add(T(L"Upgrade roll-back (setupact.log)"), in.root + L"$WINDOWS.~BT\\Sources\\Rollback\\setupact.log", T(L"Why an upgrade was undone."));
    add(T(L"Updates and system files (CBS.log)"), w + L"\\Logs\\CBS\\CBS.log", T(L"What update installation and the system file check did and where they failed."));
    add(T(L"DISM (dism.log)"), w + L"\\Logs\\DISM\\dism.log", T(L"What the image repair tool did and where it failed."));
    add(T(L"Disk check results (bootex.log)"), in.root + L"bootex.log", T(L"What the disk check at start-up found (older Windows)."));
    return out;
}

std::vector<wstr> readLogTail(const LogFile& f, size_t maxLines, wstr& err) {
    std::vector<wstr> lines;
    wstr text;
    if (f.eventLog) {
        wstr tool = findSystemTool(L"wevtutil.exe");
        if (tool.empty()) {
            err = T(L"The tool that reads event logs (wevtutil.exe) is not part of this system.");
            return lines;
        }
        ToolResult t = runTool(tool, L"qe \"" + f.path + L"\" /lf:true /c:80 /rd:true /f:text /q:\"*[System[(Level=1 or Level=2)]]\"");
        if (t.code != 0 && t.output.size() < 200) {
            err = T(L"The event log could not be read. ") + t.output;
            return lines;
        }
        text = t.output;
    } else {
        HANDLE h = CreateFileW(f.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            err = T(L"The log could not be opened: ") + errorText(GetLastError());
            return lines;
        }
        LARGE_INTEGER sz{};
        GetFileSizeEx(h, &sz);
        const LONGLONG take = 512 * 1024;  // the end of the file is what matters
        std::string b;
        bool whole = sz.QuadPart <= take;
        if (!whole) {
            LARGE_INTEGER at;
            at.QuadPart = (sz.QuadPart - take) & ~1ll;
            SetFilePointerEx(h, at, nullptr, FILE_BEGIN);
            std::string head(2, '\0');  // keep the encoding of the start
            b.resize((size_t)(sz.QuadPart - at.QuadPart));
        } else b.resize((size_t)sz.QuadPart);
        DWORD rd = 0;
        ReadFile(h, b.empty() ? nullptr : &b[0], (DWORD)b.size(), &rd, nullptr);
        b.resize(rd);
        if (!whole) {
            // was the file UTF-16? look at its first bytes
            std::string first(4, '\0');
            LARGE_INTEGER zero{};
            SetFilePointerEx(h, zero, nullptr, FILE_BEGIN);
            ReadFile(h, &first[0], 4, &rd, nullptr);
            if (rd >= 2 && (uint8_t)first[0] == 0xFF && (uint8_t)first[1] == 0xFE) b = std::string("\xFF\xFE", 2) + b;
        }
        CloseHandle(h);
        text = decodeText(b);
    }
    size_t a = 0;
    while (a < text.size()) {
        size_t e = text.find(L'\n', a);
        if (e == wstr::npos) e = text.size();
        wstr line = text.substr(a, e - a);
        a = e + 1;
        while (!line.empty() && (line.back() == L'\r' || line.back() == 0)) line.pop_back();
        for (wchar_t& c : line)
            if (c == L'\t') c = L' ';
        lines.push_back(line);
    }
    if (lines.size() > maxLines) lines.erase(lines.begin(), lines.end() - maxLines);
    return lines;
}

// ================================================================== surface test
SurfaceResult surfaceTest(int disk, const std::function<void(const wstr&, float)>& progress, volatile bool* cancel) {
    SurfaceResult r;
    HANDLE src = openDiskHandle(disk, false);
    if (src == INVALID_HANDLE_VALUE) {
        r.result = T(L"The disk could not be opened.");
        return r;
    }
    GET_LENGTH_INFORMATION gl{};
    DWORD n = 0;
    DeviceIoControl(src, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0, &gl, sizeof gl, &n, nullptr);
    uint64_t size = (uint64_t)gl.Length.QuadPart;
    uint32_t sector = diskSectorSize(src);
    const uint32_t chunk = 8u << 20;
    auto t0 = GetTickCount64();
    uint64_t badStart = 0, badLen = 0;
    auto flush = [&]() {
        if (!badLen) return;
        ++r.badAreas;
        r.badRanges.push_back({badStart, badLen});
        if (r.badAreas <= 200) r.badList += sizeText(badStart) + L" + " + std::to_wstring(badLen / sector) + L" sectors\r\n";
        badLen = 0;
    };
    toolLog(L"[surface] read test of Disk " + std::to_wstring(disk));
    bool stopped = false;
    for (uint64_t at = 0; at < size;) {
        if (cancel && *cancel) {
            stopped = true;
            break;
        }
        uint32_t len = (uint32_t)std::min<uint64_t>(chunk, size - at);
        std::string b;
        if (readDiskAt(src, at, len, b)) flush();
        else
            for (uint32_t o = 0; o < len; o += sector) {
                std::string sb;
                if (readDiskAt(src, at + o, sector, sb)) flush();
                else {
                    if (!badLen) badStart = at + o;
                    badLen += sector;
                    r.bad += sector;
                }
            }
        at += len;
        r.tested = at;
        uint64_t ms = GetTickCount64() - t0;
        if (progress && ((at / len) % 8 == 0 || at == size)) {
            wstr left = ms > 5000 ? L" · " + std::to_wstring((uint64_t)((double)ms / at * (size - at) / 60000)) + T(L" min left") : wstr();
            progress(TF(L"Reading Disk {0}: {1} of {2}", std::to_wstring(disk), sizeText(at), sizeText(size)) + (r.bad ? L" · " + TF(L"{0} unreadable", sizeText(r.bad)) : wstr()) + left, (float)at / size);
        }
    }
    flush();
    CloseHandle(src);
    r.finished = !stopped;
    wstr scope = stopped ? TF(L"Stopped after {0} of {1}.", sizeText(r.tested), sizeText(size)) : TF(L"All {0} were read.", sizeText(size));
    r.result = scope + L" " + (r.bad ? TF(L"{0} in {1} places could not be read: the disk is failing. Copy your files off it and replace it.", sizeText(r.bad), std::to_wstring(r.badAreas))
                                     : wstr(T(L"Every sector tested could be read.")));
    return r;
}

// ================================================================== registry of a Windows that is not running
std::vector<HiveMount> offlineHives(const Install& in) {
    std::vector<HiveMount> out;
    for (const wchar_t* n : {L"SYSTEM", L"SOFTWARE", L"SAM", L"SECURITY", L"DEFAULT"}) {
        wstr f = in.winDir + L"\\System32\\config\\" + n;
        if (exists(f)) out.push_back(HiveMount{n, f, wstr(L"HKLM\\BD_") + n});
    }
    wstr users = in.root + in.folder + L"Users\\";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((users + L"*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            wstr n = fd.cFileName;
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || n == L"." || n == L".." || iequals(n, L"Public") || iequals(n, L"All Users") || iequals(n, L"Default User")) continue;
            if (!exists(users + n + L"\\NTUSER.DAT")) continue;
            wstr safe;
            for (wchar_t c : n) safe += iswalnum(c) ? c : L'_';
            out.push_back(HiveMount{T(L"User: ") + n, users + n + L"\\NTUSER.DAT", L"HKLM\\BD_USER_" + safe});
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    // which are loaded now (from an earlier click)
    for (HiveMount& m : out) {
        HKEY k = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, m.key.substr(5).c_str(), 0, KEY_READ, &k) == ERROR_SUCCESS) {
            m.loaded = true;
            RegCloseKey(k);
        }
    }
    return out;
}

bool loadHiveForEditing(HiveMount& m, wstr& err) {
    enablePrivilege(L"SeBackupPrivilege");
    enablePrivilege(L"SeRestorePrivilege");
    LONG e = RegLoadKeyW(HKEY_LOCAL_MACHINE, m.key.substr(5).c_str(), m.file.c_str());
    toolLog(L"[registry] load " + m.file + L" as " + m.key + (e ? L": error " + std::to_wstring(e) : wstr()));
    if (e != ERROR_SUCCESS) {
        err = TF(L"{0} could not be loaded (error {1}). The file may be damaged or in use.", m.name, std::to_wstring(e));
        return false;
    }
    m.loaded = true;
    return true;
}

bool unloadHive(HiveMount& m, wstr& err) {
    enablePrivilege(L"SeBackupPrivilege");
    enablePrivilege(L"SeRestorePrivilege");
    LONG e = RegUnLoadKeyW(HKEY_LOCAL_MACHINE, m.key.substr(5).c_str());
    toolLog(L"[registry] unload " + m.key + (e ? L": error " + std::to_wstring(e) : wstr()));
    if (e != ERROR_SUCCESS && e != ERROR_INVALID_PARAMETER && e != ERROR_FILE_NOT_FOUND) {
        err = TF(L"{0} could not be unloaded: close the registry editor first.", m.name);
        return false;
    }
    m.loaded = false;
    return true;
}

// ================================================================== System Restore
bool startSystemRestore(const Install& in, const Partition& P, wstr& err) {
    wstr tool = findSystemTool(L"rstrui.exe");
    if (tool.empty()) {
        err = T(L"System Restore (rstrui.exe) is not part of this system. It is part of Windows and of its recovery environment; a rescue stick made from Winre.wim has it.");
        return false;
    }
    wstr args;
    if (!in.running) {
        wstr root = P.letter.empty() ? wstr() : P.letter + L"\\";
        if (root.empty()) {
            err = T(L"The Windows drive needs a drive letter for this. Give it one in Disks and partitions.");
            return false;
        }
        args = L" /offline:" + root + in.folder + L"Windows=active";
    }
    wstr cmd = L"\"" + tool + L"\"" + args;
    toolLog(L"> " + cmd);
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> b(cmd.begin(), cmd.end());
    b.push_back(0);
    if (!CreateProcessW(nullptr, b.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        err = T(L"System Restore could not be started: ") + errorText(GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// ================================================================== Memory (RAM) test
// Reads one event from wevtutil's text output. Event 1201 = no errors, 1101 = errors found.
bool parseMemoryEvent(const wstr& text, MemTestResult& out) {
    int id = 0;
    wstr when, detail;
    bool inDesc = false;
    size_t a = 0;
    while (a < text.size()) {
        size_t b = text.find(L'\n', a);
        if (b == wstr::npos) b = text.size();
        wstr line = text.substr(a, b - a);
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();
        wstr trimmed = line;
        size_t s = trimmed.find_first_not_of(L" \t");
        if (s != wstr::npos) trimmed = trimmed.substr(s);
        if (inDesc) {
            if (!trimmed.empty()) detail += (detail.empty() ? L"" : L" ") + trimmed;
        } else if (trimmed.rfind(L"Event ID:", 0) == 0) {
            id = _wtoi(trimmed.substr(9).c_str());
        } else if (trimmed.rfind(L"Date:", 0) == 0) {
            when = trimmed.substr(5);
            size_t w = when.find_first_not_of(L" \t");
            when = w == wstr::npos ? wstr() : when.substr(w);
            for (wchar_t& c : when)
                if (c == L'T') c = L' ';  // 2024-06-01T12:34 -> 2024-06-01 12:34
            if (when.size() > 16) when = when.substr(0, 16);
        } else if (trimmed.rfind(L"Description:", 0) == 0) {
            inDesc = true;
        }
        a = b + 1;
    }
    if (id == 0) return false;
    out.found = true;
    out.errors = id == 1101;  // 1201 = no errors, 1101 = errors detected
    out.when = when;
    out.detail = detail;
    return true;
}

MemTestResult lastMemoryTest(const Install& in) {
    MemTestResult r;
    wstr tool = findSystemTool(L"wevtutil.exe");
    if (tool.empty()) return r;
    wstr q = L" /q:\"*[System[Provider[@Name='Microsoft-Windows-MemoryDiagnostics-Results']]]\" /rd:true /c:1 /f:text";
    wstr args;
    if (in.running)
        args = L"qe System" + q;
    else {
        wstr evtx = in.winDir + L"\\System32\\winevt\\Logs\\System.evtx";
        if (GetFileAttributesW(evtx.c_str()) == INVALID_FILE_ATTRIBUTES) return r;
        args = L"qe \"" + evtx + L"\" /lf:true" + q;
    }
    ToolResult t = runTool(tool, args);
    parseMemoryEvent(t.output, r);
    return r;
}

bool startMemoryTest(wstr& err) {
    wstr tool = findSystemTool(L"mdsched.exe");
    if (tool.empty()) {
        err = T(L"Windows Memory Diagnostic (mdsched.exe) is not part of this system. Start the Windows you want to test and run it there, or test the RAM with the PC maker's tool.");
        return false;
    }
    wstr cmd = L"\"" + tool + L"\"";
    toolLog(L"> " + cmd);
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> b(cmd.begin(), cmd.end());
    b.push_back(0);
    if (!CreateProcessW(nullptr, b.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        err = T(L"It could not be started: ") + errorText(GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

}  // namespace bd

// Helpers for the Windows Repair section: what the last crashes were, which updates are
// installed, and a sector-by-sector image of a whole disk (to rescue from a copy).
#pragma once
#include "Diagnose.h"
#include <functional>

namespace bd {

// ---- crashes (blue screens): Windows\Minidump\*.dmp and Windows\MEMORY.DMP
struct CrashInfo {
    wstr file, when;        // when: YYYY-MM-DD HH:MM (the file's time)
    uint32_t code = 0;      // the stop code, 0 = unknown
    wstr name, meaning;     // "INACCESSIBLE_BOOT_DEVICE", what it usually means and what helps
};
std::vector<CrashInfo> recentCrashes(const wstr& winDir, size_t max = 10);
uint32_t dumpStopCode(const std::string& header);   // from the first bytes of a .dmp file
bool dumpBugCheck(const std::string& header, uint32_t& code, uint64_t param[4]);  // the stop code and its four parameters
void describeStopCode(uint32_t code, wstr& name, wstr& meaning);

// ---- how far the last start of a Windows got, from what it left behind
// Windows saves its settings file (config\SYSTEM) while it runs; the kernel writes a crash dump when it stops
// (if it can still write to the disk), the boot log (ntbtlog.txt, when boot logging is on) as it loads
// drivers, and Automatic Repair writes SrtTrail.txt. Their dates, the stop code and its parameters tell
// whether Windows got as far as its kernel, and whether the kernel lost its disk because of the disk itself
// (bad sectors, damaged file system) or because of the storage driver / controller mode.
enum BootStageKind { BS_UNKNOWN, BS_NO_SIGN, BS_KERNEL_RAN, BS_KERNEL_NO_DISK, BS_KERNEL_STOPPED };
struct BootEvidence {
    wstr what, when, detail;
};
struct BootStage {
    bool dump = false, dumpStale = false;
    uint32_t code = 0;
    uint64_t param[4]{};
    wstr codeName, codeMeaning;
    FILETIME dumpTime{}, lastRun{}, bootLogTime{}, srtTime{};
    bool bootLog = false, srt = false, ran = false;
    wstr srtCause;
    uint32_t status = 0;                         // an NTSTATUS found in the parameters (0 = none)
    wstr statusText;
    bool diskSuspect = false, driverSuspect = false;
    int stage = BS_UNKNOWN;
    wstr verdict, detail;
    std::vector<BootEvidence> evidence;
};
BootStage readBootStage(const wstr& winDir);
void judgeBootStage(BootStage& b);  // stage, verdict and advice from the evidence (also for the self-test)
// What an NTSTATUS in a stop code's parameters means here; sets which side it points at.
wstr ntStatusMeaning(uint32_t status, bool& diskSuspect, bool& driverSuspect);
wstr srtRootCause(const wstr& srtText);  // the last "Root cause found" of Automatic Repair's log

// ---- installed updates (from DISM's package list)
struct UpdateEntry {
    wstr package, when, kind;  // package identity, install time as DISM shows it, release type
};
std::vector<UpdateEntry> parsePackageTable(const wstr& dismOutput);
std::vector<UpdateEntry> listUpdates(const Install& in, const Partition& winPart, wstr& err);

// ---- a whole-disk image, saved as a fixed .vhd (raw sectors + a 512-byte footer) so Windows
// can attach it read-only and every search can run on the copy instead of the damaged disk.
struct ImageResult {
    bool ok = false;
    uint64_t copied = 0, unreadable = 0;   // bytes; unreadable ones are written as zeros
    wstr result, badList;                  // badList: the unreadable ranges, one per line
    std::vector<std::pair<uint64_t, uint64_t>> badRanges;  // the same: offset and length in bytes
};
ImageResult imageDisk(int disk, const wstr& outFile, const std::function<void(const wstr&, float)>& progress, volatile bool* cancel);
std::string vhdFooter(uint64_t size);       // the footer of a fixed .vhd of that size
bool attachImageReadOnly(const wstr& vhd, wstr& err);
bool detachImage(const wstr& vhd, wstr& err);

// ---- logs Windows wrote about its own trouble
struct LogFile {
    wstr title, path, what;   // "Automatic Repair", the file, what it tells
    bool eventLog = false;    // an .evtx file: read with wevtutil (errors and critical events only)
};
std::vector<LogFile> windowsLogs(const Install& in);
// The last lines of a log (text files in any encoding; event logs through wevtutil).
std::vector<wstr> readLogTail(const LogFile& f, size_t maxLines, wstr& err);
wstr decodeText(const std::string& bytes);  // UTF-16 with a mark, UTF-8, or ANSI

// ---- a read test of every sector of a disk (nothing is written)
struct SurfaceResult {
    bool finished = false;
    uint64_t tested = 0, bad = 0;   // bytes
    int badAreas = 0;
    wstr result, badList;
    std::vector<std::pair<uint64_t, uint64_t>> badRanges;  // offset and length in bytes from the disk start
};
SurfaceResult surfaceTest(int disk, const std::function<void(const wstr&, float)>& progress, volatile bool* cancel);

// ---- the registry of a Windows that is not running, opened in the registry editor
struct HiveMount {
    wstr name, file, key;   // "SYSTEM", its file, and where it is loaded: HKLM\BD_SYSTEM
    bool loaded = false;
};
std::vector<HiveMount> offlineHives(const Install& in);
bool loadHiveForEditing(HiveMount& h, wstr& err);
bool unloadHive(HiveMount& h, wstr& err);

// ---- System Restore for a Windows that is not running (rstrui /offline), or the running one
bool startSystemRestore(const Install& in, const Partition& winPart, wstr& err);

// ---- Windows Memory Diagnostic: the last result (from the event log), and starting a new test.
// Bad RAM causes blue screens, failed repairs and start-up failures; this says whether it was ever
// tested and what the test found, for the running Windows or an offline one (its System.evtx).
struct MemTestResult {
    bool found = false;   // a result was in the event log
    bool errors = false;  // the last test found memory errors
    wstr when;            // when it ran, as the log shows it
    wstr detail;          // the log's own sentence
};
MemTestResult lastMemoryTest(const Install& in);
bool parseMemoryEvent(const wstr& wevtutilText, MemTestResult& out);  // for the self-test
bool startMemoryTest(wstr& err);  // the running Windows only: opens mdsched (it asks when to restart)

}  // namespace bd

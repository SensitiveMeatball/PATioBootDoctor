// PATio Boot Doctor.
// Looks at every disk and partition, finds every Windows installation (also
// ones that were moved into a folder), and shows step by step what each one
// needs to start: the boot chain. Explains problems in plain words, says what
// it takes to fix them, fixes them on request, and backs up everything it
// changes so the History view can undo it.
#include "ui/KitWindow.h"
#include "Lang.h"
#include "Diagnose.h"
#include "Repair.h"
#include "Backup.h"
#include "Demo.h"
#include "Hive.h"
#include "SelfTest.h"
#include "DiskTool.h"
#include "Rescue.h"
#include "Keys.h"
#include "Recover.h"
#include "Space.h"
#include "Suggest.h"
#include "Layout.h"
#include "Migrate.h"
#include "Update.h"
#include "WinRepair.h"
#include "Accounts.h"
#include "Capture.h"
#include "SecureBoot.h"
#include "Drivers.h"
#include "StorageDrivers.h"
#include "HardwareInfo.h"
#include "Pdf.h"
#include <deque>
#include <dbt.h>
#include <regex>
#include <map>
#include <set>
#include <mutex>
#include <thread>

namespace {

using namespace bd;

const wchar_t* const kAppName = L"PATio Boot Doctor";
const wchar_t* const kVersion = L"0.5.0";
const UINT WM_BD_DECRYPT = WM_APP + 39, WM_BD_DEFER = WM_APP + 38, WM_BD_JOB = WM_APP + 37, WM_BD_CONSOLE = WM_APP + 36, WM_BD_PROGRESS = WM_APP + 30, WM_BD_SCANNED = WM_APP + 31, WM_BD_FIXED = WM_APP + 32, WM_BD_UNDONE = WM_APP + 33, WM_BD_SAVED = WM_APP + 34;

// Fixed meanings (PatKit DESIGN.md, rule 6)
const Color kGreen(0x30, 0xA4, 0x6C), kAmber(0xF5, 0x9E, 0x0B), kRed(0xE5, 0x48, 0x4D), kBlue(0x0A, 0x84, 0xFF);

enum Views { V_REPORT, V_CHAIN, V_DISKS, V_HISTORY, V_ABOUT, V_FILES, V_LAYOUT, V_SYSTEMS, V_MIGRATE, V_KEYS, V_NOTES, V_REPAIR, V_STATS, V_HARDWARE, V_COUNT };
// The left pane
enum NavItems { N_HOME, N_CHAIN, N_DISKS, N_HISTORY, N_FILES, N_SYSTEMS, N_LAYOUT, N_MIGRATE, N_KEYS, N_NOTES, N_REPAIR, N_POWER, N_MODE, N_RESCUE, N_CMD, N_PS, N_BACKUP, N_LANG, N_SHUTDOWN, N_RESTART, N_HIBERNATE, N_POWER_CANCEL };
enum Hits {
    H_SIDE = kit::kAppHit, H_SCROLL, H_FIX, H_FIX_ALL, H_FIX_PC, H_DETAILS, H_CHAIN, H_NODE, H_LINK, H_PART, H_UNDO, H_DEL_SESSION,
    H_CHANGE_FOLDER, H_BACKUP_NOW, H_SAVE_REPORT, H_SCAN, H_BACK, H_ACTION, H_GHOST, H_LESSONS, H_SECUREBOOT, H_GAP, H_DISKHEAD, H_DOP, H_TOOL_CMD, H_TOOL_PS, H_TOOL_RESCUE, H_CONSOLE_SPLIT, H_CONSOLE_CLOSE, H_CONSOLE_CLEAR, H_CONSOLE, H_LINK_MAIL, H_LINK_WEB, H_LINK_REVIEW, H_LINK_SUGGEST,
    H_NAV, H_FILE, H_FILE_FILTER, H_FILES_ALL, H_FILES_NONE, H_FILES_RECOVER, H_FILES_BACK, H_LOST, H_LOST_FULL, H_LOST_CLOSE, H_LOST_QUICK, H_LOST_FORGET, H_CLEAN_HISTORY,
    H_PREVIEW_GO, H_PREVIEW_CANCEL, H_PREVIEW_MODE, H_CONSOLE_COPY, H_LAYOUT_GO, H_LAYOUT_SCHEME, H_SYS, H_SYS_TIMEOUT, H_MIG_OPEN, H_MIG_SRC, H_MIG_DST, H_MIG_SCHEME, H_MIG_GO, H_KEY, H_KEYS_TOP, H_UPDATE, H_NOTE, H_NOTES_TOP, H_REP_SRC, H_REP_TOOL, H_REP_RESCUE,
    H_REP_TAB, H_REP_OPT, H_REP_DRV, H_REP_LOG, H_REP_LOGACT, H_REP_HIVE, H_REP_RESTORE, H_REP_ACCT, H_REP_MEM, H_HEALTH, H_STICK_NAME, H_BOOTSTAGE, H_STORAGE, H_BADMAP, H_JOB, H_STATS, H_HW, H_LOAD_DRIVER, H_JOBSHEET, H_TAB, H_GUIDE, H_ADD_DRIVER
};
enum Cmds { C_SCAN = kit::kAppCmd, C_VIEW, C_BACKUP_NOW, C_FOLDER, C_SAVE_REPORT, C_UNDO, C_DELETE_SESSION, C_OPEN_FOLDER, C_NAV_BACK, C_NAV_FWD, C_UNDO_LAST, C_REDO, C_DISKOP, C_SETTINGS_MENU, C_SETTINGS, C_ABOUT, C_LANGPACK, C_RESCUE_DISK, C_LANGTEMPLATE, C_HOME, C_SET_LANG, C_POWER, C_KEYS, C_KEY_DELETE, C_KEY_DELETE_ALL, C_KEY_PASSWORD, C_RESCUE_IMAGE, C_CLEAN_HISTORY, C_CAPTURE_KEYS, C_UPDATE_CHECK, C_UPDATE_FILE, C_UPDATE_URL, C_REMOVE_UPDATE, C_IMAGE_DISK, C_LOAD_DRIVER, C_POWER_MENU, C_PREP_MODE, C_STATS };

typedef BOOL(WINAPI* Wow64DisableFn)(PVOID*);
extern const wchar_t* const kLayoutWarning;
wstr kitTranslate(const wchar_t* english) { return i18n::tr(english); }

// The language Windows itself uses, if a pack for it exists
wstr systemLanguage() {
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
        case LANG_TURKISH: return L"tr";
        case LANG_GERMAN: return L"de";
        case LANG_SPANISH: return L"es";
        case LANG_FRENCH: return L"fr";
        case LANG_PORTUGUESE: return L"pt";
        case LANG_ITALIAN: return L"it";
        case LANG_DUTCH: return L"nl";
        case LANG_ARABIC: return L"ar";
        case LANG_RUSSIAN: return L"ru";
        case LANG_UKRAINIAN: return L"uk";
        case LANG_POLISH: return L"pl";
        case LANG_CHINESE: return L"zh";
        case LANG_JAPANESE: return L"ja";
        case LANG_KOREAN: return L"ko";
        case LANG_VIETNAMESE: return L"vi";
    }
    return L"en";
}

void noRedirection() {  // a 32-bit build on 64-bit Windows must see the real System32
    auto f = (Wow64DisableFn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "Wow64DisableWow64FsRedirection");
    PVOID old = nullptr;
    if (f) f(&old);
}

wstr plural(size_t n, const wstr& one, const wstr& many) { return std::to_wstring(n) + L" " + (n == 1 ? one : many); }

class DoctorApp : public kit::KitWindow {
protected:
    // ------------------------------------------------------------ life cycle
    void onCreate() override {
        backupRoot_ = settings().get(L"backupFolder");
        showLessons_ = settings().getInt(L"explanations", 1) != 0;
        consoleOn_ = settings().getInt(L"console", 0) != 0;
        if (!snapshot_) {
            loadLost();
            loadNotes();
            loadJob();
            journal_.onNote = [this](const wstr& t) { appendJobLog(t); };  // every recorded step goes into the open job's log
        }
        consoleH_ = (float)std::max(90, std::min(1200, settings().getInt(L"consoleHeight", 190)));
        setToolLogger([this](const wstr& line) { consoleAdd(line); });
        setProgressStyle(kAmber, 3.f);
        techMode_ = settings().getInt(L"technician", 0) != 0;
        if (!snapshot_) removeOldVersion();
        applyLanguage(settings().get(L"language"), false);
        if (snapshot_) {
            rep_ = demoReport();
            sessions_ = demoSessions();
            backupShown_ = L"E:\\PATioBootDoctor Backups";
            sessionFolder_ = L"E:\\PATioBootDoctor Backups\\2026-11-12 14-03-55";
            haveReport_ = true;
            return;
        }
        startStats();
        startScan();
    }

    void onDestroy() override {
        KillTimer(hwnd(), kStatsTimer);
        if (recording_) {  // closing: finish the file quietly
            KillTimer(hwnd(), kCaptureTimer);
            recording_ = false;
            if (usingMp4_) mp4_.close();
            else recorder_.close();
        }
        saveUses();
        appendJobLog(T(L"Program closed"));
        unloadAllHives();
        if (devNotify_) UnregisterDeviceNotification(devNotify_);
        KillTimer(hwnd(), kDecryptTimer);
        if (decryptThread_.joinable()) decryptThread_.join();
        setToolLogger(nullptr);
        cancel_ = true;
        if (job_.joinable()) job_.join();
    }

    bool canClose() override {
        if (!busy_) return true;
        showConfirm(T(L"Work is still running"), T(L"A scan or a repair is still running. Closing now can leave a repair half done. Wait until it finishes."),
                    {T(L"Keep waiting")}, 0, 0, false, nullptr);
        return false;
    }

    std::vector<kit::ToolButton> toolbarButtons() override {
        wstr undoTip = T(L"Undo: nothing to undo (Ctrl+Z)"), redoTip = T(L"Redo: nothing to redo (Ctrl+Y)");
        if (!undo_.empty()) undoTip = T(L"Undo \"") + readAction(undo_.back()).title + T(L"\" (Ctrl+Z)");
        if (!redo_.empty()) redoTip = T(L"Redo: undo \"") + readAction(redo_.back()).title + T(L"\" (Ctrl+Y)");
        return {
            {C_NAV_BACK, 0, "back", T(L"Back (Alt+Left)"), !back_.empty()},
            {C_NAV_FWD, 0, "forward", T(L"Forward (Alt+Right)"), !fwd_.empty()},
            {C_HOME, 0, "home", T(L"Home: everything found on this PC (Ctrl+H)"), true, view_ == V_REPORT && sel_ < 0},
            {C_SCAN, 0, "search", T(L"Scan all disks again (Ctrl+R)"), !busy_},
            {C_UNDO_LAST, 0, "bd_undo", undoTip, !busy_ && !undo_.empty()},
            {C_REDO, 0, "bd_redo", redoTip, !busy_ && !redo_.empty()},
            {C_SAVE_REPORT, 0, "downloads", T(L"Save the report as a text file (Ctrl+S)"), haveReport_},
            {C_SETTINGS_MENU, 0, "settings", T(L"Settings, backup folder, languages and About"), true},
        };
    }

    wstr toolbarTitle() override {
        const Install* in = selected();
        switch (view_) {
            case V_REPORT: return in ? T(L"Boot Doctor · ") + in->label() + T(L" · Report") : T(L"Boot Doctor · Report");
            case V_CHAIN: return in ? T(L"Boot Doctor · ") + in->label() + T(L" · Boot chain") : T(L"Boot Doctor · Boot chain");
            case V_DISKS: return T(L"Boot Doctor · Disks and partitions");
            case V_ABOUT: return T(L"Boot Doctor · About");
            case V_FILES: return T(L"Boot Doctor · Recover deleted files");
            case V_LAYOUT: return T(L"Boot Doctor · Partition order");
            case V_SYSTEMS: return T(L"Boot Doctor · Windows on this PC");
            case V_MIGRATE: return T(L"Boot Doctor · Move Windows to another drive");
            case V_KEYS: return T(L"Boot Doctor · BitLocker keys");
            case V_NOTES: return T(L"Boot Doctor · Notes");
            case V_STATS: return T(L"Boot Doctor · Usage statistics");
            case V_HARDWARE: return T(L"Boot Doctor · Hardware details");
            case V_REPAIR: return T(L"Boot Doctor · Windows Repair");
            default: return T(L"Boot Doctor · History and backups");
        }
    }

    wstr statusText() override {
        if (!haveReport_) return busy_ ? T(L"Scanning") : T(L"Not scanned yet");
        wstr s = plural(rep_.m.disks.size(), T(L"disk"), T(L"disks")) + L" · " + plural(rep_.m.parts.size(), T(L"partition"), T(L"partitions")) + L" · " +
                 plural(rep_.installs.size(), T(L"system found"), T(L"systems found"));
        int p = rep_.problems();
        s += p ? L" · " + plural(p, T(L"needs attention"), T(L"need attention")) : T(L" · Nothing needs attention");
        if (!sessionFolder_.empty()) s += T(L" · Backups: ") + sessionFolder_;
        return s;
    }

    std::vector<kit::KeyHelp> helpKeys() override {
        return {{T(L"Ctrl+H"), T(L"Home")},
                {T(L"Ctrl+R"), T(L"Scan again")},
                {T(L"Alt+← / Alt+→"), T(L"Back and forward")},
                {T(L"Ctrl+Z / Ctrl+Y"), T(L"Undo and redo the last change")},
                {T(L"Ctrl+1 … 4"), T(L"Report, boot chain, disks, history")},
                {L"↑ / ↓", T(L"Choose a Windows (report and boot chain)")},
                {L"← / →", T(L"Step through the boot chain")},
                {T(L"Enter"), T(L"Show details, or carry out the first fix of a step")},
                {T(L"Page up / down"), T(L"Scroll")},
                {T(L"Ctrl+B"), T(L"Back up the start-up setup now")},
                {T(L"Ctrl+S"), T(L"Save the report as a text file")},
                {T(L"Ctrl+C"), T(L"Copy the report (in a message: copy its text)")},
                {T(L"Ctrl+N"), T(L"Write a note")},
                {T(L"Ctrl+V"), T(L"Paste into a text box")},
                {T(L"Ctrl+Shift+S"), T(L"Save a screenshot (no sound)")},
                {T(L"Ctrl+Shift+R"), T(L"Start or stop recording the screen (no sound)")}};
    }

    // ------------------------------------------------------------ helpers
    const Install* selected() const { return sel_ >= 0 && sel_ < (int)rep_.installs.size() ? &rep_.installs[sel_] : nullptr; }

    Color hc(Health h) const {
        switch (h) {
            case Health::Ok: return kGreen;
            case Health::Warn: return kAmber;
            case Health::Bad: return kRed;
            case Health::Info: return theme().accent;
            default: return theme().label3;
        }
    }

    float para(const wstr& s, float x, float y, float w, HFONT f, Color c, bool draw) {
        if (s.empty() || w < 10) return 0;
        int h = canvas().measureWrapped(s, f, (int)w);
        if (draw) text(s, RectF(x, y, w, (float)h + 2), f, c, DT_WORDBREAK | DT_NOPREFIX);
        return (float)h;
    }

    float pill(float right, float cy, const wstr& t, Color c) {
        float w = canvas().measure(t, fonts().smallSemi).cx + S(18);
        drawPill(RectF(right - w, cy - S(10), w, S(20)), t, c.withAlpha(0.16f), c);
        return w;
    }

    void card(const RectF& r) {
        canvas().round(r, S(10), theme().field);
    }

    // A small "learn" box: a heading and a plain explanation on a light accent background.
    float lessonBox(const wstr& lesson, const wstr& heading, float x, float y, float w, bool draw) {
        if (lesson.empty() || !showLessons_ || techMode_) return 0;
        float pad = S(10);
        float h = pad + S(18) + (float)canvas().measureWrapped(lesson, fonts().small, (int)(w - pad * 2 - S(4))) + pad;
        if (draw) {
            RectF b(x, y, w, h);
            canvas().round(b, S(8), theme().accent.withAlpha(0.08f));
            canvas().round(RectF(b.x, b.y + S(6), S(3), b.h - S(12)), S(1.5f), theme().accent);
            text(heading, RectF(x + pad + S(4), y + pad - S(2), w - pad * 2, S(18)), fonts().smallSemi, theme().accentText);
            para(lesson, x + pad + S(4), y + pad + S(18), w - pad * 2 - S(4), fonts().small, theme().label, true);
        }
        return h + S(10);
    }

    wstr stateText(const Install& in) const {
        if (in.running) return T(L"Running now");
        if (!in.atRoot) return in.windowsOld ? T(L"Old copy in Windows.old") : T(L"Inside the folder ") + in.folder;
        if (in.isDefault) return T(L"Starts first");
        if (in.active) return T(L"In the start-up menu");
        if (in.linux) return in.active ? T(L"In the PC's start-up list") : T(L"Not in the PC's start-up list");
        return T(L"Not in the start-up menu");
    }

    bool needsWork(const Install& in) const {
        for (const Fix& f : in.plan)
            if (f.available) return in.verdict == Health::Bad || in.verdict == Health::Warn;
        return false;
    }

#include "app/draw.inl"
#include "app/input.inl"
#include "app/commands.inl"
#include "app/work.inl"
#include "app/nav.inl"
#include "app/power.inl"
#include "app/jobs.inl"
#include "app/keys_unlock.inl"
#include "app/space.inl"
#include "app/preview.inl"
#include "app/lost.inl"
#include "app/history_clean.inl"
#include "app/files.inl"
#include "app/layout.inl"
#include "app/systems.inl"
#include "app/migrate.inl"
#include "app/keys_capture.inl"
#include "app/keys_screen.inl"
#include "app/update.inl"
#include "app/decrypt.inl"
#include "app/rescue_work.inl"
#include "app/notes.inl"
#include "app/repair.inl"
#include "app/repair_tabs.inl"
#include "app/extras.inl"
#include "app/suggest.inl"
#include "app/jobsheet.inl"
#include "app/guide.inl"
#include "app/capture.inl"
#include "app/health.inl"
#include "app/badmap.inl"
#include "app/job.inl"
#include "app/stats.inl"
#include "app/hardware.inl"

private:
    void firstFixOfNode() {
        const Install* in = selected();
        if (!in || node_ < 0 || node_ >= (int)in->chain.size()) return;
        for (int fi : in->chain[node_].fixes)
            if (fi >= 0 && fi < (int)in->plan.size() && in->plan[fi].available) {
                askFixes(sel_, {fi});
                return;
            }
    }

    int hibernateKnown_ = -1;
    bool scanning_ = false, rescanPending_ = false, reopenRescueMenu_ = false, deviceScan_ = false;
    HDEVNOTIFY devNotify_ = nullptr;
    int repInstall_ = -1, repTab_ = 0, logSel_ = -1, driversFor_ = -1;
    bool techMode_ = false, driversListed_ = false, oldCopiesChecked_ = false;
    std::vector<BootDriver> bootDrivers_;
    std::vector<LogFile> logs_;
    std::vector<wstr> logLines_;
    wstr logErr_, pendingDriver_, lastDriverInf_;
    std::vector<HiveMount> hives_;
    std::vector<LocalAccount> accounts_;
    bool accountsListed_ = false;
    int accountsFor_ = -1;
    MemTestResult memTest_;
    bool memChecked_ = false;
    int memFor_ = -1;
    std::vector<DriverInf> driverInfs_;
    std::set<wstr> disksBeforeDriver_;
    std::map<wstr, wstr> driverDisks_, surface_;   // per disk id: the driver it appeared with; its last surface test
    std::vector<OldCopy> oldCopies_;
    float navW_ = 0;
    bool essentialsNext_ = false;
    std::vector<Fix> repTools_;
    std::vector<CrashInfo> crashes_;
    BootStage bootStage_;
    StorageCheck storage_;
    std::map<wstr, BadMap> badMaps_;  // per disk id: the unreadable areas found
    bool badMapsLoaded_ = false;
    JobInfo jobRec_;                 // the open job (read by worker threads under jobLock_)
    std::mutex jobLock_;
    bool jobOffered_ = false;
    wstr clockNote_;                 // the PC's clock is behind: a flat BIOS battery
    std::map<wstr, UseItem> uses_;   // usage statistics
    std::mutex useLock_;
    bool usesLoaded_ = false, usesDirty_ = false;
    uint64_t sessionMinutes_ = 0;
    static const UINT_PTR kStatsTimer = 7706;
    std::vector<HwFile> hwFiles_;
    bool hwListed_ = false;
    wstr hwShown_;
    bool storageChecked_ = false;
    int storageFor_ = -1;
    std::vector<UpdateEntry> updates_;
    std::vector<Note> notes_;
    static const UINT_PTR kRefreshTimer = 7703, kDecryptTimer = 7704, kCaptureTimer = 7705;
    AviWriter recorder_;
    Mp4Writer mp4_;
    bool usingMp4_ = false;
    bool recording_ = false;
    uint64_t recStart_ = 0;
    wstr recPath_;
    static const int kCaptureFps = 10;
    bool quietScan_ = false, decryptBusy_ = false;
    wstr decryptLetter_, decryptName_;
    int decryptLeft_ = -1;
    std::thread decryptThread_;
    DiskOpArgs lastDiskOp_;
    bool stickReminded_ = false;
    std::vector<SavedKey> keysCache_;
    bool keysOffered_ = false;
    int migInstall_ = -1, migTarget_ = -1, migUefi_ = -1;
    // partition order
    int layoutInstall_ = -1, layoutUefi_ = 1, lastLayoutKind_ = -1;
    wstr layoutKey_;
    bool layoutBootDue_ = false, movePromptShown_ = false;
    LayoutPlan layoutPlan_;
    wstr progTitle_, keysPass_;
    std::vector<std::function<void()>> deferred_;
    bool progCancel_ = false;
    Report rep_;
    std::vector<NavState> back_, fwd_;
    float viewTop_ = 0, viewBottom_ = 0;
    std::function<void()> jobDone_;
    // power
    static const UINT_PTR kPowerTimer = 7702;
    int powerAction_ = -1, powerLeft_ = 0;  // 0 shut down, 1 restart, 2 hibernate; seconds left
    // BitLocker
    SavedKey keyToSave_;
    bool lockedWarned_ = false, healthWarned_ = false;
    std::set<wstr> healthAlerted_;  // disks already warned about this session
    // lost partitions
    std::map<wstr, LostMem> lostMem_;   // per disk id
    std::set<wstr> lostHidden_;
    std::vector<wstr> lostIdsSaved_;
    // deleted files
    FileScan fileScan_;
    int filePart_ = -1, fileFilter_ = -1;
    bool fileFull_ = false;
    std::vector<char> fileSel_;
    std::vector<int> fileRows_;
    // resize preview
    struct Preview {
        bool on = false;
        int mode = 0;  // 0 unused space after it, 1 also the partition after it
        DiskOpArgs a;
    } preview_;
    static const UINT_PTR kCountdownTimer = 7701;
    int countdown_ = 0;
    bool consoleOn_ = false, dragging_ = false, consolePosted_ = false;
    float consoleH_ = 190, dragH_ = 0;
    int dragY_ = 0, consoleScroll_ = 0;
    RectF conRect_, splitRect_;
    std::deque<wstr> console_;
    std::mutex consoleLock_;
    Image logo_;
    std::vector<wstr> undo_, redo_, jobDirs_;
    wstr undoFrom_;
    int undoMode_ = 0;
    int dsKind_ = SEL_NONE, dsDisk_ = -1, dsPart_ = -1, dsGap_ = -1;
    bool haveReport_ = false, snapshotConfirm_ = false, showLessons_ = true;
    int view_ = V_REPORT, sel_ = -1, node_ = -1;
    float scroll_[V_COUNT] = {}, contentH_[V_COUNT] = {}, viewH_ = 400;
    Journal journal_;
    wstr backupRoot_, backupShown_, sessionFolder_;
    std::vector<SessionInfo> sessions_;
    wstr afterScanTitle_, afterScanNote_;

    std::thread job_;
    std::mutex lock_;
    volatile bool cancel_ = false;
    bool busy_ = false, jobOk_ = false;
    std::shared_ptr<Report> scanned_;
    wstr jobNote_, progStep_;
    float progFrac_ = 0;
};

const wchar_t* const kLayoutWarning = N(L"Before you start: back up the files you cannot lose to another drive. Moving and resizing partitions is safe when nothing interrupts it, but a power cut or a crash in the middle can damage the drive. Keep a laptop plugged in, close other programs, and do not switch the PC off while a step runs. If it is broken off anyway, start this program again: it finishes a partition move from where it stopped.");

std::vector<wstr> args() {
    // CommandLineToArgvW lives in shell32; parse by hand to keep imports small.
    std::vector<wstr> out;
    const wchar_t* c = GetCommandLineW();
    wstr cur;
    bool q = false, any = false;
    for (; *c; ++c) {
        if (*c == L'"') { q = !q; any = true; continue; }
        if (!q && (*c == L' ' || *c == L'\t')) {
            if (any || !cur.empty()) { out.push_back(cur); cur.clear(); any = false; }
            continue;
        }
        cur += *c;
    }
    if (any || !cur.empty()) out.push_back(cur);
    if (!out.empty()) out.erase(out.begin());
    return out;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    std::vector<wstr> a = args();
    if (!a.empty() && a[0] == L"--selftest") {
        platformInit();
        noRedirection();
        return runSelfTest(a.size() > 1 ? a[1] : wstr(L"."), kAppName, kVersion);
    }
    if (!a.empty() && a[0] == L"--makekey") {  // for the maker: the key pair that signs updates
        platformInit();
        wstr err;
        return a.size() > 2 && bd::makeUpdateKey(a[1], a[2], err) ? 0 : 2;
    }
    if (!a.empty() && a[0] == L"--signupdate") {  // for the maker: <exe> <private key> <url> [notes] -> <exe>.update.txt
        platformInit();
        wstr out, err;
        return a.size() > 3 && bd::writeUpdateFile(a[1], a[2], a[3], a.size() > 4 ? a[4] : wstr(), out, err) ? 0 : 2;
    }
    if (!a.empty() && a[0] == L"--rescuetest") {
        platformInit();
        noRedirection();
        return runRescueTest(a.size() > 1 ? a[1] : wstr(L"."));
    }
    if (!a.empty() && a[0] == L"--movetest") {
        platformInit();
        noRedirection();
        return runMoveTest(a.size() > 1 ? a[1] : wstr(L"."));
    }
    if (!a.empty() && a[0] == L"--mp4test") {  // check MP4 recording through Media Foundation
        platformInit();
        noRedirection();
        wstr dir = a.size() > 1 ? a[1] : wstr(L".");
        wstr path = dir + L"\\mp4test.mp4", err;
        int frames = 0;
        bool ok = bd::recordTestMp4(path, frames, err);
        wstr msg = wstr(L"Media Foundation available: ") + (bd::Mp4Writer::available() ? L"yes" : L"no") + L"\r\n" +
                   (ok ? L"OK: " + std::to_wstring(frames) + L" frames written" : L"FAILED: " + err) + L"\r\n";
        std::string u = wideToUtf8(msg);
        HANDLE f = CreateFileW((dir + L"\\mp4test.log").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD wr = 0;
            WriteFile(f, u.data(), (DWORD)u.size(), &wr, nullptr);
            CloseHandle(f);
        }
        return ok ? 0 : 1;
    }
    if (!a.empty() && a[0] == L"--opstest") {
        platformInit();
        noRedirection();
        return runOpsTest(a.size() > 1 ? a[1] : wstr(L"."));
    }
    if (!a.empty() && a[0] == L"--report") {  // scan and write the report without a window (scripts, remote help)
        platformInit();
        noRedirection();
        bd::Report r = bd::scan(nullptr);
        bd::checkTools(r);
        std::string u = "\xEF\xBB\xBF" + wideToUtf8(bd::reportText(r));
        wstr path = a.size() > 1 ? a[1] : wstr(L"PATioBootDoctor-report.txt");
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return 2;
        DWORD wr = 0;
        WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr);
        CloseHandle(h);
        return r.problems() ? 1 : 0;
    }
    if (!a.empty() && a[0] == L"--hardware") {  // collect this PC's hardware details without a window: --hardware <file> [technician]
        platformInit();
        noRedirection();
        bd::Report r = bd::scan(nullptr);
        std::string u = "\xEF\xBB\xBF" + wideToUtf8(bd::hardwareReport(r, a.size() > 2 ? a[2] : wstr(), kVersion));
        wstr path = a.size() > 1 ? a[1] : wstr(L"PATioBootDoctor-hardware.txt");
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return 2;
        DWORD wr = 0;
        WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr);
        CloseHandle(h);
        return 0;
    }
    {
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        wstr dir = exe;
        i18n::init(dir.substr(0, dir.find_last_of(L'\\')));
    }
    DoctorApp app;
    kit::KitWindow::Config cfg;
    cfg.appName = kAppName;
    cfg.version = kVersion;
    cfg.className = L"PATioBootDoctorWindow";
    cfg.iniName = L"PATioBootDoctor.ini";
    platformInit();
    cfg.settingsPath = bd::dataFolder() + L"\\PATioBootDoctor.ini";  // everything it keeps is in PATioBootDoctorData
    return app.run(inst, show, cfg);
}

// Automatic backups and the history of changes. Every session of the app gets a
// folder; every change gets a numbered sub-folder holding copies of what it is
// about to change, plus action.ini that says how to put each copy back.
//
//   <backup root>\2026-11-12 14-03-55\
//       session.log          what happened, in plain words
//       report.txt           the scan report the session started with
//       001 Start of session\action.ini, files\...
//       002 Rebuild the start-up files\action.ini, files\...
#pragma once
#include "Disk.h"

namespace bd {

struct BackupItem {
    wstr kind;               // file, dir, sectors, livebcd, move
    std::vector<wstr> f;     // fields, see Backup.cpp
};

struct ActionInfo {
    wstr dir, title, target, time, result, log;
    bool ok = false, finished = false, undone = false, canUndo = false;
    std::vector<BackupItem> items;
};

struct SessionInfo {
    wstr dir, name, host;
    std::vector<ActionInfo> actions;
    uint64_t bytes = 0;
};

class Journal {
public:
    // Creates the session folder under root (created if needed).
    bool open(const wstr& root, const wstr& host, wstr& err);
    bool isOpen() const { return !folder_.empty(); }
    const wstr& folder() const { return folder_; }
    const wstr& root() const { return root_; }

    wstr begin(const wstr& title, const wstr& target);  // returns the action folder
    bool saveFile(const wstr& action, const Partition& p, const wstr& rel, wstr& err);    // rel: path inside the partition
    bool saveDir(const wstr& action, const Partition& p, const wstr& relDir, wstr& err);
    bool saveSectors(const wstr& action, const Disk& d, uint64_t offset, uint32_t length, wstr& err);
    bool saveDiskTables(const wstr& action, const Disk& d, wstr& err);  // MBR / both GPT copies
    bool saveLiveBcd(const wstr& action, wstr& err);
    bool saveFwVar(const wstr& action, const wstr& name, wstr& err);  // a UEFI variable (Boot####, BootOrder)
    void recordMove(const wstr& action, const Partition& p, const wstr& fromRel, const wstr& toRel);
    void recordScript(const wstr& action, const wstr& diskpartScript);  // a diskpart script that undoes the change
    void recordLabel(const wstr& action, const Partition& p, const wstr& oldLabel);
    void end(const wstr& action, bool ok, const wstr& result, const wstr& log, bool canUndo);
    void note(const wstr& text);
    void saveReport(const wstr& text);
    // Sees every line written to the session log (start, result, notes); may be called from a worker thread.
    std::function<void(const wstr&)> onNote;

private:
    void addItem(const wstr& action, const wstr& line);
    wstr root_, folder_;
    int next_ = 1;
};

wstr defaultBackupRoot(const Machine& m);  // next to the exe, or on a data drive when running from read-only or RAM media
bool folderWritable(const wstr& dir);
std::vector<SessionInfo> listSessions(const wstr& root);
ActionInfo readAction(const wstr& dir);
// Puts back what the action changed. Partitions are found again by their disk id and offset.
// newDir: the history entry of the undo itself (so it can be undone too: Redo).
bool undoAction(const ActionInfo& a, const Machine& m, Journal* j, wstr& log, wstr* newDir = nullptr);
bool deleteFolderTree(const wstr& dir);
uint64_t folderBytes(const wstr& dir);
wstr bcdeditPath();

// Runs a console program and collects its output. stdinText is typed into it.
struct ToolResult {
    bool started = false;
    DWORD code = (DWORD)-1;
    wstr output;
};
ToolResult runTool(const wstr& exe, const wstr& args, volatile bool* cancel = nullptr, const std::string& stdinText = std::string());

}  // namespace bd

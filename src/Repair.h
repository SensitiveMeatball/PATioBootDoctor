// Carries out the fixes the diagnosis offers. Every fix first saves what it is
// about to change into the session's backup folder (Journal), so the history
// can undo it.
#pragma once
#include "Diagnose.h"
#include "Backup.h"

namespace bd {

struct FixOutcome {
    bool ok = false;
    wstr result;  // one or two plain sentences
    wstr log;     // what the tools said
    wstr actionDir;       // its entry in the history (for Undo / Redo)
    bool undoable = false;
};

using StepFn = std::function<void(const wstr&)>;

// r is the worker's own copy: a move updates the installation's paths in it, so
// the next fix in a plan works on the new place.
FixOutcome runFix(const Fix& f, Report& r, Journal& j, volatile bool* cancel, const StepFn& step);
// Marks fixes whose tool is missing on this system (for example bootsect.exe
// outside Windows PE) as not available, with what to do instead.
void checkTools(Report& r);
// Saves every start-up settings file and partition table (the start of each session).
bool snapshotBootSetup(const Report& r, Journal& j, wstr& log, const wstr& title = L"Start-up setup when the session began");
wstr findSystemTool(const wchar_t* name);
bool fixChangesPartitions(int kind);
// The commands a fix runs, for people who want to see them first (X: = the Windows drive, S: = the start-up partition).
wstr fixCommands(const Fix& f, const Report& r);  // refused on the disk in use (see workingDiskRefusal)

}  // namespace bd

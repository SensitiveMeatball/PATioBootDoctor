// Puts the partitions of a Windows disk back in the order Windows Setup makes them:
//   UEFI (GPT):        start-up (EFI) · reserved (MSR) · Windows · recovery
//   legacy BIOS (MBR): System Reserved (active) · Windows · recovery
// by deleting, shrinking, moving, growing and creating partitions, one step at a time.
// After every step the disk is read again and the plan is made anew, so each step
// is shown and confirmed on its own. Never used on the disk in use.
//
// Moving a partition copies its sectors in the direction that never overwrites what
// is still to be copied. How far it got is written to a file after every block, so a
// move broken off by a power cut can be finished (resumeMove).
#pragma once
#include "Diagnose.h"
#include "Backup.h"
#include "Repair.h"

namespace bd {

enum SlotKind { SK_ESP, SK_MSR, SK_SYSRES, SK_WIN, SK_RECOVERY, SK_OTHER };
struct Slot {
    int kind = SK_OTHER;
    int part = -1;           // index into Machine::parts, -1 = to be made
    uint64_t off = 0, len = 0;
};

enum StepKind { ST_SAVE_WINRE, ST_DELETE, ST_SHRINK, ST_MOVE, ST_CREATE_ESP, ST_CREATE_MSR, ST_CREATE_SYSRES, ST_EXTEND, ST_CREATE_RECOVERY, ST_BOOTFILES, ST_WINRE };
struct LayoutStep {
    int kind = ST_DELETE;
    int part = -1;           // the partition it works on (as the disk is now)
    uint64_t to = 0;         // move / create: the new start (bytes)
    uint64_t size = 0;       // shrink / extend: by how much; create: how big
    wstr title, what;
    bool danger = false, slow = false;
};

struct LayoutPlan {
    bool possible = false, done = false;
    wstr why;                // why not possible
    int disk = -1, install = -1;
    bool uefi = true;
    std::vector<Slot> now, target;  // within the part of the disk that is rearranged
    std::vector<LayoutStep> steps;  // only the first is carried out; then plan again
};

// bootFilesDue: a step that needs fresh start-up files afterwards was done in this session.
LayoutPlan planLayout(const Report& r, int install, bool uefi, bool bootFilesDue);
// Carries out plan.steps[0]. stateFolder keeps the progress file of a move (the backup folder).
FixOutcome runLayoutStep(const LayoutPlan& plan, Report& r, Journal& j, const wstr& stateFolder, const std::function<void(const wstr&, float)>& progress,
                         volatile bool* cancel);
// A move that was broken off: what it was, and finishing it.
bool pendingMove(const wstr& stateFolder, wstr& what);
FixOutcome resumeMove(const wstr& stateFolder, const std::function<void(const wstr&, float)>& progress);
// The sectors of one partition to a new start; the table entry and the file system's own record of its start are updated.
// After a partition's sectors were copied to a new start: the file system's own record of its start.
void fixHiddenSectors(HANDLE disk, uint64_t start, wstr& log);
bool movePartition(int disk, const Partition& p, uint64_t newOffset, const wstr& stateFolder, const std::function<void(const wstr&, float)>& progress, wstr& err,
                   wstr& log, uint64_t resumeDone = 0);

}  // namespace bd

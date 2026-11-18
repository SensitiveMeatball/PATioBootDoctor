// Moving Windows to another drive (for example to a new SSD): the target disk is set up
// in Windows' own order (UEFI: start-up, reserved, Windows, recovery; legacy BIOS: System
// Reserved, Windows, recovery), the Windows partition is copied sector by sector and grown
// to fill its new place, and start-up files and the troubleshoot options are written for it.
// The source is only read (unless it must give up some space to fit a smaller target).
#pragma once
#include "Diagnose.h"
#include "Backup.h"
#include "Repair.h"

namespace bd {

struct MigratePlan {
    bool possible = false;
    wstr why;                     // why not possible
    int install = -1, source = -1, target = -1;
    bool uefi = true;
    uint64_t bootLen = 0, msrLen = 0, winLen = 0, recLen = 0;  // the new partitions
    uint64_t copyLen = 0;         // bytes copied (the Windows partition after shrinking)
    uint64_t shrinkBy = 0;        // the source Windows partition gives up this much first (0 = untouched)
    std::vector<wstr> notes;      // plain warnings: what is erased, what is not copied
};

// Disks Windows could move to, with why not where it cannot.
std::vector<std::pair<int, wstr>> migrationTargets(const Report& r, int install);
MigratePlan planMigration(const Report& r, int install, int targetDisk, bool uefi);
uint64_t shrinkQueryMB(const wstr& diskpartOutput);  // from "shrink querymax"
FixOutcome runMigration(const MigratePlan& p, Report& r, Journal& j, const std::function<void(const wstr&, float)>& progress, volatile bool* cancel);

}  // namespace bd

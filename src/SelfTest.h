#pragma once
#include "core/Common.h"

// Checks the core logic on made-up data in <folder> and writes <folder>\selftest.log.
// Returns 0 when every check passed.
int runSelfTest(const wstr& folder, const wchar_t* appName, const wchar_t* version);

// Runs real disk operations (create, rename, letter, delete, wipe, and their undo) on a
// throw-away 256 MB virtual disk; writes <folder>\opstest.log. Needs administrator rights.
int runMoveTest(const wstr& folder);  // moving, recovery and imaging on a virtual disk (admin)
int runOpsTest(const wstr& folder);
// Makes a rescue stick on a throw-away 2 GB virtual disk and checks it; writes <folder>\rescuetest.log. Needs administrator rights.
int runRescueTest(const wstr& folder);

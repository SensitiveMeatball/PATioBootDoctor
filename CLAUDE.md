# PATio Boot Doctor: working notes

Follow `C:\Projects\CLAUDE.md` and PatKit's `DESIGN.md`. This file says where things are,
so a change needs only the files it touches.

## Build and check

```
build.cmd                                        full build into ..\PATioCommander\dist\PatSuite\PATio Boot Doctor (close the app first)
..\PatKit\build-app.cmd PATioBootDoctor build\test x64     test build (works while the app is open)
PATioBootDoctor_x64.exe --selftest <folder>      logic on made-up data; writes selftest.log
PATioBootDoctor_x64.exe --opstest <folder>       real disk operations on a virtual disk (administrator only)
PATioBootDoctor_x64.exe --movetest <folder>      moving, recovery, imaging on a virtual disk (administrator only)
PATioBootDoctor_x64.exe --signupdate <exe> keys\update-private.key <url> [notes]    the signed update file
PATioBootDoctor_x64.exe --snapshot out.bmp default light 1280 860 <overlay>    a picture of one screen
```

Run the self-test after every change. Run it elevated with `set __COMPAT_LAYER=RunAsInvoker`
when not administrator. Take snapshots only when the look of a screen changed.

## Where things are

Logic (no user interface, each with a header that says what it does):

| File | What |
|------|------|
| `src/Disk.*`, `DiskKind.cpp`, `Smart.cpp` | disks, partitions, firmware, disk groups and types, health |
| `src/Bcd.*`, `Hive.*` | start-up settings and registry files, read offline |
| `src/SecureBoot.*` | Secure Boot db/KEK/dbx certificate stores and the 2011→2023 certificate diagnosis |
| `src/Diagnose.*`, `Linux.*` | finding systems, boot chains, findings, the texts of every fix (`makeFix`) |
| `src/Repair.*` | carrying out fixes (`runFix`), which tools are missing (`checkTools`) |
| `src/DiskTool.*`, `Layout.*`, `Migrate.*` | disk operations, partition order, moving Windows |
| `src/Recover.*`, `WinRepair.*`, `Drivers.*` | lost partitions and files, files on unreadable sectors; crashes, how far the last start got (`readBootStage`), updates, disk image, logs, surface test, registry editing, System Restore, memory-test result; driver loading and boot drivers |
| `src/StorageDrivers.*` | the disk controller's mode (from its PCI class code) and the offline Windows' storage drivers: the plan for "turn on the right drivers" |
| `src/HardwareInfo.*` | SMBIOS (maker, model, serial, BIOS, CPU, memory modules) and the hardware report |
| `src/Accounts.*` | local accounts of an offline Windows (SAM): list, enable, unlock, clear password |
| `src/Capture.*` | screenshot (PNG), screen recording (MP4 via Media Foundation, Motion-JPEG AVI fallback), no audio |
| `src/Backup.*`, `Keys.*`, `Rescue.*`, `Update.*`, `Space.*`, `Pdf.*`, `Lang.*` | history and undo, BitLocker keys and the data folder, rescue stick, updates, clean-up, the job sheet's PDF, translations |
| `src/SelfTest.cpp`, `OpsTest.cpp`, `Demo.cpp` | tests and the made-up PC for snapshots |

The window is one class, `DoctorApp`, in `src/main.cpp` (ids, life cycle, small helpers, member
variables). Its parts are `src/app/*.inl`, included inside the class:

| Part | What |
|------|------|
| `draw.inl` | report, boot chain, disks, console, settings, About, rescue menu, Secure Boot, history |
| `input.inl`, `commands.inl`, `work.inl` | clicks, keys and tooltips; commands; scans, fixes and what happens when they end |
| `nav.inl`, `power.inl`, `jobs.inl` | left pane, Simple and Technician modes, page tabs of "Disks and data" and "Records" (`drawFamilyTabs`); shut down and timers; background jobs (`runJob`) |
| `guide.inl` | guided start on Home, adding a driver to the offline Windows |
| `repair_tabs.inl` | Windows Repair pages incl. Accounts (list/enable/unlock/clear a password) |
| `capture.inl` | Ctrl+Shift+S screenshot, Ctrl+Shift+R screen recording, the "REC · no sound" badge, the 20 % free-space stop |
| `health.inl` | a failing disk: warnings before changes and after the scan, the Home card, starting PATio Commander |
| `badmap.inl` | unreadable areas: kept per disk, red marks on the disk map, "Which files are on them?" |
| `job.inl` | the job (how it was handed in, BIOS battery, action log via `Journal::onNote`), the flat-battery check |
| `stats.inl`, `hardware.inl` | usage statistics (`countUse`), hardware details per PC |
| `repair.inl`, `repair_tabs.inl` | Windows Repair and its pages (start-up options, drivers, logs, registry) |
| `keys_unlock.inl`, `keys_capture.inl`, `keys_screen.inl`, `decrypt.inl` | BitLocker |
| `lost.inl`, `files.inl`, `preview.inl`, `layout.inl`, `migrate.inl`, `systems.inl` | recovery, resize preview, partition order, moving Windows, Windows on this PC |
| `extras.inl`, `jobsheet.inl`, `notes.inl`, `update.inl`, `space.inl`, `history_clean.inl`, `rescue_work.inl` | driver loading, old copies, surface test; job sheet; notes; updates; clean-up |

## Version numbers

`MAJOR.MINOR.PATCH`, in three places that must match: `kVersion` in `src/main.cpp`, and
`FILEVERSION` / `PRODUCTVERSION` plus the two version strings in `res/app.rc`. A batch of new
features raises MINOR (0.3.0 -> 0.4.0); a fix-only release raises PATCH. Rescue sticks and copies
on removable disks are compared against it (`compareVersions`), and update files carry it.

## How a feature is added

1. Logic in a `src/*.cpp` with a self-test in `SelfTest.cpp`.
2. A new fix: an `FX_` id in `Diagnose.h`, its texts in `makeFix`, its work in `runFix`, its
   tool check in `checkTools`.
3. Screen: a hit id (`H_`) and command id (`C_`) in `main.cpp`, drawing in the right
   `app/*.inl`, the click in `input.inl` (`onClick`) with its tooltip (`tipText`).
4. Every text through `T(L"...")` / `TF(L"... {0}", x)`; never translate names of files,
   folders, commands or registry keys. `build.cmd` collects the texts.
5. A line in `README.md`.

## Rules that matter here

- Edit files with the Edit tool or a small Python script file; never with shell heredocs
  (they lose backslashes in C++ text).
- Nothing changes the disk in use: `workingDiskRefusal` is checked in the screens and again in
  `runDiskOp`, `runFix`, `runLayoutStep`, `runMigration`.
- Data lives in `PATioAppData\PATioBootDoctorData` (`dataFolder()` in `Keys.cpp`, built on
  PatKit's `patioAppData()`): on the rescue stick when running from it, else next to PATio
  Commander, else `%APPDATA%`. PATio Commander uses `PATioAppData\PATioCommanderData`.
- Updates are signed: `res\update-key.pub` is built in, `keys\update-private.key` is the
  maker's and is never committed. An update file without a valid `sig=` is refused.
- A new screen for technicians goes under a family tab in `nav.inl`; Simple mode stays at
  Home and Boot chain.
- Translations are only done when asked.

## Asking for changes cheaply

- Name the screen or file ("in Windows Repair > Logs", "`app/nav.inl`"): the search is skipped.
- Send related small changes in one message; unrelated big ones in separate sessions.
- Say what to skip: "no screenshots", "test build only", "no README".
- Start a new session for a new topic: a long session re-reads its whole history on every step.
- For a bug, paste the exact message and say where you were (Windows or the rescue stick).
- Commit after each working batch, so a later session can read the diff instead of the files.

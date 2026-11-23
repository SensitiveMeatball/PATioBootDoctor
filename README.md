# PATio Boot Doctor

Finds out why Windows does not start, in plain words, and fixes it. Built on PatKit.
Runs on Windows 7 SP1 to 11, Windows PE, WinRE and Safe Mode, 64-bit (x64) only.

## What it does

1. **Scans every disk and partition** (raw partition tables, file systems,
   BitLocker, damage) and looks for **every Windows installation**: at the top of a
   drive, moved into a folder (`D:\Backup\Windows`), or kept in `Windows.old`.
2. **Report** â€” for each Windows: does it start, is it **active** (in a start-up
   menu) or **inactive**, is it the one running now, and **what it will take** to
   make it start, as numbered steps with time and risk.
3. **Boot chain** â€” the ten steps a Windows needs to start, drawn as a chain;
   the first broken link is where starting stops:
   PC firmware â†’ disk â†’ start-up partition (EFI or active) â†’ boot manager (and
   firmware boot entry / MBR code) â†’ start-up settings (BCD) â†’ Windows partition â†’
   Windows loader â†’ core system files and boot drivers â†’ registry â†’ starting up
   (restart loops).
4. **Detects**: missing or damaged EFI / active partitions, partitions past the
   end of the disk or overlapping, damaged GPT copies, empty MBR code, missing or
   damaged BCD, entries that point to missing partitions (**Windows moved**),
   hidden menu entries, wrong loader for the firmware mode, BitLocker-locked drives,
   drives needing a disk check, missing/damaged winload, kernel and boot drivers,
   damaged registry hives, disabled boot drivers (INACCESSIBLE_BOOT_DEVICE),
   "still in set-up" flags, and **restart loops** (Automatic Repair log, recent
   crash dumps, unfinished updates, started upgrades).
5. **Fixes** (each asked first, Cancel focused): rebuild start-up files
   (bcdboot), add to / show in the menu, make it start first, mark active, new MBR
   code (bootsect), create a new start-up partition (diskpart), repair GPT, disk
   check, BitLocker unlock, put back RegBack hives, SFC offline, turn boot drivers
   on, clear set-up flag, undo pending updates (DISM), stop the Automatic Repair
   loop, move a Windows back out of a folder.
6. **Start-up entries that lead nowhere** â€” lines in a BCD menu whose Windows or
   partition is gone, menu lines with no settings behind them, and lines in the
   PC's own UEFI list (Windows, Linux distributions, other loaders, named from
   their `\EFI\<folder>`) whose disk or start-up file is gone. Each is explained
   and can be removed (or only hidden from the menu), with backup and undo.
7. **Linux** â€” finds Linux systems (ext2/3/4 read directly and read-only: name from
   `/etc/os-release`, kernels in `/boot`, `/etc/fstab`; btrfs, XFS and LUKS are
   recognised but not opened) and their boot loaders on EFI partitions (GRUB,
   shim, systemd-boot), or GRUB in the MBR on BIOS PCs. Each Linux gets its own
   boot chain: firmware â†’ disk â†’ start-up partition â†’ PC's start-up list â†’ boot
   loader â†’ GRUB settings â†’ Linux partition â†’ kernel â†’ GRUB menu â†’ fstab. Fixes
   from Windows: add Linux to the PC's start-up list, make it start first, remove a
   leftover boot loader of a deleted Linux. Repairs inside Linux are explained
   step by step (Boot-Repair from a Linux USB stick), never attempted from Windows.
8. **Disks and partitions tool** — click a disk, a partition or unused space (or
   right-click for a menu): create (data, EFI, recovery, MSR; NTFS / FAT32 /
   exFAT; name and letter), delete, format, make bigger or smaller, change or
   remove the drive letter, rename, mark active / not active, change the type
   (data, EFI, recovery), check for errors, set up an empty disk (GPT / MBR),
   change an empty disk between MBR and GPT, MBR2GPT keeping Windows, wipe (the
   disk number must be typed), online / offline, allow writing. Each is explained,
   refused with the reason where it would hit the running Windows, this program's
   drive or the backup drive, backs up the partition table first, and runs
   Windows' own diskpart. Delete and wipe can be undone (the partition and its
   files come back when nothing new was written).
9. **Secure Boot** — its real state (from the firmware), what it does, which
   systems need it on or off, step-by-step how to switch it with hints for the
   PC's maker, and "Restart into firmware settings". Shown on the overview and in
   every boot chain's firmware step; Windows 7 with Secure Boot on is flagged.
10. **Back / forward and undo / redo** — toolbar buttons, Alt+Left / Alt+Right
   and the mouse's side buttons go back and forward through the places visited;
   Undo / Redo (Ctrl+Z / Ctrl+Y) take back or redo the last changes of the session.
11. **Explanations** â€” every step and finding has a short "What is this?" lesson,
   plus a "How a PC starts" introduction; they can be hidden.
12. **Backups and history** â€” every session gets a folder; at its start the BCD
   stores and partition tables of all disks are saved; before every fix, what it
   changes is saved. The History view lists every session and change with **Undo**
   (which is itself backed up and undoable).

Backups go next to the exe, or â€” when running from a DVD or Windows PE's RAM
disk â€” to the data drive with the most room (`<drive>\PATioBootDoctor Backups`).
The settings button picks another folder.

## Also in the program

- **Where it runs:** a notice at the top explains Windows PE, WinRE, a virtual
  machine, Windows started from a VHD file, or Safe Mode, with what they are for
  and the next steps.
- **Left pane:** Home, Boot chain, Disks and partitions, History; tools (Make a
  USB rescue disk, Command prompt and PowerShell as administrator, Back up now,
  Language); at the bottom Shut down, Restart and Hibernate (when the PC supports
  it), each now or on a timer (1 minute to 2 hours, or a time of day), cancellable
  in the pane. A timer waits while a repair is still running. Home is also on the
  toolbar (Ctrl+H). The pane shrinks to icons in a narrow window.
- **Make a USB rescue disk**: the PC's own recovery image (Winre.wim, or a Windows
  install stick's boot.wim) plus this program, which then starts by itself; the
  stick starts on UEFI and legacy BIOS PCs, with Secure Boot on. The PC's own
  start-up settings are never touched. The disk number must be typed to confirm.
- **Secure Boot on:** before restarting into the firmware to switch it on, a warning
  names the systems on this PC that would stop starting, asks to photograph the page
  and every firmware page changed, and explains how to go back; a 10-second countdown
  (cancel with Esc) runs before the restart.
- **Likely causes:** every problem in a boot chain says what may have caused it.
- **No system found:** the boot chain of the PC itself is drawn instead.
- **Console:** Settings > "Show the command console" shows every command the program
  runs and what it answered, at the bottom of the window; drag its top edge to
  resize it, close it with its X.
- **BitLocker:** locked drives can be unlocked from the report or from Disks and
  partitions; unlocked ones can be paused (no key asked at start-up until resumed) or
  turned off (decrypted). The key box adds the dashes while typing, pastes with Ctrl+V,
  and checks each group (a real key's groups divide by 11). "Save it (with a password)"
  keeps the key in `bitlocker-keys.dat` next to the exe, encrypted with a password the
  user chooses (AES-256-CBC, key from PBKDF2-SHA256, HMAC-SHA256 seal; bcrypt.dll loaded
  at run time), so it travels with the program. Running from the rescue stick, the file
  is kept on the stick (`\PATioBootDoctor\` next to the `PATioBootDoctor.stick` marker),
  and making a stick copies the saved keys onto it. Keys of the earlier version (DPAPI,
  `%APPDATA%`) are moved over when the password is first given. Settings menu > Saved
  BitLocker keys deletes one or all, or changes the password.
  When a drive is locked, start-up entries pointing into it are not called broken
  (only Unlock is offered), other "leads nowhere" findings and risky repairs warn to
  unlock first, and "no Windows found" says Windows is probably inside the drive.
- **Renamed system folders:** when no Windows is found, NTFS drives are searched
  four folders deep for a Windows whose Windows, System32 or config folder was
  renamed; "Give Windows' folders their names back" renames them (Undo renames them
  back).
- **Almost full Windows drive** (under 2 GB, or under 1 % up to 10 GB): "Free up
  space" measures temporary files, Windows Update downloads, crash reports and
  browser caches first, then removes the ones ticked. "Make the Windows partition
  bigger" uses unused space right after it, or deletes the partition right after it
  (never one before it, never one that holds a system, the backups or a start-up
  partition) and shows a before/after preview of the disk first. Every resize
  (bigger, smaller) goes through the preview.
- **Where the repair tools come from** (Recreate the recovery partition), best first: this
  Windows' own `System32\Recovery\Winre.wim`; the clean copy a rescue stick keeps in
  `\PATioBootDoctor\Winre.wim` (made with the stick); recovery partitions of the PC (their
  `\Recovery\WindowsRE` is open to the system account only, so it is read with the backup
  permission); another Windows of the same kind; install media (`install.wim`, or
  `install.esd`, which DISM first turns into a .wim); and finally the rescue stick's own
  `sources\boot.wim`, copied with this program and its start-up line taken out again. Whether
  a Windows' troubleshoot options exist is also taken from its `ReAgent.xml` (switched on,
  and on which partition), so a closed recovery folder is no longer reported as missing.
- **Troubleshoot options (WinRE):** when a Windows' recovery tools are switched off
  they are switched on again (reagentc); when the recovery partition is missing it
  is recreated (an empty recovery partition, unused space, or 1 GB from the end of
  the Windows partition), with Winre.wim from this Windows, another one, a recovery
  partition or a Windows install stick's install.wim. Shown with the risks first.
- **Partition recovery** (Disks, a disk or unused space > Find lost partitions):
  quick looks at every 1 MB boundary of the unused space, full reads every sector and
  also uses the backup copies NTFS, FAT32 and exFAT keep of their first sector.
  "Put it back" adds the entry to the partition table only (Undo removes it).
- **File recovery** (Disks, a partition > Recover deleted files): quick reads the
  NTFS file list for deleted entries (with their folders and whether their space was
  used again); full also reads the whole drive for records of an older file list
  (after a quick format) and for JPEG, PNG and PDF files by their contents. Files
  are copied to a folder on another drive; the scanned drive is never written to.
- **BitLocker keys screen** (left pane): the saved keys grouped by the PC they were saved on
  (Windows computer name, maker and model), each with its drive, date and an owner name; Show
  (with Copy), Owner…, Archive / Back from the archive, Delete; at the top Save the keys of
  this PC, Back up the keys file (a dated copy, still password-locked, to another drive),
  Change the password, Delete all. Keys saved by earlier versions show "PC not recorded".
- **Running from the rescue stick:** the keys file and the settings (`PATioBootDoctor.ini`:
  language, console, remembered lost partitions, ...) are kept on the stick in
  `\PATioBootDoctor\`, found by the `PATioBootDoctor.stick` marker, so they survive restarts.
  A notice (and the keys screen) reminds to back up the keys file to another place, since the
  stick may be its only copy. Backups and the history go to a data drive of the PC as before.
- **Updates:** Settings menu > Check for updates (and the About page) read the update file at
  the update address (default `https://www.muspc.com/updates/patio-boot-doctor.txt`, changeable
  under Settings menu > Update address), with lines `version=`, `url=` (https), `sha256=` (of
  the .exe) and `notes=`. A newer version is downloaded over HTTPS (winhttp.dll, loaded at run
  time, generic client name), its SHA-256 must match, it must be a 64-bit PATio Boot Doctor
  program with a version; then it replaces this one (kept as `.old` until the next start) and
  starts. Update from a file does the same with an .exe picked by hand. Not offered while
  running from the stick's memory (update in Windows, then make the stick again).
- **Progress** is amber, and a balloon over the bar shows what runs for 3 seconds whenever
  progress starts or moves to a new step (PatKit `setProgressStyle`).
- **Instant refresh:** the program asks Windows to report every disk that arrives or goes
  (also disks without a drive letter: new or wiped sticks, virtual disks), and scans again 0.6
  seconds later (several notices in a row count as one). Open menus do not hold it back: a
  rescue-stick menu that is open is rebuilt with the USB storage there now; questions and work
  in progress do (it runs right after them). A change during a scan is looked at once more when
  the scan ends; a scan the program does after its own changes cancels a pending one.
- **BitLocker, more:** a drive whose key is already saved is unlocked without being offered
  for saving again; saving asks for an owner name (suggested: this PC's name). When a drive
  is encrypted but has no 48-digit recovery key (TPM or PIN only), the program offers to add
  one (`manage-bde -protectors -add -RecoveryPassword`) and then backs it up. After "Turn
  BitLocker off", the decryption is followed (`manage-bde -status` every few seconds) and
  shown in the progress bar while nothing else runs; when it is finished the disks are
  scanned again.
- **Capturing BitLocker keys:** in a normal Windows session (not Windows PE) the report
  offers "Save the BitLocker recovery keys now" for unlocked BitLocker drives, and a notice
  offers it once per session while a drive's key is not saved yet (Settings menu > Save the
  BitLocker keys of this PC, too). The keys are read with `manage-bde -protectors -get
  -Type RecoveryPassword` and saved password-locked next to the program. The console hides
  any 48-digit key, also in tool output.
- **Disk in use:** nothing that changes partitions (create, delete, format, resize,
  move, types, conversions, wiping, restoring a lost partition, a new start-up or
  recovery partition, the MSR, the partition order) is done on the disk of the running
  Windows or of this program; the message says to start from the rescue stick instead.
  Reading (finding lost partitions, recovering files), unlocking, names and letters stay.
- **Copy and paste:** text boxes take Ctrl+C / Ctrl+X / Ctrl+V (and the Insert-key
  forms); Ctrl+C in a message copies its text; Ctrl+C elsewhere copies the whole report;
  the console has a Copy button. Password boxes show dots and cannot be copied from.
- **Reserved partition (MSR):** a GPT Windows disk without one is reported, with
  "Create the reserved partition" (16 MB, right after the start-up partition when there
  is room). A new start-up partition on a GPT disk now gets an MSR too, and "Reserved"
  in Create a partition is always 16 MB.
- **Windows on this PC:** every system found, with Start this one first, Show in / Hide
  from the menu, Add to the start-up menu, Rename (the menu name), and how long the menu
  waits. Windows.old is checked (kernel, loader, registry, Program Files, Users): when it
  is whole it can be brought back (current folders moved to `Windows.replaced <date>`,
  fresh start-up files; Undo moves everything back).
- **Partition order** (Disks > the disk > Put the partitions in Windows' own order, or
  from the troubleshoot and MSR findings): UEFI start-up · MSR · Windows · recovery, or
  legacy System Reserved · Windows · recovery. A plan of steps (remove, shrink, move,
  create, grow, start-up files, repair tools) is made from the disk as it is; one step is
  shown and confirmed at a time, and the disk is read again after each. Only the part of
  the disk around Windows is rearranged. A move copies sectors in the safe direction in
  blocks no larger than the distance moved and records its progress after every block in
  `partition-move.ini` in the backup folder; at the next start the program offers to
  finish a broken-off move.
- **Rescue disk with PATio Commander:** the image holds both programs in their PatSuite
  layout (`X:\PATio\PATioCommander_x64.exe`, `X:\PATio\PatSuite\PATio Boot Doctor\`), so each
  one's PatSuite menu finds the other. `winpeshl.ini` runs wpeinit (when the image has it),
  starts PATio Commander in the background, then PATio Boot Doctor, and keeps a command
  prompt open after it, so closing Boot Doctor does not restart the PC while Commander is
  in use. PATio Commander is taken from above this program's PatSuite folder; without it the
  stick has Boot Doctor alone.
- **Technician batch:**
  - **Load a storage driver** (Home): looks on the drive the program runs from, removable disks
    and optical drives for storage controller drivers (`.inf` of class SCSIAdapter / HDC), or
    Browse; loads with `drvload` (Windows PE) or `pnputil` (Windows), scans again and marks disks
    that appeared with it ("found with the driver ...").
  - **Disk groups:** Fixed disks (NVMe SSD, SATA SSD, hard disk, hybrid; size and speed from ATA
    IDENTIFY when the drive tells), Removable devices (USB disks and sticks, memory cards,
    floppy / Zip / Jaz), Optical drives, Enterprise and virtual storage (SAS, RAID, iSCSI, Fibre
    Channel, Storage Spaces, virtual disks, network drives).
  - **Windows Repair pages:** Repair tools (with System Restore: `rstrui`, `/offline:` for a
    Windows that does not start), Start-up options (Safe Mode variants, boot logging, verbose
    start, signature enforcement, ELAM, low-resolution video, F8 menu, Automatic Repair),
    Drivers (boot and system drivers of other makers from the offline SYSTEM file; Switch off
    sets Start=4 in every control set, undoable), Logs (SrtTrail.txt, ntbtlog.txt, System.evtx
    errors through wevtutil, Panther, CBS, DISM; last 600 lines, Copy, Notepad), Registry
    (loads the offline files as HKLM\BD_*, saves a copy first, opens regedit), Rescue an old
    Windows.
  - **Surface test** (Disks > the disk): reads every sector, read-only, lists unreadable areas.
  - **Job sheet** (Home): one A4 page with the MusPC logo as PDF (a packed picture of the page,
    so every language works) and TXT, in `PATioBootDoctorData`.
  - **Data folder:** `PATioBootDoctorData` holds keys, settings, notes and job sheets (on the
    stick when running from it, else next to the program); files and stick folders of earlier
    versions are moved over.
  - **Older copies:** a rescue stick made by an older version (its marker now holds the version)
    or an older exe on a removable disk is offered an update once per session.
  - **Progress:** starts next to the left pane; a note "Finished in ..." for 3 seconds after jobs
    longer than 10 seconds; start balloons stop 3 minutes after the first one.
  - **Left pane** in groups (Check, Repair, Disks and data, Records, Tools); **Technician mode**
    (Settings): no beginner boxes, compact lists and cards, console on.
  - **One home per tool:** Safe Mode moved to Start-up options; backup folder, saving and
    listing keys left the Settings menu (History and BitLocker keys have them); Back up now is
    in History only.
  - Code: `main.cpp` is split into `src/app/*.inl`; see `CLAUDE.md` for the map.
- **Simplicity and function batch:**
  - **Two modes** (switch at the bottom of the left pane, or in Settings): **Simple** shows Home,
    Boot chain, the rescue disk, Language and Power; **Technician** adds Windows on this PC,
    Windows Repair, Disks and data, Records, Command prompt and PowerShell, and shows the
    commands a fix runs before it is confirmed (`fixCommands`).
  - **Merged screens:** "Disks and data" has the pages Disks and partitions, Partition order,
    Move Windows, BitLocker keys and Recovered files; "Records" has History and backups, Notes
    and Job sheet. One **Power** button opens a menu (shut down, restart, hibernate, timers).
  - **Guided start** on Home: "What happens when you switch it on?" leads from the symptom to
    the check or repair that fits.
  - **Add a driver to the broken Windows** (Windows Repair > Drivers, and after Load driver):
    DISM adds the .inf to the offline Windows so it finds its disk at the next start.
  - **Signed updates:** the update file needs a `sig=` line, an ECDSA P-256 signature of the
    checksum, checked against the public key built into the program (`res\update-key.pub`).
    The private key is `keys\update-private.key` (never in git). Without the right signature
    the update is refused.
  - **Moving Windows to a smaller drive:** page, hibernation and swap files of the offline
    Windows are deleted and Windows is asked how far the partition can shrink
    (`shrink querymax`) before anything is planned; if it cannot shrink enough, the move is
    refused with the reason.
  - **Data folders** are now under one folder, `PATioAppData`: `PATioAppData\PATioBootDoctorData`
    and `PATioAppData\PATioCommanderData` (PATio Commander's settings). It is on the rescue
    stick when started from it, else next to PATio Commander (the folder above `PatSuite`),
    else in `%APPDATA%`. Older folders are moved over. Making a rescue stick copies both
    folders to it and keeps them when the stick is made again.
  - **Accounts** (Windows Repair > Accounts): the local accounts of a Windows that is not
    running, read from its SAM file. For a PC no one can sign in to: enable the built-in
    Administrator (switched off on most PCs), unlock an account locked after wrong passwords,
    or clear an account's password. Only local accounts — not a Microsoft or work/school
    sign-in (its password is not on the PC); clearing a password loses that account's EFS
    files and saved passwords. The accounts file is backed up first (History can undo it).
    A guided-start entry ("Windows starts but no one can sign in") opens this page.
    Logic in `src/Accounts.*`.
  - **Secure Boot certificates (2023 change)** (About Secure Boot, and a guided-start entry):
    reads the firmware's `db`, `KEK` and `dbx` stores, names the certificates in each, and says
    where the PC stands with the 2011→2023 certificate roll-out (the 2011 certificates expire in
    2026; KB5025885 revokes the 2011 Windows boot certificate). It flags the case that causes a
    "Secure Boot Violation": the 2011 certificate revoked in `dbx` while the 2023 one is not yet
    in `db`, and gives the recovery steps (turn Secure Boot off, update Windows, rebuild the
    start-up files, turn it back on). Read-only — it never writes a Secure Boot variable. Logic
    in `src/SecureBoot.*` (EFI signature-list parsing, certificate names via crypt32 or a plain
    scan, the diagnosis).
  - **Memory (RAM)** (Windows Repair > Repair tools): reads the result of the last Windows Memory
    Diagnostic from the event log — the running Windows' live log, or an offline Windows' own
    `System.evtx` — and says whether it passed (green), found errors (red: the RAM is likely
    faulty) or was never run. In a running Windows a button opens the Windows Memory Diagnostic
    (it asks whether to restart now or later). Bad RAM is a common cause of blue screens, failed
    repairs and start-up failures. Logic in `src/WinRepair.*` (`lastMemoryTest`, `startMemoryTest`).
  - **Screenshot and screen recording:** **Ctrl+Shift+S** saves a screenshot as a PNG;
    **Ctrl+Shift+R** starts and stops recording the screen. In Windows the recording is an
    **MP4 (H.264)** through Media Foundation (loaded at run time, so it adds no start-up
    dependency); in the rescue environment, which has no Media Foundation, it falls back to a
    Motion-JPEG `.avi`. Both are saved in the data folder. **No sound is ever recorded** — there
    is no audio track and no microphone is opened; a "REC … no sound" badge shows while recording,
    which auto-stops after 10 minutes. Only this window is captured (the pixels come from the
    program's own canvas), so no screen-record permission is needed. Logic in `src/Capture.*`
    (a dependency-free PNG writer, a baseline JPEG encoder and AVI writer, and an MP4 writer).
- **Windows Repair** (left pane): choose a Windows, then its repair tools, each explained,
  checked for whether it can run here (and why not), and asked before it runs: rebuild the
  start-up files, repair system files (sfc), **repair the Windows image** (DISM
  RestoreHealth: online from Windows Update, offline from install.wim/esd of the same
  version, with its scratch folder on the Windows drive), check the drive, the registry's
  backup copy, undo unfinished updates, **remove a recent update** (DISM's package list,
  newest first, then /Remove-Package), **Safe Mode at the next start / normal start again**
  (bcdedit safeboot), show the real error, disk drivers, unfinished set-up, the troubleshoot
  options, free up space. **Recent crashes** reads Windows\Minidump and MEMORY.DMP and
  explains each stop code (INACCESSIBLE_BOOT_DEVICE, CRITICAL_PROCESS_DIED, ...).
  **Rescue an old or badly damaged Windows**: (1) copy a whole disk sector by sector to a
  fixed .vhd on another drive (unreadable areas become zeros and are listed in
  `<image>.unreadable.txt`), (2) attach such an image read-only so it is searched instead of
  the damaged disk, (3) a full search that also finds registry files by their contents
  (`regf` header and `hbin` chain, named from the header), (4) a **Windows essentials**
  filter in the recovered files: registry files, DPAPI keys, credentials and certificates,
  mail (.pst/.ost), browser profiles and the user folders.
- **Notes** (left pane, Ctrl+N): short notes with the time and the PC's name, newest first;
  Change, Delete, Copy all. Kept in `PATioBootDoctor notes.txt` next to the settings (on the
  rescue stick when running from it), written to the session log, and added to the saved and
  copied report under NOTES.
- **Rescue stick size:** before anything starts, the room needed is worked out: the start-up
  image with both programs, the clean repair-tools copy, start-up files, saved keys and the
  data kept from the stick, plus a margin; and the smallest usual stick size that holds it
  (1, 2, 4 GB ...). The menu shows it, marks sticks that are too small, and shows the room
  needed on the PC while it is made (and for the image files). A stick that is too small, or
  too little room on the PC, is refused with what to use instead.
- **Making a stick keeps the data:** when the chosen stick already holds this program's data
  (`\PATioBootDoctorData\`: saved keys, settings, notes, the repair tools copy; `\PATioBootDoctor Backups\`; and a `\Drivers\` folder with the technician's storage drivers),
  it is first copied to the PC (`<work drive>\PATio Boot Doctor stick data (kept)`); the stick
  is only erased when that worked, and the data is put back on the new stick (the fresh repair
  tools copy is kept). This PC's saved keys and settings go on the stick too: when the stick
  already has keys and the same password opens both, they are merged (the newer key of a drive
  wins, an owner name is kept); otherwise this PC's file is kept beside it as
  `bitlocker-keys (this PC).dat`. If anything cannot be put back, the kept copy stays on the PC
  and the result says where.
- **Rescue disk:** the USB storage found is listed first (model and size, what gets
  erased), plus **a disk image file**: a fixed `.vhd` (for a virtual machine) and a plain
  `.img` (to write to a stick with Rufus or balenaEtcher). If the recovery partition cannot
  be made from the running Windows, the message says to start from the rescue stick.
- **Move Windows to another drive** (left pane, and at the top of Disks and partitions):
  choose a Windows, the target disk and UEFI or legacy BIOS; the target is cleaned and set
  up in Windows' own order, the Windows partition is copied block by block and grown to fill
  its place, then bcdboot and the recovery partition (Winre.wim, reagentc) are done for it.
  The running Windows is never the source (start from the rescue stick); a target that is in
  use or reports failing is refused; to a smaller disk the source first gives up free space
  at its end. The target disk number must be typed to confirm. Other data partitions are not
  copied.
- **Disk health (S.M.A.R.T.):** read at every scan (ATA attributes and thresholds, NVMe health
  log, and the drive's own failure prediction). Failing or worn disks are listed first in the
  report with what to do (back up first), shown on the disk header, in the disk's panel, and
  as a warning before any change to that disk or a partition move. Not shown when a USB
  adapter or RAID controller hides it.
- **Lost partitions are remembered** per disk in the settings file (with the date and quick or
  full), shown in every new session and checked against the disk as it is now; "Quick search
  again", "Full search", "Hide" and "Forget" are on the card.
- **History > Clean up…:** deletes sessions older than 90, 30 or 7 days, or all except the
  running one (with how many and how big), after a confirmation.
- **Shut down / Restart:** Windows PE and WinRE use wpeutil; Windows uses InitiateShutdown (like
  shutdown.exe, closing other programs), then InitiateSystemShutdownEx, shutdown.exe and
  ExitWindowsEx in turn; each attempt is written to the console.
- **About:** MusPC, licence (free for personal use; business licence on request),
  info@muspc.com, www.muspc.com, and **Leave a Google review** (opens MusPC's review page).

## Version 0.5.0

- **The start-up (EFI) partition is checked for real.** A new boot-chain step, "Start-up partition
  files", reads the EFI System Partition straight from its sectors (no drive letter) and checks the
  FAT32 file system itself: the boot sector and its backup copy in sector 6, that the two FAT copies
  agree, that every file's cluster chain is sound (no loop, no cluster shared by two files, the right
  length), that `bootmgfw.efi` and `bootx64.efi` read in full and are 64-bit programs, that the BCD
  loads, and that the partition can be written to. This catches the case the old chain missed, where
  the files look present but the FAT underneath is damaged — the hidden fault behind
  `bootrec /fixboot: Access is denied` and `bootrec /scanos: 0 installations`.
- **Rebuild the start-up partition.** When that check finds a damaged or unwritable EFI partition, a
  new repair makes a fresh FAT32 file system on it (the partition and its type stay) and writes new
  start-up files. A full image of the partition is saved first so it can be undone exactly; other
  makers' boot folders (HP, Dell, Lenovo tools, Linux GRUB) are kept and put back; BitLocker is
  paused when Windows is live, or its recovery key is called for otherwise.
- **Moving Windows is cleaner and survives a dying disk.** After a migration the copied Windows'
  saved drive-letter map (`MountedDevices\DosDevices\C:`) is cleared, so it gives itself C: cleanly on
  the new disk instead of landing on D:. And the block copy no longer stops at the first bad sector:
  on a read error it drops to one sector at a time, blanks only the sectors it truly cannot read,
  logs each unreadable area, and carries on — so a failing disk can still be migrated, with a warning
  afterwards to check Windows and recover anything important.
- **Rescue stick: choose the firmware.** Making a rescue stick now asks whether it should start both
  UEFI and legacy PCs (the default, unchanged from before), UEFI only (a GPT stick with an EFI system
  partition) or legacy only.

## Version 0.4.0 (Batch 1 and the technician batch)

- **A failing disk comes first.** When a disk reports S.M.A.R.T./NVMe problems (warning or
  failing, reallocated, pending or unreadable sectors, media errors), the program says so right
  after the scan, keeps an amber card on Home, and asks again before any repair or disk operation
  writes to that disk. It advises not to restart until there is a backup, and offers both ways:
  **Start PATio Commander** (to copy single files to a removable drive; on the rescue stick it
  already runs next to this program) or **Copy the whole disk** (a disk image on another drive).
- **How far did the last start get?** (Windows Repair > Repair tools): from the Windows' own
  traces — when it last saved its settings (it ran), its newest crash dump (stop code and its
  parameters), the boot log and Automatic Repair's log — it says whether Windows reached its
  kernel, whether the kernel lost its disk (0x7B / 0xED), and whether the cause points at **the
  disk itself** (bad sectors, damaged file system: 0xC0000032, 0xC0000185, ...) or at **the storage
  driver / controller mode** (0xC0000034, 0xC000000E, ...). A dump older than the last normal run
  is called out as an earlier problem. No dump at all is explained too (a 0x7B often leaves none),
  with how to find out (boot logging). A failing Windows disk is shown in red here as well.
  Fixed on the way: the stop code of 32-bit crash dumps was read from the wrong place.
- **Disk controller and its driver** (Windows Repair > Drivers): reads this PC's storage
  controllers and the mode the firmware runs them in (IDE, AHCI, RAID, NVMe, Intel VMD, from the PCI
  class code), and which storage drivers the offline Windows has and starts. Says how Windows was
  set up, whether it starts the driver the current mode needs, and what to do. Covers the older
  drivers too (`iaStorV`, `iaStor`, `pciide`, `intelide`, `atapi`, `msahci`) and Intel Rapid Storage
  serving both AHCI and RAID. Windows 7 also gets the controller added to its critical-device
  database when missing. Buttons: **Turn on the right drivers**, **Start in any SATA mode** (every
  built-in storage driver on) and **Prepare for a mode change** (AHCI / RAID / IDE: the driver is
  turned on *before* the firmware is switched). "Turn the disk drivers back on" from the report now
  uses the same plan instead of a fixed list. Logic in `src/StorageDrivers.*`.
- **Unreadable areas on the disk map:** the surface test and the disk copy keep each unreadable
  area; they are drawn as red marks on the disk's partition bar, listed with where they are, and
  kept across restarts (`unreadable-areas.txt`). **Which files are on them?** traces them through
  the NTFS file list to the files that sit there, marking those Windows needs to start (registry,
  drivers, kernel, boot files, the file system's own) — the answer to "can repairs help?". The
  list is saved as a text file. (On an SSD these are logical places, not physical flash.)
- **Rescue stick names:** each stick gets a name of its own when it is made (kept when it is made
  again), shown in the list of USB disks, in update notices and on About when started from it,
  where it can be renamed. Kept in the stick's marker file (`name=` line).
- **Recording and screenshots on the rescue stick** stop when less than 20 % of the stick is
  free, so it keeps working. Captures over the Windows 11 glass background no longer come out
  black (the background is filled in).
- **Jobs** (Records > Notes and job; offered after the first scan in Technician mode): how the
  PC — or just a disk or removable device — was handed in, and whether its BIOS battery works.
  From then on every step, test and note goes into the job's **action log**
  (`PATioBootDoctorData\Jobs`), with the time; a job carries over restarts until it is ended. The
  job sheet shows it, with the technician's name.
- **A flat BIOS battery** is spotted on its own (both modes): when the PC's clock is behind the
  last time Windows ran, Home says the battery is most likely flat, and that the firmware settings
  — the boot order and the SATA mode — reset with it.
- **Hardware details** (Records > Hardware details): maker, model, serial number, UUID, BIOS,
  mainboard, case, processor, every memory module (slot, size, type, speed, maker, part and serial
  number), disks and their health, storage controllers, graphics and network, and the systems
  found — with who collected them, when and from where. Each collection is a text file in
  `PATioBootDoctorData\Hardware` named after the PC, so every PC is kept separately. Logic in
  `src/HardwareInfo.*` (SMBIOS through `GetSystemFirmwareTable`, so it works on the stick too).
- **Usage statistics** (Settings menu): how often each screen, repair, disk operation, tool and
  guided-start answer was used, first and last use, starts in Windows or from the rescue stick,
  scans, repair results, time in use and the longest session. Grouped, copyable, resettable; kept
  only in `PATioBootDoctorData\usage.txt`.
- **Which mode shows what:** the job intake, Records (notes, job, hardware details) and Windows
  Repair are technician work and stay in Technician mode. The failing-disk warnings, the flat-
  battery notice, rescue stick names, screenshots/recording and usage statistics work in both
  modes.

## Languages

Every text goes through `T(L"English")` (`TF` fills in `{0}`, `{1}`; `N` marks ids
that are translated where shown; PatKit's own texts use `kt`). `build.cmd` runs
`tools\extract-strings.ps1`, which writes `lang\template.lang` (all texts) and
`lang\lang.rc` (the packs built into the exe).

- Built-in packs: `lang\<code>.lang` (UTF-8, gettext-like: `@code`, `@name`, `@rtl`,
  `msgid` / `msgstr`). Missing texts fall back to English.
- Own packs: Settings menu > **Add a language pack…** copies a `.lang` file into the
  `lang` folder next to the exe (or `%APPDATA%\PATio Boot Doctor\lang`); it adds a
  language or overrides texts of a built-in one, without rebuilding. **Make a
  template for a new language…** saves every text, ready to translate. An `en.lang`
  there can even reword the English.
- Settings > Language lists every language by its own name; the first start uses
  Windows' language when a pack exists. `BOOTDOCTOR_LANG=xx` picks one for a run.
- Right-to-left packs (`@rtl yes`, Arabic) draw text right to left and right-aligned.
  The layout itself is not mirrored yet.

## Build and check

```
build.cmd                                            x64 only into ..\PATioCommander\dist\PatSuite\PATio Boot Doctor
PATioBootDoctor_x64.exe --selftest <folder>          core logic on made-up disks; writes selftest.log
PATioBootDoctor_x64.exe --snapshot out.bmp classic light 1180 780 [detail|chain|chain2|disks|history|confirm|files|lost|grow|merge|unlock|clean|power|powermenu]
PATioBootDoctor_x64.exe --report <file.txt>          scan without a window (exit 1 = something needs attention)
PATioBootDoctor_x64.exe --rescuetest <folder>        makes a rescue stick on a throw-away 2 GB virtual disk and checks it (admin)
PATioBootDoctor_x64.exe --opstest <folder>           real disk operations and their undo on a throw-away 256 MB virtual disk (admin)
PATioBootDoctor_x64.exe --movetest <folder>          moving a partition, surface test, deleted file, lost partition and disk image on a throw-away 512 MB virtual disk (admin); writes movetest.log
PATioBootDoctor_x64.exe --makekey <private> <public> makes the update signing key pair (once)
PATioBootDoctor_x64.exe --signupdate <exe> <private> <url> [notes]   writes <exe>.update.txt with version, url, sha256, sig
PATioBootDoctor_x64.exe --hardware <file.txt> [technician]   this PC's hardware details without a window (admin, for the disks)
PATioBootDoctor_x64.exe --mp4test <folder>           records a short test clip through Media Foundation; writes mp4test.log
```

## Files

| File | Purpose |
|------|---------|
| `src/Disk.*` | disks, partitions, volumes, GPT/MBR checks, firmware boot entries, raw sector access |
| `src/Hive.*` | registry hives read offline (app hive first, `RegLoadKey` as fallback) |
| `src/Bcd.*` | BCD stores read straight from the hive, language independent |
| `src/Diagnose.*` | finding Windows, the boot chain, verdicts, plans, report text |
| `src/Repair.*` | the fixes, each backed up first |
| `src/Backup.*` | session folders, action manifests, history, undo, running console tools |
| `src/DiskTool.*` | the disk and partition tool (diskpart), with protection rules checked again before running |
| `src/Rescue.*` | the rescue USB stick |
| `src/Keys.*` | BitLocker key checks, formatting, saved keys (DPAPI) |
| `src/Recover.*` | lost partitions and deleted files (NTFS file list, full scan, carving) |
| `src/Layout.*` | the partition order plan and moving partitions |
| `src/Migrate.*` | moving Windows to another drive |
| `src/Smart.cpp` | disk health (S.M.A.R.T. / NVMe) |
| `src/Update.*` | checking, downloading and installing updates |
| `src/Space.*` | clean-up of temporary files, update downloads, crash reports, browser caches |
| `src/Lang.*` | translations |
| `src/Demo.*` | made-up PC for `--snapshot` |
| `src/SelfTest.*` | `--selftest` |

## Limits

- Fixes that change the running Windows' own registry or files are offered only
  from Windows PE / WinRE (start from a Windows USB stick and run it there).
- `bootsect.exe` (new MBR code) exists only in WinPE/WinRE and on install media;
  the app also finds it in `\boot` of a connected Windows USB stick.
- Dynamic disks and Storage Spaces are listed but only checked roughly.
- File recovery reads NTFS file lists; on FAT32 and exFAT only the full search
  (pictures and PDFs by contents) works. Files found by contents must be in one
  piece. A very fragmented file list ($MFT with an attribute list) is only partly read.
- Moving partitions and the partition order were tested on made-up disks only (self-test);
  try them on a spare or virtual disk first. BitLocker partitions are not moved.
- Lost partitions are put back with their original type (NTFS/exFAT/BitLocker as
  data, FAT32, Linux); a start-up (EFI) partition can be marked as such afterwards.
- Partition-table undo writes back the saved first/last sectors exactly; if the
  disk was re-partitioned since, scan and check before undoing.

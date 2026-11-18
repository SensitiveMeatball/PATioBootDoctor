// Part of class DoctorApp (included inside the class in main.cpp).
    // ------------------------------------------------------------ background work
    void progress(const wstr& step, float frac) {
        {
            std::lock_guard<std::mutex> l(lock_);
            progStep_ = step;
            progFrac_ = frac;
        }
        PostMessageW(hwnd(), WM_BD_PROGRESS, 0, 0);
    }

    void startScan() {
        if (busy_ || snapshot_) return;
        unloadAllHives();  // registry files opened for editing would be locked for the scan
        if (job_.joinable()) job_.join();
        busy_ = true;
        scanning_ = true;
        cancel_ = false;
        setProgress(T(L"Scanning"), 0.f, T(L"Reading disks"), false);
        wstr root = backupRoot_;
        bool openJournal = !journal_.isOpen();
        job_ = std::thread([this, root, openJournal] {
            noRedirection();
            auto r = std::make_shared<Report>(scan([this](const wstr& s, float f) { progress(s, f); }));
            checkTools(*r);
            wstr note;
            if (openJournal) {
                progress(T(L"Saving a backup of the start-up setup"), 0.95f);
                wstr err;
                if (journal_.open(root.empty() ? defaultBackupRoot(r->m) : root, r->host, err)) snapshotBootSetup(*r, journal_, note);
                else note = err;
            }
            if (journal_.isOpen()) journal_.saveReport(reportText(*r));
            {
                std::lock_guard<std::mutex> l(lock_);
                scanned_ = r;
                jobNote_ = note;
            }
            PostMessageW(hwnd(), WM_BD_SCANNED, 0, 0);
        });
        invalidate();
    }

    void askFixes(int inst, std::vector<int> which) {
        if (inst < 0 || inst >= (int)rep_.installs.size()) return;
        std::vector<Fix> list;
        for (int k : which)
            if (k >= 0 && k < (int)rep_.installs[inst].plan.size() && rep_.installs[inst].plan[k].available) list.push_back(rep_.installs[inst].plan[k]);
        askFixList(list, rep_.installs[inst].label());
    }

    void askFixList(std::vector<Fix> list, const wstr& what) {
        if (busy_ || list.empty() || snapshot_ && !snapshotConfirm_) return;
        for (Fix& f : list)
            if (f.kind == FX_UNLOCK && f.arg.empty()) {
                askUnlock(f, list, what);
                return;
            }
        for (Fix& f : list) {
            if (f.kind == FX_CLEAN_SPACE && f.arg.empty()) {
                startMeasure(f);
                return;
            }
            if (f.kind == FX_CAPTURE_KEYS) {
                captureKeys();
                return;
            }
            if (f.kind == FX_LAYOUT) {
                openLayout(f.install);
                return;
            }
            if (f.kind == FX_GROW_WINDOWS) {
                openPreview(f.part, unusedAfter(rep_, f.part) >= (64ull << 20) ? 0 : 1);
                return;
            }
        }
        // BitLocker first: a locked drive may hold the Windows these changes are about
        static const int risky[] = {FX_DELETE_BCD_ENTRY, FX_DELETE_FW_ENTRY, FX_DELETE_EFI_DIR, FX_REBUILD_BOOT, FX_CREATE_SYSPART, FX_FIX_GPT, FX_MBR_CODE,
                                    FX_REMOVE_FROM_MENU, FX_REBUILD_WINRE, FX_MAKE_DEFAULT};
        bool isRisky = false;
        for (const Fix& f : list)
            for (int k : risky) isRisky |= f.kind == k;
        if (isRisky && anyLockedDrive(rep_) && !lockedWarned_) {
            int locked = -1;
            for (size_t i = 0; i < rep_.m.parts.size() && locked < 0; ++i)
                if (rep_.m.parts[i].locked) locked = (int)i;
            showConfirm(T(L"Unlock BitLocker first?"),
                        TF(L"{0} is locked by BitLocker, so what is inside it cannot be seen. Windows may well be inside it, and the start-up entries and files this change touches may belong to it.\n\nUnlock it first and scan again; then decide.", rep_.m.parts[locked].name()),
                        {T(L"Cancel"), T(L"Unlock first"), T(L"Go on anyway")}, 1, 1, true, [this, list, what, locked](int b) {
                            if (b == 1) {
                                Fix u = makeFix(FX_UNLOCK);
                                u.part = locked;
                                u.disk = rep_.m.parts[locked].disk;
                                askFixList({u}, rep_.m.parts[locked].name());
                            } else if (b == 2) {
                                lockedWarned_ = true;
                                askFixList(list, what);
                                lockedWarned_ = false;
                            }
                        });
            return;
        }
        // A disk that reports health problems: any change (or even the next restart) can be when it stops
        // working, so offer a backup first.
        if (!healthWarned_) {
            int bad = -1;
            for (const Fix& f : list) {
                int disk = f.disk;
                if (disk < 0 && f.part >= 0 && f.part < (int)rep_.m.parts.size()) disk = rep_.m.parts[f.part].disk;
                for (const Disk& d : rep_.m.disks)
                    if (d.number == disk && diskUnhealthy(d)) bad = d.number;
            }
            if (bad >= 0) {
                showDiskHealthWarning(bad, [this, list, what] {
                    healthWarned_ = true;
                    askFixList(list, what);
                    healthWarned_ = false;
                });
                return;
            }
        }
        bool danger = false, anyUndo = false, anyNoUndo = false;
        wstr msg = list.size() == 1 ? T(L"This is done:\n\n") : T(L"These steps are done, one after another:\n\n");
        int n = 1;
        for (const Fix& f : list) {
            danger |= f.danger;
            (f.undoable ? anyUndo : anyNoUndo) = true;
            msg += (list.size() > 1 ? std::to_wstring(n++) + L". " : wstr()) + f.title + (f.undoable ? L"" : L" (cannot be undone)") + L"\n" + f.takes + L"\n\n";
        }
        if (techMode_) {
            wstr cmds;
            for (const Fix& f : list) {
                wstr c = fixCommands(f, rep_);
                if (!c.empty()) cmds += c + L"\n";
            }
            if (!cmds.empty()) msg += T(L"Commands:\n") + cmds + L"\n";
        }
        if (anyUndo) msg += T(L"Before anything changes, a copy of what will change is saved in\n") + (sessionFolder_.empty() ? backupShown_ : sessionFolder_) + T(L"\nso History can undo it.");
        if (anyNoUndo) msg += anyUndo ? T(L" Steps marked \"cannot be undone\" are the exception.") : L"";
        showConfirm(list.size() == 1 ? list[0].title + L"?" : T(L"Repair ") + what + L"?", msg, {T(L"Cancel"), list.size() == 1 ? L"Do it" : T(L"Do all steps")}, 1, 0, danger,
                    [this, list](int b) {
                        if (b == 1) startFixes(list);
                    });
    }

    void startFixes(const std::vector<Fix>& list) {
        if (busy_ || snapshot_) return;
        for (const Fix& f : list) countUse(L"Repairs: " + f.title);
        if (job_.joinable()) job_.join();
        if (!journal_.isOpen()) {
            showMessage(T(L"No backup folder"), T(L"Nothing was changed, because there is no folder to keep backups in. Choose one with the settings button."));
            return;
        }
        busy_ = true;
        cancel_ = false;
        setProgress(T(L"Repairing"), -1.f, list[0].title, true);
        auto copy = std::make_shared<Report>(rep_);
        job_ = std::thread([this, copy, list] {
            noRedirection();
            wstr summary;
            std::vector<wstr> dirs;
            bool all = true;
            int n = 0;
            for (const Fix& f : list) {
                if (cancel_) {
                    summary += T(L"Stopped before: ") + f.title + L"\n";
                    all = false;
                    break;
                }
                progress(f.title, (float)n++ / list.size());
                FixOutcome o = runFix(f, *copy, journal_, &cancel_, [this, n, &list](const wstr& s) { progress(s, (float)(n - 1) / list.size()); });
                summary += (o.ok ? T(L"Done: ") : T(L"Failed: ")) + f.title + L"\n" + o.result + L"\n\n";
                if (o.ok && o.undoable && !o.actionDir.empty()) dirs.push_back(o.actionDir);
                if (!o.ok) {
                    all = false;
                    break;
                }
            }
            {
                std::lock_guard<std::mutex> l(lock_);
                jobOk_ = all;
                jobNote_ = summary;
                jobDirs_ = dirs;
            }
            PostMessageW(hwnd(), WM_BD_FIXED, 0, 0);
        });
        invalidate();
    }

    void askUndo(int s, int a) {
        if (busy_ || s < 0 || s >= (int)sessions_.size() || a < 0 || a >= (int)sessions_[s].actions.size()) return;
        ActionInfo ai = sessions_[s].actions[a];
        showConfirm(T(L"Undo \"") + ai.title + L"\"?",
                    T(L"What this change altered on ") + ai.time + T(L" is put back from its backup. What is there now is saved first, so this undo can be undone too.\n\nIf the disks were changed since then, the result can be different from before. Scan again afterwards."),
                    {T(L"Cancel"), T(L"Undo")}, 1, 0, true, [this, ai](int b) {
                        if (b == 1) startUndo(ai, 0);
                    });
    }

    // mode 0: from History, 1: the Undo button, 2: the Redo button
    void startUndo(const ActionInfo& ai, int mode) {
        if (busy_) return;
        if (job_.joinable()) job_.join();
        busy_ = true;
        setProgress(mode == 2 ? T(L"Redoing") : T(L"Undoing"), -1.f, ai.title, false);
        Machine m = rep_.m;
        undoMode_ = mode;
        undoFrom_ = ai.dir;
        job_ = std::thread([this, ai, m] {
            noRedirection();
            wstr log, dir;
            bool ok = undoAction(ai, m, &journal_, log, &dir);
            {
                std::lock_guard<std::mutex> l(lock_);
                jobOk_ = ok;
                jobNote_ = log;
                jobDirs_ = {dir};
            }
            PostMessageW(hwnd(), WM_BD_UNDONE, 0, 0);
        });
        invalidate();
    }

    void startBackupNow() {
        if (busy_ || !haveReport_ || snapshot_) return;
        if (!journal_.isOpen()) {
            showMessage(T(L"No backup folder"), T(L"Choose a folder for backups first."));
            return;
        }
        if (job_.joinable()) job_.join();
        busy_ = true;
        setProgress(T(L"Backing up"), -1.f, T(L"Start-up settings and partition tables"), false);
        auto copy = std::make_shared<Report>(rep_);
        job_ = std::thread([this, copy] {
            noRedirection();
            wstr log;
            bool ok = snapshotBootSetup(*copy, journal_, log, T(L"Start-up setup, backed up on request"));
            {
                std::lock_guard<std::mutex> l(lock_);
                jobOk_ = ok;
                jobNote_ = log;
            }
            PostMessageW(hwnd(), WM_BD_SAVED, 0, 0);
        });
        invalidate();
    }

    void onCancelProgress() override {
        if (countdown_ > 0) {
            countdown_ = 0;
            KillTimer(hwnd(), kCountdownTimer);
            clearProgress();
            showToast(L"check", T(L"Restart cancelled"), T(L"Nothing was changed."), {}, nullptr);
            return;
        }
        cancel_ = true;
    }

    bool onMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) override {
        if (msg == WM_XBUTTONUP) {  // the mouse's own back / forward buttons
            if (!overlayOpen()) {
                if (HIWORD(wp) == XBUTTON1) navBack();
                else navForward();
            }
            result = TRUE;
            return true;
        }
        if (msg == WM_BD_CONSOLE) {
            consolePosted_ = false;
            if (consoleOn_) invalidate();
            result = 0;
            return true;
        }
        if (msg == WM_SETCURSOR && LOWORD(lp) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd(), &pt);
            if (dragging_ || splitRect_.contains((float)pt.x, (float)pt.y)) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
                result = TRUE;
                return true;
            }
        }
        if (msg == WM_BD_PROGRESS) {
            wstr s;
            float f;
            {
                std::lock_guard<std::mutex> l(lock_);
                s = progStep_;
                f = progFrac_;
            }
            setProgress(progTitle_.empty() ? T(L"Working") : progTitle_, f, s, progCancel_);
            result = 0;
            return true;
        }
        if (msg == WM_DEVICECHANGE && (wp == DBT_DEVICEARRIVAL || wp == DBT_DEVICEREMOVECOMPLETE || wp == DBT_DEVNODES_CHANGED) && !snapshot_) {
            // A disk, stick or virtual disk came or went: look again almost at once (a plug-in sends
            // several notices in a row; the short wait takes them together)
            if (scanning_) rescanPending_ = true;
            else SetTimer(hwnd(), kRefreshTimer, 600, nullptr);
            result = TRUE;
            return true;
        }
        if (msg == WM_BD_DECRYPT) {
            decryptBusy_ = false;
            int pct = (int)wp;  // tenths of a percent still encrypted, -1 unknown
            if (pct < 0) return true;
            decryptLeft_ = pct;
            if (pct == 0) {
                KillTimer(hwnd(), kDecryptTimer);
                wstr name = decryptName_;
                decryptLetter_.clear();
                if (!busy_) clearProgress();
                showToast(L"check", T(L"BitLocker is off"), TF(L"{0} is fully decrypted.", name), {}, nullptr);
                quietScan_ = true;
                if (!busy_) startScan();
            } else if (!busy_) {
                wchar_t d[64];
                swprintf(d, 64, L"%.1f %%", (1000 - pct) / 10.0);
                setProgress(TF(L"Decrypting {0}", decryptName_), (1000 - pct) / 1000.f, TF(L"{0} done; you can keep working. Keep the PC on.", wstr(d)), false);
            }
            result = 0;
            return true;
        }
        if (msg == WM_BD_DEFER) {
            std::vector<std::function<void()>> q;
            q.swap(deferred_);
            for (auto& fn : q) fn();
            result = 0;
            return true;
        }
        if (msg == WM_BD_JOB) {
            if (job_.joinable()) job_.join();
            busy_ = false;
            clearProgress();
            std::function<void()> done;
            done.swap(jobDone_);
            progTitle_.clear();
            progCancel_ = false;
            if (done) done();
            invalidate();
            result = 0;
            return true;
        }
        if (msg != WM_BD_SCANNED && msg != WM_BD_FIXED && msg != WM_BD_UNDONE && msg != WM_BD_SAVED) return false;
        result = 0;
        if (job_.joinable()) job_.join();
        busy_ = false;
        clearProgress();
        std::shared_ptr<Report> r;
        wstr note;
        bool ok;
        std::vector<wstr> dirs;
        {
            std::lock_guard<std::mutex> l(lock_);
            r = scanned_;
            scanned_.reset();
            note = jobNote_;
            ok = jobOk_;
            dirs.swap(jobDirs_);
        }
        if (msg == WM_BD_FIXED && wp == 0 && !keyToSave_.key.empty()) {
            if (ok) {
                wstr err;
                if (!saveKey(keyToSave_, keysPass_, err)) note += L"\n\n" + err;
                else {
                    note += TF(L"\n\nThe key is saved next to this program ({0}), locked with your password. BitLocker keys (in the left pane) lists, renames and deletes saved keys.", keyStorePath());
                    if (!stickFolder().empty()) note += T(L"\n\nIt is kept only on this USB stick: copy the keys file to another place too (BitLocker keys > Back up the keys file).");
                }
            }
            SecureZeroMemory(&keyToSave_.key[0], keyToSave_.key.size() * sizeof(wchar_t));
            keyToSave_ = SavedKey();
        }
        if (msg == WM_BD_FIXED && !dirs.empty()) {
            for (const wstr& d : dirs) undo_.push_back(d);
            redo_.clear();
        }
        if (msg == WM_BD_UNDONE && ok && !dirs.empty() && !dirs[0].empty()) {
            auto drop = [&](std::vector<wstr>& v) { v.erase(std::remove(v.begin(), v.end(), undoFrom_), v.end()); };
            drop(undo_);
            drop(redo_);
            if (undoMode_ == 2) undo_.push_back(dirs[0]);
            else redo_.push_back(dirs[0]);
        }
        if (msg == WM_BD_SCANNED) {
            bool wasQuiet = quietScan_, fromDevice = deviceScan_;
            quietScan_ = false;
            deviceScan_ = false;
            scanning_ = false;
            KillTimer(hwnd(), kRefreshTimer);  // just looked: a change the program made itself needs no second look
            if (rescanPending_) {  // something was plugged in or out while looking: look again
                rescanPending_ = false;
                SetTimer(hwnd(), kRefreshTimer, 300, nullptr);
            }
            if (!devNotify_ && !snapshot_) {
                // disks without a drive letter (a new or wiped stick) are reported this way too
                DEV_BROADCAST_DEVICEINTERFACE_W f{};
                f.dbcc_size = sizeof f;
                f.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
                f.dbcc_classguid = {0x53F56307, 0xB6BF, 0x11D0, {0x94, 0xF2, 0x00, 0xA0, 0xC9, 0x1E, 0xFB, 0x8B}};  // disks
                devNotify_ = RegisterDeviceNotificationW(hwnd(), &f, DEVICE_NOTIFY_WINDOW_HANDLE);
            }
            (void)wasQuiet;
            if (r) {
                rep_ = *r;
                haveReport_ = true;
                if (sel_ >= (int)rep_.installs.size()) sel_ = -1;
                node_ = -1;
            }
            if (journal_.isOpen()) {
                backupShown_ = journal_.root();
                setProtectedFolder(journal_.root());
                sessionFolder_ = journal_.folder();
            } else if (!note.empty())
                showMessage(T(L"Backups cannot be saved"), note + T(L"\n\nRepairs stay off until a backup folder is chosen with the settings button."));
            refreshSessions();
            if (layoutInstall_ >= 0) replanLayout();
            if (view_ == V_REPAIR) {
                buildRepairTools();
                repairTab(repTab_);
            }
            markDriverDisks();
            checkOldCopies();
            warnUnhealthyAfterScan();
            clockNote_ = snapshot_ ? wstr() : clockBehindNote();
            offerJobAfterScan();
            countUse(L"Program: scans");
            if (!stickReminded_ && !snapshot_ && !stickFolder().empty() && keyStoreExists() && afterScanNote_.empty()) {
                stickReminded_ = true;
                showToast(L"bd_key", T(L"Back up the keys on this stick"),
                          T(L"Your saved BitLocker keys are kept only on this USB stick. If the stick is lost or breaks, they are gone: copy the keys file to another place too."),
                          {T(L"Back up now"), T(L"Later")}, [this](int b) {
                              if (b == 0) backupKeysFile();
                          }, false);
            }
            if (!keysOffered_ && !g_sys.winPE && !snapshot_ && afterScanNote_.empty()) {
                keysOffered_ = true;
                wstr saved = L" " + settings().get(L"savedKeyIds") + L" ";
                int missing = 0;
                for (const Partition& p : rep_.m.parts)
                    if (p.bitlocker && !p.locked && saved.find(L" " + p.key + L" ") == wstr::npos) ++missing;
                if (missing) {
                    showToast(L"bd_key", T(L"Keep your BitLocker keys"),
                              T(L"While Windows runs, the recovery keys of its encrypted drives can be read and saved. You need them if Windows ever stops starting."),
                              {T(L"Save the keys"), T(L"Not now")}, [this](int b) {
                                  if (b == 0) captureKeys();
                              }, false);
                    afterScanNote_.clear();
                    invalidate();
                    return true;
                }
            }
            wstr moveWhat;
            if (!movePromptShown_ && journal_.isOpen() && pendingMove(journal_.root(), moveWhat)) {
                movePromptShown_ = true;
                showConfirm(T(L"A partition move was broken off"),
                            moveWhat + T(L"\n\nUntil it is finished, that partition cannot be used. Finishing it goes on from where it stopped. Keep the PC on and plugged in."),
                            {T(L"Later"), T(L"Finish it now")}, 1, 1, true, [this](int b) {
                                if (b == 1) startResumeMove();
                            });
            } else if (reopenRescueMenu_) {
                reopenRescueMenu_ = false;
                askRescue();  // the list of USB sticks as it is now
            } else if (wasQuiet && afterScanNote_.empty()) {
                showToast(L"check", fromDevice ? T(L"Disks changed") : T(L"Scanned again"),
                          fromDevice ? T(L"A disk or USB stick was plugged in or out: the lists are up to date.") : T(L"Disks or drives changed, so the report was brought up to date."), {}, nullptr);
            } else if (afterScanNote_.empty())
                showToast(L"check", T(L"Scan finished"),
                          plural(rep_.installs.size(), T(L"system found"), T(L"systems found")) + T(L" on ") + plural(rep_.m.disks.size(), T(L"disk"), T(L"disks")) + L". " +
                              (rep_.problems() ? plural(rep_.problems(), T(L"thing needs"), T(L"things need")) + T(L" attention.") : wstr(T(L"Nothing needs attention."))),
                          {}, nullptr);
            else {
                showMessage(afterScanTitle_, afterScanNote_);
                afterScanNote_.clear();
            }
        } else if (msg == WM_BD_SAVED) {
            refreshSessions();
            if (ok) showToast(L"check", T(L"Backed up"), T(L"The start-up setup of all disks was saved. See History."), {}, nullptr);
            else showMessage(T(L"The backup was not complete"), note);
        } else {
            // A repair or an undo changed the disks: look again, then tell what happened.
            afterScanTitle_ = msg == WM_BD_UNDONE ? (ok ? (undoMode_ == 2 ? T(L"Redone") : T(L"Undone")) : T(L"The undo was not complete"))
                                                   : wp == 2 ? (ok ? T(L"Rescue USB stick ready") : T(L"The rescue stick was not made"))
                                                   : wp == 1 || wp == 3 ? (ok ? T(L"Done") : T(L"Not done")) : (ok ? T(L"Repair finished") : T(L"Repair stopped"));
            if (wp == 1 && ok && lastDiskOp_.kind == DO_BITLOCKER_OFF && lastDiskOp_.part >= 0 && lastDiskOp_.part < (int)rep_.m.parts.size()) {
                decryptLetter_ = rep_.m.parts[lastDiskOp_.part].letter;
                decryptName_ = rep_.m.parts[lastDiskOp_.part].name();
                if (!decryptLetter_.empty()) SetTimer(hwnd(), kDecryptTimer, 4000, nullptr);
            }
            if (wp == 3 && ok) {
                if (lastLayoutKind_ == ST_MOVE || lastLayoutKind_ == ST_CREATE_ESP || lastLayoutKind_ == ST_CREATE_SYSRES) layoutBootDue_ = true;
                if (lastLayoutKind_ == ST_BOOTFILES) layoutBootDue_ = false;
            }
            if (msg == WM_BD_FIXED) countUse(ok ? L"Repair results: finished" : L"Repair results: stopped or failed");
            afterScanNote_ = (note.empty() ? wstr(ok ? T(L"Done.") : T(L"Something went wrong.")) : note) +
                             (msg == WM_BD_FIXED ? T(L"\n\nThe disks were scanned again, so the report shows how things are now.") : L"");
            refreshSessions();
            startScan();
        }
        invalidate();
        return true;
    }

    void onSnapshot(const wstr& overlay) override {
        if (overlay == L"chain") { view_ = V_CHAIN; sel_ = 1; node_ = 9; }
        else if (overlay == L"chain2") { view_ = V_CHAIN; sel_ = 0; node_ = 4; }
        else if (overlay == L"linux") { view_ = V_CHAIN; sel_ = 3; node_ = 3; }
        else if (overlay == L"linuxdetail") { sel_ = 3; }
        else if (overlay == L"detail") { sel_ = 1; }
        else if (overlay == L"disks") { view_ = V_DISKS; dsKind_ = SEL_PART; dsPart_ = 4; dsDisk_ = 1; }
        else if (overlay == L"diskgap") { view_ = V_DISKS; dsKind_ = SEL_GAP; dsDisk_ = 1; dsGap_ = 0; }
        else if (overlay == L"secureboot") { showSecureBoot(); }
        else if (overlay == L"secureon") { rep_.m.fw.secureBoot = false; warnSecureBootOn(); }
        else if (overlay == L"about") { view_ = V_ABOUT; }
        else if (overlay == L"console") {
            consoleOn_ = true;
            consoleAdd(L"> diskpart.exe /s \"C:\\Users\\Sample\\AppData\\Local\\Temp\\PATioBD-disk.txt\"");
            consoleAdd(T(L"Microsoft DiskPart\r\n\r\nDisk 1 is now the selected disk.\r\nPartition 1 is now the selected partition.\r\nDiskPart successfully assigned the drive letter or mount point."));
            consoleAdd(L"  (exit code 0)");
            consoleAdd(L"> bcdedit.exe /enum all /v");
            view_ = V_DISKS;
        }
        else if (overlay == L"nosystem") { rep_.installs.clear(); buildPcChain(rep_); view_ = V_CHAIN; node_ = 5; }
        else if (overlay == L"history") view_ = V_HISTORY;
        else if (overlay == L"systems") view_ = V_SYSTEMS;
        else if (overlay == L"migrate") { openMigrate(); migTarget_ = 2; }
        else if (overlay == L"notes") {
            notes_ = {Note{L"2026-11-13 10:42", L"ANN-LAPTOP", L"The PC showed \"INACCESSIBLE_BOOT_DEVICE\" after the BIOS update. Secure Boot was on, SATA mode was changed to RAID by the update."},
                      Note{L"2026-11-13 10:15", L"ANN-LAPTOP", L"Ann gave the BitLocker key from her Microsoft account (saved, owner: Ann's laptop)."}};
            view_ = V_NOTES;
        }
        else if (overlay == L"keys") {
            keysPass_ = L"demo";
            auto k = [](const wchar_t* name, const wchar_t* when, const wchar_t* pc, const wchar_t* model, const wchar_t* owner, bool arch) {
                SavedKey x;
                x.id = name;
                x.name = name;
                x.when = when;
                x.key = L"123453-234564-345675-456786-567897-111111-222222-333333";
                x.pc = pc;
                x.model = model;
                x.owner = owner;
                x.archived = arch;
                return x;
            };
            keysCache_ = {k(L"C: · Windows · Disk 0, partition 3", L"2026-11-12 21:05", L"ANN-LAPTOP", L"LENOVO ThinkPad T14 Gen 3", L"Ann's laptop", false),
                          k(L"D: · Data · Disk 1, partition 1", L"2026-11-12 21:05", L"ANN-LAPTOP", L"LENOVO ThinkPad T14 Gen 3", L"", false),
                          k(L"C: · Windows · Disk 0, partition 3", L"2026-10-02 18:40", L"", L"Dell Inc. OptiPlex 7090", L"Office PC (given away)", true)};
            view_ = V_KEYS;
            progTitle_.clear();
            setProgress(T(L"Copying Windows: 12.4 GB of 212 GB"), 0.06f, T(L"Disk 1 -> Disk 2"), true);
        }
        else if (overlay == L"health") {
            DiskHealth& h = rep_.m.disks[1].health;
            uint8_t attrs[512] = {};
            auto attr = [&](int k, int id, uint64_t raw) {
                attrs[2 + k * 12] = (uint8_t)id;
                attrs[2 + k * 12 + 3] = 100;
                for (int i = 0; i < 6; ++i) attrs[2 + k * 12 + 5 + i] = (uint8_t)(raw >> (8 * i));
            };
            attr(0, 9, 31877);
            attr(1, 194, 41);
            attr(2, 5, 24);
            attr(3, 197, 8);
            parseAtaSmart(attrs, nullptr, h);
            view_ = V_DISKS;
            dsKind_ = SEL_DISK;
            dsDisk_ = 1;
        }
        else if (overlay == L"layout") { openLayout(1); }
        else if (overlay == L"rescue") { askRescue(); }
        else if (overlay == L"power") { powerAction_ = 1; powerLeft_ = 299; }
        else if (overlay == L"powermenu") { powerMenu(1); }
        else if (overlay == L"files") { demoFiles(); }
        else if (overlay == L"lost") {
            view_ = V_DISKS;
            LostPartition a;
            a.offset = 120ull << 30; a.length = 200ull << 30; a.fs = L"NTFS"; a.label = L"Photos";
            LostPartition b;
            b.offset = 340ull << 30; b.length = 31ull << 30; b.fs = L"FAT32"; b.restorable = false;
            b.why = T(L"It runs into a partition that exists now, so it was partly overwritten or that partition was made over it.");
            LostMem m;
            m.when = L"2026-11-12 21:40";
            m.parts = {a, b};
            for (const Disk& d : rep_.m.disks)
                if (d.number == 1) lostMem_[d.id()] = m;
        }
        else if (overlay == L"grow") { openPreview(2, 0); }
        else if (overlay == L"merge") { openPreview(2, 1); }
        else if (overlay == L"growbad") { openPreview(1, 1); }
        else if (overlay == L"unlock") {
            Fix u = makeFix(FX_UNLOCK);
            u.part = 4;
            u.disk = 1;
            snapshotConfirm_ = true;
            askFixList({u}, L"E:");
        }
        else if (overlay == L"clean") {
            std::vector<CleanSpot> spots = cleanupSpots();
            uint64_t sizes[] = {3400ull << 20, 6100ull << 20, 820ull << 20, 2300ull << 20};
            for (size_t k = 0; k < spots.size() && k < 4; ++k) spots[k].bytes = sizes[k];
            Fix c = makeFix(FX_CLEAN_SPACE);
            c.install = 0;
            cleanForm(c, spots);
        }
        else if (overlay == L"confirm") {
            sel_ = 1;
            snapshotConfirm_ = true;
            std::vector<int> all = {0, 1, 2};
            askFixes(1, all);
        }
    }

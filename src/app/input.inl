// Part of class DoctorApp (included inside the class in main.cpp).
    // ------------------------------------------------------------ input
    void onMouseMove(int, int y) override {
        if (!dragging_) return;
        if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
            dragging_ = false;
            settings().setInt(L"consoleHeight", (int)consoleH_);
            return;
        }
        consoleH_ = std::max(90.f, std::min(1200.f, dragH_ + (dragY_ - y) / scale_));
        invalidate();
    }

    void onMouseDown(const kit::Hit& h, int, int y, bool dbl) override {
        if (h.id == H_SIDE) {
            if (h.a != sel_) navPush();
            sel_ = h.a;
            scroll_[V_REPORT] = 0;
            node_ = -1;
            if (view_ == V_CHAIN && sel_ < 0) sel_ = 0;
            invalidate();
        } else if (h.id == H_NODE) {
            node_ = h.a;
            if (dbl) firstFixOfNode();
            invalidate();
        } else if (h.id == H_LINK) {
            navPush();
            node_ = h.a;
            setView(V_CHAIN);
        } else if (h.id == H_CONSOLE_SPLIT) {
            dragging_ = true;
            dragY_ = y;
            dragH_ = consoleH_;
        } else if (h.id == H_PART || h.id == H_GAP || h.id == H_DISKHEAD) {
            int k = h.id == H_PART ? SEL_PART : h.id == H_GAP ? SEL_GAP : SEL_DISK;
            int disk = h.id == H_PART ? rep_.m.parts[h.a].disk : h.id == H_GAP ? h.a / 1000 : h.a;
            int part = h.id == H_PART ? h.a : -1, gap = h.id == H_GAP ? h.a % 1000 : -1;
            if (k != dsKind_ || disk != dsDisk_ || part != dsPart_ || gap != dsGap_) navPush();
            dsKind_ = k;
            dsDisk_ = disk;
            dsPart_ = part;
            dsGap_ = gap;
            invalidate();
        }
    }

    void onRightClick(const kit::Hit& h, int x, int y) override {
        if (h.id == H_PART || h.id == H_GAP || h.id == H_DISKHEAD) {
            onMouseDown(h, x, y, false);
            diskMenu(x, y);
        }
    }

    void onClick(const kit::Hit& h) override {
        switch (h.id) {
            case H_NAV: navClick(h.a); break;
            case H_LAYOUT_GO: askLayoutStep(); break;
            case H_LAYOUT_SCHEME: openLayout(layoutInstall_, h.a); break;
            case H_SYS: systemAction(h.a / 100, h.a % 100); break;
            case H_SYS_TIMEOUT: askTimeout(); break;
            case H_MIG_OPEN: openMigrate(); break;
            case H_MIG_SRC: migInstall_ = h.a; invalidate(); break;
            case H_MIG_DST: migTarget_ = h.a; invalidate(); break;
            case H_MIG_SCHEME: migUefi_ = h.a; invalidate(); break;
            case H_MIG_GO: askMigration(); break;
            case H_KEY: keyAction(h.a / 10, h.a % 10); break;
            case H_NOTE: noteAction(h.a / 10, h.a % 10); break;
            case H_REP_SRC:
                repInstall_ = h.a;
                buildRepairTools();
                invalidate();
                break;
            case H_REP_TOOL: repairTool(h.a); break;
            case H_REP_RESCUE: rescueTool(h.a); break;
            case H_REP_TAB: repairTab(h.a); break;
            case H_TAB: familyTab(h.a); break;
            case H_GUIDE: guideStep(h.a); break;
            case H_ADD_DRIVER: addDriverToWindows(h.a); break;
            case H_REP_OPT: bootOption(h.a); break;
            case H_REP_DRV: driverOff(h.a); break;
            case H_REP_LOG: openLog(h.a); break;
            case H_REP_LOGACT: logAction(h.a); break;
            case H_REP_HIVE: hiveAction(h.a); break;
            case H_REP_RESTORE: systemRestore(); break;
            case H_REP_ACCT: accountAction(h.a); break;
            case H_REP_MEM: memoryAction(h.a); break;
            case H_HEALTH: healthAction(h.a); break;
            case H_STICK_NAME: renameThisStick(); break;
            case H_BOOTSTAGE: bootStageAction(h.a); break;
            case H_STORAGE: storageAction(h.a); break;
            case H_BADMAP: whichFilesOnBad(h.a); break;
            case H_JOB: jobAction(h.a); break;
            case H_STATS: statsAction(h.a); break;
            case H_HW: hardwareAction(h.a); break;
            case H_LOAD_DRIVER: askLoadDriver(); break;
            case H_JOBSHEET: makeJobSheet(); break;
            case H_NOTES_TOP:
                if (h.a == 0) addNote();
                else if (h.a == 1 && copyText(notesText())) showToast(L"check", T(L"Notes copied"), plural(notes_.size(), T(L"note"), T(L"notes")), {}, nullptr);
                break;
            case H_KEYS_TOP: keysTop(h.a); break;
            case H_UPDATE: checkUpdate(true); break;
            case H_FILE:
                if (h.a >= 0 && h.a < (int)fileSel_.size()) fileSel_[h.a] = !fileSel_[h.a];
                invalidate();
                break;
            case H_FILE_FILTER:
                fileFilter_ = h.a;
                filterFiles();
                scroll_[V_FILES] = 0;
                invalidate();
                break;
            case H_FILES_ALL:
            case H_FILES_NONE:
                for (int i : fileRows_) fileSel_[i] = h.id == H_FILES_ALL;
                invalidate();
                break;
            case H_FILES_RECOVER: askRecoverFiles(); break;
            case H_FILES_BACK: navPush(); setView(V_DISKS); break;
            case H_LOST: askRestoreLost(h.a / 1000, h.a % 1000); break;
            case H_LOST_FULL: startLostScan(h.a, true); break;
            case H_LOST_QUICK: startLostScan(h.a, false); break;
            case H_LOST_CLOSE:
                for (const Disk& d : rep_.m.disks)
                    if (d.number == h.a) lostHidden_.insert(d.id());
                invalidate();
                break;
            case H_LOST_FORGET:
                for (const Disk& d : rep_.m.disks)
                    if (d.number == h.a) {
                        lostMem_.erase(d.id());
                        saveLost();
                    }
                invalidate();
                break;
            case H_CLEAN_HISTORY: cleanHistoryMenu(); break;
            case H_PREVIEW_CANCEL: preview_.on = false; invalidate(); break;
            case H_PREVIEW_MODE: openPreview(preview_.a.part, h.a); break;
            case H_PREVIEW_GO: {
                DiskOpArgs a = preview_.a;
                preview_.on = false;
                dsKind_ = SEL_PART;
                dsPart_ = a.part;
                dsDisk_ = a.disk;
                confirmDiskOp(a);
                break;
            }
            case H_FIX: askFixes(h.a / 100, {h.a % 100}); break;
            case H_FIX_ALL: {
                int i = h.a;
                std::vector<int> all;
                for (int k = 0; k < (int)rep_.installs[i].plan.size(); ++k)
                    if (rep_.installs[i].plan[k].available) all.push_back(k);
                askFixes(i, all);
                break;
            }
            case H_FIX_PC: {
                int f = h.a / 100, k = h.a % 100;
                if (f < (int)rep_.pc.size() && k < (int)rep_.pc[f].fixes.size()) askFixList({rep_.pc[f].fixes[k]}, rep_.pc[f].title);
                break;
            }
            case H_DETAILS: navPush(); sel_ = h.a; scroll_[V_REPORT] = 0; setView(V_REPORT); break;
            case H_CHAIN: navPush(); sel_ = h.a; node_ = -1; setView(V_CHAIN); break;
            case H_BACK: navPush(); sel_ = -1; scroll_[V_REPORT] = 0; invalidate(); break;
            case H_SECUREBOOT: showSecureBoot(); break;
            case H_TOOL_CMD: launchAdmin(false); break;
            case H_TOOL_PS: launchAdmin(true); break;
            case H_TOOL_RESCUE: askRescue(); break;
            case H_CONSOLE_CLOSE:
                consoleOn_ = false;
                settings().setInt(L"console", 0);
                invalidate();
                break;
            case H_CONSOLE_COPY: {
                wstr all;
                {
                    std::lock_guard<std::mutex> l(consoleLock_);
                    for (const wstr& line : console_) all += line + L"\n";
                }
                if (copyText(all)) showToast(L"check", T(L"Console copied"), plural(console_.size(), T(L"line"), T(L"lines")), {}, nullptr);
                break;
            }
            case H_CONSOLE_CLEAR: {
                std::lock_guard<std::mutex> l(consoleLock_);
                console_.clear();
                consoleScroll_ = 0;
                invalidate();
                break;
            }
            case H_LINK_MAIL: openLink(L"mailto:info@muspc.com"); break;
            case H_LINK_WEB: openLink(L"https://www.muspc.com"); break;
            case H_LINK_REVIEW: openLink(L"https://g.page/r/Cd3E4MgII6atEBM/review"); break;
            case H_LINK_SUGGEST: makeSuggestion(); break;
            case H_DOP: askDiskOp(h.a); break;
            case H_SCAN: onCommand(C_SCAN, 0, L""); break;
            case H_UNDO: onCommand(C_UNDO, h.a, L""); break;
            case H_DEL_SESSION: onCommand(C_DELETE_SESSION, h.a, L""); break;
            case H_BACKUP_NOW: onCommand(C_BACKUP_NOW, 0, L""); break;
            case H_CHANGE_FOLDER: onCommand(C_FOLDER, 0, L""); break;
            case H_LESSONS:
                showLessons_ = !showLessons_;
                settings().setInt(L"explanations", showLessons_ ? 1 : 0);
                invalidate();
                break;
        }
    }

    void onWheel(int x, int y, int delta) override {
        if (consoleOn_ && conRect_.contains((float)x, (float)y)) {
            consoleScroll_ = std::max(0, consoleScroll_ + delta * 3 / WHEEL_DELTA);
            invalidate();
            return;
        }
        scrollBy(-delta * S(60) / WHEEL_DELTA);
    }

    void scrollBy(float d) {
        float& s = scroll_[view_];
        s = std::max(0.f, std::min(s + d, std::max(0.f, contentH_[view_] - viewH_)));
        invalidate();
    }

    bool onKey(UINT vk, bool ctrl, bool shift, bool alt) override {
        if (ctrl && shift) {  // capture shortcuts, before the plain-Ctrl ones below
            if (vk == 'S') { takeScreenshot(); return true; }
            if (vk == 'R') { toggleRecording(); return true; }
        }
        if (alt && (vk == VK_LEFT || vk == VK_RIGHT)) {
            if (vk == VK_LEFT) navBack();
            else navForward();
            return true;
        }
        if (vk == VK_BROWSER_BACK) { navBack(); return true; }
        if (vk == VK_BROWSER_FORWARD) { navForward(); return true; }
        if (ctrl) {
            switch (vk) {
                case 'R': onCommand(C_SCAN, 0, L""); return true;
                case 'H': onCommand(C_HOME, 0, L""); return true;
                case 'N': addNote(); return true;
                case 'C':
                case VK_INSERT:
                    if (haveReport_ && copyText(reportWithNotes())) showToast(L"check", T(L"Report copied"), T(L"The whole report is on the clipboard: paste it into an e-mail or a message for help."), {}, nullptr);
                    return true;
                case 'Z': undoRedo(false); return true;
                case 'Y': undoRedo(true); return true;
                case '1': case '2': case '3': case '4': onCommand(C_VIEW, (int)(vk - '1'), L""); return true;
                case 'B': onCommand(C_BACKUP_NOW, 0, L""); return true;
                case 'S': onCommand(C_SAVE_REPORT, 0, L""); return true;
            }
            return false;
        }
        int n = (int)rep_.installs.size();
        switch (vk) {
            case VK_UP:
                if (view_ == V_REPORT || view_ == V_CHAIN) {
                    sel_ = std::max(view_ == V_CHAIN ? 0 : -1, sel_ - 1);
                    node_ = -1;
                    scroll_[view_] = 0;
                } else scrollBy(-S(60));
                break;
            case VK_DOWN:
                if (view_ == V_REPORT || view_ == V_CHAIN) {
                    sel_ = std::min(n - 1, sel_ + 1);
                    node_ = -1;
                    scroll_[view_] = 0;
                } else scrollBy(S(60));
                break;
            case VK_LEFT: if (view_ == V_CHAIN) node_ = std::max(0, node_ - 1); else return false; break;
            case VK_RIGHT:
                if (view_ == V_CHAIN) node_ = std::min((int)chainInstall()->chain.size() - 1, node_ + 1);
                else return false;
                break;
            case VK_APPS:
                if (view_ == V_DISKS && dsKind_ != SEL_NONE) diskMenu(mousePos().x, mousePos().y);
                else return false;
                break;
            case VK_RETURN:
                if (view_ == V_CHAIN) firstFixOfNode();
                else if (view_ == V_REPORT && sel_ < 0 && n) sel_ = 0;
                else return false;
                break;
            case VK_PRIOR: scrollBy(-viewH_ * 0.9f); break;
            case VK_NEXT: scrollBy(viewH_ * 0.9f); break;
            case VK_HOME: scroll_[view_] = 0; break;
            case VK_END: scrollBy(1e6f); break;
            default: return false;
        }
        invalidate();
        return true;
    }

    wstr tipText(const kit::Hit& h) override {
        switch (h.id) {
            case H_SIDE:
                if (h.a < 0) return T(L"Everything found on this PC (↑ / ↓)");
                if (h.a < (int)rep_.installs.size()) return rep_.installs[h.a].label() + L"\n" + rep_.installs[h.a].winDir + L"\n" + rep_.installs[h.a].verdictText;
                break;
            case H_NODE:
            case H_LINK:
                if (const Install* in = view_ == V_CHAIN ? chainInstall() : selected())
                    if (h.a < (int)in->chain.size())
                        return T(in->chain[h.a].name) + L": " + T(healthWord(in->chain[h.a].health)) +
                               (in->chain[h.a].cause.empty() ? wstr() : T(L"\n\nWhat may have caused this: ") + in->chain[h.a].cause) +
                               (in->chain[h.a].lesson.empty() ? wstr() : L"\n\n" + in->chain[h.a].lesson + L"\n") +
                               (h.id == H_NODE ? T(L"\nClick for details, double-click or Enter to carry out its first fix") : T(L"\nClick to see it in the boot chain"));
                break;
            case H_FIX:
            case H_FIX_ALL: {
                int i = h.id == H_FIX ? h.a / 100 : h.a;
                if (i < 0 || i >= (int)rep_.installs.size()) break;
                const Install& in = rep_.installs[i];
                if (h.id == H_FIX_ALL) return T(L"Carry out every step of the plan for ") + in.label() + T(L", one after another. You are asked first.");
                int k = h.a % 100;
                if (k < (int)in.plan.size()) return in.plan[k].what + L"\n" + in.plan[k].takes;
                break;
            }
            case H_FIX_PC: {
                int f = h.a / 100, k = h.a % 100;
                if (f < (int)rep_.pc.size() && k < (int)rep_.pc[f].fixes.size()) return rep_.pc[f].fixes[k].what + L"\n" + rep_.pc[f].fixes[k].takes;
                break;
            }
            case H_DETAILS: return T(L"Everything found about this Windows, and what it takes to make it start");
            case H_CHAIN: return T(L"Show every step this Windows needs to start, as a chain (Ctrl+2)");
            case H_BACK: return T(L"Back to everything found on this PC");
            case H_SCAN: return T(L"Scan all disks again (Ctrl+R)");
            case H_PART:
                if (h.a < (int)rep_.m.parts.size()) {
                    const Partition& p = rep_.m.parts[h.a];
                    wstr t = p.where() + L"\n" + T(roleName(p.role)) + L"\n" + sizeText(p.length);
                    if (!p.fs.empty()) t += L", " + p.fs;
                    if (p.totalBytes) t += L", " + sizeText(p.freeBytes) + T(L" free");
                    if (!p.letter.empty()) t += T(L"\nDrive ") + p.letter;
                    if (!p.label.empty()) t += L" \"" + p.label + L"\"";
                    if (p.active) t += T(L"\nMarked active (legacy start-up)");
                    if (p.bitlocker) t += p.locked ? T(L"\nEncrypted with BitLocker, locked") : T(L"\nEncrypted with BitLocker, unlocked");
                    if (p.dirty) t += T(L"\nMarked as needing a disk check");
                    if (!p.fsUuid.empty() && p.fs.empty()) t += T(L"\nLinux id: ") + p.fsUuid;
                    if (p.pastEnd) t += T(L"\nRuns past the end of the disk");
                    if (p.overlaps) t += T(L"\nOverlaps another partition");
                    return t + T(L"\nClick for what can be done with it, or right-click for a menu");
                }
                break;
            case H_GHOST: return T(L"A Windows on this disk has no start-up partition. Open the report to create one.");
            case H_SECUREBOOT: return T(L"What Secure Boot is, whether it is on, and how to switch it on or off");
            case H_TOOL_CMD: return T(L"Open a command prompt with administrator rights (for typing commands such as diskpart or bcdedit yourself)");
            case H_TOOL_PS: return T(L"Open PowerShell with administrator rights");
            case H_TOOL_RESCUE: return T(L"Make a USB stick that starts PATio Boot Doctor on any PC, even one whose Windows does not start. Everything on the stick is erased.");
            case H_CONSOLE_SPLIT: return T(L"Drag to make the console taller or shorter");
            case H_CONSOLE_CLOSE: return T(L"Close the console (Settings brings it back)");
            case H_CONSOLE_CLEAR: return T(L"Clear the console");
            case H_CONSOLE_COPY: return T(L"Copy everything in the console to the clipboard");
            case H_CONSOLE: return T(L"Every command this program runs, and what it answered. Scroll with the mouse wheel.");
            case H_LINK_MAIL: return T(L"Write an e-mail to info@muspc.com");
            case H_LINK_WEB: return T(L"Open www.muspc.com");
            case H_LINK_REVIEW: return T(L"Open a Google review page for MusPC in your browser");
            case H_LINK_SUGGEST: return T(L"Tell the maker what worked, what didn't, or what you'd like — saved on this PC first, sent only if you choose");
            case H_GAP: return T(L"Unused space: click to see what can be done with it (create a partition), or right-click for a menu");
            case H_DISKHEAD: return T(L"Click for what can be done with the whole disk (set up, convert, wipe, online / offline)");
            case H_DOP: return diskOpInfo(h.a).what;
            case H_LESSONS: return showLessons_ ? T(L"Hide the short explanations of what each thing is") : T(L"Show short explanations of what each thing is, for people new to this");
            case H_UNDO: return T(L"Put back what this change altered. What is there now is backed up first, so the undo can be undone too.");
            case H_DEL_SESSION: return T(L"Delete this session's backups from the disk. This cannot be undone.");
            case H_ACTION: {
                int s = h.a / 1000, a = h.a % 1000;
                if (s < (int)sessions_.size() && a < (int)sessions_[s].actions.size()) {
                    const ActionInfo& ai = sessions_[s].actions[a];
                    return ai.title + L"\n" + ai.time + L"\n" + ai.target + (ai.result.empty() ? L"" : L"\n" + ai.result) +
                           (ai.canUndo ? L"" : T(L"\nThis change cannot be undone."));
                }
                break;
            }
            case H_BACKUP_NOW: return T(L"Save the start-up settings and partition tables of all disks now (Ctrl+B)");
            case H_CHANGE_FOLDER: return T(L"Choose another folder for backups. Use a drive that is not being repaired.");
            case H_SCROLL: return T(L"Scroll with the mouse wheel or Page up / Page down");
            case H_NAV: return navTip(h.a);
            case H_LAYOUT_GO: return T(L"Carry out this step. You are asked once more, with what it does and how long it takes.");
            case H_LAYOUT_SCHEME: return h.a ? T(L"For PCs that start in UEFI mode (GPT disk): start-up, reserved, Windows, recovery") : T(L"For older PCs that start in legacy BIOS mode (MBR disk): System Reserved, Windows, recovery");
            case H_SYS: {
                static const wchar_t* tips[] = {N(L"Everything found about this system"), N(L"Make it the one that starts when nobody chooses"), N(L"Show it in the start-up menu"),
                                                N(L"Take it off the start-up menu (it stays installed)"), N(L"Add it to the start-up menu"), N(L"Change its name in the start-up menu"),
                                                N(L"Put this earlier Windows back in place of the current one")};
                int k = h.a % 100;
                return k >= 0 && k < 7 ? T(tips[k]) : wstr();
            }
            case H_KEY: {
                static const wchar_t* t[] = {N(L"Show the whole key (to read it out or copy it)"), N(L"Give it the name of its owner or PC"), N(L"Move it to the archive (kept, listed apart) or back"), N(L"Delete this key from the saved keys")};
                int k = h.a % 10;
                return k >= 0 && k < 4 ? T(t[k]) : wstr();
            }
            case H_KEYS_TOP: {
                static const wchar_t* t[] = {N(L"Give the password of the saved keys to see them"), N(L"Read the keys of this PC's unlocked BitLocker drives and save them"), N(L"Copy the file of saved keys (still locked with its password) to another drive"), N(L"Change the password of the saved keys"), N(L"Delete every saved key")};
                return h.a >= 0 && h.a < 5 ? T(t[h.a]) : wstr();
            }
            case H_UPDATE: return T(L"Look for a newer version of this program");
            case H_REP_SRC: return T(L"Repair this Windows");
            case H_REP_TOOL:
                if (h.a >= 0 && h.a < (int)repTools_.size()) return repTools_[h.a].what + L"\n" + repTools_[h.a].takes;
                break;
            case H_REP_TAB: return T(L"Show this page of Windows Repair");
            case H_TAB: return h.a == -1 ? T(L"Make a one-page job sheet (PDF and TXT) with what was found, what was done and your notes") : T(L"Show this page");
            case H_GUIDE: return T(L"What usually causes this, and the tool for it");
            case H_ADD_DRIVER: return h.a == 0 ? T(L"Put the driver that was just loaded into this Windows, so it finds its disk when it starts") : T(L"Choose a driver (.inf) and put it into this Windows");
            case H_REP_OPT: return (h.a % 2) ? T(L"Switch this on for the next start") : T(L"Switch this off again");
            case H_REP_DRV: return h.a < 0 ? T(L"Read which drivers start with this Windows") : T(L"Stop Windows from loading this driver (Undo switches it on again)");
            case H_REP_LOG: return T(L"Read the last lines of this log");
            case H_REP_LOGACT: return h.a == 0 ? T(L"Copy the lines shown to the clipboard") : T(L"Open the whole log in Notepad");
            case H_REP_HIVE: return h.a < 0 ? T(L"Open Windows' registry editor") : T(L"Load this registry file into the registry editor, or take it out again");
            case H_REP_RESTORE: return T(L"Open Windows' System Restore for this Windows");
            case H_REP_ACCT:
                return h.a < 0 ? T(L"Read the local accounts of this Windows")
                               : (h.a % 8) == 1 ? T(L"Switch this account on") : (h.a % 8) == 2 ? T(L"Clear the lock set after wrong passwords") : T(L"Make this account sign in with no password");
            case H_STICK_NAME: return T(L"Give this rescue stick another name");
            case H_JOB: return h.a == 0 ? T(L"Write down how the PC was handed in and start its action log") : h.a == 1 ? T(L"Open the job's action log") : T(L"Close the job; its log is kept");
            case H_STATS: return h.a == 0 ? T(L"Copy the usage statistics to the clipboard") : T(L"Start counting again from zero");
            case H_HW: return h.a == -1 ? T(L"Collect this PC's hardware details now") : h.a == -2 ? T(L"Open the folder with every collection") : T(L"Open this collection");
            case H_BADMAP: return T(L"Find the files that sit on the unreadable areas, and whether Windows needs them to start");
            case H_BOOTSTAGE: return h.a == 0 ? T(L"Copy every sector of this Windows' disk to an image file on another drive") : h.a == 1 ? T(L"Check the disk controller's mode and this Windows' storage drivers") : h.a == 2 ? T(L"Open the start-up options to switch on boot logging") : T(L"Read Windows' own logs");
            case H_STORAGE: return h.a == 0 ? T(L"Look at the disk controller and this Windows' storage drivers") : h.a == 1 ? T(L"Turn on the drivers this PC's disk controller needs") : h.a == 2 ? T(L"Turn on every built-in storage driver, so Windows starts whatever the firmware's SATA mode is") : T(L"Get Windows ready before the SATA mode is changed in the firmware");
            case H_HEALTH: return h.a % 2 == 0 ? T(L"Start PATio Commander to copy single files to a removable drive") : T(L"Copy every sector of this disk to an image file on another drive");
            case H_REP_MEM: return h.a == -2 ? T(L"Open the Windows Memory Diagnostic (it runs after a restart)") : T(L"Read the last memory-test result from the event log");
            case H_LOAD_DRIVER: return T(L"Look on the removable disks for a storage driver (.inf) and load it, so a disk that is not seen appears");
            case H_JOBSHEET: return T(L"Make a one-page job sheet with the MusPC logo: what was found, what was done, your notes. Saved as PDF and TXT in PATioAppData\\PATioBootDoctorData.");
            case H_REP_RESCUE: {
                static const wchar_t* t[] = {N(L"Copy a whole disk, sector by sector, to an image file on another drive, before trying to rescue anything from it"),
                                             N(L"Search a drive for the old Windows' registry files, password keys, mail and browser profiles, and the user folders"),
                                             N(L"Show only the Windows essentials among the files found by the last search"),
                                             N(L"Attach a disk image read-only, so it can be searched like a disk")};
                return h.a >= 0 && h.a < 4 ? T(t[h.a]) : wstr();
            }
            case H_NOTE: return (h.a % 10) == 0 ? T(L"Change this note") : T(L"Delete this note");
            case H_NOTES_TOP: return h.a == 0 ? T(L"Write a new note (Ctrl+N)") : T(L"Copy all notes to the clipboard");
            case H_MIG_OPEN: return T(L"Copy a Windows, with its start-up and recovery partitions, to another disk (for example a new SSD)");
            case H_MIG_SRC: return T(L"Move this Windows");
            case H_MIG_DST: return T(L"Move it to this disk (everything on it is erased)");
            case H_MIG_SCHEME: return h.a ? T(L"For PCs that start in UEFI mode: the new disk becomes GPT") : T(L"For older PCs that start in legacy BIOS mode: the new disk becomes MBR (up to 2 TB)");
            case H_MIG_GO: return T(L"Start the move. You are asked to confirm with the new disk's number first.");
            case H_SYS_TIMEOUT: return T(L"How many seconds the start-up menu waits before the first system starts");
            case H_FILE:
                if (h.a >= 0 && h.a < (int)fileScan_.files.size()) {
                    const FoundFile& f = fileScan_.files[h.a];
                    return f.folder + L"\\" + f.name + L"\n" + sizeText(f.size) + L" · " + fileStateText(f.state) + T(L"\nClick to choose it (or not) for recovering");
                }
                break;
            case H_FILE_FILTER: return T(L"Show only this kind of file");
            case H_FILES_ALL: return T(L"Choose every file shown");
            case H_FILES_NONE: return T(L"Choose none of the files shown");
            case H_FILES_RECOVER: return T(L"Copy the chosen files to a folder on another drive");
            case H_FILES_BACK: return T(L"Back to Disks and partitions");
            case H_LOST: return T(L"Put this partition back in the partition table. Its contents are not touched; Undo removes it again.");
            case H_LOST_FULL: return T(L"Read every sector of the unused space. This can take hours on a big disk.");
            case H_LOST_CLOSE: return T(L"Hide this list until the next start (it is remembered)");
            case H_LOST_QUICK: return T(L"Look again where partitions normally start (minutes)");
            case H_LOST_FORGET: return T(L"Forget what was found on this disk");
            case H_CLEAN_HISTORY: return T(L"Delete old sessions and their backups, to free space");
            case H_PREVIEW_GO: return T(L"Go on: you are asked once more before anything changes");
            case H_PREVIEW_CANCEL: return T(L"Close the preview; nothing changes");
            case H_PREVIEW_MODE: return h.a == 0 ? T(L"Use only the unused space right after it") : T(L"Delete the partition right after it and use its space");
        }
        return L"";
    }

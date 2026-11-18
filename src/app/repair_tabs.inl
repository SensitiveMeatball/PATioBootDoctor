// Part of class DoctorApp (included inside the class in main.cpp).
    // ---------------------------------------------------------------- Windows Repair: its pages
    // 0 repair tools, 1 start-up options, 2 drivers, 3 logs, 4 registry, 5 accounts, 6 rescue
    float drawRepairTabs(float x, float y, float w) {
        static const wchar_t* names[] = {N(L"Repair tools"), N(L"Start-up options"), N(L"Drivers"), N(L"Logs"), N(L"Registry"), N(L"Accounts"), N(L"Rescue an old Windows")};
        float bx = x, y0 = y;
        for (int k = 0; k < 7; ++k) {
            wstr t = T(names[k]);
            float bw = buttonWidth(t);
            if (bx + bw > x + w && bx > x) {
                bx = x;
                y += S(32);
            }
            drawButton(RectF(bx, y, bw, S(28)), t, H_REP_TAB, k, repTab_ == k ? kit::ButtonStyle::Primary : kit::ButtonStyle::Quiet);
            bx += bw + S(6);
        }
        return y + S(40) - y0;
    }

    // A card with a title, a text and buttons at the right; returns its height.
    float toolCard(float x, float y, float w, const wstr& title, const wstr& what, const wstr& note, std::vector<std::pair<wstr, int>> buttons, int hit, bool danger = false) {
        float bwAll = 0;
        for (auto& b : buttons) bwAll += buttonWidth(b.first) + S(6);
        auto body = [&](bool draw) {
            float pad = techMode_ ? S(8) : S(12), cx = x + pad, cw = w - pad * 2 - bwAll - S(10), yy = y + pad;
            if (draw) text(title, RectF(cx, yy, cw, S(22)), fonts().uiSemi, theme().label);
            yy += S(24);
            if (!techMode_ && !what.empty()) yy += para(what, cx, yy, cw, fonts().small, theme().label2, draw) + S(2);
            if (!note.empty()) yy += para(note, cx, yy, cw, fonts().small, kAmber, draw) + S(2);
            if (draw) {
                float bx = x + w - pad - bwAll + S(6);
                for (auto& b : buttons) {
                    float bw = buttonWidth(b.first);
                    drawButton(RectF(bx, y + pad, bw, S(26)), b.first, hit, b.second, danger ? kit::ButtonStyle::Danger : kit::ButtonStyle::Normal);
                    bx += bw + S(6);
                }
            }
            return std::max(yy, y + pad + S(28)) + pad - y;
        };
        float h = body(false);
        card(RectF(x, y, w, h));
        body(true);
        return h + S(6);
    }

    // ---- 1: start-up options
    struct BootOpt {
        const wchar_t* title;
        const wchar_t* what;
        const wchar_t* on;
        const wchar_t* off;
    };
    static const BootOpt* bootOpts(int& n) {
        static const BootOpt k[] = {
            {N(L"Safe Mode"), N(L"Starts with only the most basic drivers and services, so a faulty driver or program can be removed."), L"/set {id} safeboot minimal", L"/deletevalue {id} safeboot"},
            {N(L"Safe Mode with networking"), N(L"Safe Mode plus network drivers, to download a driver or a tool."), L"/set {id} safeboot network", L"/deletevalue {id} safeboot"},
            {N(L"Safe Mode with command prompt"), N(L"Safe Mode that opens a command prompt instead of the desktop."), L"/set {id} safeboot minimal\n/set {id} safebootalternateshell yes", L"/deletevalue {id} safeboot"},
            {N(L"Boot logging"), N(L"Writes every driver Windows loads to Windows\\ntbtlog.txt. Read it under Logs: the driver after the last loaded one is the suspect."), L"/set {id} bootlog yes", L"/deletevalue {id} bootlog"},
            {N(L"Show drivers while starting"), N(L"Shows the names of the drivers on screen as they load, so you see where starting stops."), L"/set {id} sos on", L"/deletevalue {id} sos"},
            {N(L"Driver signature enforcement off"), N(L"Lets Windows load drivers without a valid signature. Only works with Secure Boot off. For testing only: switch it back on afterwards."), L"/set {id} nointegritychecks on", L"/deletevalue {id} nointegritychecks"},
            {N(L"Early-launch anti-malware off"), N(L"Stops the antivirus driver that starts before all others. Use it when that driver itself stops Windows from starting."), L"/set {id} disableelamdrivers yes", L"/deletevalue {id} disableelamdrivers"},
            {N(L"Low-resolution video"), N(L"Starts with the basic display driver, when the graphics driver gives a black screen."), L"/set {id} vga on", L"/deletevalue {id} vga"},
            {N(L"Classic F8 menu"), N(L"Brings back the old menu: press F8 while the PC starts to choose Safe Mode and the other options yourself."), L"/set {id} bootmenupolicy legacy", L"/set {id} bootmenupolicy standard"},
            {N(L"Automatic Repair"), N(L"Windows' own start-up repair. Switch it off to see the real error instead of the repair loop."), L"/set {id} recoveryenabled yes", L"/set {id} recoveryenabled no"},
        };
        n = (int)(sizeof k / sizeof k[0]);
        return k;
    }

    float drawBootOptions(float x, float y, float w) {
        float top = y;
        const Install& in = rep_.installs[repInstall_];
        if (in.entryId.empty() || in.store < 0) {
            y += para(T(L"This Windows has no start-up entry, so there is nothing to switch. Rebuild its start-up files first (Repair tools)."), x + S(4), y, w - S(8), fonts().ui, kAmber, true) + S(10);
            return y - top;
        }
        y += para(T(L"These change how this Windows starts from the next start on, until switched back. They are stored in its start-up entry; Undo (Ctrl+Z) puts the old setting back."), x + S(4), y, w - S(8),
                  fonts().small, theme().label2, true) + S(8);
        int n = 0;
        const BootOpt* k = bootOpts(n);
        for (int i = 0; i < n; ++i) y += toolCard(x, y, w, T(k[i].title), T(k[i].what), L"", {{T(L"On"), i * 2 + 1}, {T(L"Off"), i * 2}}, H_REP_OPT);
        return y - top;
    }

    void bootOption(int a) {
        int n = 0;
        const BootOpt* k = bootOpts(n);
        int i = a / 2;
        bool on = a % 2 != 0;
        if (i < 0 || i >= n) return;
        Fix f = repairFix(FX_BOOT_OPTION);
        f.arg = on ? k[i].on : k[i].off;
        f.arg2 = TF(on ? L"\"{0}\" is on for this Windows from the next start." : L"\"{0}\" is off for this Windows.", T(k[i].title));
        f.title = wstr(T(k[i].title)) + L": " + (on ? T(L"switch on") : T(L"switch off"));
        f.takes = T(k[i].what);
        checkOne(f);
        askFixList({f}, rep_.installs[repInstall_].label());
    }

    // ---- 2: drivers that start with Windows
    void listDrivers() {
        if (busy_ || repInstall_ < 0) return;
        const Install in = rep_.installs[repInstall_];
        const Partition P = rep_.m.parts[in.part];
        auto list = std::make_shared<std::vector<BootDriver>>();
        auto err = std::make_shared<wstr>();
        runJob(T(L"Reading the drivers that start with Windows"), in.label(), false, [in, P, list, err] { *list = listBootDrivers(in, P, *err); },
               [this, list, err] {
                   bootDrivers_ = *list;
                   driversListed_ = true;
                   driversFor_ = repInstall_;
                   if (!err->empty()) showMessage(T(L"The drivers could not be read"), *err);
               });
    }

    // ---- the disk controller and this Windows' storage drivers (top of Drivers)
    void checkStorageDrivers() {
        if (busy_ || repInstall_ < 0) return;
        if (repTab_ != 2) repairTab(2);
        const Install in = rep_.installs[repInstall_];
        const Partition P = rep_.m.parts[in.part];
        const Disk* wd = windowsDisk();
        wstr bus = wd ? wd->bus : wstr();
        auto res = std::make_shared<StorageCheck>();
        countUse(L"Tools: storage driver checks");
        runJob(T(L"Checking the disk controller and its drivers"), in.label(), false, [in, P, bus, res] { *res = checkStorage(in, P, bus); },
               [this, res] {
                   storage_ = *res;
                   storageChecked_ = true;
                   storageFor_ = repInstall_;
                   if (!storage_.ok) showMessage(T(L"The storage drivers could not be read"), storage_.err);
               });
    }

    static wstr startText(DWORD s) {
        switch (s) {
            case 0: return T(L"starts with Windows (boot)");
            case 1: return T(L"starts early (system)");
            case 2: return T(L"automatic");
            case 3: return T(L"only when needed");
            case 4: return T(L"switched off");
            default: return T(L"not set");
        }
    }

    float drawStorageCard(float x, float y, float w) {
        float top = y;
        drawSectionHeader(RectF(x + S(4), y, w, S(24)), T(L"Disk controller and its driver"));
        y += S(30);
        bool have = storageChecked_ && storageFor_ == repInstall_ && storage_.ok;
        const StorageCheck& c = storage_;
        std::vector<std::pair<wstr, int>> btns;
        if (!have) btns.push_back({T(L"Check the storage drivers"), 0});
        else {
            if (!c.turnOn.empty() || !c.cddb.empty()) btns.push_back({T(L"Turn on the right drivers…"), 1});
            if (!allInboxStorageDrivers(c).empty()) btns.push_back({T(L"Start in any SATA mode…"), 2});
            btns.push_back({T(L"Prepare for a mode change…"), 3});
            btns.push_back({T(L"Check again"), 0});
        }
        auto body = [&](bool draw) {
            float pad = S(12), cx = x + pad, cw = w - pad * 2, yy = y + pad;
            if (!have) {
                yy += para(T(L"Checks which mode this PC's disk controller runs in (IDE, AHCI, RAID, NVMe, Intel VMD) and whether this Windows has — and starts — the driver that mode needs. A missing or switched-off driver stops Windows with INACCESSIBLE_BOOT_DEVICE even when its start-up files are fine."),
                           cx, yy, cw, fonts().small, theme().label2, draw) + S(6);
            } else {
                if (c.controllers.empty())
                    yy += para(T(L"No disk controller could be read on this system."), cx, yy, cw, fonts().small, theme().label2, draw) + S(4);
                for (const StorageController& k : c.controllers) {
                    wstr line = (k.name.empty() ? k.venDev : k.name) + L"  ·  " + TF(L"{0} mode", wstr(storageModeName(k.mode))) + (k.venDev.empty() ? wstr() : L"  ·  " + k.venDev) + (k.liveService.empty() ? wstr() : L"  ·  " + TF(L"driver now: {0}", k.liveService));
                    yy += para(line, cx, yy, cw, fonts().smallSemi, theme().label, draw) + S(2);
                }
                wstr setUp;
                for (int m : c.setUpFor) setUp += (setUp.empty() ? L"" : L", ") + wstr(storageModeName(m));
                yy += para(TF(L"This Windows was set up for: {0}", setUp.empty() ? wstr(T(L"none of them (no controller driver starts with it)")) : setUp), cx, yy, cw, fonts().small, theme().label2, draw) + S(8);
                Color vc = c.needOn ? kGreen : c.needPresent ? kAmber : kRed;
                yy += para(c.verdict, cx, yy, cw, fonts().uiSemi, vc, draw) + S(4);
                yy += para(c.advice, cx, yy, cw, fonts().small, theme().label2, draw) + S(8);
                for (const DriverState& d : c.drivers) {
                    if (!d.present || d.mode == SM_CORE) continue;
                    if (draw) {
                        text(d.service + L"  ·  " + wstr(storageModeName(d.mode)) + (d.mode2 != SM_UNKNOWN ? L"/" + wstr(storageModeName(d.mode2)) : wstr()), RectF(cx, yy, cw * 0.32f, S(20)), fonts().small, theme().label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                        text(startText(d.start) + (d.fileThere ? wstr() : L"  ·  " + wstr(T(L"its file is missing"))), RectF(cx + cw * 0.32f, yy, cw * 0.68f, S(20)), fonts().small,
                             d.start == 0 ? kGreen : theme().label2, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                    }
                    yy += S(20);
                }
            }
            yy += S(8);
            if (draw) {
                float bx = cx;
                for (auto& bt : btns) {
                    float bw = buttonWidth(bt.first);
                    if (bx + bw > x + w - pad && bx > cx) {
                        bx = cx;
                        yy += S(32);
                    }
                    drawButton(RectF(bx, yy, bw, S(26)), bt.first, H_STORAGE, bt.second, bt.second == 1 ? kit::ButtonStyle::Primary : kit::ButtonStyle::Normal);
                    bx += bw + S(6);
                }
            } else {
                float bx = cx;
                for (auto& bt : btns) {
                    float bw = buttonWidth(bt.first);
                    if (bx + bw > x + w - pad && bx > cx) {
                        bx = cx;
                        yy += S(32);
                    }
                    bx += bw + S(6);
                }
            }
            yy += S(26);
            return yy + pad - y;
        };
        float bh = body(false);
        card(RectF(x, y, w, bh));
        body(true);
        y += bh + S(14);
        return y - top;
    }

    void storageFix(const std::vector<wstr>& services, const std::vector<wstr>& cddb, const wstr& title, const wstr& takes) {
        if (repInstall_ < 0) return;
        Fix f = repairFix(FX_ENABLE_DRIVERS);
        wstr a, b;
        for (const wstr& s : services) a += (a.empty() ? L"" : L",") + s;
        for (const wstr& e : cddb) b += (b.empty() ? L"" : L";") + e;
        f.arg = a.empty() ? wstr(L"disk") : a;  // named, so the fix does exactly this
        f.arg2 = b;
        f.title = title;
        f.takes = takes;
        checkOne(f);
        storageChecked_ = false;  // read again after the change
        askFixList({f}, rep_.installs[repInstall_].label());
    }

    void storageAction(int a) {
        if (repInstall_ < 0) return;
        const StorageCheck& c = storage_;
        switch (a) {
            case 0: checkStorageDrivers(); break;
            case 1: {
                wstr list;
                for (const wstr& s : c.turnOn) list += (list.empty() ? L"" : L", ") + s;
                storageFix(c.turnOn, c.cddb, T(L"Turn on the storage drivers this PC needs"),
                           TF(L"Sets {0} to start with Windows", list.empty() ? wstr(L"-") : list) + (c.cddb.empty() ? wstr(L".") : T(L", and lists this PC's disk controller in Windows 7's critical-device database.")));
                break;
            }
            case 2: {
                std::vector<wstr> all = allInboxStorageDrivers(c);
                for (const wstr& s : c.turnOn) all.push_back(s);
                wstr list;
                for (const wstr& s : all) list += (list.empty() ? L"" : L", ") + s;
                storageFix(all, c.cddb, T(L"Start in any SATA mode"),
                           TF(L"Sets every built-in storage driver of this Windows to start with it ({0}), so it starts whether the firmware runs the disk controller in IDE, AHCI, RAID or NVMe mode. Drivers for hardware that is not there are skipped by Windows.", list));
                break;
            }
            case 3: {
                std::vector<kit::MenuItem> items;
                kit::MenuItem h;
                h.header = true;
                h.text = T(L"Get this Windows ready for the firmware's SATA mode:");
                items.push_back(h);
                for (int m : {SM_AHCI, SM_RAID, SM_IDE}) {
                    kit::MenuItem it;
                    it.text = storageModeName(m);
                    it.hint = m == c.mode ? T(L"now") : wstr();
                    it.cmd = C_PREP_MODE;
                    it.arg = m;
                    items.push_back(it);
                }
                POINT p = mousePos();
                showMenu(items, std::max(S(60), (float)p.x - S(200)), (float)p.y);
                break;
            }
        }
    }

    void prepareModeChange(int mode) {
        if (repInstall_ < 0 || !storageChecked_) return;
        StorageCheck c = storage_;
        judgeStorage(c, mode);
        const wchar_t* name = storageModeName(mode);
        if (c.turnOn.empty() && c.needPresent) {
            showMessage(TF(L"Ready for {0}", wstr(name)), TF(L"This Windows already starts its {0} driver. Change the mode in the firmware settings; if Windows does not start, set it back.", wstr(name)));
            return;
        }
        if (!c.needPresent) {
            showMessage(TF(L"Not ready for {0}", wstr(name)), c.verdict + L"\n\n" + c.advice + L"\n\n" + T(L"Do not change the mode in the firmware settings until it is: Windows would stop with INACCESSIBLE_BOOT_DEVICE."));
            return;
        }
        wstr list;
        for (const wstr& s : c.turnOn) list += (list.empty() ? L"" : L", ") + s;
        storageFix(c.turnOn, {}, TF(L"Get Windows ready for {0} mode", wstr(name)),
                   TF(L"Sets {0} to start with Windows. Then change the SATA mode to {1} in the firmware settings; Windows starts in the new mode. If it does not, set the mode back.", list, wstr(name)));
    }

    float drawDrivers(float x, float y, float w) {
        float top = y;
        y += drawStorageCard(x, y, w);
        y += para(T(L"Drivers and file-system filters that load while Windows starts. Those of other makers (antivirus, encryption, backup, storage, virtual drives) are the usual cause of crashes at start-up after an install or an update. Switching one off keeps its files; Undo switches it on again."),
                  x + S(4), y, w - S(8), fonts().small, theme().label2, true) + S(8);
        float bw = buttonWidth(T(L"Read the drivers"));
        drawButton(RectF(x, y, bw, S(28)), T(L"Read the drivers"), H_REP_DRV, -1, driversListed_ && driversFor_ == repInstall_ ? kit::ButtonStyle::Normal : kit::ButtonStyle::Primary);
        {
            // a driver this Windows lacks (its disk controller's): put it in
            float bx = x + bw + S(8);
            if (!lastDriverInf_.empty()) {
                wstr t = TF(L"Add the loaded driver ({0}) to this Windows…", lastDriverInf_.substr(lastDriverInf_.find_last_of(L'\\') + 1));
                float w2 = buttonWidth(t);
                drawButton(RectF(bx, y, w2, S(28)), t, H_ADD_DRIVER, 0, kit::ButtonStyle::Primary);
                bx += w2 + S(8);
            }
            float w3 = buttonWidth(T(L"Add a driver (.inf) to this Windows…"));
            if (bx + w3 > x + w) {
                bx = x;
                y += S(34);
            }
            drawButton(RectF(bx, y, w3, S(28)), T(L"Add a driver (.inf) to this Windows…"), H_ADD_DRIVER, 1, kit::ButtonStyle::Normal);
        }
        y += S(40);
        if (!driversListed_ || driversFor_ != repInstall_) return y - top;
        int ms = 0, others = 0;
        for (int i = 0; i < (int)bootDrivers_.size(); ++i) {
            const BootDriver& d = bootDrivers_[i];
            if (d.microsoft && !d.missing) {
                ++ms;
                continue;
            }
            ++others;
            wstr title = d.name + (d.display.empty() ? wstr() : L" · " + d.display);
            wstr what = (d.company.empty() ? wstr(T(L"Maker unknown")) : d.company) + L" · " + (d.start == 0 ? T(L"loads first (boot)") : T(L"loads with the system")) + L" · " + d.file;
            y += toolCard(x, y, w, title, what, d.missing ? T(L"Its file is missing: Windows may stop on it. Switching it off is safe.") : wstr(), {{T(L"Switch off…"), i}}, H_REP_DRV, true);
        }
        if (!others) y += para(T(L"Only Microsoft's own drivers start with this Windows."), x + S(4), y, w, fonts().ui, kGreen, true) + S(8);
        y += para(TF(L"{0} Microsoft drivers are not listed.", std::to_wstring(ms)), x + S(4), y, w, fonts().small, theme().label3, true) + S(8);
        return y - top;
    }

    void driverOff(int i) {
        if (i < 0) {
            listDrivers();
            return;
        }
        if (i >= (int)bootDrivers_.size()) return;
        Fix f = repairFix(FX_DRIVER_START);
        f.arg = bootDrivers_[i].name;
        f.arg2 = L"4";
        f.title = TF(L"Switch the driver {0} off", bootDrivers_[i].name);
        checkOne(f);
        driversListed_ = false;
        askFixList({f}, rep_.installs[repInstall_].label());
    }

    // ---- 3: logs
    void openLog(int i) {
        if (busy_ || i < 0 || i >= (int)logs_.size()) return;
        LogFile f = logs_[i];
        auto lines = std::make_shared<std::vector<wstr>>();
        auto err = std::make_shared<wstr>();
        runJob(T(L"Reading the log"), f.path, false, [f, lines, err] { *lines = readLogTail(f, 600, *err); },
               [this, lines, err, i] {
                   logLines_ = *lines;
                   logSel_ = i;
                   logErr_ = *err;
               });
    }

    float drawLogs(float x, float y, float w) {
        float top = y;
        if (logs_.empty()) {
            y += para(T(L"None of the logs Windows writes about start-up trouble were found for this Windows."), x + S(4), y, w, fonts().ui, theme().label3, true) + S(10);
            return y - top;
        }
        for (int i = 0; i < (int)logs_.size(); ++i) {
            RectF rr(x, y, w, S(30));
            bool sel = i == logSel_;
            drawRow(rr, sel, true, hovered(H_REP_LOG, i), sel, false, false);
            addHit(rr, H_REP_LOG, i);
            text(logs_[i].title, RectF(x + S(10), y, w * 0.45f, S(30)), fonts().uiSemi, rowText(sel, true, false));
            text(logs_[i].path, RectF(x + S(16) + w * 0.45f, y, w * 0.55f - S(24), S(30)), fonts().small, rowText(sel, true, true), DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_PATH_ELLIPSIS);
            y += S(32);
        }
        y += S(8);
        if (logSel_ < 0 || logSel_ >= (int)logs_.size()) {
            y += para(T(L"Click a log to read its last lines."), x + S(4), y, w, fonts().small, theme().label3, true);
            return y - top + S(10);
        }
        if (!techMode_) y += para(logs_[logSel_].what, x + S(4), y, w - S(8), fonts().small, theme().label2, true) + S(6);
        float bx = x;
        auto b = [&](int a, const wstr& t) {
            float bw = buttonWidth(t);
            drawButton(RectF(bx, y, bw, S(26)), t, H_REP_LOGACT, a, kit::ButtonStyle::Normal);
            bx += bw + S(6);
        };
        b(0, T(L"Copy"));
        b(1, T(L"Open in Notepad"));
        text(TF(L"The last {0} lines", std::to_wstring(logLines_.size())), RectF(bx + S(8), y, w - (bx - x) - S(8), S(26)), fonts().small, theme().label3);
        y += S(34);
        if (!logErr_.empty()) y += para(logErr_, x + S(4), y, w - S(8), fonts().ui, kAmber, true) + S(8);
        float lh = S(16);
        int n = (int)logLines_.size();
        canvas().round(RectF(x, y, w, n * lh + S(12)), S(6), theme().field);
        int first = std::max(0, (int)((viewTop_ - y) / lh) - 1), last = std::min(n, (int)((viewBottom_ - y) / lh) + 2);
        for (int k = first; k < last; ++k) {
            const wstr& l = logLines_[k];
            bool bad = icontains(l, L"error") || icontains(l, L"fail") || icontains(l, L"corrupt") || icontains(l, L"Root cause");
            text(l, RectF(x + S(8), y + S(6) + k * lh, w - S(16), lh), fonts().monoSmall, bad ? kRed : theme().label2);
        }
        y += n * lh + S(12);
        return y - top + S(10);
    }

    void logAction(int a) {
        if (logSel_ < 0 || logSel_ >= (int)logs_.size()) return;
        if (a == 0) {
            wstr all;
            for (const wstr& l : logLines_) all += l + L"\n";
            if (copyText(all)) showToast(L"check", T(L"Log copied"), logs_[logSel_].title, {}, nullptr);
            return;
        }
        wstr np = findSystemTool(L"notepad.exe");
        if (np.empty() || logs_[logSel_].eventLog) {
            showMessage(T(L"Not possible here"), logs_[logSel_].eventLog ? T(L"Event logs are not text files. Use Copy and paste the text where you need it.") : T(L"Notepad is not part of this system. Use Copy."));
            return;
        }
        wstr cmd = L"\"" + np + L"\" \"" + logs_[logSel_].path + L"\"";
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(0);
        STARTUPINFOW si{sizeof si};
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }

    // ---- 4: the registry, opened in the registry editor
    float drawRegistry(float x, float y, float w) {
        float top = y;
        const Install& in = rep_.installs[repInstall_];
        if (in.running) {
            y += para(T(L"This Windows is running: its registry is the one the registry editor shows."), x + S(4), y, w - S(8), fonts().ui, theme().label2, true) + S(8);
            float bw = buttonWidth(T(L"Open the registry editor"));
            drawButton(RectF(x, y, bw, S(28)), T(L"Open the registry editor"), H_REP_HIVE, -1, kit::ButtonStyle::Primary);
            return y + S(40) - top;
        }
        y += para(T(L"Loads the registry files of this Windows into the registry editor, to look at or change them by hand. Each file appears under HKEY_LOCAL_MACHINE with the name shown. A copy of the file is saved first (History can put it back). Close the registry editor and unload the files before scanning or repairing again."),
                  x + S(4), y, w - S(8), fonts().small, theme().label2, true) + S(8);
        bool any = false;
        for (int i = 0; i < (int)hives_.size(); ++i) {
            const HiveMount& m = hives_[i];
            any |= m.loaded;
            y += toolCard(x, y, w, m.name + (m.loaded ? L" · " + m.key : wstr()), m.file, L"", {{m.loaded ? T(L"Unload") : T(L"Load"), i}}, H_REP_HIVE);
        }
        float bw = buttonWidth(T(L"Open the registry editor"));
        drawButton(RectF(x, y + S(4), bw, S(28)), T(L"Open the registry editor"), H_REP_HIVE, -1, any ? kit::ButtonStyle::Primary : kit::ButtonStyle::Normal);
        return y + S(44) - top;
    }

    void hiveAction(int i) {
        if (i < 0) {
            wstr re = findSystemTool(L"regedit.exe");
            if (re.empty()) {
                wchar_t win[MAX_PATH] = {};
                GetWindowsDirectoryW(win, MAX_PATH);
                re = wstr(win) + L"\\regedit.exe";
            }
            wstr cmd = L"\"" + re + L"\"";
            std::vector<wchar_t> buf(cmd.begin(), cmd.end());
            buf.push_back(0);
            STARTUPINFOW si{sizeof si};
            PROCESS_INFORMATION pi{};
            if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            } else showMessage(T(L"The registry editor could not be opened"), errorText(GetLastError()));
            return;
        }
        if (i >= (int)hives_.size() || busy_) return;
        HiveMount& m = hives_[i];
        wstr err;
        if (m.loaded) {
            if (!unloadHive(m, err)) showMessage(T(L"Not unloaded"), err);
            invalidate();
            return;
        }
        // a copy first, so a slip of the hand can be undone from History
        const Install& in = rep_.installs[repInstall_];
        const Partition& P = rep_.m.parts[in.part];
        if (journal_.isOpen() && istartsWith(m.file, P.root)) {
            wstr act = journal_.begin(TF(L"Registry file {0} opened for editing by hand", m.name), in.label());
            wstr rel = m.file.substr(P.root.size()), e2;
            bool saved = journal_.saveFile(act, P, rel, e2);
            journal_.end(act, saved, saved ? T(L"A copy from before the editing is kept. Undo puts it back (unload the file first).") : e2, L"", saved);
            if (saved) {
                undo_.push_back(act);
                redo_.clear();
            }
            refreshSessions();
        }
        if (!loadHiveForEditing(m, err)) showMessage(T(L"Not loaded"), err);
        else showToast(L"check", TF(L"{0} is loaded", m.name), TF(L"In the registry editor: {0}", m.key), {}, nullptr);
        invalidate();
    }

    void unloadAllHives() {
        wstr err;
        for (HiveMount& m : hives_)
            if (m.loaded) unloadHive(m, err);
    }

    // ---- 5: local accounts (SAM of an offline Windows)
    void listAccounts() {
        if (busy_ || repInstall_ < 0) return;
        const Install in = rep_.installs[repInstall_];
        wstr sam = in.winDir + L"\\System32\\config\\SAM";
        auto list = std::make_shared<std::vector<LocalAccount>>();
        countUse(L"Tools: accounts read");
        auto err = std::make_shared<wstr>();
        runJob(T(L"Reading this Windows' accounts"), in.label(), false, [sam, list, err] { *list = readLocalAccounts(sam, *err); },
               [this, list, err] {
                   accounts_ = *list;
                   accountsListed_ = true;
                   accountsFor_ = repInstall_;
                   if (accounts_.empty()) showMessage(T(L"No accounts were read"), err->empty() ? T(L"No local accounts were found.") : *err);
               });
    }

    float drawAccounts(float x, float y, float w) {
        float top = y;
        const Install& in = rep_.installs[repInstall_];
        if (in.running) {
            y += para(T(L"The accounts of a running Windows cannot be changed from here (the accounts file is in use). Choose a Windows that is not running, or start this PC from the rescue stick."),
                      x + S(4), y, w - S(8), fonts().ui, kAmber, true) + S(10);
            return y - top;
        }
        y += para(T(L"The local accounts of this Windows. For a PC no one can sign in to: enable the built-in Administrator (it is there but switched off on most PCs), unlock an account locked after wrong passwords, or clear an account's password. From a working desktop you can then put the rest right."),
                  x + S(4), y, w - S(8), fonts().small, theme().label2, true) + S(6);
        y += para(T(L"These only work for an account kept on this PC. A Microsoft-account or work/school sign-in cannot be changed here: its password is not on the PC. Clearing a password loses that account's encrypted files (EFS) and its saved passwords."),
                  x + S(4), y, w - S(8), fonts().small, kAmber, true) + S(8);
        float bw = buttonWidth(T(L"Read the accounts"));
        drawButton(RectF(x, y, bw, S(28)), T(L"Read the accounts"), H_REP_ACCT, -1, accountsListed_ && accountsFor_ == repInstall_ ? kit::ButtonStyle::Normal : kit::ButtonStyle::Primary);
        y += S(40);
        if (!accountsListed_ || accountsFor_ != repInstall_) return y - top;
        if (accounts_.empty()) {
            y += para(T(L"No local accounts were found."), x + S(4), y, w, fonts().ui, theme().label3, true) + S(8);
            return y - top;
        }
        for (int i = 0; i < (int)accounts_.size(); ++i) {
            const LocalAccount& a = accounts_[i];
            wstr title = a.name + (a.fullName.empty() || a.fullName == a.name ? wstr() : L" · " + a.fullName) + (a.rid == 500 ? T(L"  (built-in Administrator)") : a.rid == 501 ? T(L"  (Guest)") : wstr());
            std::vector<wstr> tags;
            if (a.disabled) tags.push_back(T(L"disabled"));
            if (a.locked) tags.push_back(T(L"locked"));
            if (a.blankPassword) tags.push_back(T(L"no password"));
            if (a.pwNotRequired && !a.blankPassword) tags.push_back(T(L"password not required"));
            if (!a.lastLogon.empty()) tags.push_back(TF(L"last signed in {0}", a.lastLogon));
            wstr what;
            for (size_t k = 0; k < tags.size(); ++k) what += (k ? L" · " : L"") + tags[k];
            if (what.empty()) what = T(L"ready to sign in");
            std::vector<std::pair<wstr, int>> btns;
            if (a.disabled) btns.push_back({T(L"Enable"), i * 8 + 1});
            if (a.locked) btns.push_back({T(L"Unlock"), i * 8 + 2});
            if (!a.blankPassword) btns.push_back({T(L"Clear password…"), i * 8 + 3});
            y += toolCard(x, y, w, title, what, L"", btns, H_REP_ACCT, false);
        }
        return y - top;
    }

    void accountAction(int a) {
        if (a < 0) {
            listAccounts();
            return;
        }
        int i = a / 8, op = a % 8;
        if (i < 0 || i >= (int)accounts_.size() || repInstall_ < 0) return;
        const LocalAccount acc = accounts_[i];
        int kind = op == 1 ? AO_ENABLE : op == 2 ? AO_UNLOCK : AO_CLEAR_PASSWORD;
        wstr title, body;
        if (kind == AO_ENABLE) {
            title = TF(L"Enable the account \"{0}\"?", acc.name);
            body = TF(L"\"{0}\" will be switched on, so it can be signed in to again.", acc.name) + (acc.rid == 500 ? L" " + wstr(T(L"The built-in Administrator has full control of this PC.")) : wstr());
        } else if (kind == AO_UNLOCK) {
            title = TF(L"Unlock the account \"{0}\"?", acc.name);
            body = TF(L"The lock set after too many wrong passwords will be cleared for \"{0}\". Its password is not changed.", acc.name);
        } else {
            title = TF(L"Clear the password for \"{0}\"?", acc.name);
            body = TF(L"\"{0}\" will have no password: the next sign-in needs none, and a new one can be set in Windows afterwards.\n\nThis cannot help a Microsoft or work/school account. It loses this account's encrypted files (EFS) and its saved passwords, if it had any.", acc.name);
        }
        showConfirm(title, body, {T(L"Cancel"), kind == AO_CLEAR_PASSWORD ? T(L"Clear the password") : kind == AO_ENABLE ? T(L"Enable") : T(L"Unlock")}, 1, 1, kind == AO_CLEAR_PASSWORD,
                    [this, acc, kind](int b) {
                        if (b != 1) return;
                        doAccountChange(acc, kind);
                    });
    }

    void doAccountChange(const LocalAccount& acc, int kind) {
        if (repInstall_ < 0 || repInstall_ >= (int)rep_.installs.size()) return;
        const Install in = rep_.installs[repInstall_];
        const Partition P = rep_.m.parts[in.part];
        wstr sam = in.winDir + L"\\System32\\config\\SAM";
        // a copy of the accounts file first, so History can put it back
        if (journal_.isOpen() && istartsWith(sam, P.root)) {
            wstr act = journal_.begin(TF(L"Account \"{0}\": {1}", acc.name, kind == AO_ENABLE ? T(L"enabled") : kind == AO_UNLOCK ? T(L"unlocked") : T(L"password cleared")), in.label());
            wstr rel = sam.substr(P.root.size()), e2;
            bool saved = journal_.saveFile(act, P, rel, e2);
            journal_.end(act, saved, saved ? T(L"A copy of the accounts file from before the change is kept. Undo puts it back.") : e2, L"", saved);
            if (saved) {
                undo_.push_back(act);
                redo_.clear();
            }
            refreshSessions();
        }
        uint32_t rid = acc.rid;
        auto err = std::make_shared<wstr>();
        auto ok = std::make_shared<bool>(false);
        runJob(T(L"Changing the account"), acc.name, true, [sam, rid, kind, err, ok] { *ok = changeAccount(sam, rid, kind, *err); },
               [this, ok, err, acc, kind] {
                   if (*ok) {
                       accountsListed_ = false;
                       listAccounts();
                       showToast(L"check", kind == AO_ENABLE ? T(L"Account enabled") : kind == AO_UNLOCK ? T(L"Account unlocked") : T(L"Password cleared"),
                                 kind == AO_CLEAR_PASSWORD ? TF(L"\"{0}\" now signs in with no password.", acc.name) : acc.name, {}, nullptr);
                   } else
                       showMessage(T(L"The account was not changed"), err->empty() ? T(L"The change did not go through.") : *err);
               });
    }

    // ---- how far the last start got (top of Repair tools)
    const Disk* windowsDisk() {
        if (repInstall_ < 0 || repInstall_ >= (int)rep_.installs.size()) return nullptr;
        int disk = rep_.m.parts[rep_.installs[repInstall_].part].disk;
        for (const Disk& d : rep_.m.disks)
            if (d.number == disk) return &d;
        return nullptr;
    }

    float drawBootStage(float x, float y, float w) {
        const BootStage& b = bootStage_;
        float top = y;
        drawSectionHeader(RectF(x + S(4), y, w, S(24)), T(L"How far did the last start get?"));
        y += S(30);
        const Disk* wd = windowsDisk();
        bool sick = wd && diskUnhealthy(*wd);
        Color vc = b.stage == BS_KERNEL_NO_DISK ? kRed : b.stage == BS_KERNEL_STOPPED || b.stage == BS_NO_SIGN ? kAmber : theme().label;
        std::vector<std::pair<wstr, int>> btns;
        if (sick || b.diskSuspect) btns.push_back({T(L"Copy the whole disk"), 0});
        if (b.driverSuspect || (b.stage == BS_KERNEL_NO_DISK && !b.diskSuspect)) btns.push_back({T(L"Check the storage drivers"), 1});
        if (b.stage == BS_NO_SIGN) btns.push_back({T(L"Switch on boot logging"), 2});
        if (b.srt || b.bootLog) btns.push_back({T(L"Read the logs"), 3});
        float bwAll = 0;
        for (auto& bt : btns) bwAll += buttonWidth(bt.first) + S(6);
        auto body = [&](bool draw) {
            float pad = S(12), cx = x + pad, cw = w - pad * 2, yy = y + pad;
            yy += para(b.verdict, cx, yy, cw, fonts().uiSemi, vc, draw) + S(4);
            if (!b.detail.empty()) yy += para(b.detail, cx, yy, cw, fonts().small, theme().label2, draw) + S(6);
            if (sick)
                yy += para(TF(L"This Windows' disk reports a health problem: {0}. On a failing disk this is the most likely cause — back up before anything else, and do not restart until you have.", healthDetail(*wd)),
                           cx, yy, cw, fonts().smallSemi, kRed, draw) + S(6);
            for (const BootEvidence& e : b.evidence) {
                if (draw) {
                    text(e.what, RectF(cx, yy, cw * 0.45f, S(20)), fonts().small, theme().label2, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                    text(e.when + (e.detail.empty() ? wstr() : L"  ·  " + e.detail), RectF(cx + cw * 0.45f, yy, cw * 0.55f, S(20)), fonts().small, theme().label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                }
                yy += S(20);
            }
            if (!btns.empty()) {
                yy += S(8);
                if (draw) {
                    float bx = cx;
                    for (auto& bt : btns) {
                        float bw = buttonWidth(bt.first);
                        drawButton(RectF(bx, yy, bw, S(26)), bt.first, H_BOOTSTAGE, bt.second, bt.second == 0 ? kit::ButtonStyle::Primary : kit::ButtonStyle::Normal);
                        bx += bw + S(6);
                    }
                }
                yy += S(26);
            }
            return yy + pad - y;
        };
        float bh = body(false);
        card(RectF(x, y, w, bh));
        body(true);
        y += bh + S(12);
        return y - top;
    }

    void bootStageAction(int a) {
        if (repInstall_ < 0) return;
        switch (a) {
            case 0: {
                const Disk* wd = windowsDisk();
                if (wd) askImageTarget(wd->number);
                break;
            }
            case 1: checkStorageDrivers(); break;
            case 2: repairTab(1); break;
            case 3: repairTab(3); break;
        }
    }

    // ---- Memory (RAM) test: the last result, and starting a new one
    float drawMemoryCard(float x, float y, float w) {
        const Install& in = rep_.installs[repInstall_];
        float top = y;
        drawSectionHeader(RectF(x + S(4), y, w, S(24)), T(L"Memory (RAM)"));
        y += S(30);
        bool have = memChecked_ && memFor_ == repInstall_;
        wstr result;
        Color col = theme().label2;
        if (!have)
            result = T(L"Reads the result of the last Windows Memory Diagnostic from the event log. Bad RAM causes blue screens, failed repairs and start-up failures, so this is worth checking when the cause is unclear.");
        else if (!memTest_.found)
            result = T(L"No memory test is on record for this Windows. Running one (in the running Windows) rules RAM in or out.");
        else if (memTest_.errors) {
            result = (memTest_.when.empty() ? wstr() : memTest_.when + L"  ·  ") + T(L"the last memory test found errors. The RAM is most likely faulty — reseat or replace it and test again.");
            col = kRed;
        } else {
            result = (memTest_.when.empty() ? wstr() : memTest_.when + L"  ·  ") + T(L"the last memory test found no errors.");
            col = kGreen;
        }
        std::vector<std::pair<wstr, int>> btns;
        btns.push_back({have ? T(L"Check again") : T(L"Check the last test"), -1});
        if (in.running) btns.push_back({T(L"Run a memory test…"), -2});
        float bwAll = 0;
        for (auto& b : btns) bwAll += buttonWidth(b.first) + S(6);
        auto body = [&](bool draw) {
            float pad = techMode_ ? S(8) : S(12), cx = x + pad, cw = w - pad * 2 - bwAll - S(10), yy = y + pad;
            yy += para(result, cx, yy, cw, fonts().ui, col, draw);
            if (draw) {
                float bx = x + w - pad - bwAll + S(6);
                for (auto& b : btns) {
                    float bw = buttonWidth(b.first);
                    drawButton(RectF(bx, y + pad, bw, S(26)), b.first, H_REP_MEM, b.second, kit::ButtonStyle::Normal);
                    bx += bw + S(6);
                }
            }
            return std::max(yy, y + pad + S(28)) + pad - y;
        };
        float bh = body(false);
        card(RectF(x, y, w, bh));
        body(true);
        y += bh + S(10);
        return y - top;
    }

    void memoryAction(int a) {
        if (repInstall_ < 0 || repInstall_ >= (int)rep_.installs.size()) return;
        if (a == -2) {
            showConfirm(T(L"Run the Windows Memory Diagnostic?"),
                        T(L"Windows' own memory test runs when the PC restarts and takes several minutes (longer for more RAM). Windows will ask whether to restart now or at the next restart. Save any open work first. The result appears here afterwards."),
                        {T(L"Cancel"), T(L"Open it")}, 1, 1, false, [this](int b) {
                            if (b != 1) return;
                            wstr err;
                            if (!startMemoryTest(err)) showMessage(T(L"The memory test could not be started"), err);
                        });
            return;
        }
        const Install in = rep_.installs[repInstall_];
        auto res = std::make_shared<MemTestResult>();
        countUse(L"Tools: memory test results read");
        runJob(T(L"Reading the last memory test result"), in.label(), false, [in, res] { *res = lastMemoryTest(in); },
               [this, res] {
                   memTest_ = *res;
                   memChecked_ = true;
                   memFor_ = repInstall_;
               });
    }

    void repairTab(int k) {
        repTab_ = k;
        scroll_[V_REPAIR] = 0;
        if (repInstall_ >= 0 && repInstall_ < (int)rep_.installs.size()) {
            const Install& in = rep_.installs[repInstall_];
            if (k == 3) {
                logs_ = windowsLogs(in);
                logSel_ = -1;
                logLines_.clear();
            }
            if (k == 4) hives_ = in.running ? std::vector<HiveMount>() : offlineHives(in);
        }
        invalidate();
    }

    void systemRestore() {
        if (repInstall_ < 0) return;
        const Install& in = rep_.installs[repInstall_];
        showConfirm(T(L"Open System Restore?"),
                    TF(L"System Restore puts Windows' own files, drivers, programs and settings of {0} back to how they were at a restore point (Windows makes one before updates and driver installs). Personal files are not touched. Windows' own System Restore window opens; choose the point there. Programs installed after that point must be installed again.",
                       in.label()),
                    {T(L"Cancel"), T(L"Open System Restore")}, 1, 1, false, [this](int b) {
                        if (b != 1) return;
                        wstr err;
                        const Install& w = rep_.installs[repInstall_];
                        if (!startSystemRestore(w, rep_.m.parts[w.part], err)) showMessage(T(L"System Restore could not be opened"), err);
                    });
    }

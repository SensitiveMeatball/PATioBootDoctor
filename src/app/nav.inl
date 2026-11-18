// Part of class DoctorApp (included inside the class in main.cpp).
    // ---------------------------------------------------------------- left pane
    void drawNav(const RectF& r) {
        canvas().fill(r, theme().sidebar);
        canvas().fill(RectF(r.right() - 1, r.y, 1, r.h), theme().separator);
        bool narrow = r.w < S(120);
        float y = r.y + S(8);
        // Simple mode: Home and Boot chain only. Technician mode: everything, in a few entries
        // (screens that belong together share one entry and a row of tabs).
        bool disksFamily = view_ == V_DISKS || view_ == V_LAYOUT || view_ == V_MIGRATE || view_ == V_KEYS || view_ == V_FILES;
        bool recordsFamily = view_ == V_HISTORY || view_ == V_NOTES || view_ == V_HARDWARE;
        int rows = techMode_ ? 12 : 6;
        float rh = techMode_ ? S(32) : S(36), hh = S(24);
        if (rows * rh + 4 * hh + S(30) > r.h) {
            rh = std::max(S(24), (r.h - 4 * S(18) - S(30)) / rows);
            hh = S(18);
        }
        auto item = [&](int id, const char* icon, const wstr& label, bool on, bool enabled = true) {
            RectF rr(r.x + S(6), y, r.w - S(12), rh - S(2));
            if (on) canvas().round(rr, S(8), theme().accent.withAlpha(0.16f));
            else if (enabled && hovered(H_NAV, id)) canvas().round(rr, S(8), theme().label.withAlpha(0.06f));
            if (enabled) addHit(rr, H_NAV, id);
            Color tint = !enabled ? theme().label3 : on ? theme().accent : theme().label2;
            float is = std::min(S(18), rr.h - S(8));
            drawIcon(icon, rr.x + (narrow ? (rr.w - is) / 2 : S(10)), rr.cy() - is / 2, is, tint, theme().accent);
            if (!narrow) text(label, RectF(rr.x + S(38), rr.y, rr.w - S(42), rr.h), on ? fonts().uiSemi : fonts().ui, enabled ? theme().label : theme().label3);
            y += rh;
        };
        auto head = [&](const wstr& t) {
            if (narrow) {
                canvas().fill(RectF(r.x + S(14), y + S(4), r.w - S(28), 1), theme().separator);
                y += S(10);
                return;
            }
            text(t, RectF(r.x + S(16), y + S(2), r.w - S(24), hh - S(4)), fonts().smallSemi, theme().label3);
            y += hh;
        };
        item(N_HOME, "home", T(L"Home"), view_ == V_REPORT && sel_ < 0);
        item(N_CHAIN, "bd_chain", T(L"Boot chain"), view_ == V_CHAIN, haveReport_);
        if (techMode_) {
            item(N_SYSTEMS, "desktop", T(L"Windows on this PC"), view_ == V_SYSTEMS, haveReport_);
            item(N_REPAIR, "bd_repair", T(L"Windows Repair"), view_ == V_REPAIR, haveReport_);
            item(N_DISKS, "disk_hw", T(L"Disks and data"), disksFamily, haveReport_);
            item(N_HISTORY, "bd_history", notes_.empty() ? T(L"Records") : TF(L"Records ({0})", std::to_wstring(notes_.size())), recordsFamily);
        }
        head(T(L"Tools"));
        item(N_RESCUE, "drive_usb", T(L"Make a USB rescue disk"), false, haveReport_ && !busy_);
        if (techMode_) {
            item(N_CMD, "bd_terminal", T(L"Command prompt"), false);
            item(N_PS, "bd_shell", T(L"PowerShell"), false);
        }
        item(N_LANG, "bd_globe", narrow ? T(L"Language") : T(L"Language: ") + currentLanguageName(), false);
        item(N_MODE, "user", techMode_ ? T(L"Simple mode") : T(L"Technician mode"), false);
        // Power, at the bottom: one entry with a menu
        float bottom = r.bottom() - S(8) - rh;
        if (powerAction_ >= 0) bottom -= narrow ? S(40) : S(66);
        if (bottom > y + S(10)) y = bottom;
        else y += S(10);
        if (powerAction_ >= 0) {
            wchar_t t[16];
            swprintf(t, 16, L"%d:%02d", powerLeft_ / 60, powerLeft_ % 60);
            wstr what = powerAction_ == 0 ? T(L"Shutting down in") : powerAction_ == 1 ? T(L"Restarting in") : T(L"Hibernating in");
            RectF c(r.x + S(6), y, r.w - S(12), narrow ? S(34) : S(60));
            canvas().round(c, S(8), kAmber.withAlpha(0.16f));
            if (narrow) {
                text(t, RectF(c.x, c.y, c.w, c.h), fonts().smallSemi, theme().label, kit::kLine | DT_CENTER);
                addHit(c, H_NAV, N_POWER_CANCEL);
            } else {
                drawIcon("bd_timer", c.x + S(10), c.y + S(8), S(16), kAmber);
                text(what + L" " + t, RectF(c.x + S(32), c.y + S(4), c.w - S(36), S(24)), fonts().smallSemi, theme().label);
                float bw = buttonWidth(T(L"Cancel"));
                drawButton(RectF(c.x + S(32), c.y + S(30), bw, S(24)), T(L"Cancel"), H_NAV, N_POWER_CANCEL, kit::ButtonStyle::Quiet);
            }
            y += c.h + S(6);
        }
        item(N_POWER, "power", T(L"Power"), powerAction_ >= 0);
    }

    // The screens that share one left-pane entry, as a row of tabs above them.
    float drawFamilyTabs(const RectF& r) {
        struct Tab {
            int view;
            const wchar_t* name;
        };
        static const Tab disks[] = {{V_DISKS, N(L"Disks and partitions")}, {V_LAYOUT, N(L"Partition order")}, {V_MIGRATE, N(L"Move Windows")}, {V_KEYS, N(L"BitLocker keys")}, {V_FILES, N(L"Recovered files")}};
        static const Tab records[] = {{V_HISTORY, N(L"History and backups")}, {V_NOTES, N(L"Notes and job")}, {V_HARDWARE, N(L"Hardware details")}, {-1, N(L"Job sheet (PDF and TXT)…")}};
        const Tab* tabs = nullptr;
        int n = 0;
        if (view_ == V_DISKS || view_ == V_LAYOUT || view_ == V_MIGRATE || view_ == V_KEYS || view_ == V_FILES) tabs = disks, n = 5;
        else if (view_ == V_HISTORY || view_ == V_NOTES || view_ == V_HARDWARE) tabs = records, n = 4;
        if (!tabs) return 0;
        float x = r.x + S(18), y = r.y + S(10);
        for (int k = 0; k < n; ++k) {
            if (tabs[k].view == V_FILES && fileScan_.files.empty() && filePart_ < 0) continue;
            wstr t = T(tabs[k].name);
            float bw = buttonWidth(t);
            if (x + bw > r.right() - S(12)) break;
            drawButton(RectF(x, y, bw, S(28)), t, H_TAB, tabs[k].view, view_ == tabs[k].view ? kit::ButtonStyle::Primary : kit::ButtonStyle::Quiet);
            x += bw + S(6);
        }
        canvas().fill(RectF(r.x, r.y + S(46), r.w, 1), theme().separator);
        return S(47);
    }

    void familyTab(int v) {
        if (v == -1) {
            makeJobSheet();
            return;
        }
        if (v == V_LAYOUT && layoutInstall_ < 0) {
            for (int i = 0; i < (int)rep_.installs.size(); ++i)
                if (!rep_.installs[i].linux && rep_.installs[i].atRoot && !rep_.installs[i].windowsOld) {
                    openLayout(i);
                    return;
                }
            showMessage(T(L"No Windows to arrange"), T(L"The partition order is worked out for a Windows at the top of its drive, and none was found."));
            return;
        }
        if (v == V_MIGRATE) openMigrate();
        else if (v == V_KEYS) openKeys();
        else onCommand(C_VIEW, v, L"");
    }

    void powerButtonMenu() {
        std::vector<kit::MenuItem> items;
        auto add = [&](const wstr& t, int action) {
            kit::MenuItem m;
            m.text = t;
            m.cmd = C_POWER_MENU;
            m.arg = action;
            items.push_back(m);
        };
        add(T(L"Shut down…"), 0);
        add(T(L"Restart…"), 1);
        if (canHibernateCached()) add(T(L"Hibernate…"), 2);
        POINT p = mousePos();
        showMenu(items, std::max((float)p.x, S(60)), std::max(S(60), (float)p.y - S(130)));
    }

    void setMode(bool tech) {
        techMode_ = tech;
        settings().setInt(L"technician", tech ? 1 : 0);
        if (tech) consoleOn_ = true;
        else if (view_ != V_REPORT && view_ != V_CHAIN) {
            sel_ = -1;
            view_ = V_REPORT;
        }
        showToast(L"check", tech ? T(L"Technician mode") : T(L"Simple mode"),
                  tech ? T(L"Every tool is shown, with the commands it runs.") : T(L"Only what was found and how to fix it. Technician mode (left pane) shows every tool."), {}, nullptr);
        invalidate();
    }

    bool canHibernateCached() {
        if (hibernateKnown_ < 0) hibernateKnown_ = snapshot_ ? 1 : (canHibernate() ? 1 : 0);
        return hibernateKnown_ == 1;
    }

    wstr currentLanguageName() const {
        for (const auto& l : i18n::languages())
            if (l.code == i18n::current()) return l.name;
        return i18n::current();
    }

    wstr navTip(int id) {
        switch (id) {
            case N_HOME: return T(L"Everything found on this PC, and what it takes to fix it (Ctrl+H)");
            case N_CHAIN: return T(L"Every step a system needs to start, shown as a chain (Ctrl+2)");
            case N_DISKS: return T(L"Disks and partitions: create, delete, resize, unlock BitLocker, find lost partitions and recover deleted files (Ctrl+3)");
            case N_FILES: return T(L"The deleted files found by the last search");
            case N_REPAIR: return T(L"Repair tools for one Windows (start-up files, system files, image, updates, Safe Mode, crashes ...) and rescue tools for an old, damaged Windows");
            case N_NOTES: return T(L"Your own notes about this PC and the work on it (Ctrl+N adds one); they go into the saved report too");
            case N_KEYS: return T(L"The saved BitLocker recovery keys: which PC and drive each belongs to, owner names, archive, delete, back up");
            case N_MIGRATE: return T(L"Move Windows to another drive, for example a new SSD: copied with its start-up and recovery partitions");
            case N_SYSTEMS: return T(L"Every Windows found on every disk: which starts first, which are in the start-up menu, their names, and Windows.old");
            case N_LAYOUT: return T(L"The plan to put the partitions in Windows' own order");
            case N_HISTORY: return T(L"Backups and every change, with undo (Ctrl+4)");
            case N_RESCUE: return T(L"Make a USB stick that starts PATio Boot Doctor on any PC, even one whose Windows does not start. Everything on the stick is erased.");
            case N_CMD: return T(L"Open a command prompt with administrator rights (for typing commands such as diskpart or bcdedit yourself)");
            case N_PS: return T(L"Open PowerShell with administrator rights");
            case N_BACKUP: return T(L"Save the start-up settings and partition tables of all disks now (Ctrl+B)");
            case N_LANG: return T(L"Change the language of this program");
            case N_POWER: return T(L"Shut down, restart or hibernate: now, or after a time you choose");
            case N_MODE: return techMode_ ? T(L"Show only what was found and how to fix it") : T(L"For people who repair PCs: every tool, compact screens, and the commands each step runs");
            case N_SHUTDOWN: return T(L"Shut the PC down now or after a time you choose");
            case N_RESTART: return T(L"Restart the PC now or after a time you choose");
            case N_HIBERNATE: return T(L"Save everything that is open to the disk and switch off; it all comes back at the next start");
            case N_POWER_CANCEL: return T(L"Cancel the timed shut down, restart or hibernation");
        }
        return L"";
    }

    void navClick(int id) {
        switch (id) {
            case N_HOME: onCommand(C_HOME, 0, L""); break;
            case N_CHAIN: onCommand(C_VIEW, V_CHAIN, L""); break;
            case N_DISKS: onCommand(C_VIEW, V_DISKS, L""); break;
            case N_FILES: onCommand(C_VIEW, V_FILES, L""); break;
            case N_SYSTEMS: onCommand(C_VIEW, V_SYSTEMS, L""); break;
            case N_MIGRATE: openMigrate(); break;
            case N_KEYS: openKeys(); break;
            case N_REPAIR: openRepair(); break;
            case N_NOTES:
                if (view_ != V_NOTES) navPush();
                setView(V_NOTES);
                break;
            case N_LAYOUT: onCommand(C_VIEW, V_LAYOUT, L""); break;
            case N_HISTORY: onCommand(C_VIEW, V_HISTORY, L""); break;
            case N_RESCUE: askRescue(); break;
            case N_CMD: launchAdmin(false); break;
            case N_PS: launchAdmin(true); break;
            case N_BACKUP: onCommand(C_BACKUP_NOW, 0, L""); break;
            case N_LANG: languageMenu(); break;
            case N_POWER: powerButtonMenu(); break;
            case N_MODE: setMode(!techMode_); break;
            case N_SHUTDOWN: powerMenu(0); break;
            case N_RESTART: powerMenu(1); break;
            case N_HIBERNATE: powerMenu(2); break;
            case N_POWER_CANCEL: cancelPower(true); break;
        }
    }

    void languageMenu() {
        std::vector<kit::MenuItem> items;
        std::vector<i18n::LangInfo> langs = i18n::languages();
        for (size_t k = 0; k < langs.size(); ++k) {
            kit::MenuItem m;
            m.text = langs[k].name;
            m.cmd = C_SET_LANG;
            m.arg = (int)k;
            m.checked = langs[k].code == i18n::current();
            m.tip = langs[k].builtIn ? langs[k].name : langs[k].name + L" · " + langs[k].file;
            items.push_back(m);
        }
        kit::MenuItem sep;
        sep.separator = true;
        items.push_back(sep);
        kit::MenuItem add;
        add.text = T(L"Add a language pack…");
        add.cmd = C_LANGPACK;
        items.push_back(add);
        POINT p = mousePos();
        showMenu(items, (float)p.x, (float)p.y);
    }

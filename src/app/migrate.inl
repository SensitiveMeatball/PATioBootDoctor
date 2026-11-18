// Part of class DoctorApp (included inside the class in main.cpp).
    // ---------------------------------------------------------------- moving Windows to another drive
    void openMigrate() {
        if (migInstall_ < 0 || migInstall_ >= (int)rep_.installs.size()) {
            migInstall_ = -1;
            for (int i = 0; i < (int)rep_.installs.size() && migInstall_ < 0; ++i)
                if (!rep_.installs[i].linux && rep_.installs[i].atRoot && !rep_.installs[i].windowsOld && !rep_.installs[i].running) migInstall_ = i;
            for (int i = 0; i < (int)rep_.installs.size() && migInstall_ < 0; ++i)
                if (!rep_.installs[i].linux && rep_.installs[i].atRoot) migInstall_ = i;
        }
        if (migUefi_ < 0) migUefi_ = rep_.m.fw.uefi ? 1 : 0;
        if (view_ != V_MIGRATE) navPush();
        scroll_[V_MIGRATE] = 0;
        setView(V_MIGRATE);
    }

    float drawMigrate(float x, float y, float w) {
        float top = y;
        y += lessonBox(T(L"Copies a Windows to another disk, for example a new SSD, with everything it needs to start: the new disk gets a start-up partition, Windows and a recovery partition, in Windows' own order. The Windows partition is copied block by block, so programs, settings and files come along exactly. The old disk is only read."),
                       T(L"Moving Windows to another drive"), x, y, w, true);
        // 1. which Windows
        drawSectionHeader(RectF(x + S(4), y, w, S(24)), T(L"1. Which Windows"));
        y += S(28);
        {
            float bx = x;
            bool any = false;
            for (int i = 0; i < (int)rep_.installs.size(); ++i) {
                const Install& in = rep_.installs[i];
                if (in.linux || !in.atRoot || in.windowsOld) continue;
                any = true;
                wstr t = in.label() + (in.running ? T(L" (running now)") : wstr());
                float bw = buttonWidth(t);
                if (bx + bw > x + w && bx > x) {
                    bx = x;
                    y += S(34);
                }
                drawButton(RectF(bx, y, bw, S(28)), t, H_MIG_SRC, i, migInstall_ == i ? kit::ButtonStyle::Primary : kit::ButtonStyle::Normal);
                bx += bw + S(8);
            }
            if (!any) text(T(L"No Windows was found."), RectF(x + S(4), y, w, S(24)), fonts().ui, theme().label3);
            y += S(40);
        }
        // 2. to which disk
        drawSectionHeader(RectF(x + S(4), y, w, S(24)), T(L"2. To which disk"));
        y += S(28);
        auto targets = migrationTargets(rep_, migInstall_);
        if (targets.empty()) {
            y += para(T(L"There is no other disk. Connect the new disk (inside the PC, or with a USB adapter) and scan again (Ctrl+R)."), x + S(4), y, w, fonts().ui, theme().label3, true) + S(12);
        }
        for (const auto& tg : targets) {
            const Disk* d = nullptr;
            for (const Disk& q : rep_.m.disks)
                if (q.number == tg.first) d = &q;
            if (!d) continue;
            wstr names;
            for (int i : d->parts) names += (names.empty() ? L"" : L", ") + rep_.m.parts[i].name();
            wstr sub = names.empty() ? T(L"Empty") : TF(L"On it now (erased): {0}", names);
            if (d->health.known) sub += L" · " + T(L"Health: ") + d->health.summary;
            float rh = tg.second.empty() ? S(48) : S(48) + (float)canvas().measureWrapped(tg.second, fonts().small, (int)(w - S(60)));
            RectF rr(x, y, w, rh);
            bool sel = migTarget_ == tg.first;
            drawRow(rr, sel, true, tg.second.empty() && hovered(H_MIG_DST, tg.first), sel, false, false);
            if (tg.second.empty()) addHit(rr, H_MIG_DST, tg.first);
            Color c1 = rowText(sel, true, false), c2 = rowText(sel, true, true);
            drawIcon(d->removable ? "drive_usb" : "disk_hw", x + S(12), y + S(12), S(20), tg.second.empty() ? c2 : theme().label3, theme().accent);
            text(d->name(), RectF(x + S(42), y + S(4), w - S(50), S(22)), fonts().uiSemi, tg.second.empty() ? c1 : theme().label3);
            text(sub, RectF(x + S(42), y + S(24), w - S(50), S(18)), fonts().small, tg.second.empty() ? c2 : theme().label3);
            if (!tg.second.empty()) para(tg.second, x + S(42), y + S(44), w - S(60), fonts().small, theme().label3, true);
            y += rh + S(4);
        }
        y += S(12);
        // 3. UEFI or legacy
        drawSectionHeader(RectF(x + S(4), y, w, S(24)), T(L"3. How the PC starts"));
        y += S(28);
        {
            float bx = x;
            auto chip = [&](int v, const wstr& t) {
                float bw = buttonWidth(t);
                drawButton(RectF(bx, y, bw, S(28)), t, H_MIG_SCHEME, v, migUefi_ == v ? kit::ButtonStyle::Primary : kit::ButtonStyle::Normal);
                bx += bw + S(8);
            };
            chip(1, T(L"UEFI (GPT)"));
            chip(0, T(L"Legacy BIOS (MBR)"));
            text(rep_.m.fw.uefi ? T(L"This PC starts in UEFI mode.") : T(L"This PC starts in legacy BIOS mode."), RectF(bx + S(8), y, w - (bx - x) - S(8), S(28)), fonts().small, theme().label2);
            y += S(42);
        }
        // the plan
        if (migTarget_ < 0) {
            y += para(T(L"Choose the disk Windows should move to."), x + S(4), y, w, fonts().ui, theme().label3, true) + S(10);
            return y - top;
        }
        MigratePlan p = planMigration(rep_, migInstall_, migTarget_, migUefi_ != 0);
        drawSectionHeader(RectF(x + S(4), y, w, S(24)), T(L"The plan"));
        y += S(28);
        if (!p.possible) {
            y += para(p.why, x + S(4), y, w - S(8), fonts().ui, kRed, true) + S(14);
            return y - top;
        }
        std::vector<Seg> after;
        uint64_t o = 1ull << 20;
        after.push_back(Seg{o, p.bootLen, p.uefi ? T(L"Start-up (EFI)") : T(L"System Reserved"), 0});
        o += p.bootLen;
        if (p.msrLen) {
            after.push_back(Seg{o, p.msrLen, L"MSR", 0});
            o += p.msrLen;
        }
        after.push_back(Seg{o, p.winLen, L"Windows", 2});
        o += p.winLen;
        after.push_back(Seg{o, p.recLen, T(L"Recovery"), 0});
        text(TF(L"Disk {0} afterwards", std::to_wstring(p.target)), RectF(x, y, w, S(18)), fonts().smallSemi, theme().label2);
        y += S(20);
        drawSegBar(after, RectF(x, y, w, S(44)));
        y += S(54);
        for (const wstr& n : p.notes) {
            canvas().circle(x + S(8), y + S(8), S(3.5f), kAmber);
            y += para(n, x + S(20), y, w - S(24), fonts().small, theme().label, true) + S(5);
        }
        y += S(8);
        float bw = buttonWidth(T(L"Move Windows…"));
        drawButton(RectF(x, y, bw, S(30)), T(L"Move Windows…"), H_MIG_GO, 0, kit::ButtonStyle::Danger);
        y += S(44);
        return y - top;
    }

    void askMigration() {
        if (busy_) return;
        MigratePlan p = planMigration(rep_, migInstall_, migTarget_, migUefi_ != 0);
        if (!p.possible) {
            showMessage(T(L"Windows cannot be moved"), p.why);
            return;
        }
        wstr msg;
        for (const wstr& n : p.notes) msg += L"- " + n + L"\n";
        msg += TF(L"\nTo make sure the right disk is erased, type its number ({0}) below.", std::to_wstring(p.target));
        showInput(TF(L"Move {0} to Disk {1}?", rep_.installs[p.install].label(), std::to_wstring(p.target)), msg, L"", T(L"Move Windows"), [this, p](const wstr& t) {
            if (t != std::to_wstring(p.target)) {
                showMessage(T(L"Nothing was changed"), T(L"The number typed is not the new disk's number."));
                return true;
            }
            defer([this, p] { startMigration(p); });
            return true;
        });
    }

    void startMigration(const MigratePlan& p) {
        if (busy_ || snapshot_) return;
        if (!journal_.isOpen()) {
            showMessage(T(L"No backup folder"), T(L"Nothing was changed, because there is no folder to keep backups in. Choose one with the settings button."));
            return;
        }
        auto copy = std::make_shared<Report>(rep_);
        auto out = std::make_shared<FixOutcome>();
        runJob(T(L"Moving Windows to another drive"), TF(L"Disk {0} -> Disk {1}", std::to_wstring(p.source), std::to_wstring(p.target)), true,
               [this, p, copy, out] { *out = runMigration(p, *copy, journal_, [this](const wstr& s, float f) { progress(s, f); }, &cancel_); },
               [this, out] {
                   afterScanTitle_ = out->ok ? T(L"Windows was moved") : T(L"Windows was not moved completely");
                   afterScanNote_ = out->result;
                   refreshSessions();
                   startScan();
               });
    }

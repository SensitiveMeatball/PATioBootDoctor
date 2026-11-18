// Part of class DoctorApp (included inside the class in main.cpp).
    // ---------------------------------------------------------------- partition order (layout plan)
    void openLayout(int install, int uefiPref = -1) {
        if (install < 0 || install >= (int)rep_.installs.size()) return;
        layoutInstall_ = install;
        layoutKey_ = rep_.m.parts[rep_.installs[install].part].key;
        const Disk* d = nullptr;
        for (const Disk& x : rep_.m.disks)
            if (x.number == rep_.m.parts[rep_.installs[install].part].disk) d = &x;
        layoutUefi_ = uefiPref >= 0 ? uefiPref : (d && d->style == 1 ? 0 : 1);
        layoutPlan_ = planLayout(rep_, install, layoutUefi_ != 0, layoutBootDue_);
        if (view_ != V_LAYOUT) navPush();
        scroll_[V_LAYOUT] = 0;
        setView(V_LAYOUT);
    }

    void replanLayout() {
        layoutInstall_ = -1;
        for (int i = 0; i < (int)rep_.installs.size(); ++i)
            if (!rep_.installs[i].linux && rep_.m.parts[rep_.installs[i].part].key == layoutKey_) layoutInstall_ = i;
        if (layoutInstall_ >= 0) layoutPlan_ = planLayout(rep_, layoutInstall_, layoutUefi_ != 0, layoutBootDue_);
        else if (view_ == V_LAYOUT) view_ = V_DISKS;
    }

    std::vector<Seg> layoutSegs(bool target) {
        std::vector<Seg> v;
        const LayoutPlan& p = layoutPlan_;
        const std::vector<Slot>& slots = target ? p.target : p.now;
        if (p.target.empty()) return v;
        uint64_t a = p.target.front().off, b = p.target.back().off + p.target.back().len;
        auto name = [&](const Slot& s) -> wstr {
            switch (s.kind) {
                case SK_ESP: return T(L"Start-up (EFI)");
                case SK_MSR: return L"MSR";
                case SK_SYSRES: return T(L"System Reserved");
                case SK_WIN: return s.part >= 0 && !rep_.m.parts[s.part].letter.empty() ? rep_.m.parts[s.part].letter + L" Windows" : wstr(L"Windows");
                case SK_RECOVERY: return T(L"Recovery");
            }
            return L"?";
        };
        std::vector<Slot> sorted = slots;
        std::sort(sorted.begin(), sorted.end(), [](const Slot& x, const Slot& y) { return x.off < y.off; });
        uint64_t at = a;
        for (const Slot& s : sorted) {
            if (s.off > at + (1ull << 20)) v.push_back(Seg{at, s.off - at, T(L"Unused"), 1});
            v.push_back(Seg{s.off, s.len, name(s), s.kind == SK_WIN ? 2 : 0});
            at = std::max(at, s.off + s.len);
        }
        if (b > at + (1ull << 20)) v.push_back(Seg{at, b - at, T(L"Unused"), 1});
        return v;
    }

    float drawLayout(float x, float y, float w) {
        float top = y;
        if (layoutInstall_ < 0 || layoutInstall_ >= (int)rep_.installs.size()) {
            drawEmptyState(RectF(x, y, w, S(300)), T(L"No plan open"), T(L"In Disks and partitions, click a disk with Windows and choose \"Put the partitions in Windows' own order\"."));
            return S(300);
        }
        const Install& in = rep_.installs[layoutInstall_];
        const LayoutPlan& p = layoutPlan_;
        auto head = [&](bool draw) {
            float pad = S(16), cx = x + pad, cw = w - pad * 2, yy = y + pad;
            if (draw) {
                drawIcon("bd_list", cx, yy + S(2), S(20), theme().accent);
                text(TF(L"Partition order for {0}", in.label()) + L" · " + T(L"Disk ") + std::to_wstring(p.disk), RectF(cx + S(28), yy, cw - S(28), S(24)), fonts().uiSemi, theme().label);
            }
            yy += S(32);
            float bx = cx;
            auto chip = [&](int v, const wstr& t) {
                float bw = buttonWidth(t);
                if (draw) drawButton(RectF(bx, yy, bw, S(26)), t, H_LAYOUT_SCHEME, v, layoutUefi_ == v ? kit::ButtonStyle::Primary : kit::ButtonStyle::Normal);
                bx += bw + S(8);
            };
            chip(1, T(L"UEFI (GPT)"));
            chip(0, T(L"Legacy BIOS (MBR)"));
            yy += S(36);
            yy += para(layoutUefi_ ? T(L"Windows Setup makes these partitions, in this order: the start-up partition (EFI), the reserved partition (MSR), Windows, and the recovery partition with the troubleshoot options.")
                                   : T(L"Windows Setup makes these partitions, in this order: System Reserved (the start-up partition, marked active), Windows, and the recovery partition with the troubleshoot options."),
                       cx, yy, cw, fonts().small, theme().label2, draw) + S(6);
            yy += para(T(L"Only the part of the disk around this Windows is rearranged; other partitions (data, Linux) are not touched. Each step is shown and confirmed on its own, and the disk is read again after each one."),
                       cx, yy, cw, fonts().small, theme().label2, draw) + S(4);
            return yy + pad - y;
        };
        float h = head(false);
        card(RectF(x, y, w, h));
        head(true);
        y += h + S(14);
        if (!p.possible) {
            y += para(p.why, x + S(4), y, w - S(8), fonts().ui, kRed, true) + S(14);
            if (p.target.empty()) return y - top;
        }
        text(T(L"Now"), RectF(x, y, w, S(18)), fonts().smallSemi, theme().label2);
        y += S(20);
        drawSegBar(layoutSegs(false), RectF(x, y, w, S(44)));
        y += S(52);
        text(T(L"Windows' own order"), RectF(x, y, w, S(18)), fonts().smallSemi, theme().label2);
        y += S(20);
        drawSegBar(layoutSegs(true), RectF(x, y, w, S(44)));
        y += S(58);
        if (p.possible && p.done) {
            y += para(T(L"The partitions are already in Windows' own order. Nothing needs to be done."), x + S(4), y, w - S(8), fonts().uiSemi, kGreen, true) + S(10);
            return y - top;
        }
        if (!p.possible) return y - top;
        auto warnBox = [&](bool draw) {
            float pad = S(12);
            float hh = pad + (float)canvas().measureWrapped(T(kLayoutWarning), fonts().small, (int)(w - pad * 2 - S(4))) + pad;
            if (draw) {
                canvas().round(RectF(x, y, w, hh), S(8), kAmber.withAlpha(0.12f));
                para(T(kLayoutWarning), x + pad + S(4), y + pad, w - pad * 2 - S(4), fonts().small, theme().label, true);
            }
            return hh;
        };
        float wh = warnBox(false);
        warnBox(true);
        y += wh + S(12);
        drawSectionHeader(RectF(x + S(4), y, w, S(24)), TF(L"The steps ({0})", std::to_wstring(p.steps.size())));
        y += S(30);
        for (int k = 0; k < (int)p.steps.size(); ++k) {
            const LayoutStep& st = p.steps[k];
            auto body = [&](bool draw) {
                float pad = S(14), cx = x + pad + S(36), cw = w - pad * 2 - S(36) - S(150), yy = y + pad;
                if (draw) {
                    canvas().circle(x + pad + S(12), yy + S(11), S(12), k == 0 ? theme().accent : theme().label3);
                    text(std::to_wstring(k + 1), RectF(x + pad, yy, S(24), S(22)), fonts().smallSemi, Color(255, 255, 255), kit::kLine | DT_CENTER);
                    text(st.title, RectF(cx, yy, cw, S(22)), fonts().uiSemi, k == 0 ? theme().label : theme().label2);
                }
                yy += S(24);
                yy += para(st.what, cx, yy, cw, fonts().small, theme().label2, draw) + S(4);
                if (st.slow) yy += para(T(L"Slow: keep the PC on and plugged in, and do not close this program while it runs."), cx, yy, cw, fonts().smallSemi, kAmber, draw) + S(4);
                if (draw && k == 0) {
                    wstr t = T(L"Do step 1…");
                    float bw = buttonWidth(t);
                    drawButton(RectF(x + w - pad - bw, y + pad, bw, S(28)), t, H_LAYOUT_GO, 0, st.danger ? kit::ButtonStyle::Danger : kit::ButtonStyle::Primary);
                } else if (draw)
                    text(T(L"Later"), RectF(x + w - pad - S(140), y + pad, S(140), S(22)), fonts().small, theme().label3, kit::kLine | DT_RIGHT);
                return std::max(yy, y + pad + S(28)) + pad - S(4) - y;
            };
            float bh = body(false);
            card(RectF(x, y, w, bh));
            body(true);
            y += bh + S(8);
        }
        return y - top + S(10);
    }

    void askLayoutStep() {
        if (busy_ || !layoutPlan_.possible || layoutPlan_.steps.empty()) return;
        const LayoutStep& st = layoutPlan_.steps[0];
        bool undo = st.kind == ST_DELETE || st.kind == ST_CREATE_ESP || st.kind == ST_CREATE_MSR || st.kind == ST_CREATE_SYSRES || st.kind == ST_CREATE_RECOVERY;
        wstr msg = st.what + L"\n\n";
        for (const Disk& dk : rep_.m.disks)
            if (dk.number == layoutPlan_.disk && dk.health.known && dk.health.level == 2)
                msg += T(L"Warning: this disk reports that it may be failing (S.M.A.R.T.). Moving partitions reads and writes all of it, which can make a failing disk fail completely. Copy your files elsewhere first.\n\n");
        if (st.kind == ST_MOVE)
            msg += T(L"Moving a partition rewrites every sector of it. It cannot be stopped once it has started. If the power fails or the PC crashes during it, start this program again: it finishes the move from where it stopped. Without that, the partition cannot be used.\n\n");
        msg += undo ? T(L"The partition table is backed up first; Undo (Ctrl+Z) can put it back.") : T(L"This step cannot be undone with Undo.");
        msg += T(L"\n\nBack up important files first, keep a laptop plugged in, and close other programs.");
        showConfirm(st.title + L"?", msg, {T(L"Cancel"), T(L"Do it")}, 1, 0, st.danger, [this](int b) {
            if (b == 1) startLayoutStep();
        });
    }

    void startLayoutStep() {
        if (busy_ || snapshot_) return;
        if (!journal_.isOpen()) {
            showMessage(T(L"No backup folder"), T(L"Nothing was changed, because there is no folder to keep backups in. Choose one with the settings button."));
            return;
        }
        if (job_.joinable()) job_.join();
        LayoutPlan plan = layoutPlan_;
        lastLayoutKind_ = plan.steps[0].kind;
        busy_ = true;
        cancel_ = false;
        progTitle_ = plan.steps[0].title;
        progCancel_ = false;
        setProgress(plan.steps[0].title, plan.steps[0].kind == ST_MOVE ? 0.f : -1.f, T(L"Disk ") + std::to_wstring(plan.disk), false);
        auto copy = std::make_shared<Report>(rep_);
        wstr stateFolder = journal_.root();
        job_ = std::thread([this, copy, plan, stateFolder] {
            noRedirection();
            FixOutcome o = runLayoutStep(plan, *copy, journal_, stateFolder, [this](const wstr& s, float f) { progress(s, f < 0 ? 0.f : f); }, &cancel_);
            {
                std::lock_guard<std::mutex> l(lock_);
                jobOk_ = o.ok;
                jobNote_ = o.result;
                jobDirs_.clear();
                if (o.ok && o.undoable && !o.actionDir.empty()) jobDirs_.push_back(o.actionDir);
            }
            PostMessageW(hwnd(), WM_BD_FIXED, 3, 0);
        });
        invalidate();
    }

    void startResumeMove() {
        if (busy_ || snapshot_ || !journal_.isOpen()) return;
        if (job_.joinable()) job_.join();
        busy_ = true;
        progTitle_ = T(L"Finishing the partition move");
        progCancel_ = false;
        setProgress(progTitle_, 0.f, L"", false);
        wstr folder = journal_.root();
        lastLayoutKind_ = ST_MOVE;
        job_ = std::thread([this, folder] {
            noRedirection();
            FixOutcome o = resumeMove(folder, [this](const wstr& s, float f) { progress(s, f); });
            {
                std::lock_guard<std::mutex> l(lock_);
                jobOk_ = o.ok;
                jobNote_ = o.result;
                jobDirs_.clear();
            }
            PostMessageW(hwnd(), WM_BD_FIXED, 3, 0);
        });
        invalidate();
    }

// Part of class DoctorApp (included inside the class in main.cpp).
    // ------------------------------------------------------------ drawing
    void drawContent(const RectF& areaIn) override {
        drawPanel(areaIn);
        RectF area = areaIn;
        conRect_ = RectF();
        splitRect_ = RectF();
        {
            float nw = area.w >= S(1000) ? S(214) : S(58);
            drawNav(RectF(area.x, area.y, nw, area.h));
            area.x += nw;
            area.w -= nw;
            if (nw != navW_) {
                navW_ = nw;
                // the progress bar starts next to the left pane; its balloons go quiet 3 minutes after the
                // first one; jobs longer than 10 seconds say how long they took
                setProgressLayout(nw, 180.f, 10.f);
            }
        }
        if (consoleOn_) {
            float ch = std::min(S(consoleH_), areaIn.h * 0.7f);
            RectF con(area.x, areaIn.bottom() - ch, area.w, ch);
            area.h -= ch;
            drawConsole(con);
        }
        if (!haveReport_) {
            drawEmptyState(area, busy_ ? T(L"Looking at your disks") : T(L"Not scanned yet"),
                           busy_ ? T(L"Every disk and partition is checked for Windows and for what it needs to start. This takes a few seconds.")
                                 : T(L"Press Scan (Ctrl+R) to look at every disk."));
            return;
        }
        RectF main = area;
        if (view_ == V_REPORT || view_ == V_CHAIN) {
            float sw = std::min(S(270), area.w * 0.32f);
            drawSidebar(RectF(area.x, area.y, sw, area.h));
            main = RectF(area.x + sw, area.y, area.w - sw, area.h);
            canvas().fill(RectF(main.x, main.y, 1, main.h), theme().separator);
        }
        {
            float th = drawFamilyTabs(main);  // screens that share a left-pane entry
            main.y += th;
            main.h -= th;
        }
        RectF view = main.inset(S(18), S(14));
        viewTop_ = main.y;
        viewBottom_ = main.bottom();
        float& scroll = scroll_[view_];
        canvas().pushClip(main);
        float h = 0;
        float y0 = view.y - scroll;
        switch (view_) {
            case V_REPORT: h = selected() ? drawInstallPage(view.x, y0, view.w - S(12)) : drawOverview(view.x, y0, view.w - S(12)); break;
            case V_CHAIN: h = drawChain(view.x, y0, view.w - S(12), view.h); break;
            case V_DISKS: h = drawDisks(view.x, y0, view.w - S(12)); break;
            case V_HISTORY: h = drawHistory(view.x, y0, view.w - S(12)); break;
            case V_ABOUT: h = drawAbout(view.x, y0, view.w - S(12)); break;
            case V_FILES: h = drawFiles(view.x, y0, view.w - S(12)); break;
            case V_LAYOUT: h = drawLayout(view.x, y0, view.w - S(12)); break;
            case V_SYSTEMS: h = drawSystems(view.x, y0, view.w - S(12)); break;
            case V_MIGRATE: h = drawMigrate(view.x, y0, view.w - S(12)); break;
            case V_KEYS: h = drawKeys(view.x, y0, view.w - S(12)); break;
            case V_NOTES: h = drawNotes(view.x, y0, view.w - S(12)); break;
            case V_REPAIR: h = drawRepair(view.x, y0, view.w - S(12)); break;
            case V_STATS: h = drawStats(view.x, y0, view.w - S(12)); break;
            case V_HARDWARE: h = drawHardware(view.x, y0, view.w - S(12)); break;
        }
        contentH_[view_] = h;
        viewH_ = view.h;
        float maxS = std::max(0.f, h - view.h);
        if (scroll > maxS) scroll = maxS;
        if (h > view.h)
            drawScrollbar(RectF(main.right() - S(11), main.y + S(3), S(9), main.h - S(6)), (int)h, (int)view.h, (int)scroll, H_SCROLL);
        canvas().popClip();
        drawRecIndicator(areaIn);
    }

    void drawSidebar(const RectF& r) {
        float y = r.y + S(10);
        drawSectionHeader(RectF(r.x + S(14), y, r.w - S(28), S(24)), T(L"Systems found"));
        y += S(28);
        float rowH = S(50);
        int n = (int)rep_.installs.size();
        for (int i = -1; i < n; ++i) {
            RectF rr(r.x + S(6), y, r.w - S(12), rowH);
            if (view_ == V_CHAIN && i < 0) continue;
            bool sel = i == sel_;
            drawRow(rr, sel, true, hovered(H_SIDE, i), sel, false, false);
            addHit(rr, H_SIDE, i);
            Color main = rowText(sel, true, false), second = rowText(sel, true, true);
            if (i < 0) {
                drawIcon("pc", rr.x + S(10), rr.y + S(8), S(18), second, theme().accent);
                text(T(L"Whole PC"), RectF(rr.x + S(38), rr.y + S(6), rr.w - S(44), S(20)), fonts().uiSemi, main);
                int p = rep_.problems();
                text(p ? plural(p, T(L"thing needs attention"), T(L"things need attention")) : T(L"Everything looks fine"),
                     RectF(rr.x + S(38), rr.y + S(26), rr.w - S(44), S(18)), fonts().small, second);
            } else {
                const Install& in = rep_.installs[i];
                drawIcon(in.linux ? "bd_linux" : "desktop", rr.x + S(10), rr.y + S(8), S(18), second, theme().accent);
                text(in.label(), RectF(rr.x + S(38), rr.y + S(6), rr.w - S(44), S(20)), fonts().uiSemi, main);
                Color dot = sel ? main : hc(in.verdict);
                canvas().circle(rr.x + S(42), rr.y + S(35), S(4), dot);
                text(in.verdictTitle + L" · " + (in.where.empty() ? in.winDir : in.where.substr(0, in.where.find(L" · "))),
                     RectF(rr.x + S(52), rr.y + S(26), rr.w - S(58), S(18)), fonts().small, second);
            }
            y += rowH + S(2);
        }
        if (n == 0)
            text(T(L"None"), RectF(r.x + S(20), y + S(4), r.w - S(40), S(20)), fonts().ui, theme().label3);
    }

    // ---------------- Report: overview
    float drawOverview(float x, float y, float w) {
        float top = y;
        y += drawHealthBanner(x, y, w);
        y += drawClockBanner(x, y, w);
        if (!rep_.env.title.empty()) {
            auto body = [&](bool draw) {
                float pad = S(14), cx = x + pad + S(34), cw = w - pad * 2 - S(34), yy = y + pad;
                if (draw) {
                    drawIcon(rep_.env.virtualMachine || rep_.env.vhdBoot ? "disk_hw" : "bd_chip", x + pad, yy, S(22), theme().accent);
                    text(rep_.env.title, RectF(cx, yy, cw, S(22)), fonts().uiSemi, theme().label);
                }
                yy += S(26);
                yy += para(rep_.env.text, cx, yy, cw, fonts().small, theme().label2, draw) + S(6);
                yy += para(rep_.env.next, cx, yy, cw, fonts().smallSemi, theme().label, draw) + S(4);
                return yy + pad - y;
            };
            float h = body(false);
            RectF c(x, y, w, h);
            canvas().round(c, S(10), theme().accent.withAlpha(0.10f));
            canvas().round(RectF(c.x, c.y + S(8), S(4), c.h - S(16)), S(2), theme().accent);
            body(true);
            y += h + S(12);
        }
        // Headline
        {
            int bad = 0, warn = 0, inactive = 0, fine = 0;
            for (const Install& in : rep_.installs) {
                if (!in.atRoot && in.windowsOld) continue;
                if (in.verdict == Health::Bad) ++bad;
                else if (in.verdict == Health::Warn && !in.active && !in.running) ++inactive;
                else if (in.verdict == Health::Warn) ++warn;
                else ++fine;
            }
            size_t nl = std::count_if(rep_.installs.begin(), rep_.installs.end(), [](const Install& i) { return i.linux; }), nw = rep_.installs.size() - nl;
            wstr title = rep_.installs.empty() ? T(L"No Windows or Linux was found on this PC")
                         : T(L"Found ") + (nw ? plural(nw, T(L"Windows installation"), T(L"Windows installations")) : wstr()) + (nw && nl ? T(L" and ") : L"") +
                               (nl ? plural(nl, T(L"Linux system"), T(L"Linux systems")) : wstr());
            wstr sub;
            auto addPart = [&](int n, const wstr& t) {
                if (n) sub += (sub.empty() ? L"" : L" · ") + std::to_wstring(n) + L" " + t;
            };
            addPart(fine, fine == 1 ? T(L"starts fine") : T(L"start fine"));
            addPart(warn, warn == 1 ? T(L"has small problems") : T(L"have small problems"));
            addPart(inactive, L"inactive (not in the start-up menu)");
            addPart(bad, bad == 1 ? T(L"cannot start") : T(L"cannot start"));
            wstr host = T(L"Checked from ") + rep_.host + T(L" · The PC starts in ") + (rep_.m.fw.uefi ? T(L"UEFI mode") : T(L"legacy (BIOS) mode"));
            float h = S(92);
            RectF c(x, y, w, h);
            card(c);
            bool good = rep_.problems() == 0 && !rep_.installs.empty();
            drawIcon(good ? "check" : "bd_chain", c.x + S(18), c.y + S(20), S(26), good ? kGreen : theme().accent);
            float tx = c.x + S(60);
            float bw = buttonWidth(T(L"Scan again"));
            drawButton(RectF(c.right() - S(14) - bw, c.y + S(16), bw, S(28)), T(L"Scan again"), H_SCAN, 0, kit::ButtonStyle::Quiet);
            text(title, RectF(tx, c.y + S(14), c.right() - tx - bw - S(24), S(26)), fonts().title, theme().label);
            text(sub.empty() ? wstr(T(L"Nothing to check")) : sub, RectF(tx, c.y + S(42), c.w - S(80), S(20)), fonts().ui, theme().label2);
            text(host, RectF(tx, c.y + S(64), c.w - S(260), S(18)), fonts().small, theme().label3);
            if (rep_.m.fw.uefi) {
                wstr sb = !rep_.m.fw.secureBootKnown ? wstr(T(L"About Secure Boot")) : rep_.m.fw.secureBoot ? T(L"Secure Boot: on") : T(L"Secure Boot: off");
                float sbw = buttonWidth(sb);
                drawButton(RectF(c.right() - S(14) - sbw, c.y + S(56), sbw, S(26)), sb, H_SECUREBOOT, 0, kit::ButtonStyle::Quiet);
            }
            y += h + S(12);
        }
        {
            float bx = x;
            auto tool = [&](const wstr& t, const char* icon, int hit, bool strong) {
                float bw = buttonWidth(t) + S(26);
                RectF b(bx, y, bw, S(30));
                drawButton(b, t, hit, 0, strong ? kit::ButtonStyle::Primary : kit::ButtonStyle::Normal);
                drawIcon(icon, b.x + S(8), b.cy() - S(8), S(16), strong ? Color(255, 255, 255) : theme().label2);
                bx += bw + S(8);
            };
            int fixedDisks = 0;
            for (const Disk& d : rep_.m.disks) fixedDisks += d.group == DG_FIXED || d.group == DG_ENTERPRISE;
            tool(T(L"   Load a storage driver…"), "bd_chip", H_LOAD_DRIVER, fixedDisks == 0);
            tool(T(L"   Job sheet (PDF and TXT)…"), "bd_note", H_JOBSHEET, false);
            y += S(42);
            if (fixedDisks == 0)
                y += para(T(L"No internal disk is seen. On many newer PCs (Intel VMD / RST, RAID) the disk only appears after its storage driver is loaded: put the driver (its .inf, .sys and .cat files) on a USB stick and load it here."),
                          x + S(4), y, w - S(8), fonts().small, kAmber, true) + S(10);
        }
        y += drawGuide(x, y, w);
        // For newcomers: how starting works, in one paragraph
        if (showLessons_) {
            float lh = lessonBox(T(kLessonHowPcStarts), T(L"New to this? How a PC starts"), x, y, w, true);
            float bw = buttonWidth(T(L"Hide explanations"));
            drawButton(RectF(x + w - bw - S(6), y + S(4), bw, S(24)), T(L"Hide explanations"), H_LESSONS, 0, kit::ButtonStyle::Quiet);
            y += lh + S(6);
        } else {
            float bw = buttonWidth(T(L"Show explanations"));
            drawButton(RectF(x, y, bw, S(24)), T(L"Show explanations"), H_LESSONS, 0, kit::ButtonStyle::Quiet);
            y += S(34);
        }
        if (!rep_.installs.empty()) {
            drawSectionHeader(RectF(x + S(4), y, w, S(24)), T(L"Systems on this PC"));
            y += S(28);
            for (int i = 0; i < (int)rep_.installs.size(); ++i) {
                float h = installCard(i, x, y, w, false);
                card(RectF(x, y, w, h));
                installCard(i, x, y, w, true);
                y += h + S(10);
            }
            y += S(6);
        }
        if (!rep_.pc.empty()) {
            drawSectionHeader(RectF(x + S(4), y, w, S(24)), T(L"Other findings"));
            y += S(28);
            for (int i = 0; i < (int)rep_.pc.size(); ++i) {
                float h = findingCard(i, x, y, w, false);
                card(RectF(x, y, w, h));
                findingCard(i, x, y, w, true);
                y += h + S(10);
            }
        }
        return y - top;
    }

    float installCard(int i, float x, float y, float w, bool draw) {
        const Install& in = rep_.installs[i];
        float pad = S(14), cx = x + pad + S(32), cw = w - pad * 2 - S(32);
        float yy = y + pad;
        if (draw) {
            drawIcon(in.linux ? "bd_linux" : "desktop", x + pad, yy + S(2), S(22), theme().label2, theme().accent);
            float pw = pill(x + w - pad, yy + S(11), in.verdictTitle, hc(in.verdict));
            wstr t = in.label() + (in.build ? T(L" · build ") + std::to_wstring(in.build) : wstr());
            text(t, RectF(cx, yy, cw - pw - S(10), S(22)), fonts().uiSemi, theme().label);
            text(in.where + L" · " + stateText(in), RectF(cx, yy + S(22), cw, S(18)), fonts().small, theme().label2);
        }
        yy += S(46);
        yy += para(in.verdictText, cx, yy, cw, fonts().ui, theme().label, draw) + S(6);
        int shown = 0;
        for (const Link& l : in.chain) {
            if ((l.health != Health::Bad && l.health != Health::Warn) || shown >= 3) continue;
            ++shown;
            if (draw) canvas().circle(cx + S(4), yy + S(8), S(3.5f), hc(l.health));
            yy += para(T(l.name) + L": " + l.plain, cx + S(14), yy, cw - S(14), fonts().small, theme().label2, draw) + S(4);
        }
        if (!in.plan.empty() && in.verdict != Health::Ok && in.verdict != Health::Info) {
            wstr steps;
            int k = 1;
            for (const Fix& f : in.plan) steps += (steps.empty() ? L"" : L"  ") + std::to_wstring(k++) + L". " + f.title + (f.available ? L"" : L" (not possible here)");
            yy += S(2);
            yy += para(T(L"What it will take:  ") + steps, cx, yy, cw, fonts().smallSemi, theme().label, draw) + S(8);
        }
        float bx = cx;
        auto btn = [&](const wstr& t, int hit, kit::ButtonStyle st) {
            float bw = buttonWidth(t);
            if (draw) drawButton(RectF(bx, yy, bw, S(28)), t, hit, i, st);
            bx += bw + S(8);
        };
        if (needsWork(in)) btn(in.plan.size() == 1 ? T(L"Fix it") : T(L"Fix it (") + std::to_wstring(in.plan.size()) + T(L" steps)"), H_FIX_ALL, kit::ButtonStyle::Primary);
        btn(T(L"Show details"), H_DETAILS, kit::ButtonStyle::Normal);
        btn(T(L"Boot chain"), H_CHAIN, kit::ButtonStyle::Quiet);
        yy += S(28) + pad;
        return yy - y;
    }

    float findingCard(int i, float x, float y, float w, bool draw) {
        const Finding& f = rep_.pc[i];
        float pad = S(14), cx = x + pad + S(18), cw = w - pad * 2 - S(18);
        float yy = y + pad;
        if (draw) {
            canvas().circle(x + pad + S(5), yy + S(10), S(5), hc(f.health));
            text(f.title, RectF(cx, yy, cw - S(100), S(22)), fonts().uiSemi, theme().label);
            pill(x + w - pad, yy + S(11), T(healthWord(f.health)), hc(f.health));
        }
        yy += S(26);
        yy += para(f.text, cx, yy, cw, fonts().small, theme().label2, draw) + S(8);
        yy += lessonBox(f.lesson, T(L"What does this mean?"), cx, yy, cw, draw);
        if (!f.fixes.empty()) {
            float bx = cx;
            for (int k = 0; k < (int)f.fixes.size(); ++k) {
                const Fix& fx = f.fixes[k];
                float bw = buttonWidth(fx.title);
                if (draw && fx.available)
                    drawButton(RectF(bx, yy, bw, S(28)), fx.title, H_FIX_PC, i * 100 + k, fx.danger ? kit::ButtonStyle::Danger : kit::ButtonStyle::Normal);
                if (fx.available) bx += bw + S(8);
                else yy += para(fx.title + T(L": not possible here. ") + fx.notAvailable, cx, yy, cw, fonts().small, kRed, draw) + S(4);
            }
            if (bx > cx) yy += S(28);
            yy += S(4);
        }
        return yy + pad - S(8) - y;
    }

    // ---------------- Report: one installation
    float drawInstallPage(float x, float y, float w) {
        const Install& in = *selected();
        float top = y;
        // Header card
        {
            auto body = [&](bool draw) {
                float pad = S(16), cx = x + pad + S(40), cw = w - pad * 2 - S(40), yy = y + pad;
                if (draw) {
                    drawIcon(in.linux ? "bd_linux" : "desktop", x + pad, yy + S(2), S(28), theme().label2, theme().accent);
                    float pw = pill(x + w - pad, yy + S(13), in.verdictTitle, hc(in.verdict));
                    text(in.label(), RectF(cx, yy, cw - pw - S(10), S(28)), fonts().title, theme().label);
                }
                yy += S(30);
                wstr facts = in.winDir + L" · " + in.where + L" · " + stateText(in);
                if (in.build) facts += T(L" · build ") + std::to_wstring(in.build);
                if (!in.edition.empty()) facts += L" · " + in.edition;
                yy += para(facts, cx, yy, cw, fonts().small, theme().label2, draw) + S(6);
                yy += para(in.verdictText, cx, yy, cw, fonts().ui, theme().label, draw) + S(4);
                if (in.moved && !in.movedNote.empty()) yy += para(in.movedNote, cx, yy, cw, fonts().small, kAmber, draw) + S(4);
                if (draw) {
                    float bw = buttonWidth(T(L"Back to the whole PC"));
                    drawButton(RectF(cx, yy + S(4), bw, S(26)), T(L"Back to the whole PC"), H_BACK, 0, kit::ButtonStyle::Quiet);
                    float cw2 = buttonWidth(T(L"Show the boot chain"));
                    drawButton(RectF(cx + bw + S(8), yy + S(4), cw2, S(26)), T(L"Show the boot chain"), H_CHAIN, sel_, kit::ButtonStyle::Quiet);
                }
                yy += S(30) + pad;
                return yy - y;
            };
            float h = body(false);
            card(RectF(x, y, w, h));
            body(true);
            y += h + S(16);
        }
        // What it takes
        drawSectionHeader(RectF(x + S(4), y, w * 0.6f, S(24)), T(L"What it will take"));
        if (needsWork(in)) {
            float bw = buttonWidth(T(L"Fix everything"));
            drawButton(RectF(x + w - bw, y - S(2), bw, S(28)), T(L"Fix everything"), H_FIX_ALL, sel_, kit::ButtonStyle::Primary);
        }
        y += S(32);
        if (in.plan.empty()) {
            y += para(in.running || in.verdict == Health::Ok ? (in.linux ? T(L"Nothing. This Linux needs no repairs to start.") : T(L"Nothing. This Windows needs no repairs to start."))
                      : in.windowsOld ? T(L"Windows.old cannot be made to start. Copy the files you need out of it; Windows deletes it by itself after a while.")
                                      : T(L"No repair is known for what was found. The steps below tell more."),
                      x + S(4), y, w - S(8), fonts().ui, theme().label2, true) +
                 S(14);
        } else {
            for (int k = 0; k < (int)in.plan.size(); ++k) {
                auto body = [&](bool draw) {
                    const Fix& f = in.plan[k];
                    float pad = S(14), cx = x + pad + S(36), cw = w - pad * 2 - S(36) - S(140), yy = y + pad;
                    if (draw) {
                        canvas().circle(x + pad + S(12), yy + S(11), S(12), f.available ? theme().accent : theme().label3);
                        text(std::to_wstring(k + 1), RectF(x + pad, yy, S(24), S(22)), fonts().smallSemi, Color(255, 255, 255), kit::kLine | DT_CENTER);
                        text(f.title, RectF(cx, yy, cw, S(22)), fonts().uiSemi, theme().label);
                    }
                    yy += S(24);
                    yy += para(f.what, cx, yy, cw, fonts().small, theme().label, draw) + S(3);
                    yy += para(f.takes + (f.undoable ? L"" : T(L" This cannot be undone.")), cx, yy, cw, fonts().small, theme().label2, draw) + S(3);
                    if (!f.available) yy += para(T(L"Not possible here: ") + f.notAvailable, cx, yy, cw, fonts().small, kRed, draw) + S(3);
                    if (draw && f.available) {
                        wstr t = T(L"Do step ") + std::to_wstring(k + 1);
                        float bw = buttonWidth(t);
                        drawButton(RectF(x + w - pad - bw, y + pad, bw, S(28)), t, H_FIX, sel_ * 100 + k, f.danger ? kit::ButtonStyle::Danger : kit::ButtonStyle::Normal);
                    }
                    return std::max(yy, y + pad + S(28)) + pad - S(4) - y;
                };
                float h = body(false);
                card(RectF(x, y, w, h));
                body(true);
                y += h + S(8);
            }
            y += S(8);
        }
        // Loop signs
        if (!in.loopSigns.empty()) {
            drawSectionHeader(RectF(x + S(4), y, w, S(24)), in.loop ? T(L"Why it seems to be stuck in a loop") : T(L"Signs of earlier trouble"));
            y += S(28);
            for (const wstr& s : in.loopSigns) {
                canvas().circle(x + S(10), y + S(8), S(3.5f), in.loop ? kRed : kAmber);
                y += para(s, x + S(22), y, w - S(26), fonts().small, theme().label, true) + S(5);
            }
            y += S(10);
        }
        // Step by step
        drawSectionHeader(RectF(x + S(4), y, w, S(24)), T(L"Step by step, as the PC starts"));
        y += S(28);
        for (int k = 0; k < (int)in.chain.size(); ++k) {
            const Link& l = in.chain[k];
            float nameW = std::min(S(170), w * 0.3f);
            float th = (float)canvas().measureWrapped(l.plain, fonts().small, (int)(w - nameW - S(40)));
            float rh = std::max(S(34), th + S(14));
            RectF rr(x, y, w, rh);
            drawRow(rr, false, true, hovered(H_LINK, k), false, false, false);
            addHit(rr, H_LINK, k);
            canvas().circle(x + S(14), y + S(16), S(5), hc(l.health));
            text(std::to_wstring(k + 1) + L". " + T(l.name), RectF(x + S(26), y + S(6), nameW, S(20)), fonts().uiSemi, theme().label);
            para(l.plain, x + S(30) + nameW, y + S(8), w - nameW - S(40), fonts().small, theme().label2, true);
            y += rh + S(2);
        }
        return y - top + S(10);
    }

    // ---------------- Boot chain
    const Install* chainInstall() const {
        if (rep_.installs.empty()) return &rep_.pcChain;
        return &rep_.installs[sel_ >= 0 && sel_ < (int)rep_.installs.size() ? sel_ : 0];
    }

    float drawChain(float x, float y, float w, float viewH) {
        (void)viewH;
        if (!rep_.installs.empty() && sel_ < 0) sel_ = 0;
        const Install& in = *chainInstall();
        float top = y;
        float pw = pill(x + w, y + S(13), in.verdictTitle, hc(in.verdict));
        text(in.label() + L" · " + in.where, RectF(x, y, w - pw - S(10), S(26)), fonts().title, theme().label);
        y += S(30);
        y += para(T(L"When the PC is switched on, these steps happen one after another. Each step needs the one before it, so the first step with a problem is where starting stops."),
                  x, y, w, fonts().small, theme().label2, true) +
             S(14);
        // Nodes
        int n = (int)in.chain.size();
        float gap = S(30), nw = S(150), nh = S(104);
        int perRow = std::max(1, (int)((w + gap) / (nw + gap)));
        if (perRow > n) perRow = n;
        nw = std::min(S(190), (w - gap * (perRow - 1)) / perRow);
        int rows = (n + perRow - 1) / perRow;
        if (node_ >= n) node_ = n - 1;
        int firstBad = -1;
        for (int k = 0; k < n && firstBad < 0; ++k)
            if (in.chain[k].health == Health::Bad) firstBad = k;
        for (int k = 0; k < n; ++k) {
            int row = k / perRow, col = k % perRow;
            if (row % 2) col = perRow - 1 - col;  // snake, so each arrow stays short
            RectF r(x + col * (nw + gap), y + row * (nh + gap), nw, nh);
            const Link& l = in.chain[k];
            // arrow to the next node
            if (k + 1 < n) {
                int nrow = (k + 1) / perRow, ncol = (k + 1) % perRow;
                if (nrow % 2) ncol = perRow - 1 - ncol;
                RectF nr(x + ncol * (nw + gap), y + nrow * (nh + gap), nw, nh);
                Color ac = k >= firstBad && firstBad >= 0 ? theme().label3 : theme().label2;
                float t = S(1.5f), a = S(5);
                if (nrow == row) {
                    bool right = nr.x > r.x;
                    float x1 = right ? r.right() + S(4) : r.x - S(4), x2 = right ? nr.x - S(4) : nr.right() + S(4), cy = r.cy();
                    canvas().line(x1, cy, x2, cy, t, ac);
                    float d = right ? -a : a;
                    canvas().line(x2, cy, x2 + d, cy - a, t, ac);
                    canvas().line(x2, cy, x2 + d, cy + a, t, ac);
                } else {
                    float cx = r.cx(), y1 = r.bottom() + S(4), y2 = nr.y - S(4);
                    canvas().line(cx, y1, cx, y2, t, ac);
                    canvas().line(cx, y2, cx - a, y2 - a, t, ac);
                    canvas().line(cx, y2, cx + a, y2 - a, t, ac);
                }
            }
            bool sel = k == node_;
            bool hov = hovered(H_NODE, k);
            Color c = hc(l.health);
            float op = firstBad >= 0 && k > firstBad && l.health != Health::Bad ? 0.55f : 1.f;
            canvas().round(r, S(12), theme().field);
            if (hov && !sel) canvas().round(r, S(12), theme().label.withAlpha(0.05f));
            canvas().ring(r, S(12), sel ? S(2.5f) : S(1.5f), sel ? theme().accent : c.withAlpha(0.7f * op));
            canvas().round(RectF(r.x + S(1), r.y + S(8), S(4), r.h - S(16)), S(2), c.withAlpha(op));
            addHit(r, H_NODE, k);
            canvas().circle(r.x + S(22), r.y + S(22), S(11), c.withAlpha(op));
            text(std::to_wstring(k + 1), RectF(r.x + S(11), r.y + S(12), S(22), S(20)), fonts().smallSemi, Color(255, 255, 255), kit::kLine | DT_CENTER);
            drawIcon(l.icon, r.right() - S(32), r.y + S(12), S(20), theme().label2.withAlpha(op), theme().accent);
            para(T(l.name), r.x + S(14), r.y + S(40), r.w - S(24), fonts().uiSemi, theme().label.withAlpha(op), true);
            text(T(healthWord(l.health)), RectF(r.x + S(14), r.bottom() - S(26), r.w - S(24), S(18)), fonts().smallSemi, c.withAlpha(op));
        }
        y += rows * (nh + gap) - gap + S(18);
        // Details of the chosen step
        if (node_ < 0) node_ = firstBad >= 0 ? firstBad : 0;
        const Link& l = in.chain[node_];
        auto body = [&](bool draw) {
            float pad = S(16), cx = x + pad, cw = w - pad * 2, yy = y + pad;
            if (draw) {
                float pw2 = pill(x + w - pad, yy + S(12), T(healthWord(l.health)), hc(l.health));
                text(T(L"Step ") + std::to_wstring(node_ + 1) + L": " + T(l.name), RectF(cx, yy, cw - pw2 - S(10), S(26)), fonts().title, theme().label);
            }
            yy += S(32);
            yy += para(l.plain, cx, yy, cw, fonts().ui, theme().label, draw) + S(8);
            if (!l.cause.empty()) {
                yy += para(T(L"What may have caused this:"), cx, yy, cw, fonts().smallSemi, theme().label, draw) + S(2);
                yy += para(l.cause, cx, yy, cw, fonts().small, theme().label2, draw) + S(10);
            }
            yy += lessonBox(l.lesson, T(L"What is this step?"), cx, yy, cw, draw);
            if (l.aboutSecureBoot) {
                if (draw) drawButton(RectF(cx, yy, buttonWidth(T(L"About Secure Boot")), S(26)), T(L"About Secure Boot"), H_SECUREBOOT, 0, kit::ButtonStyle::Normal);
                yy += S(34);
            }
            if (!l.tech.empty()) {
                yy += para(T(L"For a technician:"), cx, yy, cw, fonts().smallSemi, theme().label3, draw) + S(2);
                yy += para(l.tech, cx, yy, cw, fonts().monoSmall, theme().label2, draw) + S(10);
            }
            if (!l.fixes.empty()) {
                float bx = cx;
                bool first = true;
                for (int fi : l.fixes) {
                    if (fi < 0 || fi >= (int)in.plan.size()) continue;
                    const Fix& f = in.plan[fi];
                    if (!f.available) {
                        yy += para(f.title + T(L": not possible here. ") + f.notAvailable, cx, yy, cw, fonts().small, kRed, draw) + S(6);
                        continue;
                    }
                    float bw = buttonWidth(f.title);
                    if (bx + bw > x + w - pad && bx > cx) {
                        bx = cx;
                        yy += S(34);
                    }
                    if (draw)
                        drawButton(RectF(bx, yy, bw, S(28)), f.title, H_FIX, sel_ * 100 + fi,
                                   f.danger ? kit::ButtonStyle::Danger : first ? kit::ButtonStyle::Primary : kit::ButtonStyle::Normal);
                    first = false;
                    bx += bw + S(8);
                }
                if (bx > cx) yy += S(30);
            }
            return yy + pad - y;
        };
        float h = body(false);
        card(RectF(x, y, w, h));
        body(true);
        y += h + S(10);
        return y - top;
    }

    // ---------------- Disks
    struct MapItem {
        int kind = 0;  // 0 partition, 1 unused space, 2 missing start-up partition
        int index = 0;
        uint64_t length = 0;
    };

    float drawDisks(float x, float y, float w) {
        float top = y;
        if (rep_.m.disks.empty()) {
            drawEmptyState(RectF(x, y, w, S(260)), T(L"No disks were found"), T(L"Windows does not see any disk. Check the cables, or the storage mode (AHCI, RAID or VMD) in the PC's firmware settings, or load the disk's storage driver."));
            float bw = buttonWidth(T(L"Load a storage driver…"));
            drawButton(RectF(x + (w - bw) / 2, y + S(260), bw, S(30)), T(L"Load a storage driver…"), H_LOAD_DRIVER, 0, kit::ButtonStyle::Primary);
            return S(310);
        }
        if (preview_.on) {
            float h = previewCard(x, y, w, false);
            card(RectF(x, y, w, h));
            canvas().ring(RectF(x, y, w, h), S(10), S(1.5f), kAmber);
            previewCard(x, y, w, true);
            y += h + S(16);
        }
        {
            float bw = buttonWidth(T(L"   Move Windows to another drive…")) + S(10);
            RectF b(x, y, bw, S(30));
            drawButton(b, T(L"   Move Windows to another drive…"), H_MIG_OPEN, 0, kit::ButtonStyle::Normal);
            drawIcon("swap", b.x + S(8), b.cy() - S(8), S(16), theme().label2);
            y += S(42);
        }
        if (showLessons_) {
            y += lessonBox(T(L"Click a disk's name, a partition or unused space to see what can be done with it. Everything that changes the disk is explained first, asks before it starts, and backs up the partition table, so most changes can be undone (Ctrl+Z)."),
                           T(L"Working with disks"), x, y, w, true);
        }
        for (int grp = 0; grp < DG_COUNT; ++grp) {
          bool anyInGroup = false;
          for (const Disk& d : rep_.m.disks) anyInGroup |= d.group == grp;
          for (const OtherDrive& o : rep_.m.others) anyInGroup |= o.group == grp;
          if (!anyInGroup) continue;
          drawSectionHeader(RectF(x + S(4), y, w, S(24)), T(diskGroupName(grp)));
          y += S(30);
          for (const Disk& d : rep_.m.disks) {
            if (d.group != grp) continue;
            wstr head = d.name() + L" · " + d.typeText + L" · " + (d.style == 2 ? L"GPT" : d.style == 1 ? L"MBR" : T(L"No partition table"));
            {
                auto dv = driverDisks_.find(d.id());
                if (dv != driverDisks_.end()) head += L" · " + TF(L"found with the driver {0}", dv->second);
            }
            float pw = 0;
            if (d.health.known && d.health.level == 2) pw = pill(x + w, y + S(12), T(L"Failing (S.M.A.R.T.)"), kRed);
            else if (!d.problems.empty()) pw = pill(x + w, y + S(12), plural(d.problems.size(), T(L"problem"), T(L"problems")), kRed);
            else if (d.health.known && d.health.level == 1) pw = pill(x + w, y + S(12), T(L"Wear (S.M.A.R.T.)"), kAmber);
            else pw = pill(x + w, y + S(12), T(L"Fine"), kGreen);
            RectF hr(x - S(4), y - S(2), w - pw - S(20), S(28));
            bool hsel = dsKind_ == SEL_DISK && dsDisk_ == d.number;
            if (hsel) canvas().round(hr, S(6), theme().accent.withAlpha(0.14f));
            else if (hovered(H_DISKHEAD, d.number)) canvas().round(hr, S(6), theme().label.withAlpha(0.05f));
            addHit(hr, H_DISKHEAD, d.number);
            drawIcon(d.removable ? "drive_usb" : "disk_hw", x, y + S(2), S(20), theme().label2, theme().accent);
            text(head, RectF(x + S(28), y, w - pw - S(48), S(24)), fonts().uiSemi, theme().label);
            y += S(30);
            // The map: partitions and unused space in disk order
            std::vector<MapItem> items;
            std::vector<std::pair<uint64_t, MapItem>> ordered;
            for (int i : d.parts)
                if (rep_.m.parts[i].role != R_EXTENDED) ordered.push_back({rep_.m.parts[i].offset, MapItem{0, i, rep_.m.parts[i].length}});
            for (size_t g = 0; g < d.gaps.size(); ++g)
                if (d.gaps[g].second >= (16ull << 20)) ordered.push_back({d.gaps[g].first, MapItem{1, (int)g, d.gaps[g].second}});
            std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            for (auto& o : ordered) items.push_back(o.second);
            bool ghost = false;
            for (const Install& in : rep_.installs)
                if (!in.linux && in.atRoot && rep_.m.parts[in.part].disk == d.number && in.sysPart < 0 && !in.running) ghost = true;
            if (ghost) items.push_back(MapItem{2, 0, 0});
            RectF bar(x, y, w, S(74));
            if (items.empty()) {
                canvas().round(bar, S(8), theme().field);
                text(d.style == 0 ? T(L"Not set up: no partition table. Click the disk's name to set it up.") : T(L"No partitions"), bar, fonts().ui, theme().label3, kit::kLine | DT_CENTER);
            } else {
                int count = (int)items.size();
                float minW = S(84), gapW = S(4), avail = w - gapW * (count - 1);
                double total = 0;
                for (const MapItem& it : items) total += (double)it.length;
                if (ghost) total += total * 0.04;
                std::vector<float> ws;
                for (const MapItem& it : items) ws.push_back(it.kind == 2 ? (float)(0.04 / 1.04) * avail : (float)(it.length / total) * avail);
                float need = 0, big = 0;
                for (float& v : ws)
                    if (v < minW) need += minW - v;
                    else big += v - minW;
                for (float& v : ws) v = v < minW ? minW : (big > 0 ? v - (v - minW) / big * std::min(need, big) : v);
                float bx = x;
                std::vector<RectF> rects;
                std::vector<uint64_t> starts;
                for (const auto& o : ordered) starts.push_back(o.first);
                for (int k = 0; k < count; ++k) {
                    RectF b(bx, bar.y, ws[k], bar.h);
                    bx += ws[k] + gapW;
                    rects.push_back(b);
                    const MapItem& it = items[k];
                    if (it.kind == 2) {
                        canvas().round(b, S(8), kRed.withAlpha(0.08f));
                        canvas().ring(b, S(8), S(1.5f), kRed);
                        addHit(b, H_GHOST, d.number);
                        para(T(L"Missing: start-up partition"), b.x + S(8), b.y + S(10), b.w - S(14), fonts().smallSemi, kRed, true);
                        continue;
                    }
                    if (it.kind == 1) {
                        int code = d.number * 1000 + it.index;
                        bool sel = dsKind_ == SEL_GAP && dsDisk_ == d.number && dsGap_ == it.index;
                        canvas().round(b, S(8), theme().label.withAlpha(hovered(H_GAP, code) ? 0.06f : 0.03f));
                        canvas().ring(b, S(8), sel ? S(2.5f) : S(1), sel ? theme().accent : theme().separator);
                        addHit(b, H_GAP, code);
                        text(T(L"Unused space"), RectF(b.x + S(8), b.y + S(10), b.w - S(14), S(18)), fonts().smallSemi, theme().label2);
                        text(sizeText(it.length), RectF(b.x + S(8), b.y + S(28), b.w - S(14), S(16)), fonts().small, theme().label3);
                        continue;
                    }
                    const Partition& p = rep_.m.parts[it.index];
                    bool sel = dsKind_ == SEL_PART && dsPart_ == it.index;
                    bool problem = p.pastEnd || p.overlaps || p.locked || p.dirty || (p.role == R_DATA && !p.readable && !p.bitlocker && p.rawFs.empty());
                    Color stripe = p.role == R_ESP || p.role == R_SYSTEM ? theme().accent : p.role == R_DATA ? kBlue : theme().label3;
                    canvas().round(b, S(8), theme().field);
                    if (hovered(H_PART, it.index)) canvas().round(b, S(8), theme().label.withAlpha(0.05f));
                    canvas().round(RectF(b.x, b.y, b.w, S(5)), S(8), S(8), 0, 0, stripe);
                    if (sel) canvas().ring(b, S(8), S(2.5f), theme().accent);
                    else if (problem) canvas().ring(b, S(8), S(1.5f), p.locked ? kAmber : kRed);
                    addHit(b, H_PART, it.index);
                    wstr name = !p.letter.empty() ? p.letter + (p.label.empty() ? L"" : L" " + p.label) : wstr(T(roleShort(p.role)));
                    for (const Install& in : rep_.installs)
                        if (in.part == it.index) name += L" · " + in.name;
                    text(name, RectF(b.x + S(8), b.y + S(10), b.w - S(14), S(18)), fonts().smallSemi, theme().label);
                    text(sizeText(p.length) + (p.fs.empty() ? (p.rawFs.empty() ? L"" : L" · " + p.rawFs) : L" · " + p.fs),
                         RectF(b.x + S(8), b.y + S(28), b.w - S(14), S(16)), fonts().small, theme().label2);
                    if (p.readable && p.totalBytes && b.w > S(60))
                        drawUsageBar(RectF(b.x + S(8), b.bottom() - S(16), b.w - S(16), S(6)), 1.f - (float)p.freeBytes / p.totalBytes, false, false);
                    else if (!p.readable && p.role != R_MSR)
                        text(p.locked ? T(L"Locked") : p.role == R_LINUX || (!p.rawFs.empty() && p.rawFs != L"BitLocker") ? T(L"Linux file system") : T(L"Not readable"),
                             RectF(b.x + S(8), b.bottom() - S(22), b.w - S(14), S(16)), fonts().small, p.role == R_DATA || p.locked ? (p.locked ? kAmber : kRed) : theme().label3);
                }
                drawBadMarks(d, items, starts, rects);
            }
            y += bar.h + S(8);
            y += drawBadSummary(d, x, y, w);
            for (const wstr& pr : d.problems) {
                canvas().circle(x + S(5), y + S(8), S(3.5f), kRed);
                y += para(pr, x + S(16), y, w - S(16), fonts().small, theme().label, true) + S(4);
            }
            // What can be done with the selection on this disk
            if (dsDisk_ == d.number && dsKind_ != SEL_NONE) {
                float h = diskPanel(d, x, y, w, false);
                card(RectF(x, y, w, h));
                diskPanel(d, x, y, w, true);
                y += h + S(6);
            }
            if (lostMem_.count(d.id()) && !lostHidden_.count(d.id())) {
                float h = lostCard(d, x, y, w, false);
                card(RectF(x, y, w, h));
                lostCard(d, x, y, w, true);
                y += h + S(6);
            }
            y += S(22);
        }
          // drives that are not disks to partition: optical, floppy, network
          for (const OtherDrive& o : rep_.m.others) {
              if (o.group != grp) continue;
              drawIcon(o.group == DG_OPTICAL ? "drive_cd" : o.group == DG_ENTERPRISE ? "drive_net" : "drive", x, y + S(2), S(20), theme().label2, theme().accent);
              wstr t = o.letter + (o.label.empty() ? wstr() : L" " + o.label) + (o.model.empty() ? wstr() : L" · " + o.model) + L" · " + o.typeText +
                       (o.hasMedia && o.size ? L" · " + sizeText(o.size) + (o.fs.empty() ? wstr() : L" · " + o.fs) : wstr());
              text(t, RectF(x + S(28), y, w - S(32), S(24)), fonts().ui, theme().label);
              y += S(34);
          }
          y += S(6);
        }
        y += para(T(L"Colours: blue top edge = data, accent = start-up partition, grey = reserved or recovery. A red outline marks a partition with a problem. Right-click for a menu."),
                  x, y, w, fonts().small, theme().label3, true);
        return y - top + S(10);
    }

    wstr selectionTitle(const Disk& d) const {
        if (dsKind_ == SEL_DISK) return d.name() + L" · " + (d.style == 2 ? L"GPT" : d.style == 1 ? L"MBR" : T(L"not set up"));
        if (dsKind_ == SEL_GAP && dsGap_ >= 0 && dsGap_ < (int)d.gaps.size()) return T(L"Unused space · ") + sizeText(d.gaps[dsGap_].second) + T(L" on Disk ") + std::to_wstring(d.number);
        if (dsKind_ == SEL_PART && dsPart_ >= 0 && dsPart_ < (int)rep_.m.parts.size()) {
            const Partition& p = rep_.m.parts[dsPart_];
            return p.name() + L" · " + sizeText(p.length) + (p.fs.empty() ? (p.rawFs.empty() ? L"" : L" · " + p.rawFs) : L" · " + p.fs) + L" · " + p.where();
        }
        return L"";
    }

    float diskPanel(const Disk& d, float x, float y, float w, bool draw) {
        float pad = S(14), cx = x + pad, cw = w - pad * 2, yy = y + pad;
        if (draw) text(selectionTitle(d), RectF(cx, yy, cw, S(22)), fonts().uiSemi, theme().label);
        yy += S(26);
        if (dsKind_ == SEL_DISK) {
            wstr ht = d.health.known ? T(L"Health (") + d.health.kind + L"): " + d.health.summary : T(L"Health: not reported (USB adapters and RAID controllers often hide it)");
            yy += para(ht, cx, yy, cw, fonts().small, d.health.level == 2 ? kRed : d.health.level == 1 ? kAmber : theme().label2, draw) + S(4);
            for (const wstr& pr : d.health.problems) yy += para(L"- " + pr, cx, yy, cw, fonts().small, theme().label, draw) + S(2);
            {
                auto st = surface_.find(d.id());
                if (st != surface_.end()) yy += para(T(L"Surface test: ") + st->second, cx, yy, cw, fonts().small, theme().label2, draw) + S(2);
            }
            yy += S(6);
        }
        if (dsKind_ == SEL_PART && dsPart_ >= 0) {
            const Partition& p = rep_.m.parts[dsPart_];
            yy += para(wstr(T(roleName(p.role))) + (p.totalBytes ? L" · " + sizeText(p.freeBytes) + T(L" free") : wstr()) + (p.active ? T(L" · active") : L"") +
                           (p.bitlocker ? (p.locked ? T(L" · BitLocker, locked") : T(L" · BitLocker")) : L""),
                       cx, yy, cw, fonts().small, theme().label2, draw) +
                  S(8);
        }
        auto ops = diskOpsFor(rep_, dsKind_, dsDisk_, dsPart_, dsGap_);
        float bx = cx;
        wstr notHere;
        for (const auto& op : ops) {
            DiskOpInfo info = diskOpInfo(op.first);
            if (!op.second.empty()) {
                notHere += (notHere.empty() ? L"" : L"\n") + info.title + L": " + op.second;
                continue;
            }
            float bw = buttonWidth(info.title);
            if (bx + bw > cx + cw && bx > cx) {
                bx = cx;
                yy += S(34);
            }
            if (draw) drawButton(RectF(bx, yy, bw, S(28)), info.title, H_DOP, op.first, info.danger ? kit::ButtonStyle::Danger : kit::ButtonStyle::Normal);
            bx += bw + S(8);
        }
        if (bx > cx) yy += S(36);
        if (!notHere.empty()) yy += para(T(L"Not possible here:\n") + notHere, cx, yy, cw, fonts().small, theme().label3, draw) + S(4);
        return yy + pad - S(6) - y;
    }

    // ---------------- Disk operations
    void askDiskOp(int kind) {
        if (busy_ || !haveReport_) return;
        DiskOpArgs a;
        a.kind = kind;
        a.disk = dsDisk_;
        a.part = dsKind_ == SEL_PART ? dsPart_ : -1;
        const Disk* d = nullptr;
        for (const Disk& x : rep_.m.disks)
            if (x.number == dsDisk_) d = &x;
        if (!d) return;
        for (const auto& op : diskOpsFor(rep_, dsKind_, dsDisk_, dsPart_, dsGap_))
            if (op.first == kind && !op.second.empty()) {
                showMessage(diskOpInfo(kind).title + T(L" is not possible here"), op.second);
                return;
            }
        using F = kit::FormField;
        auto number = [](int id, const wstr& label, int v, int mn, int mx, const wstr& tip) {
            F f;
            f.kind = F::Number; f.id = id; f.label = label; f.value = v; f.min = mn; f.max = mx; f.step = mx > 10000 ? 1024 : 100; f.unit = L"MB"; f.tip = tip;
            return f;
        };
        auto choice = [](int id, const wstr& label, int v, std::vector<wstr> opts, const wstr& tip) {
            F f;
            f.kind = F::Choice; f.id = id; f.label = label; f.value = v; f.options = opts; f.tip = tip;
            return f;
        };
        auto toggle = [](int id, const wstr& label, int v, const wstr& tip) {
            F f;
            f.kind = F::Toggle; f.id = id; f.label = label; f.value = v; f.tip = tip;
            return f;
        };
        auto note = [](const wstr& t) {
            F f;
            f.kind = F::Note; f.label = t;
            return f;
        };
        std::vector<wstr> letters = freeLetters();
        switch (kind) {
            case DO_UNLOCK: {
                Fix u = makeFix(FX_UNLOCK);
                u.part = a.part;
                u.disk = a.disk;
                askFixList({u}, rep_.m.parts[a.part].name());
                return;
            }
            case DO_FIND_LOST:
                showConfirm(diskOpInfo(kind).title + L"?", diskOpInfo(kind).what + L"\n\n" + diskOpInfo(kind).lesson,
                            {T(L"Cancel"), T(L"Quick search"), T(L"Full search")}, 1, 1, false, [this, a](int b) {
                                if (b >= 1) startLostScan(a.disk, b == 2);
                            });
                return;
            case DO_RECOVER_FILES:
                showConfirm(diskOpInfo(kind).title + L"?", diskOpInfo(kind).what + L"\n\n" + diskOpInfo(kind).lesson,
                            {T(L"Cancel"), T(L"Quick search"), T(L"Full search")}, 1, 1, false, [this, a](int b) {
                                if (b >= 1) startFileScan(a.part, b == 2);
                            });
                return;
            case DO_MERGE_NEXT:
                openPreview(a.part, 1);
                return;
            case DO_SURFACE:
                showConfirm(diskOpInfo(kind).title + L"?", diskOpInfo(kind).what + L"\n\n" + diskOpInfo(kind).lesson, {T(L"Cancel"), T(L"Test")}, 1, 1, false, [this, a](int b) {
                    if (b == 1) startSurfaceTest(a.disk);
                });
                return;
            case DO_LAYOUT:
                for (int i = 0; i < (int)rep_.installs.size(); ++i)
                    if (!rep_.installs[i].linux && rep_.installs[i].atRoot && rep_.m.parts[rep_.installs[i].part].disk == a.disk) {
                        openLayout(i);
                        return;
                    }
                return;
            case DO_CREATE: {
                if (dsGap_ < 0 || dsGap_ >= (int)d->gaps.size()) return;
                a.offset = (d->gaps[dsGap_].first + (1ull << 20) - 1) / (1ull << 20) * (1ull << 20);
                int maxMB = (int)std::min<uint64_t>((d->gaps[dsGap_].first + d->gaps[dsGap_].second - a.offset) >> 20, 0x7FFFFFFF);
                std::vector<F> f = {number(1, T(L"Size"), maxMB, 1, maxMB, T(L"How big the new partition is. All of the unused space is chosen at first.")),
                                    choice(2, T(L"Kind"), 0, d->style == 2 ? std::vector<wstr>{T(L"Data"), T(L"Start-up (EFI)"), T(L"Recovery"), T(L"Reserved")} : std::vector<wstr>{T(L"Data"), T(L"Start-up (EFI)"), T(L"Recovery")},
                                           T(L"Data: an ordinary drive. Start-up: where a PC starts from (FAT32, about 100-300 MB). Recovery: hidden, for repair tools.")),
                                    choice(3, T(L"File system"), 0, {L"NTFS", L"FAT32", L"exFAT", T(L"None")}, T(L"NTFS for Windows drives; FAT32 for start-up partitions and devices; exFAT for USB sticks.")),
                                    toggle(4, T(L"Quick format"), 1, T(L"A quick format takes seconds. A full one also checks every sector, which takes long on a big disk.")),
                                    toggle(5, T(L"Give it a drive letter"), letters.empty() ? 0 : 1, T(L"So it shows up in File Explorer"))};
                showForm(T(L"Create a partition"), f, T(L"Next"), [this, a, letters](const std::vector<F>& r) mutable {
                    for (const F& x : r) {
                        if (x.id == 1) a.sizeMB = (uint64_t)x.value;
                        if (x.id == 2) a.type = x.value;
                        if (x.id == 3) a.fs = x.value;
                        if (x.id == 4) a.quick = x.value != 0;
                        if (x.id == 5 && x.value && !letters.empty()) a.letter = letters[0];
                    }
                    if (a.type != 0) a.letter.clear();
                    askLabel(a, a.type == 1 ? T(L"System") : a.type == 2 ? T(L"Recovery") : T(L"New volume"));
                });
                return;
            }
            case DO_FORMAT: {
                const Partition& p = rep_.m.parts[a.part];
                int cur = p.fs == L"FAT32" ? 1 : p.fs == L"exFAT" ? 2 : 0;
                std::vector<F> f = {choice(3, T(L"File system"), cur, {L"NTFS", L"FAT32", L"exFAT"}, T(L"NTFS for Windows drives; FAT32 for start-up partitions and devices; exFAT for USB sticks.")),
                                    toggle(4, T(L"Quick format"), 1, T(L"A quick format takes seconds. A full one also checks every sector, which takes long on a big disk.")),
                                    note(T(L"Everything on ") + p.name() + T(L" will be erased."))};
                f.back().danger = true;
                showForm(T(L"Format ") + p.name(), f, T(L"Next"), [this, a, p](const std::vector<F>& r) mutable {
                    for (const F& x : r) {
                        if (x.id == 3) a.fs = x.value;
                        if (x.id == 4) a.quick = x.value != 0;
                    }
                    askLabel(a, p.label.empty() ? wstr(T(L"New volume")) : p.label);
                });
                return;
            }
            case DO_EXTEND: {
                const Partition& p = rep_.m.parts[a.part];
                uint64_t after = 0;
                for (const auto& g : d->gaps)
                    if (g.first >= p.offset + p.length && g.first - (p.offset + p.length) < (1ull << 20)) after = g.second;
                int maxMB = (int)std::min<uint64_t>(after >> 20, 0x7FFFFFFF);
                showForm(T(L"Make ") + p.name() + T(L" bigger"), {number(1, T(L"Add"), maxMB, 1, maxMB, T(L"How much of the unused space after it to add."))}, T(L"Next"),
                         [this, a](const std::vector<F>& r) mutable {
                             for (const F& x : r)
                                 if (x.id == 1) a.sizeMB = (uint64_t)x.value;
                             showPreview(a);
                         });
                return;
            }
            case DO_SHRINK: {
                const Partition& p = rep_.m.parts[a.part];
                int maxMB = (int)std::min<uint64_t>((p.freeBytes / 10 * 8) >> 20, 0x7FFFFFFF);
                showForm(T(L"Make ") + p.name() + T(L" smaller"),
                         {number(1, T(L"Give up"), std::min(maxMB, 10240), 1, maxMB, L"How much space to free at its end. At most 80 % of its free space is offered."),
                          note(T(L"Windows can only give up free space that lies at the end of the drive. If it refuses, try a smaller amount."))},
                         T(L"Next"), [this, a](const std::vector<F>& r) mutable {
                             for (const F& x : r)
                                 if (x.id == 1) a.sizeMB = (uint64_t)x.value;
                             showPreview(a);
                         });
                return;
            }
            case DO_LETTER: {
                if (letters.empty()) {
                    showMessage(T(L"No drive letter is free"), T(L"All letters from D: to Z: are in use. Remove a letter from another drive first."));
                    return;
                }
                wstr list;
                for (const wstr& l : letters) list += l + L" ";
                showInput(T(L"Change the drive letter"), T(L"Type the new letter. Free letters: ") + list, letters[0].substr(0, 1), T(L"Next"), [this, a, letters](const wstr& t) mutable {
                    wstr l = t.empty() ? wstr() : wstr(1, (wchar_t)towupper(t[0])) + L":";
                    if (std::find(letters.begin(), letters.end(), l) == letters.end()) {
                        showMessage(T(L"That letter cannot be used"), T(L"Choose one of the free letters."));
                        return true;
                    }
                    a.letter = l;
                    confirmDiskOp(a);
                    return true;
                });
                return;
            }
            case DO_LABEL: {
                const Partition& p = rep_.m.parts[a.part];
                showInput(T(L"Rename ") + p.name(), T(L"The name shown next to the drive letter. NTFS allows 32 characters, FAT32 11."), p.label, T(L"Rename"), [this, a](const wstr& t) mutable {
                    a.label = t;
                    startDiskOp(a);  // harmless and undoable: no extra question
                    return true;
                });
                return;
            }
            case DO_CLEAN: {
                showInput(T(L"Wipe ") + d->name() + L"?",
                          T(L"This removes every partition on the disk, so all its drives disappear. To make sure the right disk is wiped, type its number (") + std::to_wstring(d->number) +
                              T(L") below.\n\nThe partition table is backed up first, so Undo can bring the partitions back if nothing new is written to the disk."),
                          L"", T(L"Wipe the disk"), [this, a](const wstr& t) {
                              if (t != std::to_wstring(a.disk)) {
                                  showMessage(T(L"Nothing was wiped"), T(L"The number typed is not this disk's number."));
                                  return true;
                              }
                              startDiskOp(a);
                              return true;
                          });
                return;
            }
            default: confirmDiskOp(a);
        }
    }

    void askLabel(DiskOpArgs a, const wstr& suggestion) {
        if (a.fs == 3 || a.type == 3) {
            confirmDiskOp(a);
            return;
        }
        showInput(T(L"Name of the drive"), T(L"The name shown next to the drive letter (it can be changed later)."), suggestion, T(L"Next"), [this, a](const wstr& t) mutable {
            a.label = t;
            confirmDiskOp(a);
            return true;
        });
    }

    void confirmDiskOp(const DiskOpArgs& a) {
        // a failing disk: offer a backup before anything writes to it (reading it is fine)
        if (!healthWarned_ && a.kind != DO_FIND_LOST && a.kind != DO_RECOVER_FILES && a.kind != DO_SURFACE)
            for (const Disk& dk : rep_.m.disks)
                if (dk.number == a.disk && diskUnhealthy(dk)) {
                    showDiskHealthWarning(a.disk, [this, a] {
                        healthWarned_ = true;
                        confirmDiskOp(a);
                        healthWarned_ = false;
                    });
                    return;
                }
        DiskOpInfo info = diskOpInfo(a.kind);
        wstr msg = info.what + L"\n\n";
        if (!info.lesson.empty()) msg += info.lesson + L"\n\n";
        if (info.losesData) {
            wstr holds;
            for (const Install& in : rep_.installs)
                if ((a.part >= 0 && (in.part == a.part || in.bootPart == a.part)) || (a.part < 0 && rep_.m.parts[in.part].disk == a.disk))
                    holds += (holds.empty() ? L"" : L", ") + in.label();
            if (!holds.empty()) msg += T(L"Installed here: ") + holds + L". It will no longer start, and its files will be gone.\n\n";
            msg += T(L"Everything on it will be lost. Copy anything you need somewhere else first.\n\n");
        }
        for (const Disk& dk : rep_.m.disks)
            if (dk.number == a.disk && dk.health.known && dk.health.level > 0 && a.kind != DO_FIND_LOST && a.kind != DO_RECOVER_FILES)
                msg += (dk.health.level == 2 ? T(L"Warning: this disk reports that it may be failing (S.M.A.R.T.). Copy the files you cannot lose to another disk before changing anything on it.")
                                             : T(L"Note: this disk shows signs of wear (S.M.A.R.T.). Make sure your files are backed up.")) + L"\n\n";
        if ((info.danger || info.losesData) && anyLockedDrive(rep_))
            msg += T(L"Careful: a drive on this PC is locked by BitLocker, so what is inside it cannot be seen. Unlock it first if it may hold Windows or files you need.\n\n");
        msg += info.undoable ? T(L"The partition table is backed up first; Undo (Ctrl+Z) or History can put it back.")
                             : T(L"The partition table is backed up first, but this change itself cannot be undone.");
        wstr script = diskpartScript(a, rep_);
        if (!script.empty()) msg += T(L"\n\nFor a technician (diskpart):\n") + script;
        showConfirm(info.title + L"?", msg, {T(L"Cancel"), info.title}, 1, 0, info.danger || info.losesData, [this, a](int b) {
            if (b == 1) startDiskOp(a);
        });
    }

    void startDiskOp(const DiskOpArgs& a) {
        countUse(L"Disk operations: " + diskOpInfo(a.kind).title);
        lastDiskOp_ = a;
        if (busy_ || snapshot_) return;
        if (job_.joinable()) job_.join();
        if (!journal_.isOpen()) {
            showMessage(T(L"No backup folder"), T(L"Nothing was changed, because there is no folder to keep backups in. Choose one with the settings button."));
            return;
        }
        busy_ = true;
        cancel_ = false;
        setProgress(diskOpInfo(a.kind).title, -1.f, selectionTitleFor(a), a.kind == DO_CHKDSK);
        auto copy = std::make_shared<Report>(rep_);
        job_ = std::thread([this, copy, a] {
            noRedirection();
            FixOutcome o = runDiskOp(a, *copy, journal_, &cancel_);
            {
                std::lock_guard<std::mutex> l(lock_);
                jobOk_ = o.ok;
                jobNote_ = (o.ok ? T(L"Done: ") : T(L"Failed: ")) + diskOpInfo(a.kind).title + L"\n" + o.result;
                jobDirs_.clear();
                if (o.ok && o.undoable && !o.actionDir.empty()) jobDirs_.push_back(o.actionDir);
            }
            PostMessageW(hwnd(), WM_BD_FIXED, 1, 0);
        });
        invalidate();
    }

    wstr selectionTitleFor(const DiskOpArgs& a) const {
        if (a.part >= 0 && a.part < (int)rep_.m.parts.size()) return rep_.m.parts[a.part].name();
        return T(L"Disk ") + std::to_wstring(a.disk);
    }

    void diskMenu(int x, int y) {
        std::vector<kit::MenuItem> items;
        for (const auto& op : diskOpsFor(rep_, dsKind_, dsDisk_, dsPart_, dsGap_)) {
            if (!op.second.empty()) continue;
            DiskOpInfo info = diskOpInfo(op.first);
            kit::MenuItem m;
            m.text = info.title;
            m.cmd = C_DISKOP;
            m.arg = op.first;
            m.danger = info.danger;
            m.tip = info.what;
            items.push_back(m);
        }
        if (!items.empty()) showMenu(items, (float)x, (float)y);
    }

    // ---------------- Console
    void consoleAdd(const wstr& text) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        wchar_t ts[16];
        swprintf(ts, 16, L"%02d:%02d:%02d  ", t.wHour, t.wMinute, t.wSecond);
        {
            std::lock_guard<std::mutex> l(consoleLock_);
            bool first = true;
            for (size_t a = 0; a <= text.size();) {
                size_t b = text.find(L'\n', a);
                if (b == wstr::npos) b = text.size();
                wstr line = text.substr(a, b - a);
                if (!line.empty() && line.back() == L'\r') line.pop_back();
                if (!line.empty() || !first) console_.push_back((first ? wstr(ts) : wstr(10, L' ')) + line);
                first = false;
                a = b + 1;
            }
            while (console_.size() > 5000) console_.pop_front();
            if (consolePosted_) return;
            consolePosted_ = true;
        }
        if (hwnd()) PostMessageW(hwnd(), WM_BD_CONSOLE, 0, 0);
    }

    void drawConsole(const RectF& r) {
        conRect_ = r;
        canvas().fill(r, theme().window);
        splitRect_ = RectF(r.x, r.y - S(3), r.w, S(7));
        canvas().fill(RectF(r.x, r.y, r.w, 1), theme().separator);
        if (hovered(H_CONSOLE_SPLIT) || dragging_) canvas().fill(RectF(r.x, r.y - S(1), r.w, S(3)), theme().accent.withAlpha(0.6f));
        addHit(splitRect_, H_CONSOLE_SPLIT);
        RectF head(r.x + S(12), r.y + S(6), r.w - S(24), S(24));
        size_t n;
        {
            std::lock_guard<std::mutex> l(consoleLock_);
            n = console_.size();
        }
        text(T(L"Console · commands this program runs"), RectF(head.x, head.y, head.w * 0.6f, head.h), fonts().smallSemi, theme().label2);
        RectF close(head.right() - S(24), head.y, S(24), S(24));
        canvas().round(close, S(6), hovered(H_CONSOLE_CLOSE) ? theme().label.withAlpha(0.08f) : Color());
        drawIcon("close", close.x + S(4), close.y + S(4), S(16), theme().label2);
        addHit(close, H_CONSOLE_CLOSE);
        float cw = buttonWidth(T(L"Clear"));
        drawButton(RectF(close.x - S(8) - cw, head.y, cw, S(24)), T(L"Clear"), H_CONSOLE_CLEAR, 0, kit::ButtonStyle::Quiet);
        float pw = buttonWidth(T(L"Copy"));
        drawButton(RectF(close.x - S(16) - cw - pw, head.y, pw, S(24)), T(L"Copy"), H_CONSOLE_COPY, 0, kit::ButtonStyle::Quiet);
        RectF body(r.x + S(12), head.bottom() + S(4), r.w - S(24), r.bottom() - head.bottom() - S(10));
        canvas().round(body, S(6), theme().field);
        addHit(body, H_CONSOLE);
        float lh = S(16);
        int vis = std::max(1, (int)((body.h - S(8)) / lh));
        consoleScroll_ = std::max(0, std::min(consoleScroll_, (int)n - vis));
        canvas().pushClip(body);
        std::lock_guard<std::mutex> l(consoleLock_);
        if (console_.empty())
            text(T(L"Nothing has run yet. Every command a scan, repair or disk change runs shows up here, with what it answered."),
                 RectF(body.x + S(8), body.y + S(4), body.w - S(16), lh), fonts().small, theme().label3);
        int last = (int)console_.size() - consoleScroll_;
        int first = std::max(0, last - vis);
        float yy = body.y + S(4);
        for (int i = first; i < last; ++i, yy += lh) {
            const wstr& s = console_[i];
            bool cmd = s.size() > 10 && (s[10] == L'>' || s[10] == L'[');
            text(s, RectF(body.x + S(8), yy, body.w - S(16), lh), fonts().monoSmall, cmd ? theme().accentText : theme().label2);
        }
        canvas().popClip();
    }

    // ---------------- Settings, language packs, About
    void settingsMenu() {
        std::vector<kit::MenuItem> items;
        auto item = [&](const wstr& t, int cmd, const wstr& tip) {
            kit::MenuItem m;
            m.text = t;
            m.cmd = cmd;
            m.tip = tip;
            items.push_back(m);
        };
        item(T(L"Settings…"), C_SETTINGS, T(L"Technician mode, console, explanations and language"));
        item(T(L"Make a template for a new language…"), C_LANGTEMPLATE, T(L"Save every text of this program in a file, ready to translate into another language"));
        item(T(L"Add a language pack…"), C_LANGPACK, T(L"Add your own translation, or a corrected one, from a .lang file"));
        item(T(L"Check for updates…"), C_UPDATE_CHECK, T(L"Look for a newer version of this program and install it"));
        item(T(L"Update from a file…"), C_UPDATE_FILE, T(L"Install a newer PATioBootDoctor_x64.exe you downloaded yourself (for PCs without internet)"));
        item(T(L"Update address…"), C_UPDATE_URL, T(L"Where updates are looked for"));
        item(T(L"Usage statistics"), C_STATS, T(L"How often each screen, repair and tool was used, and for how long"));
        kit::MenuItem sep;
        sep.separator = true;
        items.push_back(sep);
        item(T(L"About PATio Boot Doctor"), C_ABOUT, T(L"Version, licence and contact"));
        POINT p = mousePos();
        showMenu(items, (float)p.x - S(200), (float)p.y + S(16));
    }

    void openSettings() {
        using F = kit::FormField;
        std::vector<F> f;
        F sec;
        sec.kind = F::Section;
        sec.label = T(L"Display");
        f.push_back(sec);
        F a;
        a.kind = F::Toggle; a.id = 1; a.label = T(L"Show the command console"); a.value = consoleOn_;
        a.tip = T(L"A panel at the bottom that shows every command this program runs (diskpart, bcdedit, ...) and what it answered");
        f.push_back(a);
        F b;
        b.kind = F::Toggle; b.id = 2; b.label = T(L"Show explanations for beginners"); b.value = showLessons_;
        b.tip = T(L"Short \"What is this?\" boxes next to each step and finding");
        f.push_back(b);
        F tm;
        tm.kind = F::Toggle; tm.id = 4; tm.label = T(L"Technician mode"); tm.value = techMode_;
        tm.tip = T(L"For people who repair PCs: shorter screens without the beginner explanations, compact lists, and the command console on");
        f.push_back(tm);
        F ls;
        ls.kind = F::Section;
        ls.label = T(L"Language");
        f.push_back(ls);
        F lang;
        lang.kind = F::Choice;
        lang.id = 3;
        lang.label = T(L"Language");
        lang.tip = T(L"The language of this program. Add your own with the Settings menu > Add a language pack.");
        std::vector<i18n::LangInfo> langs = i18n::languages();
        for (size_t k = 0; k < langs.size(); ++k) {
            lang.options.push_back(langs[k].name);
            lang.optionTips.push_back(langs[k].builtIn ? langs[k].name : langs[k].name + L" · " + langs[k].file);
            if (langs[k].code == i18n::current()) lang.value = (int)k;
        }
        f.push_back(lang);
        showForm(T(L"Settings"), f, T(L"Save"), [this](const std::vector<F>& r) {
            std::vector<i18n::LangInfo> langs = i18n::languages();
            for (const F& x : r) {
                if (x.id == 1) consoleOn_ = x.value != 0;
                if (x.id == 2) showLessons_ = x.value != 0;
                if (x.id == 4 && (x.value != 0) != techMode_) setMode(x.value != 0);
                if (x.id == 3 && x.value >= 0 && x.value < (int)langs.size() && langs[x.value].code != i18n::current()) applyLanguage(langs[x.value].code, true);
            }
            settings().setInt(L"console", consoleOn_ ? 1 : 0);
            settings().setInt(L"explanations", showLessons_ ? 1 : 0);
            invalidate();
        });
    }

    // Picks the language: the one saved, else Windows' own when a pack exists, else English.
    void applyLanguage(const wstr& wanted, bool save) {
        wstr code = wanted.empty() ? systemLanguage() : wanted;
        wchar_t env[16] = {};  // BOOTDOCTOR_LANG=tr: a language for this run only (scripts, screenshots)
        if (!save && GetEnvironmentVariableW(L"BOOTDOCTOR_LANG", env, 16)) code = env;
        if (!i18n::setLanguage(code)) i18n::setLanguage(L"en");
        Canvas::setRightToLeft(i18n::rtl());
        kit::setTranslator(kitTranslate);
        if (save) {
            settings().set(L"language", i18n::current());
            if (haveReport_ && !snapshot_) startScan();  // the report is written in the language, so write it again
        }
        invalidate();
    }

    void addLanguagePack() {
        showFilePicker(T(L"Add a language pack"), g_sys.exeDir, {L".lang"}, T(L"Add"), [this](const wstr& path) {
            wstr code, err;
            if (!i18n::importPack(path, code, err)) {
                showMessage(T(L"The language pack was not added"), err);
                return;
            }
            wstr name = code;
            for (const auto& l : i18n::languages())
                if (l.code == code) name = l.name;
            showConfirm(T(L"Language pack added"), TF(L"{0} is now available. Switch to it now?", name), {T(L"Not now"), T(L"Switch")}, 1, 1, false,
                        [this, code](int b) {
                            if (b == 1) applyLanguage(code, true);
                        });
        });
    }

    void makeTemplate() {
        showFilePicker(T(L"Save a template for a new language"), g_sys.exeDir + L"\\new-language.lang", {L".lang"}, T(L"Save"), [this](const wstr& path) {
            wstr p = path, err;
            if (!icontains(p.substr(p.size() > 5 ? p.size() - 5 : 0), L".lang")) p += L".lang";
            if (i18n::writeTemplate(p, L"xx", T(L"My language"), false, err))
                showMessage(T(L"Template saved"),
                            TF(L"{0}\n\nOpen it in a text editor (or a translation editor such as Poedit), set @code (two letters, for example \"sv\") and @name "
                               L"(the language's own name, for example \"Svenska\"), and translate each msgstr. Then use Settings menu > Add a language pack.", p));
            else showMessage(T(L"The template was not saved"), err);
        }, true);
    }

    void openLink(const wstr& url) {
        SHELLEXECUTEINFOW info{sizeof info};
        info.fMask = SEE_MASK_NOASYNC;
        info.lpVerb = L"open";
        info.lpFile = url.c_str();
        info.nShow = SW_SHOWNORMAL;
        if (!shell::execute(&info))
            showMessage(T(L"No browser or e-mail program here"), T(L"This system cannot open links (Windows PE has no browser). Write it down: ") +
                                                                 (url.rfind(L"mailto:", 0) == 0 ? url.substr(7) : url));
    }

    bool loadLogo() {
        if (logo_.w) return true;
        HRSRC r = FindResourceW(nullptr, L"MUSPC_LOGO", MAKEINTRESOURCEW(10));
        HGLOBAL g = r ? LoadResource(nullptr, r) : nullptr;
        const unsigned char* p = g ? (const unsigned char*)LockResource(g) : nullptr;
        DWORD n = r ? SizeofResource(nullptr, r) : 0;
        if (!p || n < 12 || memcmp(p, "PKIM", 4)) return false;
        uint32_t w, h;
        memcpy(&w, p + 4, 4);
        memcpy(&h, p + 8, 4);
        if (!w || !h || n < 12 + (uint64_t)w * h * 4) return false;
        logo_.w = (int)w;
        logo_.h = (int)h;
        logo_.px.resize((size_t)w * h);
        memcpy(logo_.px.data(), p + 12, logo_.px.size() * 4);
        return true;
    }

    float drawAbout(float x, float y, float w) {
        float top = y, cw = std::min(w, S(680)), cx = x + (w - cw) / 2;
        // The logo on white, so it reads the same in every theme
        RectF lc(cx, y, cw, S(150));
        canvas().round(lc, S(12), Color(255, 255, 255));
        canvas().ring(lc, S(12), S(1), theme().separator);
        if (loadLogo()) {
            float lh = S(90), lw = lh * logo_.w / logo_.h;
            if (lw > cw - S(40)) { lw = cw - S(40); lh = lw * logo_.h / logo_.w; }
            canvas().imageScaled(logo_, RectF(lc.cx() - lw / 2, lc.cy() - lh / 2, lw, lh));
        }
        y += lc.h + S(22);
        text(kAppName, RectF(cx, y, cw, S(30)), fonts().title, theme().label, kit::kLine | DT_CENTER);
        y += S(32);
        text(L"Version " + wstr(kVersion) + L" · 64-bit · Windows 7 to 11, Windows PE and WinRE", RectF(cx, y, cw, S(20)), fonts().small, theme().label2, kit::kLine | DT_CENTER);
        y += S(26);
        if (!snapshot_ && !rescueStickRoot().empty()) {
            StickMarker mk = readStickMarker(rescueStickRoot());
            wstr t = mk.name.empty() ? T(L"Started from a rescue stick") : TF(L"Started from the rescue stick \"{0}\"", mk.name);
            float bw = buttonWidth(T(L"Rename this stick…"));
            float tw = std::min(cw - bw - S(10), (float)canvas().measure(t, fonts().uiSemi).cx + S(4));
            float sx = cx + (cw - tw - S(10) - bw) / 2;
            text(t, RectF(sx, y, tw, S(26)), fonts().uiSemi, theme().label, kit::kLine);
            drawButton(RectF(sx + tw + S(10), y, bw, S(26)), T(L"Rename this stick…"), H_STICK_NAME, 0, kit::ButtonStyle::Quiet);
            y += S(36);
        }
        {
            float bw = buttonWidth(T(L"Check for updates"));
            drawButton(RectF(cx + (cw - bw) / 2, y, bw, S(26)), T(L"Check for updates"), H_UPDATE, 0, kit::ButtonStyle::Quiet);
            y += S(38);
        }
        auto block = [&](const wstr& head, const wstr& body) {
            y += para(head, cx, y, cw, fonts().uiSemi, theme().label, true) + S(4);
            y += para(body, cx, y, cw, fonts().ui, theme().label2, true) + S(18);
        };
        block(T(L"Free for personal use"),
              T(L"PATio Boot Doctor is free for you to use at home: on your own PCs and on those of your family and friends. No time limit, no adverts, nothing to unlock."));
        block(T(L"Using it for work?"),
              T(L"If you use it in a business, a repair shop or an IT department, or to look after other people's PCs for pay, please get in touch for a business licence. It is fairly priced, and it keeps these tools growing and improving."));
        y += para(T(L"Contact"), cx, y, cw, fonts().uiSemi, theme().label, true) + S(8);
        auto linkBtn = [&](const wstr& label, const wstr& value, int hit) {
            text(label, RectF(cx, y, S(90), S(26)), fonts().ui, theme().label2);
            float bw = buttonWidth(value);
            drawButton(RectF(cx + S(90), y, bw, S(26)), value, hit, 0, kit::ButtonStyle::Quiet);
            y += S(32);
        };
        linkBtn(T(L"E-mail"), L"info@muspc.com", H_LINK_MAIL);
        linkBtn(T(L"Website"), L"www.muspc.com", H_LINK_WEB);
        linkBtn(T(L"Review"), T(L"Leave a Google review"), H_LINK_REVIEW);
        y += S(16);
        y += para(T(L"Made by MusPC with PatKit. It uses only Windows' own tools to make changes (diskpart, bcdedit, bcdboot, DISM and others), and always backs up first."),
                  cx, y, cw, fonts().small, theme().label3, true);
        return y - top + S(20);
    }

    // ---------------- Admin tools
    void launchAdmin(bool powershell) {
        countUse(powershell ? L"Tools: PowerShell" : L"Tools: command prompt");
        wstr exe = powershell ? findSystemTool(L"WindowsPowerShell\\v1.0\\powershell.exe") : findSystemTool(L"cmd.exe");
        if (exe.empty()) {
            showMessage(powershell ? T(L"PowerShell is not available here") : T(L"The command prompt is not available here"),
                        powershell ? T(L"This Windows PE or recovery system was made without PowerShell. Use the command prompt instead.") : T(L"cmd.exe was not found."));
            return;
        }
        wchar_t dir[MAX_PATH] = {};
        GetWindowsDirectoryW(dir, MAX_PATH);
        wstr cmd = L"\"" + exe + L"\"";
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(0);
        STARTUPINFOW si{sizeof si};
        PROCESS_INFORMATION pi{};
        // This program runs as administrator, so what it starts does too.
        if (CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, wstr(dir).substr(0, 3).c_str(), &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            toolLog(L"[opened] " + exe + L" (administrator)");
        } else
            showMessage(T(L"It could not be opened"), errorText(GetLastError()));
    }

    // ---------------- Rescue USB stick
    void askRescue() {
        if (busy_ || !haveReport_) return;
        std::vector<RescueSource> src = snapshot_ ? std::vector<RescueSource>{RescueSource{L"Winre.wim", T(L"Windows Recovery Environment on Disk 0, partition 4"), 560ull << 20}}
                                                  : rescueSources(rep_);
        if (src.empty()) {
            showMessage(T(L"No recovery image was found"),
                        T(L"A rescue disk is made from the Windows Recovery Environment (Winre.wim) of this PC, or from a Windows install USB stick.\n\n- Turn the recovery environment on: open the command prompt (admin) and type: reagentc /enable\n- Or plug in a Windows 10 or 11 install USB stick.\n\nThen scan again (Ctrl+R) and try once more."));
            return;
        }
        std::vector<int> targets = rescueTargets(rep_);
        std::vector<kit::MenuItem> items;
        auto header = [&](const wstr& t) {
            kit::MenuItem h;
            h.header = true;
            h.text = t;
            items.push_back(h);
        };
        header(targets.empty() ? T(L"No USB storage found: plug one in and scan again (Ctrl+R)") : T(L"Make the rescue disk on (everything on it is erased):"));
        for (int d : targets)
            for (const Disk& x : rep_.m.disks)
                if (x.number == d) {
                    wstr names;
                    for (int i : x.parts) names += (names.empty() ? L"" : L", ") + rep_.m.parts[i].name();
                    kit::MenuItem m;
                    wstr stickName = stickNameOnDisk(rep_, x.number);
                    m.text = T(L"Disk ") + std::to_wstring(x.number) + (x.model.empty() ? wstr() : L" · " + x.model) + (stickName.empty() ? wstr() : L" · \"" + stickName + L"\"");
                    RescueNeeds nd = rescueNeeds(rep_, src[0], g_sys.exeDir + L"\\PATioBootDoctor_x64.exe", d);
                    m.hint = sizeText(x.size) + (x.size < nd.onStick ? T(L" · too small") : wstr());
                    m.tip = names.empty() ? T(L"Empty") : TF(L"Everything on it is erased: {0}", names);
                    m.cmd = C_RESCUE_DISK;
                    m.arg = d;
                    items.push_back(m);
                }
        kit::MenuItem sep;
        sep.separator = true;
        items.push_back(sep);
        kit::MenuItem img;
        img.text = T(L"A disk image file (.vhd and .img)…");
        img.tip = T(L"Makes the rescue disk as image files: start them in a virtual machine, or write the .img to a USB stick later with a tool such as Rufus or balenaEtcher.");
        img.cmd = C_RESCUE_IMAGE;
        img.hint = sizeText(rescueNeeds(rep_, src[0], g_sys.exeDir + L"\\PATioBootDoctor_x64.exe").imageFiles);
        items.push_back(img);
        header(TF(L"Made from: {0} ({1})", src[0].label, sizeText(src[0].size)));
        {
            RescueNeeds n = rescueNeeds(rep_, src[0], g_sys.exeDir + L"\\PATioBootDoctor_x64.exe");
            header(TF(L"Needs about {0} on the stick: use one of {1} or more", sizeText(n.onStick), sizeText(n.stickSize)));
            wstr wd = rescueWorkFolder(-1);
            uint64_t freeWork = driveFree(wd);
            header(freeWork && freeWork < n.work ? TF(L"Not enough room on {0} while it is made: {1} free, about {2} needed", wd, sizeText(freeWork), sizeText(n.work))
                                                 : TF(L"While it is made, about {0} is used for a while on {1}", sizeText(n.work), wd));
        }
        POINT p = mousePos();
        showMenu(items, std::max(S(60), (float)p.x), std::max(S(60), (float)p.y - S(40)));
    }

    void askRescueImage() {
        std::vector<RescueSource> src = rescueSources(rep_);
        if (src.empty()) return;
        wstr start;
        uint64_t best = 0;
        for (const Partition& p : rep_.m.parts)
            if (p.readable && !p.letter.empty() && p.freeBytes > best && p.fs == L"NTFS") {
                best = p.freeBytes;
                start = p.letter + L"\\";
            }
        uint64_t need = rescueNeeds(rep_, src[0], g_sys.exeDir + L"\\PATioBootDoctor_x64.exe").imageFiles;
        showFolderPicker(T(L"Where should the image files go?"), start, T(L"Make the image here"), [this, need](const wstr& dir) {
            ULARGE_INTEGER avail{};
            if (GetDiskFreeSpaceExW(dir.c_str(), &avail, nullptr, nullptr) && avail.QuadPart < need) {
                showMessage(T(L"Not enough room"), TF(L"The image files need about {0} free on that drive. Choose another folder.", sizeText(need)));
                return;
            }
            startRescueImage(dir);
        });
    }

    void startRescueImage(const wstr& dir) {
        countUse(L"Tools: rescue disk images made");
        if (busy_ || snapshot_) return;
        std::vector<RescueSource> src = rescueSources(rep_);
        if (src.empty()) return;
        wchar_t exe[MAX_PATH] = {}, tmp[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        GetTempPathW(MAX_PATH, tmp);
        wstr work = wstr(tmp) + L"PATioBD-rescue";
        RescueSource s = src[0];
        wstr exePath = exe;
        RescueExtras extras{keysPass_, settings().path()};
        runJob(T(L"Making the rescue disk image"), dir, true,
               [this, s, exePath, dir, work, extras] {
                   FixOutcome o = makeRescueImage(s, exePath, dir, work, [this](const wstr& st, float f) { progress(st, f); }, &cancel_, extras);
                   std::lock_guard<std::mutex> l(lock_);
                   jobOk_ = o.ok;
                   jobNote_ = o.result;
               },
               [this] {
                   wstr note;
                   bool ok;
                   {
                       std::lock_guard<std::mutex> l(lock_);
                       note = jobNote_;
                       ok = jobOk_;
                   }
                   showMessage(ok ? T(L"Rescue disk image ready") : T(L"The image was not made"), note);
               });
    }

    bool stickHasOurData(const Disk& d) const {
        for (int i : d.parts) {
            const Partition& p = rep_.m.parts[i];
            if (!p.root.empty() && (GetFileAttributesW((p.root + L"PATioBootDoctorData").c_str()) != INVALID_FILE_ATTRIBUTES ||
                                    GetFileAttributesW((p.root + L"PATioAppData").c_str()) != INVALID_FILE_ATTRIBUTES ||
                                    GetFileAttributesW((p.root + L"PATioBootDoctor").c_str()) != INVALID_FILE_ATTRIBUTES ||
                                    GetFileAttributesW((p.root + L"Drivers").c_str()) != INVALID_FILE_ATTRIBUTES ||
                                    GetFileAttributesW((p.root + L"PATioBootDoctor Backups").c_str()) != INVALID_FILE_ATTRIBUTES))
                return true;
        }
        return false;
    }

    void confirmRescue(int disk) {
        const Disk* d = nullptr;
        for (const Disk& x : rep_.m.disks)
            if (x.number == disk) d = &x;
        std::vector<RescueSource> src = rescueSources(rep_);
        if (!d || src.empty()) return;
        wstr names;
        for (int i : d->parts)
            names += (names.empty() ? L"" : L", ") + rep_.m.parts[i].name();
        // Room first: on the stick, and on this PC while it is made
        RescueNeeds need = rescueNeeds(rep_, src[0], g_sys.exeDir + L"\\PATioBootDoctor_x64.exe", disk);
        wstr wd = rescueWorkFolder(disk);
        uint64_t freeWork = driveFree(wd);
        wstr needText = TF(L"Space needed: about {0} on the stick (image {1}, repair tools {2}, start-up files {3}, saved data {4}), and about {5} free on {6} while it is made.",
                           sizeText(need.onStick), sizeText(need.image), sizeText(need.repairCopy), sizeText(need.bootFiles), sizeText(need.data), sizeText(need.work), wd);
        if (d->size < need.onStick) {
            showMessage(T(L"This stick is too small"), needText + L"\n\n" + TF(L"This stick holds {0}. Use a stick of {1} or more.", sizeText(d->size), sizeText(need.stickSize)));
            return;
        }
        if (freeWork && freeWork < need.work) {
            showMessage(T(L"Not enough room on this PC"), needText + L"\n\n" + TF(L"{0} has only {1} free. Free up space there (or connect a drive with more room, then scan again) and try once more.", wd, sizeText(freeWork)));
            return;
        }
        showInput(T(L"Make a rescue USB stick on ") + d->name() + L"?",
                  T(L"Everything on this stick will be erased") + (names.empty() ? wstr(L".") : L" (" + names + L").") +
                      (stickHasOurData(*d) ? T(L"\n\nThis stick holds PATio Boot Doctor data (saved keys, settings, backups or a Drivers folder): it is kept safe on this PC first and put back on the new stick.") : wstr()) +
                      ((GetFileAttributesW(keyStorePath().c_str()) != INVALID_FILE_ATTRIBUTES) ? T(L"\n\nThis PC's saved BitLocker keys and settings go on the stick too.") : wstr()) +
                      (findCommander(g_sys.exeDir + L"\\PATioBootDoctor_x64.exe").empty() ? wstr() : T(L"\n\nPATio Commander comes along and starts too, next to PATio Boot Doctor.")) +
                      T(L"\n\nIt is made from: ") + src[0].label + L" (" + sizeText(src[0].size) + T(L"). Making it takes about 5 to 15 minutes.") + L"\n\n" + needText +
                      T(L"\n\nTo make sure the right disk is erased, type its number (") + std::to_wstring(disk) + T(L") below."),
                  L"", T(L"Make the stick"), [this, disk](const wstr& t) {
                      if (t != std::to_wstring(disk)) {
                          showMessage(T(L"Nothing was erased"), T(L"The number typed is not this disk's number."));
                          return true;
                      }
                      defer([this, disk] { askStickName(disk); });
                      return true;
                  });
    }

    // Each rescue stick gets its own name, kept on the stick (shown when it is plugged in or started from).
    void askStickName(int disk) {
        wstr current = stickNameOnDisk(rep_, disk);
        if (current.empty()) current = TF(L"Rescue stick {0}", std::to_wstring(settings().getInt(L"stickCount", 0) + 1));
        showInput(T(L"Name this rescue stick"),
                  T(L"A name of your own makes each stick easy to tell apart (for example \"Workshop 1\" or \"Van kit\"). It is kept on the stick and shown when it is plugged in or started from."),
                  current, T(L"Make the stick"), [this, disk](const wstr& t) {
                      wstr name = t;
                      while (!name.empty() && name.back() == L' ') name.pop_back();
                      if (name.size() > 60) name.resize(60);
                      settings().setInt(L"stickCount", settings().getInt(L"stickCount", 0) + 1);
                      defer([this, disk, name] { askRescueScheme(disk, name); });
                      return true;
                  });
    }

    // Which firmware the stick should start on. "Both" is the default and makes exactly the stick
    // every earlier version made, so pressing the recommended button changes nothing.
    void askRescueScheme(int disk, const wstr& name) {
        showConfirm(T(L"Which PCs should this stick start?"),
                    T(L"Most sticks should start on both kinds of PC. Choose a single kind only if a PC refuses the usual stick.\n\n"
                      "• Both — UEFI and legacy BIOS (recommended). The usual stick.\n"
                      "• UEFI only — a GPT stick with an EFI system partition, for the few PCs that will not start the usual stick in UEFI mode.\n"
                      "• Legacy only — an MBR stick for old BIOS PCs."),
                    {T(L"Both"), T(L"UEFI only"), T(L"Legacy only")}, 0, 0, false, [this, disk, name](int b) {
                        startRescue(disk, name, b == 1 ? RS_UEFI_ONLY : b == 2 ? RS_LEGACY_ONLY : RS_BOTH);
                    });
    }

    // Running from a rescue stick: rename it.
    void renameThisStick() {
        wstr root = rescueStickRoot();
        if (root.empty()) return;
        StickMarker mk = readStickMarker(root);
        showInput(T(L"Rename this rescue stick"), T(L"The name is kept on the stick."), mk.name, T(L"Rename"), [this, root, mk](const wstr& t) {
            wstr name = t;
            while (!name.empty() && name.back() == L' ') name.pop_back();
            if (name.size() > 60) name.resize(60);
            if (writeStickMarker(root, mk.version.empty() ? wstr(kVersion) : mk.version, name)) {
                showToast(L"check", T(L"Stick renamed"), name, {}, nullptr);
                invalidate();
            } else
                showMessage(T(L"The stick was not renamed"), errorText(GetLastError()));
            return true;
        });
    }

    void startRescue(int disk, const wstr& stickName = L"", int scheme = RS_BOTH) {
        countUse(L"Tools: rescue sticks made");
        if (busy_) return;
        if (job_.joinable()) job_.join();
        std::vector<RescueSource> src = rescueSources(rep_);
        if (src.empty()) return;
        wstr work = rescueWorkFolder(disk);
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        busy_ = true;
        cancel_ = false;
        setProgress(T(L"Making the rescue USB stick"), 0.f, T(L"Starting"), true);
        auto copy = std::make_shared<Report>(rep_);
        RescueSource s = src[0];
        wstr exePath = exe;
        RescueExtras extras{keysPass_, settings().path(), stickName, scheme};
        job_ = std::thread([this, copy, disk, s, exePath, work, extras] {
            noRedirection();
            FixOutcome o = makeRescueStick(*copy, disk, s, exePath, work, [this](const wstr& step, float f) { progress(step, f); }, &cancel_, extras);
            {
                std::lock_guard<std::mutex> l(lock_);
                jobOk_ = o.ok;
                jobNote_ = o.result;
                jobDirs_.clear();
            }
            PostMessageW(hwnd(), WM_BD_FIXED, 2, 0);
        });
        invalidate();
    }

    // ---------------- Secure Boot
    void showSecureBoot() {
        countUse(L"Screens: Secure Boot");
        const Firmware& fw = rep_.m.fw;
        wstr msg = secureBootState(fw) + L"\n\n" + T(kLessonSecureBoot);
        if (fw.uefi && !snapshot_) {
            wstr note = secureBoot2023Note(readSecureBootCerts(), fw);
            if (!note.empty()) msg += L"\n\n" + note;
        }
        if (fw.uefi) msg += L"\n\n" + secureBootHowTo(fw, !fw.secureBoot);
        std::vector<wstr> buttons = {T(L"Close")};
        bool canRestart = fw.uefi && fw.canOpenSettings && !snapshot_;
        if (fw.uefi && fw.canOpenSettings) buttons.push_back(T(L"Restart into firmware settings"));
        bool turningOn = fw.uefi && !fw.secureBoot;
        showConfirm(T(L"Secure Boot"), msg, buttons, 0, 0, false, [this, canRestart, turningOn](int b) {
            if (b != 1 || !canRestart) return;
            if (turningOn) {
                warnSecureBootOn();
                return;
            }
            showConfirm(T(L"Restart now?"), T(L"The PC restarts straight into its firmware settings in 10 seconds. Save your work and close other programs first."), {T(L"Cancel"), T(L"Restart")}, 1, 0, true,
                        [this](int b2) {
                            if (b2 == 1) startCountdown();
                        });
        });
    }

    // Switching Secure Boot on can leave a system unable to start: say which, and how to go back.
    void warnSecureBootOn() {
        const Firmware& fw = rep_.m.fw;
        wstr risk;
        for (const Install& in : rep_.installs) {
            bool old = !in.linux && in.major == 6 && in.minor <= 1;
            bool unsignedLinux = in.linux && in.loader >= 0 && in.loader < (int)rep_.linuxLoaders.size() && !rep_.linuxLoaders[in.loader].shim;
            if (old || unsignedLinux) risk += L"\n- " + in.label() + (old ? T(L": too old for Secure Boot") : T(L": starts without the signed \"shim\" loader"));
        }
        bool bitlocker = false;
        for (const Partition& p : rep_.m.parts) bitlocker |= p.bitlocker;
        wstr msg = T(L"Switching Secure Boot on can stop a system from starting at the next start, if that system or one of its drivers is not signed.");
        msg += risk.empty() ? T(L" Nothing on this PC is known to need it off, but older tools, USB sticks and some drivers can still be refused.")
                            : T(L" On this PC these would not start any more:") + risk;
        if (bitlocker) msg += T(L"\n\nBitLocker is in use: Windows may ask for the recovery key once. Have it ready (aka.ms/myrecoverykey).");
        msg += T(L"\n\nBefore you go on:\n1. Take a photo of this window with your phone now, so the steps below are with you while the firmware settings are on screen.\n2. In the firmware settings, take a photo of each page before you change anything on it, and of every change you make, before you choose \"Save and exit\". Then you can set everything back exactly.\n3. If the PC does not start afterwards: open the firmware settings again with the same key, set Secure Boot back to Disabled (and anything else back as in your photos), then save and exit.\n\n");
        msg += secureBootHowTo(fw, true);
        showConfirm(T(L"Before you switch Secure Boot on"), msg, {T(L"Cancel"), T(L"I have taken the photo")}, 1, 0, true, [this](int b) {
            if (b == 1) startCountdown();
        });
    }

    void startCountdown() {
        if (snapshot_ || busy_) return;
        countdown_ = 10;
        SetTimer(hwnd(), kCountdownTimer, 1000, nullptr);
        setProgress(T(L"Restarting into the firmware settings"), 0.f, T(L"in 10 seconds. Press Esc or the stop button to cancel."), true);
    }

    void onTimer(UINT_PTR id) override {
        if (id == kRefreshTimer) {
            KillTimer(hwnd(), kRefreshTimer);
            if (scanning_) {  // looked already started: look once more when it ends
                rescanPending_ = true;
                return;
            }
            if (busy_ || (overlayOpen() && !onlyMenusOpen())) {
                SetTimer(hwnd(), kRefreshTimer, 1000, nullptr);  // not in the middle of work or a question
                return;
            }
            // An open menu listing disks (the rescue stick's) is rebuilt with what is there now
            reopenRescueMenu_ = menuWithCommandOpen(C_RESCUE_IMAGE);
            if (onlyMenusOpen()) closeMenus();
            quietScan_ = true;
            deviceScan_ = true;
            startScan();
            return;
        }
        if (id == kDecryptTimer) {
            pollDecryption();
            return;
        }
        if (id == kPowerTimer) {
            powerTick();
            return;
        }
        if (id == kCaptureTimer) {
            captureFrame();
            return;
        }
        if (id == kStatsTimer) {
            statsTick();
            return;
        }
        if (id != kCountdownTimer || countdown_ <= 0) return;
        if (--countdown_ > 0) {
            setProgress(T(L"Restarting into the firmware settings"), (10 - countdown_) / 10.f,
                        L"in " + std::to_wstring(countdown_) + (countdown_ == 1 ? T(L" second") : T(L" seconds")) + L". Press Esc or the stop button to cancel.", true);
            return;
        }
        KillTimer(hwnd(), kCountdownTimer);
        clearProgress();
        startFixes({makeFix(FX_FIRMWARE_SETTINGS)});
    }

    // ---------------- Navigation (back / forward)
    struct NavState {
        int view = V_REPORT, sel = -1, node = -1, dsKind = SEL_NONE, dsDisk = -1, dsPart = -1, dsGap = -1;
        float scroll = 0;
    };
    NavState navNow() const { return NavState{view_, sel_, node_, dsKind_, dsDisk_, dsPart_, dsGap_, scroll_[view_]}; }
    void navApply(const NavState& n) {
        view_ = n.view;
        sel_ = n.sel < (int)rep_.installs.size() ? n.sel : -1;
        node_ = n.node;
        dsKind_ = n.dsKind;
        dsDisk_ = n.dsDisk;
        dsPart_ = n.dsPart;
        dsGap_ = n.dsGap;
        scroll_[view_] = n.scroll;
        if (view_ == V_HISTORY && !snapshot_) refreshSessions();
        invalidate();
    }
    // Remembers where the user is before a change of place, so Back returns there.
    void navPush() {
        NavState n = navNow();
        if (!back_.empty()) {
            const NavState& t = back_.back();
            if (t.view == n.view && t.sel == n.sel && t.node == n.node && t.dsKind == n.dsKind && t.dsDisk == n.dsDisk && t.dsPart == n.dsPart && t.dsGap == n.dsGap) return;
        }
        back_.push_back(n);
        if (back_.size() > 100) back_.erase(back_.begin());
        fwd_.clear();
    }
    void navBack() {
        if (back_.empty()) return;
        fwd_.push_back(navNow());
        NavState n = back_.back();
        back_.pop_back();
        navApply(n);
    }
    void navForward() {
        if (fwd_.empty()) return;
        back_.push_back(navNow());
        NavState n = fwd_.back();
        fwd_.pop_back();
        navApply(n);
    }

    // ---------------- Undo / redo of this session's changes
    void undoRedo(bool redo) {
        if (busy_ || snapshot_) return;
        std::vector<wstr>& from = redo ? redo_ : undo_;
        if (from.empty()) return;
        ActionInfo ai = readAction(from.back());
        wstr what = redo ? L"Redo: this undoes the undo of \"" + ai.title.substr(ai.title.rfind(L"Undo ", 0) == 0 ? 5 : 0) + L"\", so the change is made again."
                         : T(L"This puts back what \"") + ai.title + T(L"\" changed, from its backup. What is there now is saved first, so Redo can make the change again.");
        showConfirm(redo ? T(L"Redo?") : T(L"Undo \"") + ai.title + L"\"?", what + T(L"\n\nIf the disks were changed in other ways since then, the result can differ. The disks are scanned again afterwards."),
                    {T(L"Cancel"), redo ? T(L"Redo") : T(L"Undo")}, 1, 0, true, [this, ai, redo](int b) {
                        if (b == 1) startUndo(ai, redo ? 2 : 1);
                    });
    }

    // ---------------- History
    float drawHistory(float x, float y, float w) {
        float top = y;
        auto head = [&](bool draw) {
            float pad = S(14), yy = y + pad, cx = x + pad + S(34), cw = w - pad * 2 - S(34);
            if (draw) {
                drawIcon("bd_backup", x + pad, yy + S(2), S(22), theme().accent);
                text(T(L"Automatic backups"), RectF(cx, yy, cw, S(22)), fonts().uiSemi, theme().label);
            }
            yy += S(26);
            yy += para(T(L"Before every change, this program saves a copy of what it is about to change. At the start of every session it also saves the start-up settings and partition tables of all disks. Undo puts a change back."),
                       cx, yy, cw, fonts().small, theme().label2, draw) +
                  S(6);
            yy += para(T(L"Backups are kept in: ") + (backupShown_.empty() ? wstr(L"(chosen at the first scan)") : backupShown_), cx, yy, cw, fonts().smallSemi, theme().label,
                       draw) +
                  S(8);
            if (draw) {
                float bx = cx;
                auto b = [&](const wstr& t, int hit, kit::ButtonStyle st) {
                    float bw = buttonWidth(t);
                    drawButton(RectF(bx, yy, bw, S(28)), t, hit, 0, st);
                    bx += bw + S(8);
                };
                b(T(L"Back up now"), H_BACKUP_NOW, kit::ButtonStyle::Primary);
                b(T(L"Change folder"), H_CHANGE_FOLDER, kit::ButtonStyle::Normal);
                b(T(L"Clean up…"), H_CLEAN_HISTORY, kit::ButtonStyle::Normal);
            }
            yy += S(28) + pad;
            return yy - y;
        };
        float h = head(false);
        card(RectF(x, y, w, h));
        head(true);
        y += h + S(16);
        if (sessions_.empty()) {
            y += para(T(L"No sessions yet."), x + S(4), y, w, fonts().ui, theme().label3, true);
            return y - top;
        }
        float rowH = S(46);
        for (int s = 0; s < (int)sessions_.size(); ++s) {
            const SessionInfo& si = sessions_[s];
            bool current = !sessionFolder_.empty() && iequals(si.dir, sessionFolder_);
            RectF hr(x, y, w, S(34));
            drawSectionHeader(RectF(x + S(4), y + S(5), w * 0.7f, S(24)),
                              si.name + (current ? T(L" · this session") : L"") + L" · " + si.host + L" · " + plural(si.actions.size(), T(L"entry"), T(L"entries")) + L" · " + sizeText(si.bytes));
            if (!current) {
                float bw = buttonWidth(T(L"Delete"));
                drawButton(RectF(hr.right() - bw, hr.y + S(3), bw, S(26)), T(L"Delete"), H_DEL_SESSION, s, kit::ButtonStyle::Quiet);
            }
            y += S(36);
            for (int a = 0; a < (int)si.actions.size(); ++a) {
                const ActionInfo& ai = si.actions[a];
                RectF rr(x, y, w, rowH);
                drawRow(rr, false, true, hovered(H_ACTION, s * 1000 + a), false, false, false);
                addHit(rr, H_ACTION, s * 1000 + a);
                wstr t = ai.time.size() >= 19 ? ai.time.substr(11, 5) : ai.time;
                text(t, RectF(x + S(12), y + S(4), S(52), S(20)), fonts().mono, theme().label2);
                float right = rr.right() - S(10);
                if (ai.ok && ai.canUndo && !ai.undone && !busy_) {
                    float bw = buttonWidth(T(L"Undo"));
                    drawButton(RectF(right - bw, rr.cy() - S(13), bw, S(26)), T(L"Undo"), H_UNDO, s * 1000 + a, kit::ButtonStyle::Normal);
                    right -= bw + S(10);
                }
                Color pc = !ai.finished ? kAmber : !ai.ok ? kRed : ai.undone ? theme().label3 : kGreen;
                wstr pt = !ai.finished ? T(L"Not finished") : !ai.ok ? T(L"Failed") : ai.undone ? T(L"Undone") : T(L"Done");
                right -= pill(right, rr.cy(), pt, pc) + S(10);
                text(ai.title, RectF(x + S(70), y + S(4), right - x - S(74), S(20)), fonts().uiSemi, theme().label);
                text(ai.result.empty() ? ai.target : ai.result, RectF(x + S(70), y + S(24), right - x - S(74), S(18)), fonts().small, theme().label2);
                y += rowH + S(2);
            }
            y += S(12);
        }
        return y - top;
    }

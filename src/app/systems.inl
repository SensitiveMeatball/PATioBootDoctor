// Part of class DoctorApp (included inside the class in main.cpp).
    // ---------------------------------------------------------------- Windows on this PC
    float drawSystems(float x, float y, float w) {
        float top = y;
        if (showLessons_)
            y += lessonBox(T(L"Every Windows (and Linux) found on every disk. Choose which one starts when nobody chooses, which ones show in the start-up menu and what they are called there. A Windows kept in Windows.old after an upgrade can be checked and, if it is whole, brought back."),
                           T(L"Several systems on one PC"), x, y, w, true);
        // the start-up menu itself
        const BcdStore* menu = nullptr;
        for (const BcdStore& s : rep_.stores)
            if (s.live) menu = &s;
        if (!menu)
            for (const Install& in : rep_.installs)
                if (in.store >= 0 && in.isDefault) menu = &rep_.stores[in.store];
        if (menu) {
            wstr t = menu->timeout >= 0 ? TF(L"The start-up menu waits {0} seconds before the first system starts.", std::to_wstring(menu->timeout)) : T(L"How long the start-up menu waits is not set.");
            float bw = buttonWidth(T(L"Change…"));
            text(t, RectF(x + S(4), y, w - bw - S(20), S(28)), fonts().ui, theme().label);
            drawButton(RectF(x + w - bw, y, bw, S(28)), T(L"Change…"), H_SYS_TIMEOUT, 0, kit::ButtonStyle::Normal);
            y += S(40);
        }
        if (rep_.installs.empty()) {
            y += para(T(L"No Windows or Linux was found."), x + S(4), y, w, fonts().ui, theme().label3, true);
            return y - top;
        }
        for (int i = 0; i < (int)rep_.installs.size(); ++i) {
            const Install& in = rep_.installs[i];
            auto body = [&](bool draw) {
                float pad = S(14), cx = x + pad + S(32), cw = w - pad * 2 - S(32), yy = y + pad;
                if (draw) {
                    drawIcon(in.linux ? "bd_linux" : "desktop", x + pad, yy + S(2), S(22), theme().label2, theme().accent);
                    float pw = pill(x + w - pad, yy + S(11), in.verdictTitle, hc(in.verdict));
                    wstr t = in.label() + (in.build ? T(L" · build ") + std::to_wstring(in.build) : wstr());
                    text(t, RectF(cx, yy, cw - pw - S(10), S(22)), fonts().uiSemi, theme().label);
                }
                yy += S(24);
                wstr facts = in.where + L" · " + stateText(in);
                if (!in.entryId.empty() && in.store >= 0 && in.store < (int)rep_.stores.size())
                    if (const BcdEntry* e = rep_.stores[in.store].find(in.entryId)) facts += TF(L" · in the menu as \"{0}\"", e->description);
                yy += para(facts, cx, yy, cw, fonts().small, theme().label2, draw) + S(8);
                float bx = cx;
                auto btn = [&](int act, const wstr& t, kit::ButtonStyle st) {
                    float bw = buttonWidth(t);
                    if (bx + bw > x + w - pad && bx > cx) {
                        bx = cx;
                        yy += S(34);
                    }
                    if (draw) drawButton(RectF(bx, yy, bw, S(28)), t, H_SYS, i * 100 + act, st);
                    bx += bw + S(8);
                };
                btn(0, T(L"Details"), kit::ButtonStyle::Normal);
                if (!in.linux && !in.windowsOld) {
                    if (!in.entryId.empty() && !in.isDefault) btn(1, T(L"Start this one first"), kit::ButtonStyle::Normal);
                    if (!in.entryId.empty() && !in.inMenu) btn(2, T(L"Show in the menu"), kit::ButtonStyle::Normal);
                    if (!in.entryId.empty() && in.inMenu && !in.running) btn(3, T(L"Hide from the menu"), kit::ButtonStyle::Quiet);
                    if (in.entryId.empty() && in.atRoot) btn(4, T(L"Add to the start-up menu"), kit::ButtonStyle::Normal);
                    if (!in.entryId.empty()) btn(5, T(L"Rename…"), kit::ButtonStyle::Quiet);
                }
                if (in.windowsOld) btn(6, T(L"Check and bring back…"), kit::ButtonStyle::Normal);
                yy += S(28) + pad;
                return yy - y;
            };
            float bh = body(false);
            card(RectF(x, y, w, bh));
            body(true);
            y += bh + S(10);
        }
        return y - top;
    }

    void systemAction(int i, int act) {
        if (i < 0 || i >= (int)rep_.installs.size()) return;
        const Install& in = rep_.installs[i];
        auto fx = [&](int kind) {
            Fix f = makeFix(kind);
            f.install = i;
            f.store = in.store;
            f.entry = in.entryId;
            f.part = in.store >= 0 && in.store < (int)rep_.stores.size() ? rep_.stores[in.store].part : in.part;
            f.disk = rep_.m.parts[in.part].disk;
            return f;
        };
        switch (act) {
            case 0:
                navPush();
                sel_ = i;
                scroll_[V_REPORT] = 0;
                setView(V_REPORT);
                break;
            case 1: askFixList({fx(FX_MAKE_DEFAULT)}, in.label()); break;
            case 2: askFixList({fx(FX_SHOW_IN_MENU)}, in.label()); break;
            case 3: askFixList({fx(FX_REMOVE_FROM_MENU)}, in.label()); break;
            case 4: {
                Fix f = fx(FX_ADD_ENTRY);
                f.part = in.part;
                checkOne(f);
                askFixList({f}, in.label());
                break;
            }
            case 5: {
                wstr cur;
                if (in.store >= 0 && in.store < (int)rep_.stores.size())
                    if (const BcdEntry* e = rep_.stores[in.store].find(in.entryId)) cur = e->description;
                Fix f = fx(FX_RENAME_ENTRY);
                showInput(T(L"Name in the start-up menu"), T(L"The name shown for this Windows when the PC starts, for example \"Windows 11 (work)\"."), cur, T(L"Rename"), [this, f](const wstr& t) {
                    if (t.empty()) return false;
                    Fix x = f;
                    x.arg = t;
                    defer([this, x] { startFixes({x}); });
                    return true;
                });
                break;
            }
            case 6: {
                for (const Finding& fd : rep_.pc)
                    for (const Fix& f : fd.fixes)
                        if (f.kind == FX_RESTORE_WINDOWS_OLD && f.install == i) {
                            showConfirm(fd.title, fd.text, {T(L"Cancel"), f.available ? T(L"Bring it back") : T(L"Close")}, f.available ? 1 : 0, 0, true, [this, f](int b) {
                                if (b == 1 && f.available) askFixList({f}, T(L"Windows.old"));
                            });
                            return;
                        }
                for (const Finding& fd : rep_.pc)
                    if (fd.title.find(L"Windows.old") != wstr::npos || fd.text.find(in.root + in.folder.substr(0, in.folder.size() - 1)) == 0) {
                        showMessage(fd.title, fd.text);
                        return;
                    }
                showMessage(T(L"Windows.old"), T(L"No check result for it. Scan again."));
                break;
            }
        }
    }

    void checkOne(Fix& f) {
        Report tmp = rep_;
        Finding fd;
        fd.fixes = {f};
        tmp.pc = {fd};
        checkTools(tmp);
        f = tmp.pc[0].fixes[0];
    }

    void askTimeout() {
        const BcdStore* menu = nullptr;
        int si = -1;
        for (size_t k = 0; k < rep_.stores.size(); ++k)
            if (rep_.stores[k].live) si = (int)k;
        if (si < 0)
            for (const Install& in : rep_.installs)
                if (in.store >= 0 && in.isDefault) si = in.store;
        if (si < 0) return;
        menu = &rep_.stores[si];
        Fix f = makeFix(FX_SET_TIMEOUT);
        f.store = si;
        f.part = menu->part;
        f.disk = rep_.m.parts[menu->part].disk;
        f.entry = L"{bootmgr}";
        showInput(T(L"How long the start-up menu waits"), T(L"Seconds before the first system starts by itself (0 to 999). With only one system the menu is not shown at all."),
                  std::to_wstring(menu->timeout >= 0 ? menu->timeout : 30), T(L"Set"), [this, f](const wstr& t) {
                      int v = _wtoi(t.c_str());
                      if (t.empty() || v < 0 || v > 999) {
                          showMessage(T(L"That is not a number of seconds"), T(L"Type a number from 0 to 999."));
                          return false;
                      }
                      Fix x = f;
                      x.arg = std::to_wstring(v);
                      defer([this, x] { startFixes({x}); });
                      return true;
                  });
    }

// Part of class DoctorApp (included inside the class in main.cpp).
    // ---------------------------------------------------------------- the BitLocker keys screen
    SavedKey newKey(const Partition& p, const wstr& key) {
        SavedKey k;
        k.id = p.key;
        k.name = p.name() + L" · " + sizeText(p.length);
        k.key = key;
        k.pc = thisPcName();
        k.model = rep_.m.fw.maker + (rep_.m.fw.model.empty() ? wstr() : L" " + rep_.m.fw.model);
        return k;
    }

    void reloadKeys() {
        keysCache_.clear();
        if (!keysPass_.empty() && !snapshot_) loadKeys(keysPass_, keysCache_);
    }

    void openKeys() {
        if (view_ != V_KEYS) navPush();
        scroll_[V_KEYS] = 0;
        setView(V_KEYS);
        if (!snapshot_ && keysPass_.empty() && keyStoreExists())
            withKeyPassword(false, [this] {
                reloadKeys();
                invalidate();
            });
        else reloadKeys();
    }

    float drawKeys(float x, float y, float w) {
        float top = y;
        bool exists = snapshot_ || keyStoreExists();
        bool locked = exists && keysPass_.empty();
        // header
        auto head = [&](bool draw) {
            float pad = S(14), cx = x + pad + S(34), cw = w - pad * 2 - S(34), yy = y + pad;
            if (draw) {
                drawIcon("bd_key", x + pad, yy + S(2), S(22), theme().accent);
                text(T(L"Saved BitLocker recovery keys"), RectF(cx, yy, cw, S(22)), fonts().uiSemi, theme().label);
            }
            yy += S(26);
            wstr where = exists ? TF(L"Kept in {0}, locked with your password.", keyStorePath()) : T(L"No keys are saved yet.");
            yy += para(where, cx, yy, cw, fonts().small, theme().label2, draw) + S(8);
            float bx = cx;
            auto b = [&](int a, const wstr& t, kit::ButtonStyle st) {
                float bw = buttonWidth(t);
                if (bx + bw > x + w - pad && bx > cx) {
                    bx = cx;
                    yy += S(34);
                }
                if (draw) drawButton(RectF(bx, yy, bw, S(28)), t, H_KEYS_TOP, a, st);
                bx += bw + S(8);
            };
            if (locked) b(0, T(L"Unlock with the password…"), kit::ButtonStyle::Primary);
            if (!g_sys.winPE) b(1, T(L"Save the keys of this PC…"), locked ? kit::ButtonStyle::Normal : kit::ButtonStyle::Primary);
            if (exists) b(2, T(L"Back up the keys file…"), kit::ButtonStyle::Normal);
            if (exists && !locked) b(3, T(L"Change the password…"), kit::ButtonStyle::Quiet);
            if (exists) b(4, T(L"Delete all…"), kit::ButtonStyle::Quiet);
            yy += S(28) + pad;
            return yy - y;
        };
        float h = head(false);
        card(RectF(x, y, w, h));
        head(true);
        y += h + S(12);
        if (!stickFolder().empty() || snapshot_) {
            wstr t = T(L"Running from the rescue USB stick: the keys are kept only on this stick. If it is lost or breaks, they are gone. Back up the keys file to another drive or stick too (it stays locked with your password).");
            float th = (float)canvas().measureWrapped(t, fonts().small, (int)(w - S(32))) + S(20);
            canvas().round(RectF(x, y, w, th), S(8), kAmber.withAlpha(0.14f));
            para(t, x + S(16), y + S(10), w - S(32), fonts().small, theme().label, true);
            y += th + S(12);
        }
        if (locked) {
            y += para(T(L"The keys are locked. Give the password to see them."), x + S(4), y, w, fonts().ui, theme().label3, true) + S(10);
            return y - top;
        }
        if (keysCache_.empty()) {
            y += para(exists ? T(L"No keys in the file.") : T(L"When you unlock a drive (choose \"Save it\") or save the keys of this PC, they appear here, grouped by the PC they belong to."),
                      x + S(4), y, w, fonts().ui, theme().label3, true) + S(10);
            return y - top;
        }
        auto section = [&](bool archived) {
            std::vector<wstr> pcs;
            for (const SavedKey& k : keysCache_)
                if (k.archived == archived) {
                    wstr g = (k.pc.empty() ? wstr() : k.pc + L" · ") + (k.model.empty() ? wstr(T(L"PC not recorded")) : k.model);
                    if (std::find(pcs.begin(), pcs.end(), g) == pcs.end()) pcs.push_back(g);
                }
            if (pcs.empty()) return;
            if (archived) {
                drawSectionHeader(RectF(x + S(4), y, w, S(24)), T(L"Archive"));
                y += S(30);
            }
            for (const wstr& g : pcs) {
                drawIcon("pc", x + S(4), y + S(3), S(16), theme().label2);
                text(g, RectF(x + S(28), y, w - S(32), S(22)), fonts().uiSemi, archived ? theme().label2 : theme().label);
                y += S(28);
                for (int i = 0; i < (int)keysCache_.size(); ++i) {
                    const SavedKey& k = keysCache_[i];
                    wstr kg = (k.pc.empty() ? wstr() : k.pc + L" · ") + (k.model.empty() ? wstr(T(L"PC not recorded")) : k.model);
                    if (k.archived != archived || kg != g) continue;
                    auto body = [&](bool draw) {
                        float pad = S(12), cx = x + pad + S(24), cw = w - pad * 2 - S(24), yy = y + pad;
                        if (draw) {
                            text(k.owner.empty() ? wstr(T(L"No owner name")) : k.owner, RectF(cx, yy, cw * 0.5f, S(20)), fonts().uiSemi, k.owner.empty() ? theme().label3 : theme().label);
                            text(k.key.substr(0, 6) + L"-••••••-…", RectF(cx + cw * 0.5f, yy, cw * 0.5f, S(20)), fonts().mono, theme().label2, kit::kLine | DT_RIGHT);
                        }
                        yy += S(22);
                        yy += para(k.name + L" · " + TF(L"saved {0}", k.when), cx, yy, cw, fonts().small, theme().label2, draw) + S(8);
                        float bx = cx;
                        auto b = [&](int a, const wstr& t, kit::ButtonStyle st) {
                            float bw = buttonWidth(t);
                            if (draw) drawButton(RectF(bx, yy, bw, S(26)), t, H_KEY, i * 10 + a, st);
                            bx += bw + S(8);
                        };
                        b(0, T(L"Show"), kit::ButtonStyle::Normal);
                        b(1, T(L"Owner…"), kit::ButtonStyle::Normal);
                        b(2, archived ? T(L"Back from the archive") : T(L"Archive"), kit::ButtonStyle::Quiet);
                        b(3, T(L"Delete"), kit::ButtonStyle::Quiet);
                        yy += S(26) + pad;
                        return yy - y;
                    };
                    float bh = body(false);
                    card(RectF(x + S(12), y, w - S(12), bh));
                    float save = x;
                    x += S(12);
                    body(true);
                    x = save;
                    y += bh + S(8);
                }
                y += S(8);
            }
        };
        section(false);
        section(true);
        return y - top;
    }

    void keysTop(int a) {
        switch (a) {
            case 0:
                withKeyPassword(false, [this] {
                    reloadKeys();
                    invalidate();
                });
                break;
            case 1: captureKeys(); break;
            case 2: backupKeysFile(); break;
            case 3: onCommand(C_KEY_PASSWORD, 0, L""); break;
            case 4: onCommand(C_KEY_DELETE_ALL, 0, L""); break;
        }
    }

    void editKey(const wstr& id, std::function<void(SavedKey&)> fn, const wstr& done) {
        wstr err;
        bool ok = snapshot_ || editKeys(keysPass_, [&](std::vector<SavedKey>& keys) {
            for (SavedKey& k : keys)
                if (k.id == id) fn(k);
        }, err);
        if (!ok) showMessage(T(L"The key was not changed"), err);
        else if (!done.empty()) showToast(L"check", done, L"", {}, nullptr);
        reloadKeys();
        invalidate();
    }

    void keyAction(int i, int a) {
        if (i < 0 || i >= (int)keysCache_.size()) return;
        SavedKey k = keysCache_[i];
        switch (a) {
            case 0:
                showConfirm(k.owner.empty() ? k.name : k.owner, TF(L"Recovery key of {0}:\n\n{1}\n\nSaved {2}{3}.", k.name, k.key, k.when, k.pc.empty() ? wstr() : TF(L" on {0}", k.pc)),
                            {T(L"Close"), T(L"Copy")}, 0, 0, false, [this, k](int b) {
                                if (b == 1 && copyText(k.key)) showToast(L"check", T(L"Key copied"), T(L"Paste it where it is asked for. Clear the clipboard afterwards by copying something else."), {}, nullptr);
                            });
                break;
            case 1:
                showInput(T(L"Owner of this key"), T(L"A name that tells whose PC or which disk this is, for example \"Ann's laptop\" or \"Office PC 3\"."), k.owner, T(L"Save"), [this, k](const wstr& t) {
                    wstr id = k.id, name = t;
                    defer([this, id, name] { editKey(id, [name](SavedKey& x) { x.owner = name; }, T(L"Owner name saved")); });
                    return true;
                });
                break;
            case 2: {
                bool to = !k.archived;
                editKey(k.id, [to](SavedKey& x) { x.archived = to; }, to ? T(L"Moved to the archive") : T(L"Back from the archive"));
                break;
            }
            case 3:
                showConfirm(T(L"Delete this saved key?"), TF(L"The BitLocker recovery key of {0}{1} is deleted from the saved keys. Keep a copy elsewhere (the Microsoft account of the PC's owner, a printout) if it may be needed again.",
                                                         k.name, k.owner.empty() ? wstr() : L" (" + k.owner + L")"),
                            {T(L"Cancel"), T(L"Delete")}, 1, 0, true, [this, k](int b) {
                                if (b != 1) return;
                                if (!snapshot_ && !deleteKey(k.id, keysPass_)) showMessage(T(L"The key was not deleted"), keyStorePath());
                                reloadKeys();
                                invalidate();
                            });
                break;
        }
    }

    void backupKeysFile() {
        if (!keyStoreExists()) {
            showMessage(T(L"No saved keys"), T(L"There is no keys file to back up yet."));
            return;
        }
        wstr start;
        for (const Partition& p : rep_.m.parts)
            if (p.readable && !p.letter.empty() && keyStorePath().rfind(p.letter, 0) != 0 && (p.fs == L"NTFS" || p.fs == L"exFAT" || p.fs == L"FAT32")) start = p.letter + L"\\";
        showFolderPicker(T(L"Where should the copy of the keys go? (another drive)"), start, T(L"Copy here"), [this](const wstr& dir) {
            SYSTEMTIME t;
            GetLocalTime(&t);
            wchar_t name[64];
            swprintf(name, 64, L"\\bitlocker-keys %04d-%02d-%02d.dat", t.wYear, t.wMonth, t.wDay);
            wstr d = dir;
            while (!d.empty() && d.back() == L'\\') d.pop_back();
            wstr to = d + name;
            if (CopyFileW(keyStorePath().c_str(), to.c_str(), FALSE))
                showMessage(T(L"Keys backed up"), TF(L"A copy is in {0}. It is locked with the same password. To use it, copy it next to PATio Boot Doctor as bitlocker-keys.dat.", to));
            else showMessage(T(L"The keys were not copied"), errorText(GetLastError()));
        });
    }

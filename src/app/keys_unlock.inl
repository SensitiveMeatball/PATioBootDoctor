// Part of class DoctorApp (included inside the class in main.cpp).
    // ---------------------------------------------------------------- BitLocker keys
    // Runs fn after the current dialog has closed (a dialog opened inside another's callback would be closed with it).
    void defer(std::function<void()> fn) {
        deferred_.push_back(std::move(fn));
        PostMessageW(hwnd(), WM_BD_DEFER, 0, 0);
    }

    // The saved keys are locked with a password, asked once per session. create: a new file may be made.
    void withKeyPassword(bool create, std::function<void()> then) {
        if (!keysPass_.empty() || snapshot_) {
            then();
            return;
        }
        bool exists = keyStoreExists();
        if (!exists && !create) {
            then();
            return;
        }
        wstr title = exists ? T(L"Password for saved keys") : T(L"Choose a password for saved keys");
        wstr msg = exists ? TF(L"The saved BitLocker keys ({0}) are locked with a password. Type it to use them, or leave it empty to go on without them.", keyStorePath())
                          : TF(L"Saved keys are kept in a file next to this program ({0}), locked with a password, so they also work when this program runs from the rescue USB stick. Choose a password of at least 6 characters. It is not stored anywhere: if it is forgotten, the saved keys cannot be opened.", keyStorePath());
        showInput(title, msg, L"", exists ? T(L"Open") : T(L"Save the key"), [this, exists, then](const wstr& pw) {
            if (pw.empty()) {
                if (!exists) {
                    showMessage(T(L"No password"), T(L"A password is needed to save the key. Without one, the key is not saved."));
                    return false;
                }
                defer(then);
                return true;
            }
            if (!exists) {
                if (pw.size() < 6) {
                    showMessage(T(L"Too short"), T(L"Choose a password of at least 6 characters."));
                    return false;
                }
            } else {
                std::vector<SavedKey> k;
                int r = loadKeys(pw, k);
                if (r != KL_OK) {
                    showMessage(T(L"That password is not right"), r == KL_ERROR ? T(L"The file of saved keys is damaged or was made by another program.") : T(L"Try again, or leave it empty to go on without the saved keys."));
                    return false;
                }
            }
            keysPass_ = pw;
            int moved = migrateOldKeys(pw);
            if (moved) consoleAdd(L"[keys] " + std::to_wstring(moved) + L" keys moved from the earlier place into " + keyStorePath());
            defer(then);
            return true;
        });
        setInputMasked(true);
    }

    void askUnlock(const Fix& f, std::vector<Fix> list, const wstr& what, bool asked = false) {
        if (f.part < 0 || f.part >= (int)rep_.m.parts.size()) return;
        if (!asked && !snapshot_ && keysPass_.empty() && keyStoreExists()) {
            withKeyPassword(false, [this, f, list, what] { askUnlock(f, list, what, true); });
            return;
        }
        const Partition& p = rep_.m.parts[f.part];
        std::vector<SavedKey> keys;
        if (snapshot_) keys = {SavedKey{p.key, p.name(), L"2026-11-10 09:12", L"123453-234564-345675-456786-567897-678908-111111-222222"}};
        else if (!keysPass_.empty()) loadKeys(keysPass_, keys);
        const SavedKey* saved = findKey(keys, p.key);
        wstr msg = TF(L"Type the 48-digit recovery key of {0}. You find it in your Microsoft account (aka.ms/myrecoverykey), on a printout, or with your IT department. The dashes are added for you; Ctrl+V pastes a copied key.", p.name());
        if (saved) msg += TF(L"\n\nThe key saved on {0} for this drive was filled in.", saved->when);
        bool known = saved != nullptr;  // already saved: not offered for saving again
        showInput(T(L"BitLocker recovery key"), msg, saved ? saved->key : wstr(), T(L"Unlock"),
                  [this, list, what, p, known](const wstr& key) {
                      if (!validRecoveryKey(key)) {
                          showMessage(T(L"That is not a recovery key"),
                                      keyDigits(key).size() != 48 ? T(L"A BitLocker recovery key has 48 digits, in 8 groups of 6. Check it and try again.")
                                                                 : T(L"One of the groups of 6 digits is mistyped (each group of a real key can be divided by 11). Check it and try again."));
                          return false;
                      }
                      wstr k = formatRecoveryKey(key);
                      std::vector<Fix> l2 = list;
                      for (Fix& x : l2)
                          if (x.kind == FX_UNLOCK) x.arg = k;
                      if (!known && inputOption() == 1) {
                          SavedKey sk = newKey(p, k);
                          defer([this, sk, l2] {
                              askOwner(TF(L"Whose PC or disk is {0}? The name is shown with the saved key.", sk.name), [this, sk, l2](const wstr& owner) {
                                  withKeyPassword(true, [this, sk, l2, owner] {
                                      if (!keysPass_.empty()) {
                                          keyToSave_ = sk;
                                          keyToSave_.owner = owner;
                                      }
                                      startFixes(l2);
                                  });
                              });
                          });
                          return true;
                      }
                      startFixes(l2);
                      return true;
                  },
                  known ? std::vector<wstr>() : std::vector<wstr>{T(L"Do not save the key"), T(L"Save it (with a password)")});
        setInputFormat(keyInputFormat);
    }

    void keysMenu() {
        if (!keyStoreExists()) {
            showMessage(T(L"Saved BitLocker keys"), TF(L"No recovery keys are saved. When you unlock a drive, choose \"Save it (with a password)\": the key is then kept next to this program ({0}), locked with your password, and filled in next time.", keyStorePath()));
            return;
        }
        withKeyPassword(false, [this] {
            std::vector<SavedKey> keys;
            if (!keysPass_.empty()) loadKeys(keysPass_, keys);
            std::vector<kit::MenuItem> items;
            kit::MenuItem h;
            h.header = true;
            h.text = keysPass_.empty() ? T(L"Locked: the password was not given") : T(L"Click a key to delete it");
            items.push_back(h);
            for (size_t k = 0; k < keys.size(); ++k) {
                kit::MenuItem m;
                m.text = keys[k].name;
                m.hint = keys[k].when;
                m.tip = keys[k].key.substr(0, 6) + L"-…";
                m.cmd = C_KEY_DELETE;
                m.arg = (int)k;
                items.push_back(m);
            }
            kit::MenuItem sep;
            sep.separator = true;
            items.push_back(sep);
            if (!keysPass_.empty()) {
                kit::MenuItem pw;
                pw.text = T(L"Change the password…");
                pw.cmd = C_KEY_PASSWORD;
                items.push_back(pw);
            }
            kit::MenuItem all;
            all.text = T(L"Delete all saved keys");
            all.cmd = C_KEY_DELETE_ALL;
            all.danger = true;
            items.push_back(all);
            POINT p = mousePos();
            showMenu(items, std::max(S(60), (float)p.x - S(200)), (float)p.y + S(16));
        });
    }

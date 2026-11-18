// Part of class DoctorApp (included inside the class in main.cpp).
    // ---------------------------------------------------------------- reading the keys while Windows runs
    // Reads (or, with add, creates) the 48-digit recovery key of the given drives. Runs in the background.
    static void readRecoveryKeys(const std::vector<Partition>& parts, bool add, std::vector<std::pair<Partition, wstr>>& found, std::vector<Partition>& none) {
        wstr bde = findSystemTool(L"manage-bde.exe");
        std::wregex pat(L"\\d{6}(-\\d{6}){7}");
        for (const Partition& p : parts) {
            wstr letter = p.letter, temp, err;
            if (letter.empty() && !p.volume.empty()) {
                temp = freeLetter();
                if (!temp.empty() && mountAt(p.volume, temp, err)) letter = temp;
            }
            if (letter.empty() || bde.empty()) {
                none.push_back(p);
                continue;
            }
            // the console never shows the key: toolLog hides it
            ToolResult t = add ? bd::runTool(bde, L"-protectors -add " + letter + L" -RecoveryPassword")
                               : bd::runTool(bde, L"-protectors -get " + letter + L" -Type RecoveryPassword");
            if (!temp.empty()) unmount(temp);
            std::wsmatch m;
            wstr out = t.output;
            bool any = false;
            while (std::regex_search(out, m, pat)) {
                wstr k = m.str();
                if (validRecoveryKey(k)) {
                    found.push_back({p, k});
                    any = true;
                    break;  // one key per drive is enough to open it
                }
                out = m.suffix().str();
            }
            if (!t.output.empty()) SecureZeroMemory(&t.output[0], t.output.size() * sizeof(wchar_t));
            if (!out.empty()) SecureZeroMemory(&out[0], out.size() * sizeof(wchar_t));
            if (!any) none.push_back(p);
        }
    }

    void captureKeys() {
        if (busy_ || g_sys.winPE) return;
        std::vector<Partition> parts;
        for (const Partition& p : rep_.m.parts)
            if (p.bitlocker && !p.locked) parts.push_back(p);
        if (parts.empty()) {
            showMessage(T(L"No unlocked BitLocker drive"), T(L"There is no BitLocker drive whose key can be read now. A locked drive must be unlocked first."));
            return;
        }
        auto found = std::make_shared<std::vector<std::pair<Partition, wstr>>>();
        auto none = std::make_shared<std::vector<Partition>>();
        runJob(T(L"Reading the BitLocker recovery keys"), plural(parts.size(), T(L"drive"), T(L"drives")), false,
               [parts, found, none] { readRecoveryKeys(parts, false, *found, *none); },
               [this, found, none] {
                   if (none->empty()) {
                       saveCapturedKeys(*found);
                       return;
                   }
                   // BitLocker is on, but there is no 48-digit key to back up: offer to add one
                   wstr names;
                   for (const Partition& p : *none) names += (names.empty() ? L"" : L", ") + p.name();
                   showConfirm(T(L"No recovery key to back up"),
                               TF(L"{0} is encrypted with BitLocker but has no 48-digit recovery key: it opens only through the PC's security chip (TPM) or a PIN. If the PC's hardware, firmware or start-up changes, BitLocker asks for a recovery key, and without one the files are lost.\n\nWindows can add a recovery key now. It is added as one more way to open the drive; nothing else on the drive changes. The new key is then backed up here.", names),
                               {T(L"Not now"), T(L"Add a recovery key")}, 1, 1, false, [this, found, none](int b) {
                                   if (b != 1) {
                                       defer([this, found] { saveCapturedKeys(*found); });
                                       return;
                                   }
                                   defer([this, found, none] {
                                       auto added = std::make_shared<std::vector<std::pair<Partition, wstr>>>();
                                       auto still = std::make_shared<std::vector<Partition>>();
                                       std::vector<Partition> missing = *none;
                                       runJob(T(L"Adding a BitLocker recovery key"), plural(missing.size(), T(L"drive"), T(L"drives")), false,
                                              [missing, added, still] { readRecoveryKeys(missing, true, *added, *still); },
                                              [this, found, added, still] {
                                                  for (auto& k : *added) found->push_back(k);
                                                  if (!still->empty()) {
                                                      wstr n;
                                                      for (const Partition& p : *still) n += (n.empty() ? L"" : L", ") + p.name();
                                                      showMessage(T(L"No recovery key could be added"), TF(L"Windows did not add a recovery key to {0}. Try it in Windows: Control Panel > BitLocker Drive Encryption > Back up your recovery key.", n));
                                                  }
                                                  saveCapturedKeys(*found);
                                              });
                                   });
                               });
               });
    }

    // Asks whose keys these are, then saves them with the password.
    void saveCapturedKeys(const std::vector<std::pair<Partition, wstr>>& got) {
        if (got.empty()) return;
        auto keys = std::make_shared<std::vector<SavedKey>>();
        for (const auto& g : got) keys->push_back(newKey(g.first, g.second));
        wstr list;
        for (const SavedKey& k : *keys) list += L"- " + k.name + L": " + k.key.substr(0, 6) + L"-…\n";
        askOwner(TF(L"These recovery keys were read:\n{0}\nWhose PC are they from? The name is shown with the keys (for example \"Ann's laptop\").", list), [this, keys](const wstr& owner) {
            withKeyPassword(true, [this, keys, owner] {
                if (keysPass_.empty()) return;
                int ok = 0;
                wstr err, ids = settings().get(L"savedKeyIds");
                for (SavedKey& k : *keys) {
                    k.owner = owner;
                    if (saveKey(k, keysPass_, err)) {
                        ++ok;
                        if ((L" " + ids + L" ").find(L" " + k.id + L" ") == wstr::npos) ids += L" " + k.id;
                    }
                }
                settings().set(L"savedKeyIds", ids);
                for (SavedKey& k : *keys) SecureZeroMemory(&k.key[0], k.key.size() * sizeof(wchar_t));
                reloadKeys();
                if (ok == (int)keys->size()) showToast(L"check", T(L"BitLocker keys saved"), keyStorePath(), {}, nullptr);
                else showMessage(T(L"Not every key was saved"), err);
                invalidate();
            });
        });
    }

    // The owner name for keys about to be saved (suggested: this PC's name or model).
    void askOwner(const wstr& msg, std::function<void(const wstr&)> then) {
        wstr suggest = thisPcName();
        if (suggest.empty()) suggest = rep_.m.fw.maker + (rep_.m.fw.model.empty() ? wstr() : L" " + rep_.m.fw.model);
        showInput(T(L"Owner of the key"), msg, suggest, T(L"Next"), [this, then](const wstr& t) {
            wstr name = t;
            defer([then, name] { then(name); });
            return true;
        });
    }

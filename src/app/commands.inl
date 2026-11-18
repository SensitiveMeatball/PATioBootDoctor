// Part of class DoctorApp (included inside the class in main.cpp).
    // ------------------------------------------------------------ commands
    void setView(int v) {
        if (v != view_ && v >= 0 && v < V_COUNT) countUse(wstr(L"Screens: ") + viewName(v));
        view_ = v;
        if (v == V_CHAIN && sel_ < 0 && !rep_.installs.empty()) sel_ = 0;
        if (v == V_HISTORY && !snapshot_) refreshSessions();
        invalidate();
    }

    void onCommand(int cmd, int arg, const wstr&) override {
        switch (cmd) {
            case C_SCAN: startScan(); break;
            case C_VIEW:
                if (arg != view_) navPush();
                setView(arg);
                break;
            case C_NAV_BACK: navBack(); break;
            case C_NAV_FWD: navForward(); break;
            case C_UNDO_LAST: undoRedo(false); break;
            case C_REDO: undoRedo(true); break;
            case C_DISKOP: askDiskOp(arg); break;
            case C_SETTINGS_MENU: settingsMenu(); break;
            case C_SETTINGS: openSettings(); break;
            case C_ABOUT:
                if (view_ != V_ABOUT) navPush();
                setView(V_ABOUT);
                break;
            case C_LANGPACK: addLanguagePack(); break;
            case C_HOME:
                if (view_ != V_REPORT || sel_ >= 0) navPush();
                sel_ = -1;
                scroll_[V_REPORT] = 0;
                setView(V_REPORT);
                break;
            case C_SET_LANG: {
                std::vector<i18n::LangInfo> langs = i18n::languages();
                if (arg >= 0 && arg < (int)langs.size() && langs[arg].code != i18n::current()) applyLanguage(langs[arg].code, true);
                break;
            }
            case C_POWER: powerChoice(arg / 100, arg % 100); break;
            case C_KEYS: openKeys(); break;
            case C_UPDATE_CHECK: checkUpdate(true); break;
            case C_REMOVE_UPDATE:
                if (arg >= 0 && arg < (int)updates_.size()) {
                    Fix f = repairFix(FX_REMOVE_UPDATE);
                    f.arg = updates_[arg].package;
                    f.title = TF(L"Remove the update {0}", updates_[arg].package);
                    f.takes = TF(L"Installed {0}. A few minutes; Windows Update may offer it again later.", updates_[arg].when);
                    askFixList({f}, rep_.installs[repInstall_].label());
                }
                break;
            case C_IMAGE_DISK: askImageTarget(arg); break;
            case C_LOAD_DRIVER: loadDriverCmd(arg); break;
            case C_POWER_MENU: powerMenu(arg); break;
            case C_UPDATE_FILE: updateFromFile(); break;
            case C_UPDATE_URL:
                showInput(T(L"Update address"), T(L"The https:// address of the update file that names the newest version (see the README). Leave it empty for the standard address."),
                          settings().get(L"updateUrl", kDefaultUpdateUrl), T(L"Save"), [this](const wstr& t) {
                              if (!t.empty() && t.rfind(L"https://", 0) != 0) {
                                  showMessage(T(L"Not an https:// address"), T(L"Only secure (https://) addresses are used for updates."));
                                  return false;
                              }
                              settings().set(L"updateUrl", t.empty() ? wstr(kDefaultUpdateUrl) : t);
                              return true;
                          });
                break;
            case C_KEY_DELETE: {
                std::vector<SavedKey> keys;
                loadKeys(keysPass_, keys);
                if (arg < 0 || arg >= (int)keys.size()) break;
                SavedKey k = keys[arg];
                showConfirm(T(L"Delete this saved key?"), TF(L"The BitLocker recovery key for {0} (saved {1}) is deleted from the saved keys. Keep a copy elsewhere (your Microsoft account, a printout) if you may need it again.", k.name, k.when),
                            {T(L"Cancel"), T(L"Delete")}, 1, 0, true, [this, k](int b) {
                                if (b == 1) showToast(L"check", deleteKey(k.id, keysPass_) ? T(L"Key deleted") : T(L"The key was not deleted"), k.name, {}, nullptr);
                            });
                break;
            }
            case C_KEY_DELETE_ALL:
                showConfirm(T(L"Delete all saved keys?"), TF(L"Every saved BitLocker recovery key is deleted ({0}). Keep copies elsewhere if you may need them again.", keyStorePath()), {T(L"Cancel"), T(L"Delete all")}, 1, 0, true,
                            [this](int b) {
                                if (b != 1) return;
                                bool ok = deleteAllKeys();
                                if (ok) keysPass_.clear();
                                showToast(L"check", ok ? T(L"All saved keys deleted") : T(L"The keys were not deleted"), keyStorePath(), {}, nullptr);
                            });
                break;
            case C_KEY_PASSWORD:
                showInput(T(L"New password for saved keys"), T(L"At least 6 characters. It is not stored anywhere: if it is forgotten, the saved keys cannot be opened any more."), L"", T(L"Change"),
                          [this](const wstr& pw) {
                              if (pw.size() < 6) {
                                  showMessage(T(L"Too short"), T(L"Choose a password of at least 6 characters."));
                                  return false;
                              }
                              wstr err;
                              if (changeKeyPassword(keysPass_, pw, err)) {
                                  keysPass_ = pw;
                                  showToast(L"check", T(L"Password changed"), keyStorePath(), {}, nullptr);
                              } else showMessage(T(L"The password was not changed"), err);
                              return true;
                          });
                setInputMasked(true);
                break;
            case C_LANGTEMPLATE: makeTemplate(); break;
            case C_RESCUE_DISK: confirmRescue(arg); break;
            case C_RESCUE_IMAGE: askRescueImage(); break;
            case C_PREP_MODE: prepareModeChange(arg); break;
            case C_STATS:
                if (view_ != V_STATS) navPush();
                setView(V_STATS);
                break;
            case C_CLEAN_HISTORY: cleanHistory(arg); break;
            case C_CAPTURE_KEYS: captureKeys(); break;
            case C_BACKUP_NOW: startBackupNow(); break;
            case C_SAVE_REPORT: saveReport(); break;
            case C_FOLDER:
                if (busy_) break;
                showFolderPicker(T(L"Choose where backups are kept"), backupShown_, T(L"Use this folder"), [this](const wstr& dir) {
                    wstr d = dir;
                    while (!d.empty() && d.back() == L'\\') d.pop_back();
                    if (!folderWritable(d)) {
                        showMessage(T(L"This folder cannot be used"), T(L"Nothing can be written to ") + d + L". Choose a folder on a drive that is not read-only.");
                        return;
                    }
                    settings().set(L"backupFolder", d);
                    backupRoot_ = d;
                    wstr err;
                    if (journal_.open(d, rep_.host, err)) {
                        backupShown_ = d;
                        sessionFolder_ = journal_.folder();
                        journal_.saveReport(reportText(rep_));
                        refreshSessions();
                        showToast(L"check", T(L"Backup folder changed"), T(L"New backups go to ") + d + L". Older ones stay where they are.", {}, nullptr);
                    } else showMessage(T(L"The backup folder was not changed"), err);
                    invalidate();
                });
                break;
            case C_UNDO: askUndo(arg / 1000, arg % 1000); break;
            case C_DELETE_SESSION:
                if (arg >= 0 && arg < (int)sessions_.size()) {
                    wstr dir = sessions_[arg].dir;
                    showConfirm(T(L"Delete these backups?"), T(L"The backups of the session from ") + sessions_[arg].name + T(L" are deleted from the disk. Its changes then cannot be undone any more."),
                                {T(L"Cancel"), T(L"Delete")}, 1, 0, true, [this, dir](int b) {
                                    if (b != 1) return;
                                    if (!deleteFolderTree(dir)) showMessage(T(L"Not all backups were deleted"), T(L"Some files in ") + dir + T(L" are in use or read-only."));
                                    refreshSessions();
                                    invalidate();
                                });
                }
                break;
        }
        invalidate();
    }

    void refreshSessions() {
        wstr root = journal_.isOpen() ? journal_.root() : backupShown_;
        sessions_ = root.empty() ? std::vector<SessionInfo>() : listSessions(root);
    }

    void saveReport() {
        if (!haveReport_) return;
        wstr folder = backupShown_.empty() ? g_sys.exeDir : backupShown_;
        while (!folder.empty() && folder.back() == L'\\') folder.pop_back();
        wchar_t stamp[64];
        swprintf(stamp, 64, L"Boot Doctor report %04d-%02d-%02d %02d-%02d.txt", rep_.when.wYear, rep_.when.wMonth, rep_.when.wDay, rep_.when.wHour, rep_.when.wMinute);
        wstr start = folder + L"\\" + stamp;
        showFilePicker(T(L"Save the report"), start, {L".txt"}, T(L"Save"), [this](const wstr& path) {
            wstr p = path;
            if (!icontains(p.substr(p.size() > 4 ? p.size() - 4 : 0), L".txt")) p += L".txt";
            std::string u = "\xEF\xBB\xBF" + wideToUtf8(reportWithNotes());
            HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            DWORD wr = 0;
            bool ok = h != INVALID_HANDLE_VALUE && WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr);
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
            if (ok) showToast(L"check", T(L"Report saved"), p, {}, nullptr);
            else showMessage(T(L"The report was not saved"), T(L"The file ") + p + T(L" could not be written. Choose another folder."));
        }, true);
    }

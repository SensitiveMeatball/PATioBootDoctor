// Part of class DoctorApp (included inside the class in main.cpp).
    // ---------------------------------------------------------------- updating this program
    void checkUpdate(bool tellIfNone) {
        if (busy_) return;
        if (g_sys.winPE && g_sys.exeDir.size() >= 2 && towupper(g_sys.exeDir[0]) == L'X') {
            showMessage(T(L"Update in Windows"), T(L"This program runs from the rescue stick's memory, where an update would be lost at the next start. Update PATio Boot Doctor in Windows, then make the rescue stick again."));
            return;
        }
        wstr url = settings().get(L"updateUrl", kDefaultUpdateUrl);
        auto info = std::make_shared<UpdateInfo>();
        runJob(T(L"Looking for updates"), url, false, [info, url] { *info = checkForUpdate(url); },
               [this, info, tellIfNone] {
                   if (!info->ok) {
                       if (tellIfNone) showMessage(T(L"No update information"), info->error + T(L"\n\nYou can also download the new version yourself and use Settings menu > Update from a file."));
                       return;
                   }
                   if (compareVersions(info->version, kVersion) <= 0) {
                       if (tellIfNone) showToast(L"check", T(L"You have the newest version"), TF(L"PATio Boot Doctor {0}", wstr(kVersion)), {}, nullptr);
                       return;
                   }
                   UpdateInfo u = *info;
                   showConfirm(TF(L"Version {0} is available", u.version),
                               TF(L"You have version {0}.", wstr(kVersion)) + (u.notes.empty() ? wstr() : L"\n\n" + u.notes) +
                                   T(L"\n\nThe new version is downloaded, checked (its checksum must match) and put in place of this one; the program then starts again. Repairs that are running must finish first."),
                               {T(L"Not now"), T(L"Update")}, 1, 1, false, [this, u](int b) {
                                   if (b == 1) defer([this, u] { downloadAndInstall(u); });
                               });
               });
    }

    void downloadAndInstall(const UpdateInfo& u) {
        auto path = std::make_shared<wstr>();
        auto err = std::make_shared<wstr>();
        runJob(T(L"Downloading the update"), u.version, true,
               [this, u, path, err] { *path = downloadUpdate(u, [this](float f) { progress(T(L"Downloading the update"), f); }, &cancel_, *err); },
               [this, path, err] {
                   if (path->empty()) {
                       showMessage(T(L"The update was not installed"), *err);
                       return;
                   }
                   installAndRestart(*path);
               });
    }

    void installAndRestart(const wstr& file) {
        wstr err;
        if (!installUpdate(file, err)) {
            showMessage(T(L"The update was not installed"), err);
            return;
        }
        DestroyWindow(hwnd());  // the new version is starting
    }

    void updateFromFile() {
        if (busy_) return;
        showFilePicker(T(L"Choose the new PATioBootDoctor_x64.exe"), g_sys.exeDir, {L".exe"}, T(L"Update"), [this](const wstr& path) {
            wstr v, err;
            if (!checkProgramFile(path, L"", v, err)) {
                showMessage(T(L"This file cannot be used"), err);
                return;
            }
            if (compareVersions(v, kVersion) <= 0) {
                showMessage(T(L"Not newer"), TF(L"That file is version {0}; this one is {1}. Nothing was changed.", v, wstr(kVersion)));
                return;
            }
            showConfirm(TF(L"Update to version {0}?", v), T(L"This program is replaced by the chosen file (the old one is kept until the next start), and then starts again."), {T(L"Cancel"), T(L"Update")}, 1, 1, false,
                        [this, path](int b) {
                            if (b == 1) defer([this, path] { installAndRestart(path); });
                        });
        });
    }

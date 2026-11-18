// Part of class DoctorApp (included inside the class in main.cpp).
    // ---------------------------------------------------------------- cleaning the history
    void cleanHistoryMenu() {
        refreshSessions();
        auto count = [&](int days) {
            int n = 0;
            uint64_t bytes = 0;
            for (const SessionInfo& si : sessions_)
                if (sessionOld(si, days)) {
                    ++n;
                    bytes += si.bytes;
                }
            return std::make_pair(n, bytes);
        };
        std::vector<kit::MenuItem> items;
        kit::MenuItem h;
        h.header = true;
        h.text = T(L"Delete sessions and their backups:");
        items.push_back(h);
        int opts[] = {90, 30, 7, 0};
        for (int d : opts) {
            auto c = count(d);
            kit::MenuItem m;
            m.text = d == 0 ? T(L"All except this session") : TF(L"Older than {0} days", std::to_wstring(d));
            m.hint = c.first ? plural(c.first, T(L"session"), T(L"sessions")) + L" · " + sizeText(c.second) : T(L"none");
            m.cmd = C_CLEAN_HISTORY;
            m.arg = d;
            m.danger = d == 0;
            items.push_back(m);
        }
        POINT p = mousePos();
        showMenu(items, std::max(S(60), (float)p.x - S(220)), (float)p.y + S(16));
    }

    bool sessionOld(const SessionInfo& si, int days) const {
        if (!sessionFolder_.empty() && iequals(si.dir, sessionFolder_)) return false;  // never the current one
        if (days == 0) return true;
        // the folder name starts with the date: 2026-11-12 14-03-55
        if (si.name.size() < 10) return false;
        SYSTEMTIME st{};
        st.wYear = (WORD)_wtoi(si.name.substr(0, 4).c_str());
        st.wMonth = (WORD)_wtoi(si.name.substr(5, 2).c_str());
        st.wDay = (WORD)_wtoi(si.name.substr(8, 2).c_str());
        FILETIME ft, now;
        if (!st.wYear || !SystemTimeToFileTime(&st, &ft)) return false;
        GetSystemTimeAsFileTime(&now);
        ULARGE_INTEGER a{}, b{};
        a.LowPart = ft.dwLowDateTime;
        a.HighPart = ft.dwHighDateTime;
        b.LowPart = now.dwLowDateTime;
        b.HighPart = now.dwHighDateTime;
        return b.QuadPart > a.QuadPart && (b.QuadPart - a.QuadPart) / 864000000000ull >= (ULONGLONG)days;
    }

    void cleanHistory(int days) {
        std::vector<wstr> dirs;
        uint64_t bytes = 0;
        for (const SessionInfo& si : sessions_)
            if (sessionOld(si, days)) {
                dirs.push_back(si.dir);
                bytes += si.bytes;
            }
        if (dirs.empty()) {
            showMessage(T(L"Nothing to delete"), T(L"No session matches. The session that is running now is always kept."));
            return;
        }
        showConfirm(T(L"Clean up the history?"),
                    TF(L"{0} ({1}) and their backups are deleted. Changes made in them can then no longer be undone. The session that is running now is kept.", plural(dirs.size(), T(L"session"), T(L"sessions")), sizeText(bytes)),
                    {T(L"Cancel"), T(L"Delete")}, 1, 0, true, [this, dirs](int b) {
                        if (b != 1) return;
                        int bad = 0;
                        for (const wstr& d : dirs) {
                            if (!deleteFolderTree(d)) ++bad;
                            undo_.erase(std::remove_if(undo_.begin(), undo_.end(), [&](const wstr& u) { return istartsWith(u, d); }), undo_.end());
                            redo_.erase(std::remove_if(redo_.begin(), redo_.end(), [&](const wstr& u) { return istartsWith(u, d); }), redo_.end());
                        }
                        refreshSessions();
                        if (bad) showMessage(T(L"Not everything was deleted"), TF(L"{0} could not be deleted completely (files in use or read-only).", plural(bad, T(L"session"), T(L"sessions"))));
                        else showToast(L"check", T(L"History cleaned up"), plural(dirs.size(), T(L"session deleted"), T(L"sessions deleted")), {}, nullptr);
                        invalidate();
                    });
    }

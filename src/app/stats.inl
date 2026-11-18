// Part of class DoctorApp (included inside the class in main.cpp).
// ---------------------------------------------------------------- usage statistics
// How often each screen, repair, disk operation and tool is used, when it was first and last used, how
// often the program was started (in Windows or from the rescue stick) and for how long. Grouped, kept in
// PATioBootDoctorData\usage.txt (on the stick when running from it), shown under Settings > Usage
// statistics. Nothing leaves the PC.
    struct UseItem {
        wstr group, name, first, last;
        uint64_t count = 0;
    };

    wstr usesPath() { return dataFolder() + L"\\usage.txt"; }

    void loadUses() {
        std::lock_guard<std::mutex> l(useLock_);
        if (usesLoaded_ || snapshot_) return;
        usesLoaded_ = true;
        std::string raw;
        if (!readFileText(usesPath(), raw)) return;
        wstr t = utf8ToWide(raw);
        if (!t.empty() && t[0] == 0xFEFF) t.erase(0, 1);
        for (size_t a = 0; a < t.size();) {
            size_t b = t.find(L'\n', a);
            if (b == wstr::npos) b = t.size();
            wstr line = t.substr(a, b - a);
            a = b + 1;
            while (!line.empty() && line.back() == L'\r') line.pop_back();
            std::vector<wstr> f;
            for (size_t p = 0; p <= line.size();) {
                size_t q = line.find(L'\t', p);
                if (q == wstr::npos) q = line.size();
                f.push_back(line.substr(p, q - p));
                p = q + 1;
            }
            if (f.size() < 5) continue;
            UseItem u{f[0], f[1], f[3], f[4], _wcstoui64(f[2].c_str(), nullptr, 10)};
            uses_[u.group + L": " + u.name] = u;
        }
    }

    void saveUses() {
        if (snapshot_) return;
        wstr t;
        {
            std::lock_guard<std::mutex> l(useLock_);
            if (!usesDirty_) return;
            usesDirty_ = false;
            for (const auto& kv : uses_) t += kv.second.group + L"\t" + kv.second.name + L"\t" + std::to_wstring(kv.second.count) + L"\t" + kv.second.first + L"\t" + kv.second.last + L"\r\n";
        }
        std::string u = "\xEF\xBB\xBF" + wideToUtf8(t);
        HANDLE h = CreateFileW(usesPath().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        DWORD wr = 0;
        WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr);
        CloseHandle(h);
    }

    // key: "Group: what" (the part before the first ": " is the group). add: how much to add.
    void countUse(const wstr& key, uint64_t add = 1) {
        if (snapshot_) return;
        if (!usesLoaded_) loadUses();
        size_t c = key.find(L": ");
        wstr group = c == wstr::npos ? wstr(L"Other") : key.substr(0, c), name = c == wstr::npos ? key : key.substr(c + 2);
        std::lock_guard<std::mutex> l(useLock_);
        UseItem& u = uses_[group + L": " + name];
        if (u.first.empty()) {
            u.group = group;
            u.name = name;
            u.first = nowStamp();
        }
        u.count += add;
        u.last = nowStamp();
        usesDirty_ = true;
    }

    // a value that keeps the highest seen (the longest session)
    void keepMax(const wstr& key, uint64_t v) {
        if (snapshot_) return;
        size_t c = key.find(L": ");
        std::lock_guard<std::mutex> l(useLock_);
        UseItem& u = uses_[key];
        if (u.first.empty()) {
            u.group = key.substr(0, c);
            u.name = key.substr(c + 2);
            u.first = nowStamp();
        }
        if (v > u.count) {
            u.count = v;
            u.last = nowStamp();
            usesDirty_ = true;
        }
    }

    // every minute while the program runs
    void statsTick() {
        ++sessionMinutes_;
        countUse(L"Time: minutes in use");
        if (techMode_) countUse(L"Time: minutes in Technician mode");
        if (recording_) countUse(L"Time: minutes recording the screen");
        keepMax(L"Time: longest session (minutes)", sessionMinutes_);
        saveUses();
    }

    void startStats() {
        if (snapshot_) return;
        loadUses();
        countUse(L"Program: starts");
        countUse(g_sys.winPE ? (rescueStickRoot().empty() ? L"Program: starts in Windows PE / recovery" : L"Program: starts from the rescue stick") : L"Program: starts in Windows");
        countUse(wstr(L"Program: version ") + kVersion);
        SetTimer(hwnd(), kStatsTimer, 60 * 1000, nullptr);
    }

    static const wchar_t* viewName(int v) {
        switch (v) {
            case V_REPORT: return N(L"Home / report");
            case V_CHAIN: return N(L"Boot chain");
            case V_DISKS: return N(L"Disks and partitions");
            case V_HISTORY: return N(L"History and backups");
            case V_ABOUT: return N(L"About");
            case V_FILES: return N(L"Recovered files");
            case V_LAYOUT: return N(L"Partition order");
            case V_SYSTEMS: return N(L"Windows on this PC");
            case V_MIGRATE: return N(L"Move Windows");
            case V_KEYS: return N(L"BitLocker keys");
            case V_NOTES: return N(L"Notes and job");
            case V_REPAIR: return N(L"Windows Repair");
            case V_STATS: return N(L"Usage statistics");
            case V_HARDWARE: return N(L"Hardware details");
        }
        return L"?";
    }

    wstr usesText() {
        std::lock_guard<std::mutex> l(useLock_);
        std::map<wstr, std::vector<const UseItem*>> groups;
        for (const auto& kv : uses_) groups[kv.second.group].push_back(&kv.second);
        wstr t = T(L"PATio Boot Doctor — usage statistics") + L"\r\n\r\n";
        for (auto& g : groups) {
            t += T(g.first.c_str()) + L"\r\n";
            std::sort(g.second.begin(), g.second.end(), [](const UseItem* a, const UseItem* b) { return a->count > b->count; });
            for (const UseItem* u : g.second) t += L"  " + std::to_wstring(u->count) + L"  " + u->name + L"  (" + T(L"first ") + u->first + L", " + T(L"last ") + u->last + L")\r\n";
            t += L"\r\n";
        }
        return t;
    }

    float drawStats(float x, float y, float w) {
        float top = y;
        if (!usesLoaded_) loadUses();
        std::map<wstr, std::vector<UseItem>> groups;
        {
            std::lock_guard<std::mutex> l(useLock_);
            for (const auto& kv : uses_) groups[kv.second.group].push_back(kv.second);
        }
        auto get = [&](const wchar_t* g, const wchar_t* n) -> uint64_t {
            auto it = groups.find(g);
            if (it == groups.end()) return 0;
            for (const UseItem& u : it->second)
                if (u.name == n) return u.count;
            return 0;
        };
        // summary card
        {
            uint64_t mins = get(L"Time", L"minutes in use"), longest = get(L"Time", L"longest session (minutes)");
            wstr first;
            for (auto& g : groups)
                for (const UseItem& u : g.second)
                    if (!u.first.empty() && (first.empty() || u.first < first)) first = u.first;
            auto body = [&](bool draw) {
                float pad = S(14), cx = x + pad, cw = w - pad * 2, yy = y + pad;
                if (draw) text(T(L"Usage statistics"), RectF(cx, yy, cw, S(24)), fonts().uiSemi, theme().label);
                yy += S(28);
                yy += para(TF(L"Started {0} times · {1} h {2} min in use · longest session {3} min · since {4}", std::to_wstring(get(L"Program", L"starts")), std::to_wstring(mins / 60),
                              std::to_wstring(mins % 60), std::to_wstring(longest), first.empty() ? wstr(L"-") : first),
                           cx, yy, cw, fonts().small, theme().label2, draw) + S(4);
                yy += para(TF(L"Kept only on this PC (or the rescue stick), in {0}.", usesPath()), cx, yy, cw, fonts().small, theme().label3, draw) + S(8);
                float bx = cx;
                for (auto& b : std::vector<std::pair<wstr, int>>{{T(L"Copy"), 0}, {T(L"Reset…"), 1}}) {
                    float bw = buttonWidth(b.first);
                    if (draw) drawButton(RectF(bx, yy, bw, S(28)), b.first, H_STATS, b.second, kit::ButtonStyle::Normal);
                    bx += bw + S(8);
                }
                yy += S(28) + pad;
                return yy - y;
            };
            float h = body(false);
            card(RectF(x, y, w, h));
            body(true);
            y += h + S(14);
        }
        if (groups.empty()) {
            y += para(T(L"Nothing counted yet."), x + S(4), y, w, fonts().ui, theme().label3, true);
            return y - top + S(10);
        }
        for (auto& g : groups) {
            drawSectionHeader(RectF(x + S(4), y, w, S(24)), T(g.first.c_str()));
            y += S(28);
            std::sort(g.second.begin(), g.second.end(), [](const UseItem& a, const UseItem& b) { return a.count > b.count; });
            for (const UseItem& u : g.second) {
                text(std::to_wstring(u.count), RectF(x + S(4), y, S(60), S(22)), fonts().uiSemi, theme().label, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
                text(T(u.name.c_str()), RectF(x + S(76), y, w * 0.55f, S(22)), fonts().ui, theme().label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                text(TF(L"last {0}", u.last), RectF(x + S(80) + w * 0.55f, y, w * 0.45f - S(84), S(22)), fonts().small, theme().label3, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                y += S(24);
            }
            y += S(12);
        }
        return y - top + S(10);
    }

    void statsAction(int a) {
        if (a == 0) {
            if (copyText(usesText())) showToast(L"check", T(L"Usage statistics copied"), T(L"Paste them where you need them."), {}, nullptr);
            return;
        }
        showConfirm(T(L"Reset the usage statistics?"), T(L"Every count starts again from zero."), {T(L"Cancel"), T(L"Reset")}, 1, 0, true, [this](int b) {
            if (b != 1) return;
            {
                std::lock_guard<std::mutex> l(useLock_);
                uses_.clear();
                usesDirty_ = true;
            }
            saveUses();
            invalidate();
        });
    }

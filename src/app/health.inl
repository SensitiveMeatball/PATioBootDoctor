// Part of class DoctorApp (included inside the class in main.cpp).
// ---------------------------------------------------------------- an unhealthy disk: back up before anything else
// A disk whose S.M.A.R.T. / NVMe health reports a problem can stop working at any moment: while it is
// being changed, or the next time the PC starts. Before anything changes it, a backup is offered: single
// files with PATio Commander, or the whole disk as a copy (disk image) onto another removable drive.
    static bool diskUnhealthy(const Disk& d) {
        const DiskHealth& h = d.health;
        return h.known && (h.level >= 1 || h.reallocated > 0 || h.pending > 0 || h.uncorrectable > 0 || h.mediaErrors > 0);
    }

    // "26 reallocated sectors, 3 waiting to be reallocated" (or the health summary)
    wstr healthDetail(const Disk& d) {
        const DiskHealth& h = d.health;
        std::vector<wstr> bits;
        if (h.reallocated > 0) bits.push_back(TF(L"{0} reallocated sectors", std::to_wstring(h.reallocated)));
        if (h.pending > 0) bits.push_back(TF(L"{0} sectors waiting to be reallocated", std::to_wstring(h.pending)));
        if (h.uncorrectable > 0) bits.push_back(TF(L"{0} sectors that could not be read", std::to_wstring(h.uncorrectable)));
        if (h.mediaErrors > 0) bits.push_back(TF(L"{0} media errors", std::to_wstring(h.mediaErrors)));
        for (const wstr& p : h.problems)
            if (bits.size() < 5) bits.push_back(p);
        wstr s;
        for (size_t i = 0; i < bits.size(); ++i) s += (i ? L"; " : L"") + bits[i];
        return s.empty() ? h.summary : s;
    }

    wstr commanderPath() {
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        return findCommander(exe);
    }

    void launchCommander() {
        countUse(L"Tools: PATio Commander started");
        wstr exe = commanderPath();
        if (exe.empty()) {
            showMessage(T(L"PATio Commander was not found"),
                        T(L"It is not next to this program. Start it from its own folder (PATioCommander_x64.exe, one level above the PatSuite folder), or from the rescue stick, where it starts together with PATio Boot Doctor."));
            return;
        }
        wstr cmd = L"\"" + exe + L"\"";
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(0);
        STARTUPINFOW si{sizeof si};
        PROCESS_INFORMATION pi{};
        wstr dir = exe.substr(0, exe.find_last_of(L'\\'));
        if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        } else
            showMessage(T(L"PATio Commander could not be started"), errorText(GetLastError()));
    }

    wstr backupAdvice() {
        wstr how = commanderPath().empty()
                       ? T(L"Start PATio Commander from its own folder (or from the rescue stick, where it starts together with this program)")
                       : (g_sys.winPE ? T(L"On the rescue stick it is already running next to this program; or start it with the button below")
                                      : T(L"Start it with the button below"));
        return T(L"Do not restart this PC until what matters is backed up: a failing drive may not start again.\n\n") +
               T(L"• Single files: PATio Commander copies them to a removable drive. ") + how + L".\n" +
               T(L"• The whole disk: Disks and partitions > this disk > Copy the whole disk (disk image), onto another removable drive. Further repairs are then safest on the copy.");
    }

    // Asks before a change to an unhealthy disk. proceed runs when the technician goes on anyway.
    void showDiskHealthWarning(int disk, std::function<void()> proceed) {
        const Disk* d = nullptr;
        for (const Disk& x : rep_.m.disks)
            if (x.number == disk) d = &x;
        if (!d) {
            proceed();
            return;
        }
        wstr msg = TF(L"{0} reports a health problem: {1}.\n\nA failing drive can stop working at any moment — while it is being changed, or the next time the PC starts.\n\n", d->name(), healthDetail(*d)) + backupAdvice();
        showConfirm(T(L"This disk is not healthy — back up first"), msg,
                    {T(L"Cancel"), T(L"Start PATio Commander"), T(L"Copy the whole disk"), T(L"Change it anyway")}, 1, 0, false,
                    [this, disk, proceed](int b) {
                        if (b == 1) launchCommander();
                        else if (b == 2) askImageTarget(disk);
                        else if (b == 3) proceed();
                    });
    }

    // Once per disk and session, right after a scan: say so before anything else is done.
    void warnUnhealthyAfterScan() {
        if (snapshot_) return;
        for (const Disk& d : rep_.m.disks) {
            if (!diskUnhealthy(d) || healthAlerted_.count(d.id())) continue;
            healthAlerted_.insert(d.id());
            wstr msg = TF(L"{0} reports a health problem: {1}.\n\n", d.name(), healthDetail(d)) + backupAdvice();
            int disk = d.number;
            showConfirm(T(L"A disk is failing — back up before anything else"), msg,
                        {T(L"Later"), T(L"Start PATio Commander"), T(L"Copy the whole disk")}, 1, 0, false, [this, disk](int b) {
                            if (b == 1) launchCommander();
                            else if (b == 2) askImageTarget(disk);
                        });
            return;  // one at a time
        }
    }

    // An amber card at the top of Home while any disk is unhealthy.
    float drawHealthBanner(float x, float y, float w) {
        const Disk* bad = nullptr;
        for (const Disk& d : rep_.m.disks)
            if (diskUnhealthy(d) && !bad) bad = &d;
        if (!bad) return 0;
        int disk = bad->number;
        wstr title = TF(L"{0} is failing", bad->name());
        wstr body = healthDetail(*bad) + L". " + T(L"Back up before any repair, and do not restart until you have: a failing drive may not start again.");
        float bwA = buttonWidth(T(L"Start PATio Commander")), bwB = buttonWidth(T(L"Copy the whole disk"));
        auto draw = [&](bool on) {
            float pad = S(14), cx = x + pad + S(34), cw = w - pad * 2 - S(34) - bwA - bwB - S(16), yy = y + pad;
            if (on) {
                drawIcon("warn", x + pad, yy, S(22), kAmber);
                text(title, RectF(cx, yy, cw, S(22)), fonts().uiSemi, theme().label);
            }
            yy += S(26);
            yy += para(body, cx, yy, cw, fonts().small, theme().label2, on);
            if (on) {
                float bx = x + w - pad - bwB;
                drawButton(RectF(bx, y + pad, bwB, S(26)), T(L"Copy the whole disk"), H_HEALTH, disk * 2 + 1, kit::ButtonStyle::Normal);
                drawButton(RectF(bx - S(8) - bwA, y + pad, bwA, S(26)), T(L"Start PATio Commander"), H_HEALTH, disk * 2, kit::ButtonStyle::Primary);
            }
            return std::max(yy, y + pad + S(30)) + pad - y;
        };
        float h = draw(false);
        card(RectF(x, y, w, h));
        canvas().fill(RectF(x, y + S(6), S(3), h - S(12)), kAmber);
        draw(true);
        return h + S(12);
    }

    void healthAction(int a) {
        if (a % 2 == 0)
            launchCommander();
        else
            askImageTarget(a / 2);
    }

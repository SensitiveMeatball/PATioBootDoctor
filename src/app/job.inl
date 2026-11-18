// Part of class DoctorApp (included inside the class in main.cpp).
// ---------------------------------------------------------------- the job: how it came in, and everything done to it
// A job starts with the state the PC (or a disk or removable device) was handed in, and whether its BIOS
// battery works. From then on every step the program takes, every test and every note is added to the
// job's action log (PATioBootDoctorData\Jobs), with the time. A job carries over a restart (for example of
// the rescue stick) until the technician ends it. Technician mode offers to start one after the first scan.
    struct JobInfo {
        bool open = false;
        wstr file, pc, what, state, battery, started;
    };

    wstr jobsFolder() {
        wstr d = dataFolder() + L"\\Jobs";
        CreateDirectoryW(d.c_str(), nullptr);
        return d;
    }

    static wstr nowStamp() {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t b[32];
        swprintf(b, 32, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
        return b;
    }

    wstr technicianName() { return settings().get(L"technicianName"); }

    void loadJob() {
        if (snapshot_) return;
        JobInfo j;
        j.file = settings().get(L"jobFile");
        if (j.file.empty() || GetFileAttributesW(j.file.c_str()) == INVALID_FILE_ATTRIBUTES) return;
        j.open = true;
        j.pc = settings().get(L"jobPc");
        j.what = settings().get(L"jobWhat");
        j.state = settings().get(L"jobState");
        j.battery = settings().get(L"jobBattery");
        j.started = settings().get(L"jobStarted");
        std::lock_guard<std::mutex> l(jobLock_);
        jobRec_ = j;
    }

    void writeJobFile(const wstr& path, const std::string& u, bool append) {
        HANDLE h = CreateFileW(path.c_str(), append ? FILE_APPEND_DATA : GENERIC_WRITE, FILE_SHARE_READ, nullptr, append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        DWORD wr = 0;
        WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr);
        CloseHandle(h);
    }

    // Called for every line of the session log, also from worker threads.
    void appendJobLog(const wstr& text) {
        std::lock_guard<std::mutex> l(jobLock_);
        if (!jobRec_.open || jobRec_.file.empty()) return;
        wstr line = text;
        for (wchar_t& c : line)
            if (c == L'\r' || c == L'\n') c = L' ';
        writeJobFile(jobRec_.file, wideToUtf8(nowStamp() + L"  " + line + L"\r\n"), true);
    }

    // The name of the Windows the job is about (from its own settings), else the PC's maker and model.
    wstr jobPcName() {
        for (const Install& in : rep_.installs) {
            if (in.linux || in.windowsOld) continue;
            if (in.running) {
                wstr n = thisPcName();
                if (!n.empty()) return n;
                continue;
            }
            LoadedHive h;
            wstr err;
            if (!h.load(in.winDir + L"\\System32\\config\\SYSTEM", true, err)) continue;
            DWORD cur = 1;
            regDword(h.root(), L"Select", L"Current", cur);
            wchar_t cs[64];
            swprintf(cs, 64, L"ControlSet%03u\\Control\\ComputerName\\ComputerName", (unsigned)cur);
            wstr n = regString(h.root(), cs, L"ComputerName");
            if (!n.empty()) return n;
        }
        const Firmware& fw = rep_.m.fw;
        wstr mm = fw.maker + (fw.model.empty() ? wstr() : L" " + fw.model);
        return mm.empty() ? wstr(T(L"this PC")) : mm;
    }

    void askTechnicianThen(std::function<void()> next) {
        if (!technicianName().empty()) {
            next();
            return;
        }
        showInput(T(L"Who is working on it?"), T(L"Your name goes on job sheets, action logs and collected hardware details. It is asked once."), L"", T(L"Next"),
                  [this, next](const wstr& t) {
                      wstr n = t;
                      while (!n.empty() && n.back() == L' ') n.pop_back();
                      if (!n.empty()) settings().set(L"technicianName", n);
                      defer(next);
                      return true;
                  });
    }

    void askJobIntake() {
        if (snapshot_) return;
        askTechnicianThen([this] {
            wstr pc = jobPcName();
            showInput(T(L"New job: how was it handed in?"),
                      TF(L"Write down the state of {0} as it came in: what the customer said, what it does when switched on, and anything you notice (damage, noises, missing parts). This starts the job's action log: every step from now on is added to it, with the time.", pc),
                      L"", T(L"Next"), [this, pc](const wstr& state) {
                          defer([this, pc, state] { askJobDetails(pc, state); });
                          return true;
                      });
        });
    }

    void askJobDetails(const wstr& pc, const wstr& state) {
        std::vector<kit::FormField> f(4);
        f[0].kind = kit::FormField::Choice;
        f[0].id = 1;
        f[0].label = T(L"Handed in");
        f[0].options = {T(L"The whole PC"), T(L"A disk or removable device")};
        f[1].kind = kit::FormField::Toggle;
        f[1].id = 2;
        f[1].label = T(L"The BIOS battery works");
        f[1].tip = T(L"Tick it when the PC keeps its date, time and firmware settings after being unplugged.");
        f[2].kind = kit::FormField::Note;
        f[2].label = T(L"A flat BIOS battery resets the firmware settings whenever the PC is unplugged — the clock, the boot order and the SATA mode (AHCI / RAID), which alone can stop Windows starting.");
        f[3].kind = kit::FormField::Note;
        f[3].label = TF(L"Technician: {0}", technicianName());
        showForm(TF(L"New job: {0}", pc), f, T(L"Start the job"), [this, pc, state](const std::vector<kit::FormField>& v) {
            int what = v.size() > 0 ? v[0].value : 0;
            bool battery = v.size() > 1 && v[1].value != 0;
            startJob(pc, state, what, battery);
        });
    }

    void startJob(const wstr& pc, const wstr& state, int what, bool battery) {
        JobInfo j;
        j.open = true;
        j.pc = pc;
        j.what = what == 0 ? T(L"The whole PC") : T(L"A disk or removable device");
        j.state = state;
        j.battery = what != 0 ? wstr(T(L"not applicable (no PC)")) : battery ? wstr(T(L"works")) : wstr(T(L"NOT working (or not checked)"));
        j.started = nowStamp();
        wstr safe;
        for (wchar_t c : pc) safe += (iswalnum(c) || c == L' ' || c == L'-' || c == L'_') ? c : L'_';
        wstr stamp = j.started;
        for (wchar_t& c : stamp)
            if (c == L':') c = L'-';
        j.file = jobsFolder() + L"\\" + stamp + L" " + safe + L".txt";
        const Firmware& fw = rep_.m.fw;
        wstr wins;
        for (const Install& in : rep_.installs) wins += (wins.empty() ? L"" : L"; ") + in.label() + L" — " + in.verdictTitle;
        wstr head = L"PATio Boot Doctor " + wstr(kVersion) + L" — " + T(L"job") + L"\r\n" + T(L"Started: ") + j.started + L"\r\n" + T(L"Technician: ") + technicianName() + L"\r\n" +
                    T(L"Handed in: ") + j.what + L"\r\n" + T(L"PC: ") + pc + (fw.maker.empty() ? wstr() : L" · " + fw.maker + L" " + fw.model) + L"\r\n" +
                    T(L"Windows found: ") + (wins.empty() ? wstr(T(L"none")) : wins) + L"\r\n" + T(L"BIOS battery: ") + j.battery + L"\r\n" + T(L"State when handed in:") + L"\r\n" +
                    (state.empty() ? wstr(T(L"(not written down)")) : state) + L"\r\n\r\n" + T(L"ACTION LOG") + L"\r\n";
        writeJobFile(j.file, "\xEF\xBB\xBF" + wideToUtf8(head), false);
        settings().set(L"jobFile", j.file);
        settings().set(L"jobPc", j.pc);
        settings().set(L"jobWhat", j.what);
        settings().set(L"jobState", j.state);
        settings().set(L"jobBattery", j.battery);
        settings().set(L"jobStarted", j.started);
        {
            std::lock_guard<std::mutex> l(jobLock_);
            jobRec_ = j;
        }
        appendJobLog(T(L"Job started"));
        countUse(L"job: started");
        showToast(L"check", TF(L"Job started: {0}", pc), T(L"Every step from now on goes into its action log."), {T(L"Open the log")}, [this](int) { openLink(jobRec_.file); });
        invalidate();
    }

    void endJob() {
        if (!jobRec_.open) return;
        appendJobLog(T(L"Job ended"));
        wstr file = jobRec_.file;
        {
            std::lock_guard<std::mutex> l(jobLock_);
            jobRec_ = JobInfo();
        }
        for (const wchar_t* k : {L"jobFile", L"jobPc", L"jobWhat", L"jobState", L"jobBattery", L"jobStarted"}) settings().set(k, L"");
        showToast(L"check", T(L"Job ended"), file, {T(L"Open the log")}, [this, file](int) { openLink(file); });
        invalidate();
    }

    // After the first scan in Technician mode: offer to start a job (a toast, so it never blocks a warning).
    void offerJobAfterScan() {
        if (snapshot_ || jobOffered_ || !techMode_ || jobRec_.open || rep_.installs.empty()) return;
        jobOffered_ = true;
        showToast(L"bd_note", T(L"Start a job?"), T(L"Write down how the PC was handed in and whether its BIOS battery works; every step is then logged."), {T(L"Start a job"), T(L"Not now")},
                  [this](int b) {
                      if (b == 0) askJobIntake();
                  }, false);
    }

    // The card at the top of Records > Notes.
    float drawJobCard(float x, float y, float w) {
        float top = y;
        std::vector<std::pair<wstr, int>> btns;
        if (jobRec_.open) {
            btns = {{T(L"Open the action log"), 1}, {T(L"End the job"), 2}};
        } else
            btns = {{T(L"Start a job…"), 0}};
        auto body = [&](bool draw) {
            float pad = S(14), cx = x + pad + S(34), cw = w - pad * 2 - S(34), yy = y + pad;
            if (draw) {
                drawIcon("bd_note", x + pad, yy + S(2), S(22), theme().accent);
                text(jobRec_.open ? TF(L"Job: {0}", jobRec_.pc) : T(L"No job open"), RectF(cx, yy, cw, S(22)), fonts().uiSemi, theme().label);
            }
            yy += S(26);
            if (jobRec_.open) {
                yy += para(TF(L"Started {0} · {1} · BIOS battery: {2}", jobRec_.started, jobRec_.what, jobRec_.battery), cx, yy, cw, fonts().small, theme().label2, draw) + S(4);
                if (!jobRec_.state.empty()) yy += para(TF(L"Handed in: {0}", jobRec_.state), cx, yy, cw, fonts().small, theme().label, draw) + S(4);
                yy += para(TF(L"Every step goes into {0}", jobRec_.file), cx, yy, cw, fonts().small, theme().label3, draw) + S(8);
            } else
                yy += para(T(L"A job keeps how the PC (or a disk) was handed in, whether its BIOS battery works, and an action log of every step, with times. It also goes on the job sheet."), cx, yy, cw, fonts().small,
                           theme().label2, draw) + S(8);
            float bx = cx;
            for (auto& b : btns) {
                float bw = buttonWidth(b.first);
                if (draw) drawButton(RectF(bx, yy, bw, S(28)), b.first, H_JOB, b.second, b.second == 0 ? kit::ButtonStyle::Primary : kit::ButtonStyle::Normal);
                bx += bw + S(8);
            }
            yy += S(28) + pad;
            return yy - y;
        };
        float h = body(false);
        card(RectF(x, y, w, h));
        body(true);
        y += h + S(12);
        return y - top;
    }

    void jobAction(int a) {
        if (a == 0) askJobIntake();
        else if (a == 1 && jobRec_.open) openLink(jobRec_.file);
        else if (a == 2) endJob();
    }

    // ---------------------------------------------------------------- the PC's clock (a flat BIOS battery)
    // A PC whose clock is behind the last time Windows ran has almost certainly lost its firmware settings:
    // its BIOS battery is flat. That also resets the SATA mode and the boot order.
    wstr clockBehindNote() {
        SYSTEMTIME st;
        GetSystemTime(&st);
        FILETIME now;
        SystemTimeToFileTime(&st, &now);
        uint64_t n = (uint64_t)now.dwHighDateTime << 32 | now.dwLowDateTime, last = 0;
        wstr lastText;
        for (const Install& in : rep_.installs) {
            if (in.linux || in.running) continue;
            WIN32_FILE_ATTRIBUTE_DATA a{};
            if (!GetFileAttributesExW((in.winDir + L"\\System32\\config\\SYSTEM").c_str(), GetFileExInfoStandard, &a)) continue;
            uint64_t t = (uint64_t)a.ftLastWriteTime.dwHighDateTime << 32 | a.ftLastWriteTime.dwLowDateTime;
            if (t > last) {
                last = t;
                FILETIME lf;
                SYSTEMTIME s2;
                FileTimeToLocalFileTime(&a.ftLastWriteTime, &lf);
                FileTimeToSystemTime(&lf, &s2);
                wchar_t b[32];
                swprintf(b, 32, L"%04d-%02d-%02d %02d:%02d", s2.wYear, s2.wMonth, s2.wDay, s2.wHour, s2.wMinute);
                lastText = b;
            }
        }
        const uint64_t day = 864000000000ULL;
        if (st.wYear < 2020 || (last && n + 2 * day < last))
            return TF(L"This PC's clock says {0}, but Windows last ran on {1}: the clock was reset, so the BIOS battery is most likely flat. A flat battery also resets the firmware settings — the boot order and the SATA mode (AHCI / RAID), which can stop Windows starting. Set them again, and replace the battery (usually a CR2032 coin cell).",
                      nowStamp(), lastText.empty() ? wstr(T(L"a later date")) : lastText);
        return L"";
    }

    float drawClockBanner(float x, float y, float w) {
        if (clockNote_.empty()) return 0;
        auto draw = [&](bool on) {
            float pad = S(14), cx = x + pad + S(34), cw = w - pad * 2 - S(34), yy = y + pad;
            if (on) {
                drawIcon("warn", x + pad, yy, S(22), kAmber);
                text(T(L"The BIOS battery looks flat"), RectF(cx, yy, cw, S(22)), fonts().uiSemi, theme().label);
            }
            yy += S(26);
            yy += para(clockNote_, cx, yy, cw, fonts().small, theme().label2, on);
            return yy + pad - y;
        };
        float h = draw(false);
        card(RectF(x, y, w, h));
        canvas().fill(RectF(x, y + S(6), S(3), h - S(12)), kAmber);
        draw(true);
        return h + S(12);
    }

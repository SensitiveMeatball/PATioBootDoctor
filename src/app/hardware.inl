// Part of class DoctorApp (included inside the class in main.cpp).
// ---------------------------------------------------------------- hardware details, kept per PC
// Each collection is a text file in PATioBootDoctorData\Hardware named after the date and the PC (maker,
// model and serial number), so every PC's details are kept separately and can be compared over time.
    struct HwFile {
        wstr path, title, when, by;
    };

    wstr hardwareFolder() {
        wstr d = dataFolder() + L"\\Hardware";
        CreateDirectoryW(d.c_str(), nullptr);
        return d;
    }

    void refreshHardwareFiles() {
        hwFiles_.clear();
        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW((hardwareFolder() + L"\\*.txt").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            HwFile f;
            f.path = hardwareFolder() + L"\\" + fd.cFileName;
            wstr n = fd.cFileName;
            n = n.substr(0, n.size() - 4);
            f.when = n.size() > 16 ? n.substr(0, 16) : n;
            for (wchar_t& c : f.when)
                if (c == L'-' && &c - &f.when[0] > 10) c = L':';
            f.title = n.size() > 17 ? n.substr(17) : n;
            std::string head;
            if (readFileText(f.path, head, 2048)) {
                wstr t = utf8ToWide(head);
                size_t a = t.find(L'\n');
                size_t b = a == wstr::npos ? wstr::npos : t.find(L'\n', a + 1);
                if (b != wstr::npos) {
                    wstr l = t.substr(a + 1, b - a - 1);
                    size_t c = l.find(L": ");
                    f.by = c == wstr::npos ? wstr() : l.substr(c + 2);
                    while (!f.by.empty() && (f.by.back() == L'\r' || f.by.back() == L' ')) f.by.pop_back();
                }
            }
            hwFiles_.push_back(f);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        std::sort(hwFiles_.begin(), hwFiles_.end(), [](const HwFile& a, const HwFile& b) { return a.when > b.when; });
    }

    void collectHardware() {
        if (busy_ || snapshot_) return;
        askTechnicianThen([this] {
            auto text = std::make_shared<wstr>();
            auto title = std::make_shared<wstr>();
            Report copy = rep_;
            wstr tech = technicianName();
            wstr fallback = jobPcName();
            runJob(T(L"Collecting the hardware details"), fallback, false,
                   [copy, tech, fallback, text, title] {
                       *text = hardwareReport(copy, tech, kVersion);
                       *title = hardwareTitle(readSmbios(), fallback);
                   },
                   [this, text, title] {
                       wstr safe;
                       for (wchar_t c : *title) safe += (iswalnum(c) || c == L' ' || c == L'-' || c == L'_' || c == L'(' || c == L')' || c == L'.') ? c : L'_';
                       if (safe.size() > 90) safe.resize(90);
                       wstr stamp = nowStamp();
                       for (wchar_t& c : stamp)
                           if (c == L':') c = L'-';
                       wstr path = hardwareFolder() + L"\\" + stamp + L" " + safe + L".txt";
                       std::string u = "\xEF\xBB\xBF" + wideToUtf8(*text);
                       HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                       bool ok = false;
                       if (h != INVALID_HANDLE_VALUE) {
                           DWORD wr = 0;
                           ok = WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr) != 0;
                           CloseHandle(h);
                       }
                       countUse(L"Tools: hardware details collected");
                       appendJobLog(TF(L"Hardware details collected: {0}", path));
                       refreshHardwareFiles();
                       hwShown_ = path;
                       if (ok) showToast(L"check", T(L"Hardware details saved"), *title, {T(L"Open")}, [this, path](int) { openLink(path); });
                       else showMessage(T(L"The details were not saved"), path);
                       invalidate();
                   });
        });
    }

    float drawHardware(float x, float y, float w) {
        float top = y;
        if (!hwListed_ && !snapshot_) {
            hwListed_ = true;
            refreshHardwareFiles();
        }
        auto body = [&](bool draw) {
            float pad = S(14), cx = x + pad + S(34), cw = w - pad * 2 - S(34), yy = y + pad;
            if (draw) {
                drawIcon("bd_chip", x + pad, yy + S(2), S(22), theme().accent);
                text(T(L"Hardware details"), RectF(cx, yy, cw, S(22)), fonts().uiSemi, theme().label);
            }
            yy += S(26);
            yy += para(T(L"Collects this PC's details: maker, model and serial number, BIOS, mainboard, processor, every memory module, disks and their health, storage controllers, graphics, network and the systems on it — with who collected them and when. Each PC is kept separately."),
                       cx, yy, cw, fonts().small, theme().label2, draw) + S(4);
            if (!technicianName().empty()) yy += para(TF(L"Collected by: {0}", technicianName()), cx, yy, cw, fonts().small, theme().label3, draw) + S(4);
            yy += S(4);
            float bw = buttonWidth(T(L"Collect this PC's details"));
            if (draw) drawButton(RectF(cx, yy, bw, S(28)), T(L"Collect this PC's details"), H_HW, -1, kit::ButtonStyle::Primary);
            float bw2 = buttonWidth(T(L"Open the folder"));
            if (draw) drawButton(RectF(cx + bw + S(8), yy, bw2, S(28)), T(L"Open the folder"), H_HW, -2, kit::ButtonStyle::Normal);
            yy += S(28) + pad;
            return yy - y;
        };
        float h = body(false);
        card(RectF(x, y, w, h));
        body(true);
        y += h + S(14);
        if (hwFiles_.empty()) {
            y += para(T(L"Nothing collected yet."), x + S(4), y, w, fonts().ui, theme().label3, true);
            return y - top + S(10);
        }
        drawSectionHeader(RectF(x + S(4), y, w, S(24)), T(L"Collected (newest first)"));
        y += S(28);
        for (int i = 0; i < (int)hwFiles_.size(); ++i) {
            const HwFile& f = hwFiles_[i];
            RectF rr(x, y, w, S(30));
            bool sel = f.path == hwShown_;
            drawRow(rr, sel, true, hovered(H_HW, i), sel, false, false);
            addHit(rr, H_HW, i);
            text(f.title, RectF(x + S(10), y, w * 0.55f, S(30)), fonts().uiSemi, rowText(sel, true, false), DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            text(f.when + (f.by.empty() ? wstr() : L" · " + f.by), RectF(x + S(16) + w * 0.55f, y, w * 0.45f - S(24), S(30)), fonts().small, rowText(sel, true, true),
                 DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            y += S(32);
        }
        return y - top + S(10);
    }

    void hardwareAction(int a) {
        if (a == -1) collectHardware();
        else if (a == -2) openLink(hardwareFolder());
        else if (a >= 0 && a < (int)hwFiles_.size()) {
            hwShown_ = hwFiles_[a].path;
            openLink(hwFiles_[a].path);
        }
    }

// Part of class DoctorApp (included inside the class in main.cpp).
    // ---------------------------------------------------------------- job sheet (one page: PDF and TXT)
    struct SheetLine {
        int style;   // 0 text, 1 section heading, 2 small, 3 gap
        wstr text;
    };

    std::vector<SheetLine> sheetLines() {
        std::vector<SheetLine> L;
        auto sec = [&](const wstr& t) {
            L.push_back({3, L""});
            L.push_back({1, t});
        };
        const Firmware& fw = rep_.m.fw;
        sec(T(L"PC"));
        wstr pc = thisPcName();
        L.push_back({0, (pc.empty() ? wstr() : pc + L" · ") + fw.maker + (fw.model.empty() ? wstr() : L" " + fw.model)});
        L.push_back({0, (fw.uefi ? T(L"Starts in UEFI mode") : T(L"Starts in legacy BIOS mode")) + (fw.secureBootKnown ? (fw.secureBoot ? T(L" · Secure Boot on") : T(L" · Secure Boot off")) : wstr()) + T(L" · checked from ") + rep_.host});
        L.push_back({0, T(L"Customer: ______________________________     Technician: ") + (technicianName().empty() ? wstr(L"______________________________") : technicianName())});
        if (jobRec_.open) {
            sec(T(L"As handed in"));
            L.push_back({0, jobRec_.what + L" · " + T(L"BIOS battery: ") + jobRec_.battery + L" · " + T(L"job started ") + jobRec_.started});
            if (!jobRec_.state.empty()) L.push_back({0, jobRec_.state});
        }
        if (!clockNote_.empty()) L.push_back({2, clockNote_});
        sec(T(L"Systems found"));
        for (const Install& in : rep_.installs) L.push_back({0, in.label() + L" · " + in.where + L" — " + in.verdictTitle});
        if (rep_.installs.empty()) L.push_back({0, T(L"None")});
        sec(T(L"Disks"));
        for (const Disk& d : rep_.m.disks) {
            wstr h = !d.health.known ? wstr(T(L"health not reported")) : d.health.level == 2 ? wstr(T(L"FAILING")) : d.health.level == 1 ? wstr(T(L"worn")) : wstr(T(L"healthy"));
            auto st = surface_.find(d.id());
            L.push_back({0, d.name() + L" · " + d.typeText + L" — " + h + (d.health.known ? L" (" + d.health.summary + L")" : wstr())});
            if (st != surface_.end()) L.push_back({2, T(L"Surface test: ") + st->second});
        }
        sec(T(L"Findings"));
        int shown = 0;
        for (const Finding& f : rep_.pc) {
            if (f.health != Health::Bad && f.health != Health::Warn) continue;
            if (++shown > 10) break;
            L.push_back({0, wstr(T(healthWord(f.health))) + L": " + f.title});
        }
        for (const Install& in : rep_.installs)
            for (const Link& l : in.chain)
                if (l.health == Health::Bad && ++shown <= 14) L.push_back({0, in.name + L" · " + T(l.name) + L": " + l.plain});
        if (!shown) L.push_back({0, T(L"Nothing needs attention.")});
        sec(T(L"Work done in this session"));
        bool any = false;
        for (const SessionInfo& si : sessions_) {
            if (sessionFolder_.empty() || !iequals(si.dir, sessionFolder_)) continue;
            for (size_t a = 1; a < si.actions.size(); ++a) {  // the first entry is the automatic backup
                const ActionInfo& ai = si.actions[a];
                any = true;
                L.push_back({0, (ai.time.size() >= 16 ? ai.time.substr(11, 5) : ai.time) + L"  " + ai.title + L" — " + (!ai.ok ? T(L"FAILED") : ai.undone ? T(L"undone") : T(L"done"))});
                if (!ai.result.empty()) L.push_back({2, ai.result.substr(0, ai.result.find(L'\n'))});
            }
        }
        if (!any) L.push_back({0, T(L"No changes were made.")});
        if (!notes_.empty()) {
            sec(T(L"Notes"));
            for (const Note& n : notes_) L.push_back({0, n.when + L"  " + n.text});
        }
        return L;
    }

    // Draws the sheet at a text size; returns the height it needs. The page is 1240 x 1754 (A4 at 150 dpi).
    float layoutSheet(Canvas& cv, float px, bool draw) {
        const float W = 1240, M = 80;
        auto font = [&](float size, bool bold) { return CreateFontW(-(int)size, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI"); };
        HFONT fText = font(px, false), fHead = font(px * 1.08f, true), fSmall = font(px * 0.86f, false), fTitle = font(34, true);
        const Color ink(20, 24, 32), grey(90, 96, 108), line(205, 210, 218), accent(10, 110, 200);
        float y = 60;
        if (draw) {
            if (loadLogo()) {
                float lh = 84, lw = lh * logo_.w / logo_.h;
                cv.imageScaled(logo_, RectF(M, y, lw, lh));
            }
            SYSTEMTIME st;
            GetLocalTime(&st);
            wchar_t d[48];
            swprintf(d, 48, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
            cv.text(T(L"Job sheet"), RectF(W / 2, y, W / 2 - M, 44), fTitle, ink, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
            cv.text(wstr(d) + L" · PATio Boot Doctor " + kVersion, RectF(W / 2, y + 50, W / 2 - M, 26), fSmall, grey, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
            cv.fill(RectF(M, y + 100, W - 2 * M, 2), accent);
        }
        y += 112;
        for (const SheetLine& l : sheetLines()) {
            if (l.style == 3) {
                y += px * 0.5f;
                continue;
            }
            HFONT f = l.style == 1 ? fHead : l.style == 2 ? fSmall : fText;
            float x = l.style == 2 ? M + px * 1.6f : M;
            int h = cv.measureWrapped(l.text, f, (int)(W - M - x));
            if (draw) cv.text(l.text, RectF(x, y, W - M - x, (float)h + 2), f, l.style == 1 ? accent : l.style == 2 ? grey : ink, DT_WORDBREAK | DT_NOPREFIX);
            y += h + px * (l.style == 1 ? 0.35f : 0.22f);
            if (l.style == 1) {
                if (draw) cv.fill(RectF(M, y - px * 0.15f, W - 2 * M, 1), line);
                y += px * 0.2f;
            }
        }
        if (draw) {
            cv.fill(RectF(M, 1754 - 92, W - 2 * M, 1), line);
            cv.text(L"MusPC · info@muspc.com · www.muspc.com", RectF(M, 1754 - 84, W - 2 * M, 28), fSmall, grey, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        for (HFONT f : {fText, fHead, fSmall, fTitle}) DeleteObject(f);
        return y;
    }

    void makeJobSheet() {
        if (!haveReport_) return;
        refreshSessions();
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t stamp[64];
        swprintf(stamp, 64, L"\\Job sheet %04d-%02d-%02d %02d-%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
        wstr base = dataFolder() + stamp;
        // the text version
        wstr t = L"MusPC · " + wstr(T(L"Job sheet")) + L" · PATio Boot Doctor " + kVersion + L"\r\n";
        for (const SheetLine& l : sheetLines())
            t += l.style == 3 ? wstr(L"\r\n") : l.style == 1 ? toUpperCopy(l.text) + L"\r\n" : (l.style == 2 ? L"    " : L"") + l.text + L"\r\n";
        t += L"\r\nMusPC · info@muspc.com · www.muspc.com\r\n";
        std::string u = "\xEF\xBB\xBF" + wideToUtf8(t);
        HANDLE h = CreateFileW((base + L".txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD wr = 0;
        bool okTxt = h != INVALID_HANDLE_VALUE && WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        // the page: the biggest text size at which everything fits on one page
        Canvas cv;
        bool okPdf = cv.resize(1240, 1754);
        if (okPdf) {
            bool rtl = Canvas::rightToLeft();
            Canvas::setRightToLeft(false);
            float px = 24;
            while (px > 11 && layoutSheet(cv, px, false) > 1754 - 110) px -= 1;
            cv.clear(Color(255, 255, 255));
            layoutSheet(cv, px, true);
            cv.fixAlpha();
            Canvas::setRightToLeft(rtl);
            okPdf = writePagePdf(base + L".pdf", cv.bits(), 1240, 1754, "Job sheet");
        }
        if (okTxt || okPdf)
            showConfirm(T(L"Job sheet made"), (okPdf ? base + L".pdf\n" : wstr()) + (okTxt ? base + L".txt" : wstr()) + T(L"\n\nOne page: the PC, the systems and disks, what was found, what was done in this session, and your notes."),
                        {T(L"Close"), T(L"Open the folder")}, 0, 0, false, [this](int b) {
                            if (b == 1) openLink(dataFolder());
                        });
        else showMessage(T(L"The job sheet was not made"), dataFolder());
    }

    static wstr toUpperCopy(wstr s) {
        if (!s.empty()) CharUpperBuffW(&s[0], (DWORD)s.size());
        return s;
    }

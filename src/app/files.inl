// Part of class DoctorApp (included inside the class in main.cpp).
    // ---------------------------------------------------------------- deleted files
    static wstr fileStateText(int st) { return st == 0 ? T(L"Good") : st == 1 ? T(L"May be damaged") : T(L"Probably overwritten"); }

    void startFileScan(int part, bool full) {
        if (part < 0 || part >= (int)rep_.m.parts.size()) return;
        auto out = std::make_shared<FileScan>();
        Partition p = rep_.m.parts[part];
        runJob(full ? T(L"Full search for deleted files") : T(L"Quick search for deleted files"), p.name(), true,
               [this, out, p, full] { *out = scanDeletedFiles(p, full, [this](const wstr& s, float f) { progress(s, f); }, &cancel_); },
               [this, out, part, full] {
                   fileScan_ = std::move(*out);
                   filePart_ = part;
                   fileFull_ = full;
                   fileSel_.assign(fileScan_.files.size(), 0);
                   fileFilter_ = essentialsNext_ ? -2 : -1;
                   essentialsNext_ = false;
                   filterFiles();
                   navPush();
                   scroll_[V_FILES] = 0;
                   setView(V_FILES);
               });
    }

    void filterFiles() {
        fileRows_.clear();
        for (int i = 0; i < (int)fileScan_.files.size(); ++i)
            if (fileFilter_ == -2 ? isWindowsEssential(fileScan_.files[i]) : (fileFilter_ < 0 || fileScan_.files[i].kind == fileFilter_)) fileRows_.push_back(i);
    }

    float drawFiles(float x, float y, float w) {
        float top = y;
        if (filePart_ < 0 || filePart_ >= (int)rep_.m.parts.size()) {
            drawEmptyState(RectF(x, y, w, S(300)), T(L"No search yet"), T(L"In Disks and partitions, click a partition and choose \"Recover deleted files\"."));
            return S(300);
        }
        const Partition& p = rep_.m.parts[filePart_];
        int chosen = 0;
        for (char c : fileSel_) chosen += c ? 1 : 0;
        // header
        {
            auto body = [&](bool draw) {
                float pad = S(14), cx = x + pad + S(32), cw = w - pad * 2 - S(32), yy = y + pad;
                if (draw) {
                    drawIcon("bd_recover", x + pad, yy + S(2), S(22), theme().accent);
                    text(TF(L"Deleted files on {0}", p.name()) + (fileFull_ ? T(L" · full search") : T(L" · quick search")), RectF(cx, yy, cw, S(24)), fonts().uiSemi, theme().label);
                }
                yy += S(28);
                yy += para(fileScan_.note, cx, yy, cw, fonts().small, theme().label2, draw) + S(6);
                yy += para(T(L"Save recovered files to a different drive, never to this one: writing here can overwrite the very files you want back."), cx, yy, cw, fonts().small, kAmber, draw) + S(8);
                float bx = cx;
                auto b = [&](const wstr& t, int hit, kit::ButtonStyle st) {
                    float bw = buttonWidth(t);
                    if (draw) drawButton(RectF(bx, yy, bw, S(28)), t, hit, 0, st);
                    bx += bw + S(8);
                };
                b(chosen ? TF(L"Recover {0} files…", std::to_wstring(chosen)) : wstr(T(L"Recover chosen files…")), H_FILES_RECOVER, chosen ? kit::ButtonStyle::Primary : kit::ButtonStyle::Normal);
                b(T(L"Choose all shown"), H_FILES_ALL, kit::ButtonStyle::Normal);
                b(T(L"Choose none"), H_FILES_NONE, kit::ButtonStyle::Quiet);
                b(T(L"Back to the disks"), H_FILES_BACK, kit::ButtonStyle::Quiet);
                yy += S(28) + pad;
                return yy - y;
            };
            float h = body(false);
            card(RectF(x, y, w, h));
            body(true);
            y += h + S(10);
        }
        // kinds
        {
            int counts[FK_COUNT] = {};
            for (const FoundFile& f : fileScan_.files) ++counts[f.kind];
            float bx = x;
            auto chip = [&](int k, const wstr& t) {
                wstr label = t + L" (" + std::to_wstring(k < 0 ? fileScan_.files.size() : (size_t)counts[k]) + L")";
                float bw = buttonWidth(label);
                if (bx + bw > x + w) return;
                drawButton(RectF(bx, y, bw, S(26)), label, H_FILE_FILTER, k, fileFilter_ == k ? kit::ButtonStyle::Primary : kit::ButtonStyle::Quiet);
                bx += bw + S(6);
            };
            chip(-1, T(L"All"));
            {
                size_t ess = 0;
                for (const FoundFile& f : fileScan_.files) ess += isWindowsEssential(f) ? 1 : 0;
                wstr label = T(L"Windows essentials") + L" (" + std::to_wstring(ess) + L")";
                float bw = buttonWidth(label);
                if (bx + bw <= x + w) {
                    drawButton(RectF(bx, y, bw, S(26)), label, H_FILE_FILTER, -2, fileFilter_ == -2 ? kit::ButtonStyle::Primary : kit::ButtonStyle::Quiet);
                    bx += bw + S(6);
                }
            }
            chip(FK_REGISTRY, T(L"Registry"));
            chip(FK_PICTURE, T(L"Pictures"));
            chip(FK_DOCUMENT, T(L"Documents"));
            chip(FK_MEDIA, T(L"Music and videos"));
            chip(FK_ARCHIVE, T(L"Archives"));
            chip(FK_OTHER, T(L"Other"));
            y += S(36);
        }
        // the list (only visible rows are drawn)
        float rowH = S(30), sizeW = S(90), stateW = S(130);
        int n = (int)fileRows_.size();
        int first = std::max(0, (int)((viewTop_ - y) / rowH) - 1), last = std::min(n, (int)((viewBottom_ - y) / rowH) + 2);
        for (int r = first; r < last; ++r) {
            int i = fileRows_[r];
            const FoundFile& f = fileScan_.files[i];
            RectF rr(x, y + r * rowH, w, rowH);
            bool sel = fileSel_[i] != 0;
            drawRow(rr, false, true, hovered(H_FILE, i), false, false, false);
            addHit(rr, H_FILE, i);
            RectF box(rr.x + S(8), rr.cy() - S(8), S(16), S(16));
            canvas().round(box, S(4), sel ? theme().accent : theme().field);
            canvas().ring(box, S(4), S(1), sel ? theme().accent : theme().label3);
            if (sel) drawIcon("check", box.x + S(1), box.y + S(1), S(14), Color(255, 255, 255));
            float nameW = (w - S(40) - sizeW - stateW) * 0.45f;
            text(f.name, RectF(rr.x + S(34), rr.y, nameW, rowH), fonts().ui, theme().label);
            text(f.folder, RectF(rr.x + S(40) + nameW, rr.y, w - S(40) - nameW - sizeW - stateW, rowH), fonts().small, theme().label2);
            text(sizeText(f.size), RectF(rr.right() - sizeW - stateW, rr.y, sizeW - S(10), rowH), fonts().small, theme().label2, kit::kLine | DT_RIGHT);
            text(fileStateText(f.state), RectF(rr.right() - stateW, rr.y, stateW - S(8), rowH), fonts().smallSemi, f.state == 0 ? kGreen : f.state == 1 ? kAmber : kRed);
        }
        y += n * rowH;
        return y - top + S(10);
    }

    void askRecoverFiles() {
        std::vector<int> which;
        for (int i = 0; i < (int)fileSel_.size(); ++i)
            if (fileSel_[i]) which.push_back(i);
        if (which.empty()) {
            showMessage(T(L"No files chosen"), T(L"Click the files you want back (or \"Choose all shown\"), then try again."));
            return;
        }
        if (filePart_ < 0) return;
        Partition p = rep_.m.parts[filePart_];
        wstr start;
        for (const Partition& q : rep_.m.parts)
            if (q.readable && !q.letter.empty() && q.disk != p.disk && q.freeBytes > (1ull << 30)) start = q.letter + L"\\";
        showFolderPicker(T(L"Where should the files go? (another drive)"), start, T(L"Recover here"), [this, which, p](const wstr& dir) {
            wchar_t vp[MAX_PATH] = {}, vn[MAX_PATH] = {};
            if (GetVolumePathNameW(dir.c_str(), vp, MAX_PATH) && GetVolumeNameForVolumeMountPointW(vp, vn, MAX_PATH) && iequals(vn, p.volume)) {
                showMessage(T(L"Choose another drive"), T(L"This folder is on the drive the files are recovered from. Writing there can overwrite the deleted files. Choose a folder on another drive, for example a USB stick."));
                return;
            }
            uint64_t need = 0;
            for (int i : which) need += fileScan_.files[i].size;
            wstr target = dir;
            while (!target.empty() && target.back() == L'\\') target.pop_back();
            target += L"\\" + T(L"Recovered files");
            auto okN = std::make_shared<int>(0), bad = std::make_shared<int>(0);
            auto log = std::make_shared<wstr>();
            auto scan = std::make_shared<FileScan>(fileScan_);
            runJob(T(L"Recovering files"), TF(L"{0} files, {1}", std::to_wstring(which.size()), sizeText(need)), true,
                   [this, p, scan, which, target, okN, bad, log] { *okN = recoverFiles(p, *scan, which, target, [this](const wstr& s, float f) { progress(s, f); }, &cancel_, *bad, *log); },
                   [this, okN, bad, log, target] {
                       wstr msg = TF(L"{0} files were recovered to {1}.", std::to_wstring(*okN), target);
                       if (*bad) msg += L"\n\n" + TF(L"{0} files came back only partly or not at all; their space was probably used again.", std::to_wstring(*bad)) + L"\n\n" + log->substr(0, 1500);
                       showMessage(*bad ? T(L"Recovered, with some problems") : T(L"Files recovered"), msg);
                       consoleAdd(L"[recover] " + std::to_wstring(*okN) + L" files to " + target);
                   });
        });
    }

    void demoFiles() {
        filePart_ = 4;
        fileFull_ = false;
        fileScan_ = FileScan();
        fileScan_.ntfs = true;
        struct D { const wchar_t* n; const wchar_t* f; uint64_t s; int st; };
        D d[] = {{L"Holiday 2025 (14).jpg", L"Users\\Ann\\Pictures\\Holiday 2025", 3800000, 0}, {L"Holiday 2025 (15).jpg", L"Users\\Ann\\Pictures\\Holiday 2025", 4100000, 0},
                 {L"Tax return 2025.pdf", L"Users\\Ann\\Documents", 812000, 0}, {L"Budget.xlsx", L"Users\\Ann\\Documents", 64000, 1},
                 {L"Thesis final.docx", L"Users\\Ann\\Desktop", 2400000, 0}, {L"Birthday.mp4", L"Users\\Ann\\Videos", 512000000, 1},
                 {L"setup.exe", L"Users\\Ann\\Downloads", 98000000, 2}, {L"Backup.zip", L"(folder unknown)", 1200000000, 1}};
        for (const D& x : d) {
            FoundFile f;
            f.name = x.n;
            f.folder = x.f;
            f.size = x.s;
            f.state = x.st;
            f.kind = fileKindOf(f.name);
            fileScan_.files.push_back(f);
        }
        fileScan_.note = TF(L"{0} files found. Files marked \"may be damaged\" had part of their space used again since they were deleted.", std::to_wstring(fileScan_.files.size()));
        fileSel_.assign(fileScan_.files.size(), 0);
        fileSel_[0] = fileSel_[1] = fileSel_[2] = 1;
        filterFiles();
        view_ = V_FILES;
    }

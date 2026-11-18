// Part of class DoctorApp (included inside the class in main.cpp).
    // ---------------------------------------------------------------- Windows Repair
    void openRepair() {
        if (repInstall_ < 0 || repInstall_ >= (int)rep_.installs.size() || rep_.installs[repInstall_].linux) {
            repInstall_ = -1;
            for (int i = 0; i < (int)rep_.installs.size() && repInstall_ < 0; ++i)
                if (!rep_.installs[i].linux && !rep_.installs[i].windowsOld && rep_.installs[i].verdict == Health::Bad) repInstall_ = i;
            for (int i = 0; i < (int)rep_.installs.size() && repInstall_ < 0; ++i)
                if (!rep_.installs[i].linux && !rep_.installs[i].windowsOld) repInstall_ = i;
        }
        buildRepairTools();
        if (view_ != V_REPAIR) navPush();
        scroll_[V_REPAIR] = 0;
        setView(V_REPAIR);
    }

    Fix repairFix(int kind) {
        Fix f = makeFix(kind);
        if (repInstall_ < 0 || repInstall_ >= (int)rep_.installs.size()) return f;
        const Install& in = rep_.installs[repInstall_];
        f.install = repInstall_;
        f.part = in.part;
        f.disk = rep_.m.parts[in.part].disk;
        f.store = in.store;
        f.entry = in.entryId;
        return f;
    }

    // The tools for the chosen Windows, each checked for whether it can run here (and why not).
    void buildRepairTools() {
        repTools_.clear();
        crashes_.clear();
        if (repInstall_ < 0 || repInstall_ >= (int)rep_.installs.size()) return;
        const Install& in = rep_.installs[repInstall_];
        // one home for each tool: Safe Mode and the other start-up switches are on their own page
        static const int kinds[] = {FX_REBUILD_BOOT, FX_SFC, FX_DISM_HEAL, FX_CHKDSK, FX_RESTORE_HIVE, FX_REVERT_UPDATES, FX_REMOVE_UPDATE,
                                    FX_ENABLE_DRIVERS, FX_CLEAR_SETUP, FX_ENABLE_WINRE, FX_REBUILD_WINRE, FX_CLEAN_SPACE};
        Report tmp = rep_;
        Finding fd;
        for (size_t k = 0; k < sizeof kinds / sizeof kinds[0]; ++k) {
            Fix f = repairFix(kinds[k]);
            if (kinds[k] == FX_CHKDSK) f.title = T(L"Check the Windows drive for errors");
            fd.fixes.push_back(f);
        }
        tmp.pc = {fd};
        checkTools(tmp);
        repTools_ = tmp.pc[0].fixes;
        crashes_ = snapshot_ ? std::vector<CrashInfo>() : recentCrashes(in.winDir, 8);
        bootStage_ = snapshot_ || in.running ? BootStage() : readBootStage(in.winDir);
    }

    float drawRepair(float x, float y, float w) {
        float top = y;
        if (showLessons_)
            y += lessonBox(T(L"The tools Windows' own repair needs, in one place: from writing fresh start-up files to repairing system files and removing a bad update. Choose the Windows first. Start with the report's own plan when there is one; use these when it does not help or when you know what is wrong. Each tool says what it does and asks first."),
                           T(L"Repairing Windows"), x, y, w, true);
        // which Windows
        drawSectionHeader(RectF(x + S(4), y, w, S(24)), T(L"Which Windows"));
        y += S(28);
        {
            float bx = x;
            bool any = false;
            for (int i = 0; i < (int)rep_.installs.size(); ++i) {
                const Install& in = rep_.installs[i];
                if (in.linux || in.windowsOld) continue;
                any = true;
                wstr t = in.label() + (in.running ? T(L" (running now)") : wstr());
                float bw = buttonWidth(t);
                if (bx + bw > x + w && bx > x) {
                    bx = x;
                    y += S(34);
                }
                drawButton(RectF(bx, y, bw, S(28)), t, H_REP_SRC, i, repInstall_ == i ? kit::ButtonStyle::Primary : kit::ButtonStyle::Normal);
                bx += bw + S(8);
            }
            if (!any) text(T(L"No Windows was found. \"Rescue an old Windows\" can still help."), RectF(x + S(4), y, w, S(24)), fonts().ui, theme().label3);
            y += S(42);
        }
        if (repInstall_ < 0 || repInstall_ >= (int)rep_.installs.size()) repTab_ = 6;
        y += drawRepairTabs(x, y, w);
        if (repTab_ >= 1 && repTab_ <= 5) {
            y += repTab_ == 1 ? drawBootOptions(x, y, w) : repTab_ == 2 ? drawDrivers(x, y, w) : repTab_ == 3 ? drawLogs(x, y, w) : repTab_ == 4 ? drawRegistry(x, y, w) : drawAccounts(x, y, w);
            return y - top + S(10);
        }
        if (repTab_ == 0 && repInstall_ >= 0 && !rep_.installs[repInstall_].running) y += drawBootStage(x, y, w);
        if (repTab_ == 0 && repInstall_ >= 0)
            y += toolCard(x, y, w, T(L"System Restore"), T(L"Puts Windows' own files, drivers, programs and settings back to an earlier restore point; personal files stay. Often the quickest fix after a bad driver or update. Works for a Windows that does not start, too."),
                          L"", {{T(L"Open…"), 0}}, H_REP_RESTORE);
        // repair tools
        if (repTab_ == 0 && repInstall_ >= 0 && !repTools_.empty()) {
            drawSectionHeader(RectF(x + S(4), y, w, S(24)), T(L"Repair this Windows"));
            y += S(30);
            for (int k = 0; k < (int)repTools_.size(); ++k) {
                const Fix& f = repTools_[k];
                auto body = [&](bool draw) {
                    float pad = S(12), cx = x + pad, cw = w - pad * 2 - S(150), yy = y + pad;
                    if (draw) text(f.title, RectF(cx, yy, cw, S(22)), fonts().uiSemi, f.available ? theme().label : theme().label3);
                    yy += S(24);
                    if (!techMode_) yy += para(f.what, cx, yy, cw, fonts().small, theme().label2, draw) + S(2);
                    else {
                        wstr c = fixCommands(f, rep_);
                        if (!c.empty()) yy += para(c, cx, yy, cw, fonts().monoSmall, theme().label2, draw) + S(2);
                    }
                    if (!f.available) yy += para(T(L"Not possible here: ") + f.notAvailable, cx, yy, cw, fonts().small, kAmber, draw) + S(2);
                    if (draw && f.available) {
                        float bw = buttonWidth(T(L"Do it…"));
                        drawButton(RectF(x + w - pad - bw, y + pad, bw, S(28)), T(L"Do it…"), H_REP_TOOL, k, f.danger ? kit::ButtonStyle::Danger : kit::ButtonStyle::Normal);
                    }
                    return std::max(yy, y + pad + S(30)) + pad - y;
                };
                float bh = body(false);
                card(RectF(x, y, w, bh));
                body(true);
                y += bh + S(6);
            }
            y += S(10);
            y += drawMemoryCard(x, y, w);
            // recent crashes
            drawSectionHeader(RectF(x + S(4), y, w, S(24)), T(L"Recent crashes (blue screens)"));
            y += S(28);
            if (crashes_.empty())
                y += para(T(L"No crash files were found (Windows writes them to Windows\\Minidump when it stops with a blue screen, if that is switched on)."), x + S(4), y, w - S(8), fonts().small, theme().label3, true) + S(12);
            for (const CrashInfo& c : crashes_) {
                auto body = [&](bool draw) {
                    float pad = S(12), cx = x + pad, cw = w - pad * 2, yy = y + pad;
                    if (draw) text(c.when + L" · " + c.name, RectF(cx, yy, cw, S(20)), fonts().uiSemi, theme().label);
                    yy += S(22);
                    yy += para(c.meaning, cx, yy, cw, fonts().small, theme().label2, draw);
                    return yy + pad - y;
                };
                float bh = body(false);
                card(RectF(x, y, w, bh));
                body(true);
                y += bh + S(6);
            }
            y += S(10);
        }
        // rescuing an old Windows
        if (repTab_ != 6) return y - top + S(10);
        drawSectionHeader(RectF(x + S(4), y, w, S(24)), T(L"Rescue an old or badly damaged Windows"));
        y += S(28);
        y += para(T(L"After a format, a new Windows installed over the old one, or a damaged file system: first copy the whole disk, then search the copy, and save the files that matter most before anything else."),
                  x + S(4), y, w - S(8), fonts().small, theme().label2, true) + S(8);
        auto row = [&](int a, const wstr& title, const wstr& what, const wstr& btn) {
            auto body = [&](bool draw) {
                float pad = S(12), cx = x + pad, cw = w - pad * 2 - S(170), yy = y + pad;
                if (draw) text(title, RectF(cx, yy, cw, S(22)), fonts().uiSemi, theme().label);
                yy += S(24);
                yy += para(what, cx, yy, cw, fonts().small, theme().label2, draw);
                if (draw) {
                    float bw = buttonWidth(btn);
                    drawButton(RectF(x + w - pad - bw, y + pad, bw, S(28)), btn, H_REP_RESCUE, a, kit::ButtonStyle::Normal);
                }
                return std::max(yy, y + pad + S(30)) + pad - y;
            };
            float bh = body(false);
            card(RectF(x, y, w, bh));
            body(true);
            y += bh + S(6);
        };
        row(0, T(L"1. Copy the whole disk (disk image)"),
            T(L"Copies every sector of a disk to an image file (.vhd) on another drive, skipping and listing what cannot be read. Every search then runs on the copy, so nothing more is written to the damaged disk."), T(L"Copy a disk…"));
        row(3, T(L"2. Open a disk image"), T(L"Attaches a .vhd image read-only: it then appears as a disk under Disks and partitions and can be searched like one."), T(L"Open an image…"));
        row(1, T(L"3. Find Windows' registry files and other essentials"),
            T(L"A full search of the chosen Windows' drive (or the copy) for the old Windows' registry (SYSTEM, SOFTWARE, SAM, NTUSER.DAT ...), also by their contents when their names are gone, plus the keys for saved passwords, mail, browser profiles and the user folders."),
            T(L"Search…"));
        row(2, T(L"4. Save the Windows essentials first"), T(L"Shows only those files among what the last search found, so they can be recovered before anything else."), T(L"Show them"));
        return y - top + S(10);
    }

    void repairTool(int k) {
        if (k < 0 || k >= (int)repTools_.size() || repInstall_ < 0) return;
        Fix f = repTools_[k];
        if (!f.available) return;
        if (f.kind == FX_REMOVE_UPDATE) {
            const Install in = rep_.installs[repInstall_];
            const Partition P = rep_.m.parts[in.part];
            auto list = std::make_shared<std::vector<UpdateEntry>>();
            auto err = std::make_shared<wstr>();
            runJob(T(L"Listing the installed updates"), in.label(), false, [in, P, list, err] { *list = listUpdates(in, P, *err); },
                   [this, list, err] {
                       if (list->empty()) {
                           showMessage(T(L"No updates to remove"), err->empty() ? T(L"No removable updates were found.") : *err);
                           return;
                       }
                       updates_ = *list;
                       std::vector<kit::MenuItem> items;
                       kit::MenuItem h;
                       h.header = true;
                       h.text = T(L"Remove which update? (newest first)");
                       items.push_back(h);
                       for (size_t i = 0; i < updates_.size() && i < 12; ++i) {
                           kit::MenuItem m;
                           wstr id = updates_[i].package;
                           size_t t1 = id.find(L'~');
                           m.text = t1 == wstr::npos ? id : id.substr(0, t1);
                           m.hint = updates_[i].when;
                           m.tip = id + L"\n" + updates_[i].kind;
                           m.cmd = C_REMOVE_UPDATE;
                           m.arg = (int)i;
                           items.push_back(m);
                       }
                       POINT p = mousePos();
                       showMenu(items, std::max(S(60), (float)p.x - S(300)), (float)p.y);
                   });
            return;
        }
        askFixList({f}, rep_.installs[repInstall_].label());
    }

    void rescueTool(int a) {
        switch (a) {
            case 0: {
                std::vector<kit::MenuItem> items;
                kit::MenuItem h;
                h.header = true;
                h.text = T(L"Copy which disk?");
                items.push_back(h);
                for (const Disk& d : rep_.m.disks) {
                    kit::MenuItem m;
                    m.text = d.name();
                    m.hint = d.health.known && d.health.level > 0 ? T(L"health warning") : wstr();
                    m.cmd = C_IMAGE_DISK;
                    m.arg = d.number;
                    items.push_back(m);
                }
                POINT p = mousePos();
                showMenu(items, std::max(S(60), (float)p.x - S(300)), (float)p.y);
                break;
            }
            case 1: {
                int part = repInstall_ >= 0 && repInstall_ < (int)rep_.installs.size() ? rep_.installs[repInstall_].part : -1;
                if (part < 0) {
                    showMessage(T(L"Choose the drive to search"), T(L"No Windows is chosen. In Disks and partitions, click the drive (or the attached disk image) and choose \"Recover deleted files\" > Full search; the Windows essentials are then shown first."));
                    essentialsNext_ = true;
                    return;
                }
                showConfirm(T(L"Search for the Windows essentials?"),
                            TF(L"A full search reads all of {0}. It can take hours on a big drive. Searching a disk image (step 1 and 2) instead keeps the damaged disk untouched.", rep_.m.parts[part].name()),
                            {T(L"Cancel"), T(L"Search")}, 1, 1, false, [this, part](int b) {
                                if (b == 1) {
                                    essentialsNext_ = true;
                                    startFileScan(part, true);
                                }
                            });
                break;
            }
            case 2:
                if (fileScan_.files.empty()) {
                    showMessage(T(L"Nothing found yet"), T(L"Search first (step 3), then the Windows essentials are shown here."));
                    return;
                }
                fileFilter_ = -2;
                filterFiles();
                navPush();
                scroll_[V_FILES] = 0;
                setView(V_FILES);
                break;
            case 3:
                showFilePicker(T(L"Open a disk image (.vhd)"), g_sys.exeDir, {L".vhd", L".vhdx"}, T(L"Open read-only"), [this](const wstr& path) {
                    wstr err;
                    if (attachImageReadOnly(path, err)) showToast(L"check", T(L"Disk image attached (read-only)"), T(L"It appears under Disks and partitions in a moment."), {}, nullptr);
                    else showMessage(T(L"The image was not attached"), err);
                });
                break;
        }
    }

    void askImageTarget(int disk) {
        const Disk* d = nullptr;
        for (const Disk& x : rep_.m.disks)
            if (x.number == disk) d = &x;
        if (!d) return;
        uint64_t size = d->size;
        wstr start;
        uint64_t best = 0;
        for (const Partition& p : rep_.m.parts)
            if (p.readable && !p.letter.empty() && p.disk != disk && p.freeBytes > best) {
                best = p.freeBytes;
                start = p.letter + L"\\";
            }
        showFolderPicker(TF(L"Where should the copy of Disk {0} go? (another disk, {1} free needed)", std::to_wstring(disk), sizeText(size)), start, T(L"Copy here"),
                         [this, disk, size](const wstr& dir) {
                             wstr dd = dir;
                             while (!dd.empty() && dd.back() == L'\\') dd.pop_back();
                             for (const Partition& p : rep_.m.parts)
                                 if (p.disk == disk && !p.letter.empty() && dd.size() >= 2 && towupper(dd[0]) == towupper(p.letter[0])) {
                                     showMessage(T(L"Choose another disk"), T(L"The copy cannot go on the disk that is being copied."));
                                     return;
                                 }
                             ULARGE_INTEGER avail{};
                             wchar_t fs[32] = {};
                             GetDiskFreeSpaceExW((dd.substr(0, 2) + L"\\").c_str(), &avail, nullptr, nullptr);
                             GetVolumeInformationW((dd.substr(0, 2) + L"\\").c_str(), nullptr, 0, nullptr, nullptr, nullptr, fs, 32);
                             if (avail.QuadPart && avail.QuadPart < size + (1ull << 20)) {
                                 showMessage(T(L"Not enough room"), TF(L"The copy needs {0}; that drive has {1} free.", sizeText(size), sizeText(avail.QuadPart)));
                                 return;
                             }
                             if (iequals(fs, L"FAT32") && size > (4ull << 30) - 1) {
                                 showMessage(T(L"FAT32 cannot hold the copy"), T(L"FAT32 drives hold no file bigger than 4 GB. Choose an NTFS or exFAT drive."));
                                 return;
                             }
                             SYSTEMTIME t;
                             GetLocalTime(&t);
                             wchar_t nm[96];
                             swprintf(nm, 96, L"\\Disk %d copy %04d-%02d-%02d.vhd", disk, t.wYear, t.wMonth, t.wDay);
                             wstr out = dd + nm;
                             auto res = std::make_shared<ImageResult>();
                             countUse(L"Tools: disk copies (images)");
                             runJob(TF(L"Copying Disk {0}", std::to_wstring(disk)), out, true,
                                    [this, disk, out, res] { *res = imageDisk(disk, out, [this](const wstr& s, float f) { progress(s, f); }, &cancel_); },
                                    [this, res, out, disk] {
                                        for (const Disk& x : rep_.m.disks)  // keep where the unreadable areas are
                                            if (x.number == disk) rememberBad(x.id(), res->badRanges, res->ok, T(L"disk copy"));
                                        if (!res->ok) {
                                            showMessage(T(L"The copy is not complete"), res->result);
                                            return;
                                        }
                                        showConfirm(T(L"Disk copied"), res->result + T(L"\n\nOpen the copy now (read-only), so it can be searched instead of the damaged disk?"), {T(L"Later"), T(L"Open it")}, 1, 1, false,
                                                    [this, out](int b) {
                                                        wstr err;
                                                        if (b == 1 && !attachImageReadOnly(out, err)) showMessage(T(L"The image was not attached"), err);
                                                    });
                                    });
                         });
    }

// Part of class DoctorApp (included inside the class in main.cpp).
    // ---------------------------------------------------------------- lost partitions (remembered per disk)
    struct LostMem {
        bool full = false;
        wstr when;
        std::vector<LostPartition> parts;
    };

    static wstr lostField(wstr t) {
        for (wchar_t& c : t)
            if (c == L',' || c == L';' || c == L'|' || c == L'\n' || c == L'\r') c = L' ';
        return t;
    }

    // Saved in the program's settings: one line per disk, so a search need not be repeated in every session.
    void saveLost() {
        wstr ids;
        for (const auto& kv : lostMem_) {
            wstr v = wstr(kv.second.full ? L"full" : L"quick") + L"|" + kv.second.when + L"|";
            for (const LostPartition& lp : kv.second.parts)
                v += std::to_wstring(lp.offset) + L"," + std::to_wstring(lp.length) + L"," + lostField(lp.fs) + L"," + lostField(lp.label) + L"," + (lp.fromBackup ? L"1" : L"0") + L";";
            settings().set((L"lost " + kv.first).c_str(), v);
            ids += kv.first + L" ";
        }
        for (const wstr& old : lostIdsSaved_)
            if (!lostMem_.count(old)) settings().set((L"lost " + old).c_str(), L"");
        settings().set(L"lostDisks", ids);
        lostIdsSaved_.clear();
        for (const auto& kv : lostMem_) lostIdsSaved_.push_back(kv.first);
    }

    void loadLost() {
        wstr ids = settings().get(L"lostDisks");
        for (size_t a = 0; a < ids.size();) {
            size_t b = ids.find(L' ', a);
            if (b == wstr::npos) b = ids.size();
            wstr id = ids.substr(a, b - a);
            a = b + 1;
            if (id.empty()) continue;
            wstr v = settings().get((L"lost " + id).c_str());
            size_t p1 = v.find(L'|'), p2 = p1 == wstr::npos ? wstr::npos : v.find(L'|', p1 + 1);
            if (p2 == wstr::npos) continue;
            LostMem m;
            m.full = v.substr(0, p1) == L"full";
            m.when = v.substr(p1 + 1, p2 - p1 - 1);
            wstr list = v.substr(p2 + 1);
            for (size_t c = 0; c < list.size();) {
                size_t e = list.find(L';', c);
                if (e == wstr::npos) e = list.size();
                wstr item = list.substr(c, e - c);
                c = e + 1;
                std::vector<wstr> f;
                for (size_t q = 0; q <= item.size();) {
                    size_t r = item.find(L',', q);
                    if (r == wstr::npos) r = item.size();
                    f.push_back(item.substr(q, r - q));
                    q = r + 1;
                }
                if (f.size() < 5) continue;
                LostPartition lp;
                lp.offset = _wcstoui64(f[0].c_str(), nullptr, 10);
                lp.length = _wcstoui64(f[1].c_str(), nullptr, 10);
                lp.fs = f[2];
                lp.label = f[3];
                lp.fromBackup = f[4] == L"1";
                m.parts.push_back(lp);
            }
            lostMem_[id] = m;
            lostIdsSaved_.push_back(id);
        }
    }

    // What was found, checked against the disk as it is now: one that is back in the table is left out.
    std::vector<LostPartition> lostNow(const Disk& d) {
        std::vector<LostPartition> out;
        auto it = lostMem_.find(d.id());
        if (it == lostMem_.end()) return out;
        for (LostPartition lp : it->second.parts) {
            bool back = false, overlap = false;
            for (int i : d.parts) {
                const Partition& p = rep_.m.parts[i];
                if (p.offset == lp.offset) back = true;
                else if (p.offset < lp.offset + lp.length && lp.offset < p.offset + p.length) overlap = true;
            }
            if (back) continue;
            lp.restorable = !overlap && lp.offset + lp.length <= d.size;
            if (overlap) lp.why = T(L"It runs into a partition that exists now, so it was partly overwritten or that partition was made over it.");
            out.push_back(lp);
        }
        return out;
    }

    void startLostScan(int disk, bool full) {
        if (disk < 0) return;
        auto out = std::make_shared<std::vector<LostPartition>>();
        auto copy = std::make_shared<Report>(rep_);
        runJob(full ? T(L"Full search for lost partitions") : T(L"Quick search for lost partitions"), T(L"Disk ") + std::to_wstring(disk), true,
               [this, out, copy, disk, full] { *out = findLostPartitions(*copy, disk, full, [this](const wstr& s, float f) { progress(s, f); }, &cancel_); },
               [this, out, disk, full] {
                   if (cancel_) return;
                   for (const Disk& d : rep_.m.disks)
                       if (d.number == disk) {
                           SYSTEMTIME t;
                           GetLocalTime(&t);
                           wchar_t when[32];
                           swprintf(when, 32, L"%04d-%02d-%02d %02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
                           LostMem m;
                           m.full = full;
                           m.when = when;
                           m.parts = *out;
                           lostMem_[d.id()] = m;
                           lostHidden_.erase(d.id());
                           saveLost();
                       }
                   if (view_ != V_DISKS) {
                       navPush();
                       setView(V_DISKS);
                   }
                   showToast(L"check", T(L"Search finished"), out->empty() ? T(L"No lost partitions were found.") : plural(out->size(), T(L"lost partition found"), T(L"lost partitions found")), {}, nullptr);
               });
    }

    float lostCard(const Disk& d, float x, float y, float w, bool draw) {
        const LostMem& m = lostMem_[d.id()];
        std::vector<LostPartition> list = lostNow(d);
        float pad = S(14), cx = x + pad, cw = w - pad * 2, yy = y + pad;
        if (draw) {
            drawIcon("bd_recover", cx, yy + S(2), S(20), theme().accent);
            text((m.full ? T(L"Lost partitions · full search") : T(L"Lost partitions · quick search")) + L" · " + TF(L"found {0}", m.when), RectF(cx + S(28), yy, cw - S(200), S(24)),
                 fonts().uiSemi, theme().label);
            float bw = buttonWidth(T(L"Hide")), fw = buttonWidth(T(L"Forget"));
            drawButton(RectF(x + w - pad - bw, yy, bw, S(24)), T(L"Hide"), H_LOST_CLOSE, d.number, kit::ButtonStyle::Quiet);
            drawButton(RectF(x + w - pad - bw - S(6) - fw, yy, fw, S(24)), T(L"Forget"), H_LOST_FORGET, d.number, kit::ButtonStyle::Quiet);
        }
        yy += S(32);
        if (list.empty()) {
            yy += para(m.parts.empty() ? (m.full ? T(L"Nothing was found. The partitions were probably overwritten.")
                                                 : T(L"Nothing was found where partitions normally start. The full search reads every sector and also finds the backup copies file systems keep."))
                                       : T(L"Everything found then is back in the partition table now."),
                       cx, yy, cw, fonts().small, theme().label2, draw) + S(8);
        }
        for (int i = 0; i < (int)list.size(); ++i) {
            const LostPartition& lp = list[i];
            wstr head = lp.fs + L" · " + sizeText(lp.length) + (lp.label.empty() ? wstr() : L" · \"" + lp.label + L"\"") + T(L" · starts at ") + sizeText(lp.offset);
            if (draw) {
                canvas().circle(cx + S(5), yy + S(10), S(4), lp.restorable ? kGreen : kAmber);
                text(head, RectF(cx + S(16), yy, cw - S(170), S(20)), fonts().uiSemi, theme().label);
                if (lp.restorable) {
                    float bw = buttonWidth(T(L"Put it back"));
                    drawButton(RectF(x + w - pad - bw, yy - S(2), bw, S(26)), T(L"Put it back"), H_LOST, d.number * 1000 + i, kit::ButtonStyle::Primary);
                }
            }
            yy += S(24);
            wstr sub = (lp.fromBackup ? T(L"Found through its backup copy (its first sector is damaged). ") : wstr()) + lp.why;
            if (!sub.empty()) yy += para(sub, cx + S(16), yy, cw - S(16), fonts().small, theme().label2, draw) + S(4);
            yy += S(6);
        }
        float bx = cx;
        auto b = [&](const wstr& t, int hit) {
            float bw = buttonWidth(t);
            if (draw) drawButton(RectF(bx, yy, bw, S(26)), t, hit, d.number, kit::ButtonStyle::Normal);
            bx += bw + S(8);
        };
        b(T(L"Quick search again"), H_LOST_QUICK);
        b(T(L"Full search (slow)"), H_LOST_FULL);
        yy += S(32);
        return yy + pad - S(6) - y;
    }

    void askRestoreLost(int disk, int i) {
        const Disk* d = nullptr;
        for (const Disk& x : rep_.m.disks)
            if (x.number == disk) d = &x;
        if (!d) return;
        std::vector<LostPartition> list = lostNow(*d);
        if (i < 0 || i >= (int)list.size()) return;
        LostPartition lp = list[i];
        wstr msg = TF(L"The {0} partition ({1}) is put back in the partition table of Disk {2}, where it was. Its files are not touched; nothing is written inside it.\n\nIf the files look wrong afterwards, Undo (Ctrl+Z) removes it from the table again.",
                      lp.fs, sizeText(lp.length), std::to_wstring(disk));
        if (lp.fromBackup) msg += T(L"\n\nIts first sector is damaged: after putting it back, run \"Check for errors\" on it, which repairs the first sector from the backup copy.");
        showConfirm(T(L"Put the partition back?"), msg, {T(L"Cancel"), T(L"Put it back")}, 1, 0, true, [this, lp, disk](int b) {
            if (b != 1 || busy_ || snapshot_) return;
            if (!journal_.isOpen()) {
                showMessage(T(L"No backup folder"), T(L"Nothing was changed, because there is no folder to keep backups in. Choose one with the settings button."));
                return;
            }
            if (job_.joinable()) job_.join();
            busy_ = true;
            setProgress(T(L"Restore a lost partition"), -1.f, lp.fs + L" · " + sizeText(lp.length), false);
            auto copy = std::make_shared<Report>(rep_);
            job_ = std::thread([this, copy, lp, disk] {
                FixOutcome o = restorePartition(lp, disk, *copy, journal_);
                {
                    std::lock_guard<std::mutex> l(lock_);
                    jobOk_ = o.ok;
                    jobNote_ = o.result;
                    jobDirs_.clear();
                    if (o.ok) jobDirs_.push_back(o.actionDir);
                }
                PostMessageW(hwnd(), WM_BD_FIXED, 1, 0);
            });
            invalidate();
        });
    }

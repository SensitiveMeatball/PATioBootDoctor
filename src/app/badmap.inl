// Part of class DoctorApp (included inside the class in main.cpp).
// ---------------------------------------------------------------- unreadable areas on the disk map
// The surface test and the disk copy record each unreadable area. They are kept per disk (in the data
// folder, so they survive a restart), drawn as red marks on the disk's partition bar, and can be traced
// to the files that sit on them.
    struct BadMap {
        std::vector<std::pair<uint64_t, uint64_t>> ranges;  // offset, length (bytes from the disk start)
        wstr when, source;
    };

    wstr badMapsPath() { return dataFolder() + L"\\unreadable-areas.txt"; }

    void loadBadMaps() {
        if (badMapsLoaded_ || snapshot_) return;
        badMapsLoaded_ = true;
        std::string raw;
        if (!readFileText(badMapsPath(), raw)) return;
        wstr t = utf8ToWide(raw);
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
            if (f.size() < 4 || f[0].empty()) continue;
            BadMap m;
            m.when = f[1];
            m.source = f[2];
            for (size_t p = 0; p < f[3].size();) {
                size_t q = f[3].find(L',', p);
                if (q == wstr::npos) q = f[3].size();
                wstr item = f[3].substr(p, q - p);
                size_t c = item.find(L':');
                if (c != wstr::npos) m.ranges.push_back({_wcstoui64(item.c_str(), nullptr, 10), _wcstoui64(item.c_str() + c + 1, nullptr, 10)});
                p = q + 1;
            }
            badMaps_[f[0]] = m;
        }
    }

    void saveBadMaps() {
        wstr t;
        for (const auto& kv : badMaps_) {
            t += kv.first + L"\t" + kv.second.when + L"\t" + kv.second.source + L"\t";
            size_t n = 0;
            for (const auto& r : kv.second.ranges) {
                if (++n > 5000) break;
                t += std::to_wstring(r.first) + L":" + std::to_wstring(r.second) + L",";
            }
            t += L"\r\n";
        }
        std::string u = wideToUtf8(t);
        HANDLE h = CreateFileW(badMapsPath().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        DWORD wr = 0;
        WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr);
        CloseHandle(h);
    }

    // complete: the whole disk was read, so this replaces what was known; else it is added to it
    void rememberBad(const wstr& diskId, const std::vector<std::pair<uint64_t, uint64_t>>& ranges, bool complete, const wstr& source) {
        loadBadMaps();
        BadMap& m = badMaps_[diskId];
        if (complete) m.ranges = ranges;
        else
            for (const auto& r : ranges) m.ranges.push_back(r);
        std::sort(m.ranges.begin(), m.ranges.end());
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t b[32];
        swprintf(b, 32, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
        m.when = b;
        m.source = source;
        saveBadMaps();
        invalidate();
    }

    // red marks over the partition bar. items/starts/rects: what was drawn, its first byte, where.
    void drawBadMarks(const Disk& d, const std::vector<MapItem>& items, const std::vector<uint64_t>& starts, const std::vector<RectF>& rects) {
        loadBadMaps();
        auto it = badMaps_.find(d.id());
        if (it == badMaps_.end()) return;
        for (const auto& r : it->second.ranges)
            for (size_t k = 0; k < starts.size() && k < rects.size() && k < items.size(); ++k) {
                uint64_t s = starts[k], len = items[k].length;
                if (!len || r.first >= s + len || r.first + r.second <= s) continue;
                float x0 = rects[k].x + (float)((double)(std::max(r.first, s) - s) / len * rects[k].w);
                float x1 = rects[k].x + (float)((double)(std::min(r.first + r.second, s + len) - s) / len * rects[k].w);
                canvas().fill(RectF(std::min(x0, rects[k].right() - S(3)), rects[k].y + S(5), std::max(S(3), x1 - x0), rects[k].h - S(5)), kRed);
            }
    }

    float drawBadSummary(const Disk& d, float x, float y, float w) {
        auto it = badMaps_.find(d.id());
        if (it == badMaps_.end()) return 0;
        const BadMap& m = it->second;
        if (m.ranges.empty()) {
            text(TF(L"The {0} on {1} read every sector.", m.source, m.when), RectF(x, y, w, S(20)), fonts().small, kGreen, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            return S(26);
        }
        uint64_t total = 0;
        std::map<wstr, int> where;
        for (const auto& r : m.ranges) {
            total += r.second;
            wstr in = T(L"unused space");
            for (int i : d.parts) {
                const Partition& p = rep_.m.parts[i];
                if (p.role != R_EXTENDED && r.first < p.offset + p.length && r.first + r.second > p.offset) in = p.name();
            }
            ++where[in];
        }
        wstr places;
        for (const auto& kv : where) places += (places.empty() ? L"" : L"; ") + kv.first + (kv.second > 1 ? L" (" + std::to_wstring(kv.second) + L")" : wstr());
        wstr line = TF(L"Unreadable areas found by the {0} on {1}: {2} places, {3}. In: {4}.", m.source, m.when, std::to_wstring(m.ranges.size()), sizeText(total), places);
        float bw = buttonWidth(T(L"Which files are on them?"));
        float h = para(line, x + S(16), y, w - bw - S(30), fonts().small, kRed, true);
        canvas().circle(x + S(5), y + S(8), S(3.5f), kRed);
        drawButton(RectF(x + w - bw, y - S(3), bw, S(26)), T(L"Which files are on them?"), H_BADMAP, d.number, kit::ButtonStyle::Normal);
        return std::max(h, S(26)) + S(6);
    }

    void whichFilesOnBad(int disk) {
        if (busy_) return;
        const Disk* d = nullptr;
        for (const Disk& x : rep_.m.disks)
            if (x.number == disk) d = &x;
        if (!d) return;
        auto it = badMaps_.find(d->id());
        if (it == badMaps_.end() || it->second.ranges.empty()) return;
        std::vector<Partition> parts;
        for (int i : d->parts) {
            const Partition& p = rep_.m.parts[i];
            if (p.role == R_EXTENDED) continue;
            for (const auto& r : it->second.ranges)
                if (r.first < p.offset + p.length && r.first + r.second > p.offset) {
                    parts.push_back(p);
                    break;
                }
        }
        if (parts.empty()) {
            showMessage(T(L"No files are affected"), T(L"Every unreadable area is in unused space, outside any partition."));
            return;
        }
        auto ranges = it->second.ranges;
        countUse(L"Tools: files traced on unreadable sectors");
        wstr dname = d->name();
        auto text = std::make_shared<wstr>();
        auto full = std::make_shared<wstr>();
        auto critical = std::make_shared<int>(0);
        runJob(T(L"Finding the files on unreadable sectors"), dname, true,
               [this, parts, ranges, text, full, critical] {
                   for (const Partition& p : parts) {
                       wstr note;
                       std::vector<FileOnBad> files = filesOnBadRanges(p, ranges, note, [this](const wstr& s, float f) { progress(s, f); }, &cancel_);
                       *full += p.name() + L"\r\n";
                       *text += p.name() + L":\n";
                       if (!note.empty()) {
                           *text += L"  " + note + L"\n";
                           *full += L"  " + note + L"\r\n";
                       }
                       int shown = 0;
                       for (const FileOnBad& f : files) {
                           if (f.critical) ++*critical;
                           wstr ln = (f.critical ? L"  ! " : L"    ") + f.path + (f.bytes ? L"  (" + sizeText(f.bytes) + L")" : wstr());
                           *full += ln + L"\r\n";
                           if (shown++ < 25) *text += ln + L"\n";
                       }
                       if (files.size() > 25) *text += L"  " + TF(L"... and {0} more", std::to_wstring(files.size() - 25)) + L"\n";
                   }
               },
               [this, text, full, critical, dname] {
                   SYSTEMTIME st;
                   GetLocalTime(&st);
                   wchar_t b[48];
                   swprintf(b, 48, L"%04d-%02d-%02d %02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
                   wstr path = dataFolder() + L"\\Files on unreadable sectors " + b + L".txt";
                   std::string u = "\xEF\xBB\xBF" + wideToUtf8(dname + L"\r\n\r\n" + *full);
                   HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                   if (h != INVALID_HANDLE_VALUE) {
                       DWORD wr = 0;
                       WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr);
                       CloseHandle(h);
                   }
                   wstr head = *critical ? TF(L"{0} of these are files Windows needs to start (marked !): repairing the start-up files cannot help while they are unreadable. Copy the disk to a healthy one and repair the copy, or reinstall Windows on a new disk.\n\n", std::to_wstring(*critical))
                                         : wstr();
                   showMessage(*critical ? T(L"Windows' own files are on unreadable sectors") : T(L"Files on unreadable sectors"),
                               head + *text + L"\n" + TF(L"The full list is saved in {0}.", path));
               });
    }

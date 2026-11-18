// Part of class DoctorApp (included inside the class in main.cpp).
    // ---------------------------------------------------------------- resize preview
    void openPreview(int part, int mode) {
        if (part < 0 || part >= (int)rep_.m.parts.size()) return;
        const Partition& p = rep_.m.parts[part];
        int nx = nextPartition(rep_, part);
        uint64_t after = unusedAfter(rep_, part);
        // Only what the disk tool allows for this partition (never the running Windows, the backup drive, ...)
        auto why = [&](int kind) {
            for (const auto& op : diskOpsFor(rep_, SEL_PART, p.disk, part, -1))
                if (op.first == kind) return op.second;
            return wstr(T(L"This is not possible for this kind of partition."));
        };
        if (mode == 0 && (after < (64ull << 20) || !why(DO_EXTEND).empty())) mode = 1;
        if (mode == 1 && nx < 0) {
            showMessage(T(L"There is nothing after it to use"), T(L"A partition can only grow into space right after it. There is no unused space and no partition right after this one."));
            return;
        }
        if (mode == 1 && !why(DO_MERGE_NEXT).empty()) {
            showMessage(diskOpInfo(DO_MERGE_NEXT).title + T(L" is not possible here"), why(DO_MERGE_NEXT));
            return;
        }
        DiskOpArgs a;
        a.disk = p.disk;
        a.part = part;
        if (mode == 0) {
            a.kind = DO_EXTEND;
            a.sizeMB = after >> 20;
        } else {
            a.kind = DO_MERGE_NEXT;
            a.part2 = nx;
            a.sizeMB = (rep_.m.parts[nx].length + unusedAfterEnd(rep_, p.disk, rep_.m.parts[nx].offset + rep_.m.parts[nx].length)) >> 20;
        }
        showPreview(a);
        preview_.mode = mode;
    }

    void showPreview(const DiskOpArgs& a) {
        preview_.on = true;
        preview_.a = a;
        preview_.mode = a.kind == DO_MERGE_NEXT ? 1 : 0;
        if (view_ != V_DISKS) navPush();
        dsKind_ = SEL_PART;
        dsPart_ = a.part;
        dsDisk_ = a.disk;
        scroll_[V_DISKS] = 0;
        setView(V_DISKS);
    }

    struct Seg {
        uint64_t off, len;
        wstr name;
        int kind;  // 0 partition, 1 unused, 2 the one that changes, 3 deleted
    };

    std::vector<Seg> segments(const Disk& d, bool after) {
        std::vector<Seg> v;
        const DiskOpArgs& a = preview_.a;
        for (int i : d.parts) {
            const Partition& q = rep_.m.parts[i];
            if (q.role == R_EXTENDED) continue;
            wstr n = !q.letter.empty() ? q.letter : wstr(T(roleShort(q.role)));
            Seg s{q.offset, q.length, n, i == a.part ? 2 : 0};
            if (after && i == a.part) {
                if (a.kind == DO_SHRINK) s.len -= a.sizeMB << 20;
                else s.len += a.sizeMB << 20;
            }
            if (after && a.kind == DO_MERGE_NEXT && i == a.part2) continue;
            if (!after && a.kind == DO_MERGE_NEXT && i == a.part2) s.kind = 3;
            v.push_back(s);
        }
        for (const auto& g : d.gaps) v.push_back(Seg{g.first, g.second, T(L"Unused"), 1});
        std::sort(v.begin(), v.end(), [](const Seg& x, const Seg& y) { return x.off < y.off; });
        if (after) {  // the space the partition grew into is no longer unused (or the shrunk space is)
            std::vector<Seg> w;
            for (const Seg& s : v) {
                if (s.kind == 1) {
                    bool covered = false;
                    for (const Seg& t : v)
                        if (t.kind == 2 && s.off < t.off + t.len && s.off + s.len > t.off) covered = true;
                    if (covered) {
                        uint64_t end = 0;
                        for (const Seg& t : v)
                            if (t.kind == 2) end = t.off + t.len;
                        if (s.off + s.len > end + (1ull << 20)) w.push_back(Seg{end, s.off + s.len - end, s.name, 1});
                        continue;
                    }
                }
                w.push_back(s);
                if (s.kind == 2 && a.kind == DO_SHRINK) w.push_back(Seg{s.off + s.len, a.sizeMB << 20, T(L"Unused"), 1});
            }
            v.swap(w);
        }
        return v;
    }

    void drawSegBar(const std::vector<Seg>& v, const RectF& bar) {
        double total = 0;
        for (const Seg& s : v) total += (double)s.len;
        if (total <= 0) return;
        float minW = S(46), gapW = S(3), avail = bar.w - gapW * (v.size() - 1);
        std::vector<float> ws;
        for (const Seg& s : v) ws.push_back((float)(s.len / total) * avail);
        float need = 0, big = 0;
        for (float& w : ws)
            if (w < minW) need += minW - w;
            else big += w - minW;
        for (float& w : ws) w = w < minW ? minW : (big > 0 ? w - (w - minW) / big * std::min(need, big) : w);
        float x = bar.x;
        for (size_t k = 0; k < v.size(); ++k) {
            RectF b(x, bar.y, ws[k], bar.h);
            x += ws[k] + gapW;
            const Seg& s = v[k];
            Color c = s.kind == 2 ? theme().accent : s.kind == 3 ? kRed : s.kind == 1 ? theme().label3 : kBlue;
            canvas().round(b, S(6), s.kind == 1 ? theme().label.withAlpha(0.04f) : c.withAlpha(0.16f));
            canvas().ring(b, S(6), s.kind >= 2 ? S(2) : S(1), s.kind == 1 ? theme().separator : c);
            text(s.name, RectF(b.x + S(6), b.y + S(4), b.w - S(10), S(18)), fonts().smallSemi, s.kind == 3 ? kRed : theme().label);
            text(s.kind == 3 ? T(L"deleted") : sizeText(s.len), RectF(b.x + S(6), b.y + S(22), b.w - S(10), S(16)), fonts().small, theme().label2);
        }
    }

    float previewCard(float x, float y, float w, bool draw) {
        const DiskOpArgs& a = preview_.a;
        const Disk* d = nullptr;
        for (const Disk& q : rep_.m.disks)
            if (q.number == a.disk) d = &q;
        if (!d || a.part < 0) return 0;
        const Partition& p = rep_.m.parts[a.part];
        float pad = S(16), cx = x + pad, cw = w - pad * 2, yy = y + pad;
        wstr title = a.kind == DO_SHRINK ? TF(L"Preview: make {0} smaller", p.name()) : TF(L"Preview: make {0} bigger", p.name());
        if (draw) {
            drawIcon("disk_hw", cx, yy + S(2), S(20), theme().accent);
            text(title, RectF(cx + S(28), yy, cw - S(28), S(24)), fonts().uiSemi, theme().label);
        }
        yy += S(30);
        if (a.kind != DO_SHRINK) {
            int nx = nextPartition(rep_, a.part);
            bool gap = unusedAfter(rep_, a.part) >= (64ull << 20);
            float bx = cx;
            auto chip = [&](int mode, const wstr& t, bool enabled) {
                float bw = buttonWidth(t);
                if (draw && enabled) drawButton(RectF(bx, yy, bw, S(26)), t, H_PREVIEW_MODE, mode, preview_.mode == mode ? kit::ButtonStyle::Primary : kit::ButtonStyle::Normal);
                if (enabled) bx += bw + S(8);
            };
            chip(0, TF(L"Use the unused space after it ({0})", sizeText(unusedAfter(rep_, a.part))), gap);
            if (nx >= 0) chip(1, TF(L"Delete {0} after it and use its space", rep_.m.parts[nx].name()), true);
            if (bx > cx) yy += S(36);
        }
        if (draw) text(T(L"Now"), RectF(cx, yy, cw, S(18)), fonts().smallSemi, theme().label2);
        yy += S(20);
        if (draw) drawSegBar(segments(*d, false), RectF(cx, yy, cw, S(44)));
        yy += S(52);
        if (draw) text(T(L"Afterwards"), RectF(cx, yy, cw, S(18)), fonts().smallSemi, theme().label2);
        yy += S(20);
        if (draw) drawSegBar(segments(*d, true), RectF(cx, yy, cw, S(44)));
        yy += S(54);
        wstr warn = T(L"Changing partitions is safe in almost all cases, but a power cut or a crash while it runs can damage the drive and lose data. Back up important files first, keep a laptop plugged in, and close other programs.");
        if (a.kind == DO_MERGE_NEXT && a.part2 >= 0) {
            const Partition& n = rep_.m.parts[a.part2];
            wstr gone = TF(L"Everything on {0} is erased.", n.name());
            if (n.role == R_RECOVERY)
                gone += T(L" It is a recovery partition: Windows' troubleshoot options are on it. After this, recreate them from the report (\"Recreate the recovery partition\"), which takes 1 GB back.");
            warn = gone + L" " + warn;
        }
        if (p.bitlocker) warn += T(L" This drive uses BitLocker: have its recovery key ready.");
        yy += para(warn, cx, yy, cw, fonts().small, a.kind == DO_MERGE_NEXT ? kRed : theme().label, draw) + S(10);
        if (draw) {
            wstr go = diskOpInfo(a.kind).title + L"…";
            float gw = buttonWidth(go), cw2 = buttonWidth(T(L"Cancel"));
            drawButton(RectF(cx, yy, gw, S(28)), go, H_PREVIEW_GO, 0, a.kind == DO_MERGE_NEXT ? kit::ButtonStyle::Danger : kit::ButtonStyle::Primary);
            drawButton(RectF(cx + gw + S(8), yy, cw2, S(28)), T(L"Cancel"), H_PREVIEW_CANCEL, 0, kit::ButtonStyle::Normal);
        }
        yy += S(28) + pad;
        return yy - y;
    }

// Part of class DoctorApp (included inside the class in main.cpp).
    // ---------------------------------------------------------------- notes
    struct Note {
        wstr when, pc, text;
    };

    // Kept next to the settings (on the rescue stick when running from it), one note per line.
    wstr notesPath() { return dataFolder() + L"\\PATioBootDoctor notes.txt"; }

    void loadNotes() {
        notes_.clear();
        std::string raw;
        if (!readFileText(notesPath(), raw)) return;
        wstr t = utf8ToWide(raw);
        if (!t.empty() && t[0] == 0xFEFF) t.erase(0, 1);
        size_t a = 0;
        while (a < t.size()) {
            size_t b = t.find(L'\n', a);
            if (b == wstr::npos) b = t.size();
            wstr line = t.substr(a, b - a);
            a = b + 1;
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            size_t p1 = line.find(L'\t'), p2 = p1 == wstr::npos ? wstr::npos : line.find(L'\t', p1 + 1);
            if (p2 == wstr::npos) continue;
            notes_.push_back(Note{line.substr(0, p1), line.substr(p1 + 1, p2 - p1 - 1), line.substr(p2 + 1)});
        }
    }

    void saveNotes() {
        if (snapshot_) return;
        wstr t;
        for (const Note& n : notes_) {
            wstr text = n.text;
            for (wchar_t& c : text)
                if (c == L'\t' || c == L'\n' || c == L'\r') c = L' ';
            t += n.when + L"\t" + n.pc + L"\t" + text + L"\r\n";
        }
        std::string u = "\xEF\xBB\xBF" + wideToUtf8(t);
        HANDLE h = CreateFileW(notesPath().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD wr = 0;
        bool ok = h != INVALID_HANDLE_VALUE && WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        if (!ok) showMessage(T(L"The notes were not saved"), notesPath());
    }

    wstr notesText() const {
        wstr t;
        for (const Note& n : notes_) t += n.when + (n.pc.empty() ? wstr() : L" · " + n.pc) + L"\r\n" + n.text + L"\r\n\r\n";
        return t;
    }

    wstr reportWithNotes() {
        wstr t = reportText(rep_);
        if (!notes_.empty()) t += T(L"\r\nNOTES\r\n") + notesText();
        return t;
    }

    void addNote() {
        showInput(T(L"New note"), T(L"Write down what you noticed or did, for example what the PC showed when it did not start, or which key a person gave you. The time and this PC's name are added."), L"", T(L"Save"),
                  [this](const wstr& t) {
                      if (t.empty()) return true;
                      SYSTEMTIME st;
                      GetLocalTime(&st);
                      wchar_t when[32];
                      swprintf(when, 32, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
                      wstr pc = thisPcName();
                      if (pc.empty()) pc = rep_.m.fw.maker + (rep_.m.fw.model.empty() ? wstr() : L" " + rep_.m.fw.model);
                      notes_.insert(notes_.begin(), Note{when, pc, t});
                      countUse(L"Tools: notes written");
                      saveNotes();
                      if (journal_.isOpen()) journal_.note(T(L"Note: ") + t);
                      invalidate();
                      return true;
                  });
    }

    void noteAction(int i, int a) {
        if (i < 0 || i >= (int)notes_.size()) return;
        if (a == 0) {
            showInput(T(L"Change the note"), notes_[i].when + (notes_[i].pc.empty() ? wstr() : L" · " + notes_[i].pc), notes_[i].text, T(L"Save"), [this, i](const wstr& t) {
                if (i < (int)notes_.size()) {
                    notes_[i].text = t;
                    if (t.empty()) notes_.erase(notes_.begin() + i);
                    saveNotes();
                    invalidate();
                }
                return true;
            });
            return;
        }
        showConfirm(T(L"Delete this note?"), notes_[i].text, {T(L"Cancel"), T(L"Delete")}, 1, 0, true, [this, i](int b) {
            if (b == 1 && i < (int)notes_.size()) {
                notes_.erase(notes_.begin() + i);
                saveNotes();
                invalidate();
            }
        });
    }

    float drawNotes(float x, float y, float w) {
        float top = y;
        y += drawJobCard(x, y, w);
        auto head = [&](bool draw) {
            float pad = S(14), cx = x + pad + S(34), cw = w - pad * 2 - S(34), yy = y + pad;
            if (draw) {
                drawIcon("bd_note", x + pad, yy + S(2), S(22), theme().accent);
                text(T(L"Notes"), RectF(cx, yy, cw, S(22)), fonts().uiSemi, theme().label);
            }
            yy += S(26);
            yy += para(TF(L"Your own notes, newest first. They are kept in {0} and go into the saved and copied report.", notesPath()), cx, yy, cw, fonts().small, theme().label2, draw) + S(8);
            float bx = cx;
            auto b = [&](int a, const wstr& t, kit::ButtonStyle st) {
                float bw = buttonWidth(t);
                if (draw) drawButton(RectF(bx, yy, bw, S(28)), t, H_NOTES_TOP, a, st);
                bx += bw + S(8);
            };
            b(0, T(L"Write a note… (Ctrl+N)"), kit::ButtonStyle::Primary);
            if (!notes_.empty()) b(1, T(L"Copy all"), kit::ButtonStyle::Normal);
            yy += S(28) + pad;
            return yy - y;
        };
        float h = head(false);
        card(RectF(x, y, w, h));
        head(true);
        y += h + S(12);
        if (notes_.empty()) {
            y += para(T(L"No notes yet."), x + S(4), y, w, fonts().ui, theme().label3, true);
            return y - top + S(10);
        }
        for (int i = 0; i < (int)notes_.size(); ++i) {
            const Note& n = notes_[i];
            auto body = [&](bool draw) {
                float pad = S(12), cx = x + pad, cw = w - pad * 2 - S(170), yy = y + pad;
                if (draw) text(n.when + (n.pc.empty() ? wstr() : L" · " + n.pc), RectF(cx, yy, cw, S(18)), fonts().smallSemi, theme().label2);
                yy += S(22);
                yy += para(n.text, cx, yy, cw, fonts().ui, theme().label, draw) + S(4);
                if (draw) {
                    float dw = buttonWidth(T(L"Delete")), ew = buttonWidth(T(L"Change…"));
                    drawButton(RectF(x + w - pad - dw, y + pad, dw, S(26)), T(L"Delete"), H_NOTE, i * 10 + 1, kit::ButtonStyle::Quiet);
                    drawButton(RectF(x + w - pad - dw - S(6) - ew, y + pad, ew, S(26)), T(L"Change…"), H_NOTE, i * 10, kit::ButtonStyle::Normal);
                }
                return std::max(yy, y + pad + S(30)) + pad - y;
            };
            float bh = body(false);
            card(RectF(x, y, w, bh));
            body(true);
            y += bh + S(8);
        }
        return y - top + S(10);
    }

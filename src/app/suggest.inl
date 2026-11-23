// Part of class DoctorApp (included inside the class in main.cpp).
// ---------------------------------------------------------------- "Make a suggestion"
// Offline-first feedback: the app often runs on a broken PC with no internet, so every
// suggestion is saved to suggestions.txt in the shared data folder FIRST. Only then, and
// only if the technician chooses, is a pre-filled GitHub issue or mailto link opened. Nothing
// is ever sent automatically, and nothing is collected but what the technician types plus the
// app version — no customer data, no disk layout.

    // Where suggestions are kept (one line each), next to usage.txt, notes and job logs.
    wstr suggestionsPath() { return dataFolder() + L"\\suggestions.txt"; }

    // The maker's feedback address — change here to redirect feedback. It is only used for
    // the optional "Send by email" route, and nothing is sent until the technician presses it.
    static const wchar_t* feedbackEmail() { return L"info@muspc.com"; }

    void appendSuggestion(const wstr& line) {
        if (snapshot_) return;
        wstr path = suggestionsPath();
        bool fresh = GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES;
        std::string u = (fresh ? std::string("\xEF\xBB\xBF") : std::string()) + wideToUtf8(line + L"\r\n");
        HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            showMessage(T(L"The suggestion was not saved"), path);
            return;
        }
        DWORD wr = 0;
        WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr);
        CloseHandle(h);
    }

    // Step 1: the explanation and the "Kind" choice.
    void makeSuggestion() {
        std::vector<kit::FormField> f(4);
        f[0].kind = kit::FormField::Note;
        f[0].label = T(L"Before you start: PATio Boot Doctor is free, and it isn't perfect. If the data on a PC matters, make a backup before you let any tool change a disk - a PC that won't boot can also mean a failing drive, and no repair is a substitute for a backup.");
        f[1].kind = kit::FormField::Note;
        f[1].label = T(L"Tell me how it went: Whether it fixed the PC or not, I'd genuinely like to know - what worked, what didn't, what confused you, and what you wish it did. The failures help as much as the wins, and every suggestion is read and taken seriously.");
        f[2].kind = kit::FormField::Note;
        f[2].label = T(L"Your privacy: Nothing is sent automatically. Your note is saved on this PC first, and you choose if and when to send it - no customer's details leave the shop unless you add them yourself.");
        f[3].kind = kit::FormField::Choice;
        f[3].id = 1;
        f[3].label = T(L"Kind");
        f[3].options = {T(L"What worked"), T(L"What didn't"), T(L"An idea"), T(L"A repair that failed"), T(L"Something else")};
        showForm(T(L"Make a suggestion"), f, T(L"Next…"), [this](const std::vector<kit::FormField>& v) {
            wstr kind;
            for (const kit::FormField& ff : v)
                if (ff.id == 1 && ff.value >= 0 && ff.value < (int)ff.options.size()) kind = ff.options[ff.value];
            defer([this, kind] { askSuggestionBody(kind); });
        });
    }

    // Step 2: the suggestion itself (free text, same box the notes use).
    void askSuggestionBody(const wstr& kind) {
        showInput(T(L"Make a suggestion"),
                  T(L"Write what worked, what didn't, what confused you, or what you'd like it to do. Only what you type here is used — no customer or disk details are added."),
                  L"", T(L"Next…"), [this, kind](const wstr& text) {
                      defer([this, kind, text] { askSuggestionContact(kind, text); });
                      return true;
                  });
    }

    // Step 3: an optional name or email, then save.
    void askSuggestionContact(const wstr& kind, const wstr& message) {
        showInput(T(L"Your name or email (optional)"),
                  T(L"Leave it blank to stay anonymous. It is only added if you later choose to send the suggestion."),
                  L"", T(L"Save"), [this, kind, message](const wstr& contact) {
                      wstr c = contact;
                      while (!c.empty() && c.back() == L' ') c.pop_back();
                      saveSuggestion(kind, message, c);
                      return true;
                  });
    }

    void saveSuggestion(const wstr& kind, const wstr& message, const wstr& contact) {
        appendSuggestion(suggestionRecord(nowStamp(), kind, kVersion, contact, message));
        countUse(L"Help: Make a suggestion");
        wstr msg = message;
        while (!msg.empty() && (msg.back() == L' ' || msg.back() == L'\r' || msg.back() == L'\n')) msg.pop_back();
        if (msg.empty()) {
            showToast(L"check", T(L"Thank you — saved on this PC"), suggestionsPath(), {T(L"Show in folder")}, [this](int) { openLink(dataFolder()); }, false);
            return;
        }
        // Saved. Now (and only now) offer to send it — never automatic.
        showToast(L"check", T(L"Saved on this PC — send it now?"),
                  T(L"Your suggestion is saved. If this PC is online you can send it; otherwise it stays saved and you can send it later from the data folder."),
                  {T(L"Open as a GitHub issue"), T(L"Send by email"), T(L"Just save it")},
                  [this, kind, msg](int a) {
                      if (a == 0) openSuggestionIssue(kind, msg);
                      else if (a == 1) openSuggestionMail(kind, msg);
                  },
                  false);
    }

    void openSuggestionIssue(const wstr& kind, const wstr& message) {
        wstr body = message + L"\n\nApp: Boot Doctor " + kVersion;
        wstr url = L"https://github.com/SensitiveMeatball/PATioBootDoctor/issues/new?title=" +
                   utf8ToWide(urlEncode(T(L"Suggestion: ") + kind)) + L"&body=" + utf8ToWide(urlEncode(body));
        openLink(url);
    }

    void openSuggestionMail(const wstr& kind, const wstr& message) {
        wstr body = message + L"\n\nApp: Boot Doctor " + kVersion;
        wstr url = wstr(L"mailto:") + feedbackEmail() + L"?subject=" +
                   utf8ToWide(urlEncode(T(L"Suggestion: ") + kind)) + L"&body=" + utf8ToWide(urlEncode(body));
        openLink(url);
    }

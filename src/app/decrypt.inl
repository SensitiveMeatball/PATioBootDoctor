// Part of class DoctorApp (included inside the class in main.cpp).
    // ---------------------------------------------------------------- BitLocker decryption progress
    void pollDecryption() {
        if (decryptLetter_.empty() || decryptBusy_) return;
        if (decryptThread_.joinable()) decryptThread_.join();
        decryptBusy_ = true;
        wstr letter = decryptLetter_;
        HWND w = hwnd();
        decryptThread_ = std::thread([letter, w] {
            // "Percentage Encrypted: 37.5%" (the only percentage manage-bde -status shows, in any language)
            ToolResult t = bd::runTool(findSystemTool(L"manage-bde.exe"), L"-status " + letter);
            int tenths = -1;
            size_t pc = t.output.find(L'%');
            if (pc != wstr::npos) {
                size_t a = pc;
                while (a > 0 && (iswdigit(t.output[a - 1]) || t.output[a - 1] == L'.' || t.output[a - 1] == L',' || t.output[a - 1] == L' ')) --a;
                wstr num = t.output.substr(a, pc - a);
                num.erase(std::remove(num.begin(), num.end(), L' '), num.end());
                std::replace(num.begin(), num.end(), L',', L'.');
                if (!num.empty()) tenths = (int)(_wtof(num.c_str()) * 10 + 0.5);
            }
            PostMessageW(w, WM_BD_DECRYPT, (WPARAM)tenths, 0);
        });
    }

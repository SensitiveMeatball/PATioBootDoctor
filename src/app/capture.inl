// Part of class DoctorApp (included inside the class in main.cpp).
// ---------------------------------------------------------------- screenshot and screen recording
// The pixels come from the program's own canvas (what it drew), so only this window is captured
// and no screen-grab or recording permission is needed. No sound is ever recorded: there is no
// audio track and no microphone is opened.
    wstr captureStamp() {
        SYSTEMTIME t;
        GetLocalTime(&t);
        wchar_t b[64];
        swprintf(b, 64, L"%04d-%02d-%02d %02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        return b;
    }

    // Flatten the canvas (which can have see-through pixels over Windows 11 glass) onto an opaque
    // window-coloured background, and force every pixel opaque, so a capture never comes out black.
    // Also clamps to w x h when the window was resized since recording began.
    std::vector<uint32_t> flattenCapture(const uint32_t* px, int cw, int ch, int w, int h) {
        Color bg = theme().window;
        std::vector<uint32_t> out((size_t)w * h);
        for (int y = 0; y < h; ++y) {
            int sy = y < ch ? y : ch - 1;
            for (int x = 0; x < w; ++x) {
                int sx = x < cw ? x : cw - 1;
                uint32_t c = px[(size_t)sy * cw + sx];
                uint32_t a = (c >> 24) & 0xFF, inv = 255 - a;
                uint32_t R = ((c >> 16) & 0xFF) + (uint32_t)bg.r * inv / 255;
                uint32_t G = ((c >> 8) & 0xFF) + (uint32_t)bg.g * inv / 255;
                uint32_t B = (c & 0xFF) + (uint32_t)bg.b * inv / 255;
                out[(size_t)y * w + x] = 0xFF000000u | (std::min(R, 255u) << 16) | (std::min(G, 255u) << 8) | std::min(B, 255u);
            }
        }
        return out;
    }

    void takeScreenshot() {
        if (snapshot_) return;
        RedrawWindow(hwnd(), nullptr, nullptr, RDW_UPDATENOW | RDW_INVALIDATE);
        GdiFlush();
        int w = canvas().width(), h = canvas().height();
        const uint32_t* px = canvas().bits();
        if (!px || w <= 0 || h <= 0) {
            showToast(L"warn", T(L"Could not take a screenshot"), T(L"The window is not ready yet."), {}, nullptr);
            return;
        }
        if (captureSpaceLow()) {
            showToast(L"warn", T(L"The disk is nearly full"), T(L"Less than 20% is free, so the screenshot was not saved — this keeps the rescue disk working. Free some space first."), {}, nullptr);
            return;
        }
        std::vector<uint32_t> flat = flattenCapture(px, w, h, w, h);
        wstr path = dataFolder() + L"\\Screenshot " + captureStamp() + L".png";
        countUse(L"Tools: screenshots");
        if (writePng(path, flat.data(), w, h))
            showToast(L"check", T(L"Screenshot saved"), path.substr(path.find_last_of(L'\\') + 1), {T(L"Show in folder")}, [this](int) { openLink(dataFolder()); });
        else
            showMessage(T(L"The screenshot was not saved"), TF(L"It could not be written to {0}.", path));
    }

    void toggleRecording() {
        if (recording_)
            stopRecording(false);
        else
            startRecording();
    }

    int recFrames() { return usingMp4_ ? mp4_.frames() : recorder_.frames(); }
    int recW() { return usingMp4_ ? mp4_.width() : recorder_.width(); }
    int recH() { return usingMp4_ ? mp4_.height() : recorder_.height(); }

    bool addRecFrame(const uint32_t* px) {
        if (usingMp4_) return mp4_.addFrame(px);
        std::string j = encodeJpeg(px, recW(), recH(), 72);
        return !j.empty() && recorder_.addFrame(j);
    }

    // True when the drive that holds the captures (the rescue stick, when running from it) has less
    // than 20% free, so screenshots and recordings stop before they fill it and stop the stick working.
    bool captureSpaceLow() {
        wstr root = dataFolder();
        wchar_t vol[MAX_PATH] = {};
        if (GetVolumePathNameW(root.c_str(), vol, MAX_PATH)) root = vol;
        ULARGE_INTEGER freeAvail{}, total{};
        if (!GetDiskFreeSpaceExW(root.c_str(), &freeAvail, &total, nullptr) || total.QuadPart == 0) return false;
        return freeAvail.QuadPart * 5 < total.QuadPart;
    }

    void startRecording() {
        if (snapshot_ || recording_) return;
        int w = canvas().width() & ~1, h = canvas().height() & ~1;  // even sizes play more widely
        if (w <= 0 || h <= 0) {
            showToast(L"warn", T(L"Cannot record"), T(L"The window is not ready yet."), {}, nullptr);
            return;
        }
        wstr stamp = captureStamp();
        bool opened = false;
        if (Mp4Writer::available()) {  // normal Windows: MP4 (H.264)
            recPath_ = dataFolder() + L"\\Recording " + stamp + L".mp4";
            usingMp4_ = opened = mp4_.open(recPath_, w, h, kCaptureFps);
        }
        if (!opened) {  // the rescue environment has no Media Foundation: fall back to Motion-JPEG .avi
            usingMp4_ = false;
            recPath_ = dataFolder() + L"\\Recording " + stamp + L".avi";
            opened = recorder_.open(recPath_, w, h, kCaptureFps);
        }
        if (!opened) {
            showMessage(T(L"Recording could not start"), TF(L"The file {0} could not be made.", recPath_));
            return;
        }
        recording_ = true;
        countUse(L"Tools: screen recordings");
        recStart_ = GetTickCount64();
        SetTimer(hwnd(), kCaptureTimer, 1000 / kCaptureFps, nullptr);
        showToast(L"check", T(L"Recording the screen"),
                  usingMp4_ ? T(L"Saving to MP4. No sound is recorded. Press Ctrl+Shift+R to stop.") : T(L"No sound is recorded. Press Ctrl+Shift+R to stop."), {}, nullptr);
        invalidate();
    }

    void stopRecording(bool autoStopped, bool lowSpace = false) {
        if (!recording_) return;
        KillTimer(hwnd(), kCaptureTimer);
        recording_ = false;
        int frames = recFrames();
        if (usingMp4_)
            mp4_.close();
        else
            recorder_.close();
        uint64_t secs = (GetTickCount64() - recStart_) / 1000;
        wstr name = recPath_.substr(recPath_.find_last_of(L'\\') + 1);
        if (frames == 0)
            showMessage(T(L"Nothing was recorded"), T(L"No frames could be captured, so the file is empty."));
        else if (lowSpace)
            showMessage(T(L"Recording stopped — disk nearly full"),
                        TF(L"Less than 20% of the space was left, so recording stopped to keep the rescue disk working. The {0} seconds recorded so far are saved as {1}.", std::to_wstring(secs), name));
        else
            showToast(L"check", autoStopped ? T(L"Recording stopped (time limit)") : T(L"Recording saved"),
                      name + L" · " + TF(L"{0} seconds, no sound", std::to_wstring(secs)), {T(L"Show in folder")}, [this](int) { openLink(dataFolder()); });
        invalidate();
    }

    // one frame, on the capture timer
    void captureFrame() {
        if (!recording_) return;
        if (GetTickCount64() - recStart_ > 10ull * 60 * 1000) {  // a 10-minute safety limit
            stopRecording(true);
            return;
        }
        if ((recFrames() & 7) == 0 && captureSpaceLow()) {  // every ~8 frames: stop before the disk fills
            stopRecording(true, true);
            return;
        }
        RedrawWindow(hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);  // a fresh, full frame
        GdiFlush();                                                              // make sure text is in the canvas bitmap
        int cw = canvas().width(), ch = canvas().height();
        const uint32_t* px = canvas().bits();
        if (!px || cw <= 0 || ch <= 0) return;  // minimised: skip this frame
        int w = recW(), h = recH();
        std::vector<uint32_t> frame = flattenCapture(px, cw, ch, w, h);  // opaque, and clamped to the recording size
        if (!addRecFrame(frame.data())) stopRecording(true);             // write / encode failed (disk full?)
    }

    // a small "recording" badge, drawn over everything so it is in the video too
    void drawRecIndicator(const RectF& area) {
        if (!recording_) return;
        uint64_t secs = (GetTickCount64() - recStart_) / 1000;
        wchar_t tm[16];
        swprintf(tm, 16, L"%llu:%02llu", (unsigned long long)(secs / 60), (unsigned long long)(secs % 60));
        wstr label = wstr(L"REC  ") + tm + L"  ·  " + T(L"no sound");
        float dot = S(9), gap = S(7), pad = S(11), hpill = S(26);
        float wpill = buttonWidth(label) + dot + gap;
        RectF r(area.right() - wpill - S(14), area.y + S(10), wpill, hpill);
        canvas().round(r, S(13), Color(0x1C, 0x1C, 0x1E));
        canvas().round(RectF(r.x + pad, r.y + (hpill - dot) / 2, dot, dot), dot / 2, kRed);
        text(label, RectF(r.x + pad + dot + gap, r.y, wpill, hpill), fonts().uiSemi, Color(0xFF, 0xFF, 0xFF), DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }

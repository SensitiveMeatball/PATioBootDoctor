// Part of class DoctorApp (included inside the class in main.cpp).
    // ---------------------------------------------------------------- background jobs (generic)
    void runJob(const wstr& title, const wstr& detail, bool cancellable, std::function<void()> work, std::function<void()> done) {
        if (busy_ || snapshot_) return;
        if (job_.joinable()) job_.join();
        busy_ = true;
        cancel_ = false;
        jobDone_ = std::move(done);
        progTitle_ = title;
        progCancel_ = cancellable;
        setProgress(title, cancellable ? 0.f : -1.f, detail, cancellable);
        job_ = std::thread([this, work] {
            noRedirection();
            work();
            PostMessageW(hwnd(), WM_BD_JOB, 0, 0);
        });
        invalidate();
    }

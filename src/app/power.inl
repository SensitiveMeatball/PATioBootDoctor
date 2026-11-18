// Part of class DoctorApp (included inside the class in main.cpp).
    // ---------------------------------------------------------------- power, with a timer
    void powerMenu(int action) {
        std::vector<kit::MenuItem> items;
        wstr what = action == 0 ? T(L"Shut down") : action == 1 ? T(L"Restart") : T(L"Hibernate");
        kit::MenuItem h;
        h.header = true;
        h.text = what;
        items.push_back(h);
        static const int mins[] = {0, 1, 5, 15, 30, 60, 120};
        for (int k = 0; k < 7; ++k) {
            kit::MenuItem m;
            m.text = mins[k] == 0 ? T(L"Now") : mins[k] < 60 ? TF(L"In {0} minutes", std::to_wstring(mins[k])) : mins[k] == 60 ? T(L"In 1 hour") : T(L"In 2 hours");
            if (mins[k] == 1) m.text = T(L"In 1 minute");
            m.cmd = C_POWER;
            m.arg = action * 100 + k;
            items.push_back(m);
        }
        kit::MenuItem c;
        c.text = T(L"At a time I choose…");
        c.cmd = C_POWER;
        c.arg = action * 100 + 7;
        items.push_back(c);
        if (powerAction_ >= 0) {
            kit::MenuItem sep;
            sep.separator = true;
            items.push_back(sep);
            kit::MenuItem x;
            x.text = T(L"Cancel the timer");
            x.cmd = C_POWER;
            x.arg = action * 100 + 99;
            items.push_back(x);
        }
        POINT p = mousePos();
        showMenu(items, std::max((float)p.x, S(60)), std::max(S(60), (float)p.y - S(300)));
    }

    void powerChoice(int action, int k) {
        static const int mins[] = {0, 1, 5, 15, 30, 60, 120};
        if (k == 99) {
            cancelPower(true);
            return;
        }
        wstr what = action == 0 ? T(L"shut down") : action == 1 ? T(L"restart") : T(L"hibernate");
        if (k == 0) {
            wstr msg = action == 2 ? T(L"Everything that is open is saved to the disk and the PC switches off. It all comes back at the next start.")
                                   : T(L"Programs that are open are closed; unsaved work in them is lost.");
            if (busy_) msg += T(L"\n\nWork is still running here: it waits until that has finished.");
            showConfirm(action == 0 ? T(L"Shut down now?") : action == 1 ? T(L"Restart now?") : T(L"Hibernate now?"), msg, {T(L"Cancel"), action == 0 ? T(L"Shut down") : action == 1 ? T(L"Restart") : T(L"Hibernate")}, 1, 0,
                        action != 2, [this, action](int b) {
                            if (b == 1) armPower(action, 1);
                        });
            return;
        }
        if (k >= 1 && k <= 6) {
            armPower(action, mins[k] * 60);
            return;
        }
        showInput(T(L"When?"), TF(L"Type in how many minutes to {0} (for example 45), or a time of day (for example 23:30).", what), L"30", T(L"Set the timer"),
                  [this, action](const wstr& t) {
                      int secs = -1;
                      size_t colon = t.find(L':');
                      if (colon != wstr::npos) {
                          int hh = _wtoi(t.substr(0, colon).c_str()), mm = _wtoi(t.substr(colon + 1).c_str());
                          if (hh >= 0 && hh < 24 && mm >= 0 && mm < 60) {
                              SYSTEMTIME now;
                              GetLocalTime(&now);
                              int cur = now.wHour * 3600 + now.wMinute * 60 + now.wSecond, want = hh * 3600 + mm * 60;
                              secs = want > cur ? want - cur : want + 86400 - cur;
                          }
                      } else {
                          int m = _wtoi(t.c_str());
                          if (m > 0 && m <= 24 * 60) secs = m * 60;
                      }
                      if (secs <= 0) {
                          showMessage(T(L"That is not a time"), T(L"Type a number of minutes (1 to 1440) or a time of day such as 23:30."));
                          return false;
                      }
                      armPower(action, secs);
                      return true;
                  });
    }

    void armPower(int action, int seconds) {
        if (snapshot_) return;
        powerAction_ = action;
        powerLeft_ = std::max(1, seconds);
        SetTimer(hwnd(), kPowerTimer, 1000, nullptr);
        if (seconds > 1) {
            wchar_t t[16];
            swprintf(t, 16, L"%d:%02d", powerLeft_ / 60, powerLeft_ % 60);
            showToast(L"check", action == 0 ? T(L"Shut down timer set") : action == 1 ? T(L"Restart timer set") : T(L"Hibernate timer set"),
                      TF(L"In {0}. Cancel it at the bottom of the left pane.", wstr(t)), {}, nullptr);
        }
        invalidate();
    }

    void cancelPower(bool tell) {
        if (powerAction_ < 0) return;
        KillTimer(hwnd(), kPowerTimer);
        powerAction_ = -1;
        powerLeft_ = 0;
        if (tell) showToast(L"check", T(L"Timer cancelled"), T(L"The PC stays on."), {}, nullptr);
        invalidate();
    }

    void powerTick() {
        if (powerAction_ < 0) return;
        if (powerLeft_ > 1) {
            --powerLeft_;
            if (powerLeft_ == 60)
                showToast(L"bd_timer", powerAction_ == 0 ? T(L"Shutting down in 1 minute") : powerAction_ == 1 ? T(L"Restarting in 1 minute") : T(L"Hibernating in 1 minute"),
                          T(L"Save your work in other programs."), {T(L"Cancel")}, [this](int) { cancelPower(true); });
            invalidate();
            return;
        }
        if (busy_) return;  // wait for the running work to finish
        int action = powerAction_;
        cancelPower(false);
        wstr err;
        bool ok = action == 0 ? shutdownPc(err) : action == 1 ? restartPc(err) : hibernatePc(err);
        if (!ok) showMessage(T(L"It did not work"), err);
    }

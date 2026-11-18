// Part of class DoctorApp (included inside the class in main.cpp).
    // ---------------------------------------------------------------- loading a storage driver
    // Where drivers are looked for: the drive this program runs from first, then removable disks and optical drives.
    std::vector<wstr> driverRoots() {
        std::vector<wstr> roots;
        auto add = [&](const wstr& r) {
            if (r.empty()) return;
            for (const wstr& x : roots)
                if (iequals(x, r)) return;
            roots.push_back(r);
        };
        wstr stick = stickFolder();
        if (!stick.empty()) add(stick.substr(0, 3));
        if (g_sys.exeDir.size() >= 3 && g_sys.exeDir[1] == L':' && towupper(g_sys.exeDir[0]) != L'X') add(g_sys.exeDir.substr(0, 3));
        for (const Disk& d : rep_.m.disks)
            if (d.group == DG_REMOVABLE)
                for (int i : d.parts)
                    if (!rep_.m.parts[i].letter.empty()) add(rep_.m.parts[i].letter + L"\\");
        for (const OtherDrive& o : rep_.m.others)
            if (o.hasMedia && o.group != DG_ENTERPRISE) add(o.letter + L"\\");
        return roots;
    }

    void askLoadDriver() {
        if (busy_) return;
        std::vector<wstr> roots = driverRoots();
        auto found = std::make_shared<std::vector<DriverInf>>();
        runJob(T(L"Looking for storage drivers"), plural(roots.size(), T(L"drive"), T(L"drives")), true, [this, roots, found] { *found = findStorageDrivers(roots, &cancel_); },
               [this, found] {
                   driverInfs_ = *found;
                   std::vector<kit::MenuItem> items;
                   kit::MenuItem h;
                   h.header = true;
                   h.text = driverInfs_.empty() ? T(L"No storage driver was found on the removable disks") : T(L"Storage drivers found (click one to load it):");
                   items.push_back(h);
                   for (size_t i = 0; i < driverInfs_.size() && i < 20; ++i) {
                       kit::MenuItem m;
                       m.text = driverInfs_[i].title;
                       m.tip = driverInfs_[i].path;
                       wstr dir = driverInfs_[i].path.substr(0, driverInfs_[i].path.find_last_of(L'\\'));
                       m.hint = dir.size() > 34 ? L"…" + dir.substr(dir.size() - 33) : dir;
                       m.cmd = C_LOAD_DRIVER;
                       m.arg = (int)i;
                       items.push_back(m);
                   }
                   kit::MenuItem sep;
                   sep.separator = true;
                   items.push_back(sep);
                   kit::MenuItem b;
                   b.text = T(L"Browse for a driver (.inf)…");
                   b.cmd = C_LOAD_DRIVER;
                   b.arg = -1;
                   items.push_back(b);
                   showMenu(items, S(260), S(120));
               });
    }

    void loadDriverCmd(int i) {
        if (i < 0) {
            std::vector<wstr> roots = driverRoots();
            showFilePicker(T(L"Choose the driver's .inf file"), roots.empty() ? g_sys.exeDir : roots[0], {L".inf"}, T(L"Load"), [this](const wstr& path) { defer([this, path] { startLoadDriver(path); }); });
            return;
        }
        if (i < (int)driverInfs_.size()) startLoadDriver(driverInfs_[i].path);
    }

    void startLoadDriver(const wstr& inf) {
        if (busy_) return;
        disksBeforeDriver_.clear();
        for (const Disk& d : rep_.m.disks) disksBeforeDriver_.insert(d.id());
        auto res = std::make_shared<wstr>();
        auto ok = std::make_shared<bool>(false);
        runJob(T(L"Loading the driver"), inf, false, [inf, res, ok] { *ok = loadDriver(inf, *res); },
               [this, inf, res, ok] {
                   if (!*ok) {
                       showMessage(T(L"The driver was not loaded"), *res);
                       return;
                   }
                   pendingDriver_ = inf.substr(inf.find_last_of(L'\\') + 1);
                   lastDriverInf_ = inf;
                   afterScanTitle_ = T(L"Driver loaded");
                   afterScanNote_ = *res;
                   SetTimer(hwnd(), kRefreshTimer, 1500, nullptr);  // give Windows a moment to find the disks
                   quietScan_ = false;
               });
    }

    // After the scan that follows a driver load: which disks are new?
    void markDriverDisks() {
        if (pendingDriver_.empty()) return;
        int fresh = 0;
        for (const Disk& d : rep_.m.disks)
            if (!disksBeforeDriver_.count(d.id())) {
                driverDisks_[d.id()] = pendingDriver_;
                ++fresh;
            }
        afterScanNote_ += L"\n\n" + (fresh ? TF(L"{0} appeared with this driver. They are marked under Disks and partitions.", plural(fresh, T(L"new disk"), T(L"new disks")))
                                         : wstr(T(L"No new disk appeared. The driver may be for another controller: try another one.")));
        if (fresh) afterScanNote_ += L"\n\n" + T(L"The Windows on that disk does not have this driver yet and will stop again when it starts. Add it: Windows Repair > Drivers > \"Add the loaded driver to this Windows\".");
        pendingDriver_.clear();
    }

    // ---------------------------------------------------------------- older copies of this program on removable disks
    struct OldCopy {
        int disk = -1;
        bool stick = false;    // a rescue stick (made again), else a plain copy of the program (replaced)
        wstr path, version, where;
    };

    void checkOldCopies() {
        if (oldCopiesChecked_ || snapshot_) return;
        oldCopiesChecked_ = true;
        oldCopies_.clear();
        wstr own = stickFolder();
        for (const Disk& d : rep_.m.disks) {
            if (d.group != DG_REMOVABLE) continue;
            for (int i : d.parts) {
                const Partition& p = rep_.m.parts[i];
                if (p.root.empty() || (!own.empty() && istartsWith(own, p.root))) continue;
                StickMarker mk = readStickMarker(p.root);
                if (mk.found) {
                    wstr ver = mk.version;
                    wstr where = mk.name.empty() ? d.name() : L"\"" + mk.name + L"\" (" + d.name() + L")";
                    if (ver.empty() || compareVersions(ver, kVersion) < 0) oldCopies_.push_back(OldCopy{d.number, true, p.root, ver.empty() ? wstr(T(L"an earlier version")) : ver, where});
                    continue;
                }
                for (const wchar_t* rel : {L"PATioBootDoctor_x64.exe", L"PatSuite\\PATio Boot Doctor\\PATioBootDoctor_x64.exe", L"PATio Boot Doctor\\PATioBootDoctor_x64.exe"}) {
                    int ma = 0, mi = 0, bu = 0;
                    if (!fileVersion(p.root + rel, ma, mi, bu)) continue;
                    wstr ver = std::to_wstring(ma) + L"." + std::to_wstring(mi) + L"." + std::to_wstring(bu);
                    if (compareVersions(ver, kVersion) < 0) oldCopies_.push_back(OldCopy{d.number, false, p.root + rel, ver, d.name()});
                }
            }
        }
        if (oldCopies_.empty()) return;
        const OldCopy c = oldCopies_[0];
        showToast(L"drive_usb", T(L"An older PATio Boot Doctor is on a removable disk"),
                  TF(c.stick ? L"{0} is a rescue stick made by version {1}; this is {2}." : L"{0} holds version {1}; this is {2}.", c.where, c.version, wstr(kVersion)),
                  {T(L"Update it"), T(L"Not now")}, [this, c](int b) {
                      if (b == 0) updateOldCopy(c);
                  }, false);
    }

    void updateOldCopy(const OldCopy& c) {
        if (c.stick) {
            confirmRescue(c.disk);  // made again; its keys, settings and backups are kept
            return;
        }
        showConfirm(T(L"Update the copy?"), TF(L"{0} (version {1}) is replaced by this version ({2}). Its settings and saved data stay.", c.path, c.version, wstr(kVersion)), {T(L"Cancel"), T(L"Update")}, 1, 1,
                    false, [this, c](int b) {
                        if (b != 1) return;
                        wchar_t exe[MAX_PATH] = {};
                        GetModuleFileNameW(nullptr, exe, MAX_PATH);
                        if (CopyFileW(exe, c.path.c_str(), FALSE)) showToast(L"check", T(L"Copy updated"), c.path, {}, nullptr);
                        else showMessage(T(L"The copy was not updated"), errorText(GetLastError()));
                    });
    }

    // ---------------------------------------------------------------- surface test
    void startSurfaceTest(int disk) {
        const Disk* d = nullptr;
        for (const Disk& x : rep_.m.disks)
            if (x.number == disk) d = &x;
        if (!d || busy_) return;
        wstr id = d->id(), name = d->name();
        auto res = std::make_shared<SurfaceResult>();
        countUse(L"Tools: surface tests");
        runJob(TF(L"Testing the surface of Disk {0}", std::to_wstring(disk)), name, true, [this, disk, res] { *res = surfaceTest(disk, [this](const wstr& s, float f) { progress(s, f); }, &cancel_); },
               [this, res, id, name] {
                   surface_[id] = res->result;
                   rememberBad(id, res->badRanges, res->finished, T(L"surface test"));
                   showMessage(res->bad ? T(L"Unreadable sectors were found") : res->finished ? T(L"The surface is fine") : T(L"Surface test stopped"),
                               name + L"\n\n" + res->result + (res->badList.empty() ? wstr() : T(L"\n\nWhere (from the start of the disk):\n") + res->badList.substr(0, 1200)));
                   if (journal_.isOpen()) journal_.note(T(L"Surface test: ") + name + L": " + res->result);
               });
    }

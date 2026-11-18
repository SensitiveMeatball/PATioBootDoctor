// Part of class DoctorApp (included inside the class in main.cpp).
    // ---------------------------------------------------------------- guided start: "what happens when you switch it on?"
    struct GuideCase {
        const wchar_t* button;
        const wchar_t* title;
        const wchar_t* text;
        const wchar_t* go;   // the button that opens the tool
        int target;          // 0 boot chain, 1 repair tools, 2 logs, 3 keys, 4 start-up options, 5 load driver
    };
    static const GuideCase* guideCases(int& n) {
        static const GuideCase k[] = {
            {N(L"Black screen or \"no boot device\""), N(L"The PC does not find anything to start"),
             N(L"The PC's firmware finds no start-up files: the start-up partition or its files are missing or damaged, the start-up entry points to the wrong place, the disk is not seen, or the PC is set to start from another disk.\n\n1. Look at the boot chain: it shows the first step that fails.\n2. Use the fix offered there, usually \"Rebuild the start-up files\".\n3. If no disk is listed at all, load its storage driver (Home), or check the cable and the storage mode in the firmware settings."),
             N(L"Show the boot chain"), 0},
            {N(L"Blue screen with a stop code"), N(L"Windows starts, then stops with a blue screen"),
             N(L"Windows itself found something it cannot go on with: a faulty or missing driver, damaged system files, a damaged registry, or failing hardware.\n\n1. Read the stop code under Windows Repair > Repair tools > Recent crashes: it says what the code means.\n2. INACCESSIBLE_BOOT_DEVICE: \"Turn the disk drivers back on\", and check the storage mode (AHCI / RAID / VMD).\n3. After a new driver or update: System Restore, or remove the update.\n4. Otherwise: \"Repair Windows system files\", then \"Repair the Windows image\"."),
             N(L"Open the repair tools"), 1},
            {N(L"Repair loop (\"Preparing Automatic Repair\")"), N(L"Windows keeps trying to repair itself"),
             N(L"Windows failed to start twice and now starts its own repair each time, which does not succeed.\n\n1. Read Automatic Repair's own log (Windows Repair > Logs > SrtTrail.txt): its last lines name the root cause.\n2. Usual cures: \"Undo unfinished updates\", \"Put back the registry's backup copy\", \"Repair Windows system files\", \"Check the Windows drive for errors\".\n3. To see the real error instead of the loop: Start-up options > Automatic Repair > Off."),
             N(L"Read the logs"), 2},
            {N(L"It asks for a BitLocker recovery key"), N(L"The drive is locked by BitLocker"),
             N(L"BitLocker locks the drive after changes to the firmware, Secure Boot, the start-up files or the hardware. The files are fine, but nothing can be read or repaired until the drive is unlocked with its 48-digit recovery key.\n\n1. Find the key: the Microsoft account of the PC's owner (aka.ms/myrecoverykey), a printout, the IT department, or the keys saved here.\n2. Unlock the drive, then scan again.\n3. Do not remove start-up entries or partitions before that."),
             N(L"Open BitLocker keys"), 3},
            {N(L"It starts, then freezes or restarts"), N(L"Windows gets part of the way"),
             N(L"Something that loads late fails: a driver, a service or a program that starts with Windows, or overheating or failing hardware.\n\n1. Start in Safe Mode (Windows Repair > Start-up options). If Safe Mode works, a driver or a start-up program is the cause: remove what was installed last.\n2. Switch on boot logging and read ntbtlog.txt under Logs.\n3. Check the disks' health and run a surface test (Disks and data)."),
             N(L"Open the start-up options"), 4},
            {N(L"It broke right after an update"), N(L"Windows stopped starting after an update"),
             N(L"An update was installed only half, or it brought a driver this PC does not get along with.\n\n1. \"Undo unfinished updates\" first.\n2. Then \"Remove a recent update\" and pick the newest one.\n3. Or System Restore to the point Windows made before the update."),
             N(L"Open the repair tools"), 1},
            {N(L"Windows starts but no one can sign in"), N(L"The password is wrong, forgotten, or the account is locked"),
             N(L"The account is disabled or locked, or its password is not known. This only helps an account kept on this PC, not a Microsoft or work/school sign-in.\n\n1. Open Accounts (Windows Repair) and read the accounts.\n2. Locked after wrong passwords: Unlock it.\n3. No admin account to sign in to: enable the built-in Administrator.\n4. Password forgotten: clear it, then set a new one once Windows starts (this loses that account's encrypted files and saved passwords)."),
             N(L"Open the accounts"), 6},
            {N(L"\"Secure Boot Violation\" or won't start after an update"), N(L"The firmware refuses the boot files"),
             N(L"Secure Boot refuses the boot manager. Often the 2011/2023 certificate change: a Windows update revoked the old certificate and the boot files or the firmware's trust list have not caught up. It can also be unsigned boot files or media.\n\n1. Open \"About Secure Boot\" to see which certificates this PC trusts and revokes, and the exact steps for it.\n2. Usually: turn Secure Boot off in the firmware to start, install every Windows update, then rebuild the start-up files and turn Secure Boot back on.\n3. Or rebuild the start-up files first (Windows Repair), so a current, signed boot manager is written."),
             N(L"About Secure Boot"), 7},
        };
        n = (int)(sizeof k / sizeof k[0]);
        return k;
    }

    float drawGuide(float x, float y, float w) {
        if (rep_.problems() == 0 && !rep_.installs.empty() && !techMode_) return 0;  // nothing is wrong: no questions
        float top = y;
        drawSectionHeader(RectF(x + S(4), y, w, S(24)), T(L"What happens when you switch the PC on?"));
        y += S(28);
        int n = 0;
        const GuideCase* k = guideCases(n);
        float bx = x;
        for (int i = 0; i < n; ++i) {
            wstr t = T(k[i].button);
            float bw = buttonWidth(t);
            if (bx + bw > x + w && bx > x) {
                bx = x;
                y += S(34);
            }
            drawButton(RectF(bx, y, bw, S(28)), t, H_GUIDE, i, kit::ButtonStyle::Normal);
            bx += bw + S(8);
        }
        return y + S(42) - top;
    }

    void guideStep(int i) {
        int n = 0;
        const GuideCase* k = guideCases(n);
        if (i < 0 || i >= n) return;
        int target = k[i].target;
        countUse(wstr(L"Guided start: ") + k[i].button);
        showConfirm(T(k[i].title), T(k[i].text), {T(L"Close"), T(k[i].go)}, 1, 1, false, [this, target](int b) {
            if (b != 1) return;
            defer([this, target] {
                switch (target) {
                    case 0: onCommand(C_VIEW, V_CHAIN, L""); break;
                    case 1: openRepair(); repairTab(0); break;
                    case 2: openRepair(); repairTab(3); break;
                    case 3: openKeys(); break;
                    case 4: openRepair(); repairTab(1); break;
                    case 6: openRepair(); repairTab(5); break;
                    case 7: showSecureBoot(); break;
                }
            });
        });
    }

    // ---------------------------------------------------------------- a driver for the Windows that does not start
    // Loading a driver makes the disk visible here; this puts it into that Windows, so it sees its disk too.
    void addDriverToWindows(int which) {
        if (repInstall_ < 0 || repInstall_ >= (int)rep_.installs.size() || busy_) return;
        auto go = [this](const wstr& inf) {
            Fix f = repairFix(FX_ADD_DRIVER);
            f.arg = inf;
            f.title = TF(L"Add the driver {0} to this Windows", inf.substr(inf.find_last_of(L'\\') + 1));
            checkOne(f);
            askFixList({f}, rep_.installs[repInstall_].label());
        };
        if (which == 0 && !lastDriverInf_.empty()) {
            go(lastDriverInf_);
            return;
        }
        std::vector<wstr> roots = driverRoots();
        wstr start = lastDriverInf_.empty() ? (roots.empty() ? g_sys.exeDir : roots[0]) : lastDriverInf_.substr(0, lastDriverInf_.find_last_of(L'\\'));
        showFilePicker(T(L"Choose the driver's .inf file"), start, {L".inf"}, T(L"Add"), [this, go](const wstr& path) { defer([go, path] { go(path); }); });
    }

#include "Migrate.h"
#include "Lang.h"
#include "DiskTool.h"
#include "Layout.h"
#include "Hive.h"
#include "core/Platform.h"

namespace bd {

namespace {

const uint64_t kMB = 1ull << 20;
uint64_t upMB(uint64_t v) { return (v + kMB - 1) / kMB * kMB; }
uint64_t downMB(uint64_t v) { return v / kMB * kMB; }
bool exists(const wstr& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

const Disk* diskByNum(const Report& r, int n) {
    for (const Disk& d : r.m.disks)
        if (d.number == n) return &d;
    return nullptr;
}

ToolResult diskpartRun(const wstr& script, wstr& log) {
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    wstr file = wstr(tmp) + L"PATioBD-migrate.txt";
    std::string u = wideToUtf8(script + L"exit\r\n");
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD wr = 0;
    if (h != INVALID_HANDLE_VALUE) {
        WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr);
        CloseHandle(h);
    }
    log += L"diskpart:\r\n" + script;
    ToolResult t = runTool(findSystemTool(L"diskpart.exe"), L"/s \"" + file + L"\"");
    DeleteFileW(file.c_str());
    log += t.output + L"\r\n";
    return t;
}

// Why Windows cannot come from this installation at all
wstr sourceProblem(const Report& r, int install) {
    if (install < 0 || install >= (int)r.installs.size()) return T(L"Choose a Windows first.");
    const Install& in = r.installs[install];
    if (in.linux || !in.atRoot || in.windowsOld) return T(L"Only a Windows at the top of its drive can be moved.");
    const Partition& W = r.m.parts[in.part];
    if (in.running)
        return T(L"This is the Windows that is running now. A running Windows keeps changing its files, so a copy of it would not be consistent. Start the PC from the Boot Doctor rescue USB stick (Tools > Make a USB rescue disk) and move it from there.");
    if (W.bitlocker || W.locked) return T(L"This Windows uses BitLocker. Turn BitLocker off first (Disks and partitions > the partition > Turn BitLocker off) and wait until the drive is decrypted.");
    if (!W.readable || W.fs != L"NTFS") return T(L"The Windows partition must be readable NTFS.");
    return L"";
}

}  // namespace

// "The maximum number of reclaimable bytes is: 152 GB (155648MB)": the number before "MB", in any language.
uint64_t shrinkQueryMB(const wstr& out) {
    size_t pos = out.rfind(L"MB");
    while (pos != wstr::npos && pos > 0) {
        size_t e = pos;
        while (e > 0 && out[e - 1] == L' ') --e;
        size_t a = e;
        while (a > 0 && iswdigit(out[a - 1])) --a;
        if (a < e) return _wcstoui64(out.substr(a, e - a).c_str(), nullptr, 10);
        pos = pos ? out.rfind(L"MB", pos - 1) : wstr::npos;
    }
    return 0;
}

std::vector<std::pair<int, wstr>> migrationTargets(const Report& r, int install) {
    std::vector<std::pair<int, wstr>> out;
    int src = install >= 0 && install < (int)r.installs.size() ? r.m.parts[r.installs[install].part].disk : -1;
    for (const Disk& d : r.m.disks) {
        if (d.number == src) continue;
        wstr why = workingDiskRefusal(r, d.number);
        if (why.empty() && d.health.known && d.health.level == 2) why = T(L"This disk reports that it may be failing (S.M.A.R.T.). Do not move Windows onto it.");
        if (why.empty() && !d.readable) why = T(L"This disk cannot be read.");
        if (why.empty() && d.size < (24ull << 30)) why = T(L"This disk is too small for Windows (at least 24 GB).");
        out.push_back({d.number, why});
    }
    return out;
}

MigratePlan planMigration(const Report& r, int install, int targetDisk, bool uefi) {
    MigratePlan p;
    p.install = install;
    p.target = targetDisk;
    p.uefi = uefi;
    p.why = sourceProblem(r, install);
    if (!p.why.empty()) return p;
    const Install& in = r.installs[install];
    const Partition& W = r.m.parts[in.part];
    p.source = W.disk;
    const Disk* t = diskByNum(r, targetDisk);
    if (!t) {
        p.why = T(L"Choose the disk Windows should move to.");
        return p;
    }
    for (const auto& c : migrationTargets(r, install))
        if (c.first == targetDisk && !c.second.empty()) {
            p.why = c.second;
            return p;
        }
    if (targetDisk == W.disk) {
        p.why = T(L"Windows is already on this disk.");
        return p;
    }
    p.bootLen = uefi ? 260 * kMB : 500 * kMB;
    p.msrLen = uefi ? 16 * kMB : 0;
    p.recLen = 1024 * kMB;
    uint64_t end = downMB(t->size - (uefi ? kMB : 0));
    if (!uefi && t->size > (2ull << 40)) end = 2ull << 40;  // MBR reaches 2 TB
    uint64_t room = end > kMB + p.bootLen + p.msrLen + p.recLen ? end - kMB - p.bootLen - p.msrLen - p.recLen : 0;
    p.winLen = downMB(room);
    uint64_t used = W.totalBytes > W.freeBytes ? W.totalBytes - W.freeBytes : W.length;
    if (p.winLen >= W.length) {
        p.copyLen = W.length;
    } else if (p.winLen >= used + (4ull << 30)) {
        wstr refuse = workingDiskRefusal(r, W.disk);
        if (!refuse.empty()) {
            p.why = T(L"The new disk is smaller than the Windows partition, so Windows would have to give up space first, but its disk is in use. ") + refuse;
            return p;
        }
        p.shrinkBy = upMB(W.length - p.winLen);
        p.copyLen = W.length - p.shrinkBy;
        p.notes.push_back(T(L"Before that, the page and hibernation files of the old Windows are deleted (Windows makes them again), and Windows is asked how much it can really give up: if that is not enough, nothing is changed."));
        p.notes.push_back(TF(L"The new disk is smaller than the Windows partition ({0}). The Windows partition on the old disk is first made {1} smaller (only free space at its end is given up); its files stay.",
                             sizeText(W.length), sizeText(p.shrinkBy)));
    } else {
        p.why = TF(L"The new disk is too small: Windows uses {0}, and only {1} would be left for it there (4 GB more than it uses are needed).", sizeText(used), sizeText(p.winLen));
        return p;
    }
    wstr names;
    for (int i : t->parts) names += (names.empty() ? L"" : L", ") + r.m.parts[i].name();
    p.notes.insert(p.notes.begin(), names.empty() ? TF(L"{0} is erased and set up anew.", t->name()) : TF(L"Everything on {0} is erased: {1}.", t->name(), names));
    int others = 0;
    for (const Partition& q : r.m.parts)
        if (q.disk == W.disk && q.role == R_DATA && &q != &W) ++others;
    if (others) p.notes.push_back(T(L"Only Windows (with its start-up and recovery partitions) is moved. Other data partitions on the old disk are not copied; copy their files yourself if you need them on the new disk."));
    if (W.freeBytes > 0) p.notes.push_back(TF(L"All {0} of the Windows partition are copied, block by block. That takes roughly 5 to 20 minutes for every 100 GB.", sizeText(p.copyLen)));
    const Disk* s = diskByNum(r, W.disk);
    if (s && s->health.known && s->health.level == 2)
        p.notes.push_back(T(L"The old disk reports that it may be failing (S.M.A.R.T.). Copying reads all of it; if it fails during the copy, the new disk stays incomplete and the old one is left as it is."));
    p.notes.push_back(T(L"Afterwards both disks hold the same Windows. Start from the new disk by choosing it in the PC's boot menu, or disconnect the old disk; only then wipe or reuse the old one."));
    p.possible = true;
    return p;
}

FixOutcome runMigration(const MigratePlan& p, Report& r, Journal& j, const std::function<void(const wstr&, float)>& progress, volatile bool* cancel) {
    FixOutcome o;
    auto step = [&](const wstr& s, float f) {
        if (progress) progress(s, f);
        o.log += L"* " + s + L"\r\n";
    };
    if (!p.possible) {
        o.result = p.why;
        return o;
    }
    const Install in = r.installs[p.install];
    const Partition W = r.m.parts[in.part];
    const Disk* t = diskByNum(r, p.target);
    if (!t || !workingDiskRefusal(r, p.target).empty()) {
        o.result = T(L"Nothing was changed: the new disk is in use or was not found.");
        return o;
    }
    wstr act = j.begin(T(L"Move Windows to another drive"), in.label() + L" -> " + t->name());
    wstr err;
    j.saveDiskTables(act, *t, err);
    auto done = [&](bool ok, const wstr& result) {
        o.ok = ok;
        o.result = result;
        j.end(act, ok, result, o.log, false);
        o.actionDir = act;
        return o;
    };
    // 1. the old Windows partition gives up space, when the new disk is smaller
    uint64_t copyLen = W.length;
    if (p.shrinkBy) {
        // The page, hibernation and swap files of a Windows that is not running sit in the way and
        // are made again by Windows at its next start: out of the way first.
        if (!in.running && !W.root.empty())
            for (const wchar_t* f : {L"pagefile.sys", L"hiberfil.sys", L"swapfile.sys"}) {
                wstr path = W.root + f;
                SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
                if (DeleteFileW(path.c_str())) o.log += wstr(L"Deleted ") + f + L" (Windows makes it again)\r\n";
            }
        // Ask Windows how much it can really give up, before anything is changed
        step(T(L"Checking how much the old Windows partition can give up"), 0.005f);
        ToolResult q = diskpartRun(L"select disk " + std::to_wstring(W.disk) + L"\r\nselect partition " + std::to_wstring(W.number) + L"\r\nshrink querymax\r\n", o.log);
        uint64_t maxMB = shrinkQueryMB(q.output);
        if (maxMB && maxMB < p.shrinkBy / kMB)
            return done(false, TF(L"Nothing was changed. The old Windows partition must give up {0}, but Windows can only free {1} at its end: files that cannot be moved are in the way. In that Windows, switch off hibernation (powercfg /h off), delete old restore points, run \"Optimize drives\", then try again; or use a bigger new disk.",
                                  sizeText(p.shrinkBy), sizeText(maxMB * kMB)));
        step(T(L"Making the old Windows partition smaller"), 0.01f);
        ToolResult s = diskpartRun(L"select disk " + std::to_wstring(W.disk) + L"\r\nselect partition " + std::to_wstring(W.number) + L"\r\nshrink desired=" +
                                       std::to_wstring(p.shrinkBy / kMB) + L" minimum=" + std::to_wstring(p.shrinkBy / kMB) + L"\r\n",
                                   o.log);
        if (s.code != 0) return done(false, T(L"The old Windows partition could not be made smaller (Windows can only give up free space at its end). Nothing else was changed.\n\n") + s.output.substr(s.output.size() > 300 ? s.output.size() - 300 : 0));
        copyLen = W.length - p.shrinkBy;
    }
    // 2. the new disk, in Windows' own order
    step(T(L"Setting up the new disk"), 0.03f);
    wstr sc = L"select disk " + std::to_wstring(p.target) + L"\r\nonline disk noerr\r\nattributes disk clear readonly noerr\r\nclean\r\n";
    wstr winMB = std::to_wstring(p.winLen / kMB);
    if (p.uefi)
        sc += L"convert gpt\r\ncreate partition efi size=" + std::to_wstring(p.bootLen / kMB) + L"\r\nformat quick fs=fat32 label=\"System\"\r\ncreate partition msr size=16\r\ncreate partition primary size=" + winMB +
              L"\r\ncreate partition primary\r\nformat quick fs=ntfs label=\"Recovery\"\r\nset id=de94bba4-06d1-4d40-a16a-bfd50179d6ac override\r\ngpt attributes=0x8000000000000001\r\n";
    else
        sc += L"convert mbr\r\ncreate partition primary size=" + std::to_wstring(p.bootLen / kMB) + L"\r\nformat quick fs=ntfs label=\"System Reserved\"\r\nactive\r\ncreate partition primary size=" + winMB +
              L"\r\ncreate partition primary size=" + std::to_wstring(p.recLen / kMB) + L"\r\nformat quick fs=ntfs label=\"Recovery\"\r\nset id=27 override\r\n";
    ToolResult ds = diskpartRun(sc, o.log);
    if (ds.code != 0) return done(false, T(L"The new disk could not be set up. Diskpart said:\n") + ds.output.substr(ds.output.size() > 400 ? ds.output.size() - 400 : 0));
    Sleep(2000);
    Machine m = readMachine();
    const Partition* nb = nullptr;  // new start-up partition
    const Partition* nw = nullptr;  // new Windows partition
    const Partition* nr = nullptr;  // new recovery partition
    for (const Partition& q : m.parts) {
        if (q.disk != p.target) continue;
        if (q.offset == kMB) nb = &q;
        else if (q.offset == kMB + p.bootLen + p.msrLen) nw = &q;
        else if (q.role == R_RECOVERY || (q.offset > kMB + p.bootLen + p.msrLen && q.length <= p.recLen + kMB)) nr = &q;
    }
    if (!nw || nw->length < copyLen) return done(false, T(L"The new Windows partition was not found where it was made, or it is too small. Nothing was copied."));
    // copies: the list is read again below
    Partition nwC = *nw, nbC = nb ? *nb : Partition(), nrC = nr ? *nr : Partition();
    nw = &nwC;
    nb = nb ? &nbC : nullptr;
    nr = nr ? &nrC : nullptr;
    // 3. the copy, block by block
    wstr srcVolErr, dstVolErr;
    HANDLE sv = W.volume.empty() ? INVALID_HANDLE_VALUE : lockVolume(W.volume, srcVolErr);
    if (!W.volume.empty() && sv == INVALID_HANDLE_VALUE)
        return done(false, T(L"The old Windows partition is in use (a program or a window has files open on it), so it cannot be copied safely. Close everything that uses it and try again."));
    HANDLE dv = nw->volume.empty() ? INVALID_HANDLE_VALUE : lockVolume(nw->volume, dstVolErr);
    HANDLE hs = openDiskHandle(W.disk, false), ht = openDiskHandle(p.target, true);
    bool ok = hs != INVALID_HANDLE_VALUE && ht != INVALID_HANDLE_VALUE;
    if (!ok) err = T(L"The disks could not be opened.");
    const uint32_t chunk = 8 * (uint32_t)kMB;
    uint32_t srcSector = hs != INVALID_HANDLE_VALUE ? diskSectorSize(hs) : 512;
    if (!srcSector) srcSector = 512;
    std::vector<std::pair<uint64_t, uint64_t>> badRanges;  // unreadable areas on the old disk: offset, length (bytes)
    uint64_t badBytes = 0;
    uint64_t copied = 0;
    auto t0 = GetTickCount64();
    while (ok && copied < copyLen) {
        if (cancel && *cancel) {
            ok = false;
            err = T(L"Stopped. The new disk is incomplete; the old one is unchanged.");
            break;
        }
        uint32_t n = (uint32_t)std::min<uint64_t>(chunk, copyLen - copied);
        std::string b;
        if (!readDiskAt(hs, W.offset + copied, n, b)) {
            // A failing disk: the whole chunk could not be read. Drop to one sector at a time,
            // zero-fill the sectors that cannot be read, log where they are, and carry on, so a
            // dying disk can still be migrated instead of the copy stopping dead.
            b.assign(n, '\0');
            for (uint32_t off = 0; off < n && !(cancel && *cancel); off += srcSector) {
                uint32_t m = (uint32_t)std::min<uint64_t>(srcSector, n - off);
                std::string s;
                uint64_t at = W.offset + copied + off;
                if (readDiskAt(hs, at, m, s)) {
                    memcpy(&b[off], s.data(), m);
                } else {
                    badBytes += m;
                    if (!badRanges.empty() && badRanges.back().first + badRanges.back().second == at)
                        badRanges.back().second += m;  // merge touching bad sectors
                    else
                        badRanges.push_back({at, m});
                }
            }
        }
        if (!writeDiskAt(ht, nw->offset + copied, b)) {
            ok = false;
            err = TF(L"The new disk could not be written at {0}.", sizeText(copied));
            break;
        }
        copied += n;
        if ((copied / n) % 16 == 0 || copied == copyLen) {
            uint64_t ms = GetTickCount64() - t0;
            wstr left = ms > 5000 ? L" · " + std::to_wstring((uint64_t)((double)ms / copied * (copyLen - copied) / 60000)) + T(L" min left") : wstr();
            step(TF(L"Copying Windows: {0} of {1}", sizeText(copied), sizeText(copyLen)) + left, 0.05f + 0.8f * copied / copyLen);
        }
    }
    if (ok) {
        FlushFileBuffers(ht);
        fixHiddenSectors(ht, nw->offset, o.log);
        // The copy says it is as long as the old partition; the end of the new one carries the backup first sector.
    }
    if (!badRanges.empty()) {
        o.log += TF(L"Unreadable areas on the old disk, zero-filled on the new one ({0} ranges, {1} total):\r\n", std::to_wstring(badRanges.size()), sizeText(badBytes));
        size_t shown = 0;
        for (const auto& r2 : badRanges) {
            if (shown++ >= 200) {
                o.log += TF(L"  ... and {0} more.\r\n", std::to_wstring(badRanges.size() - shown + 1));
                break;
            }
            o.log += L"  LBA " + std::to_wstring(r2.first / srcSector) + L" (" + sizeText(r2.second) + L")\r\n";
        }
    }
    if (hs != INVALID_HANDLE_VALUE) CloseHandle(hs);
    if (ht != INVALID_HANDLE_VALUE) CloseHandle(ht);
    if (dv != INVALID_HANDLE_VALUE) CloseHandle(dv);
    if (sv != INVALID_HANDLE_VALUE) CloseHandle(sv);
    if (!ok) return done(false, err);
    Sleep(2000);
    // 4. Windows fills its new place
    if (nw->length > copyLen + kMB) {
        step(T(L"Making Windows fill its new partition"), 0.87f);
        diskpartRun(L"select disk " + std::to_wstring(p.target) + L"\r\nselect partition " + std::to_wstring(nw->number) + L"\r\nextend filesystem\r\n", o.log);
    }
    // 5. start-up files
    step(T(L"Writing start-up files"), 0.9f);
    m = readMachine();
    Partition newWin, newBoot, newRec;
    for (const Partition& q : m.parts)
        if (q.disk == p.target) {
            if (q.offset == nw->offset) newWin = q;
            if (nb && q.offset == nb->offset) newBoot = q;
            if (nr && q.offset == nr->offset) newRec = q;
        }
    wstr wl = freeLetter();
    if (wl.empty() || newWin.volume.empty() || !mountAt(newWin.volume, wl, err)) return done(false, T(L"Windows was copied, but its new partition could not be opened to write start-up files. Use \"Rebuild the start-up files\" after scanning again."));
    wstr bl = freeLetter();
    bool bootOk = false;
    if (!bl.empty() && !newBoot.volume.empty() && mountAt(newBoot.volume, bl, err)) {
        wstr tool = exists(wl + L"\\Windows\\System32\\bcdboot.exe") ? wl + L"\\Windows\\System32\\bcdboot.exe" : findSystemTool(L"bcdboot.exe");
        wstr args = wl + L"\\Windows /s " + bl + (p.uefi ? L" /f UEFI" : L" /f BIOS");
        o.log += L"> bcdboot " + args + L"\r\n";
        ToolResult b = runTool(tool, args);
        o.log += b.output;
        bootOk = b.code == 0;
        unmount(bl);
    }
    // 6. the troubleshoot options
    step(T(L"Putting the troubleshoot options on the new disk"), 0.95f);
    bool reOk = false;
    wstr rl = freeLetter();
    if (!rl.empty() && !newRec.volume.empty() && mountAt(newRec.volume, rl, err)) {
        wstr src;
        for (const Partition& q : r.m.parts)
            if (q.disk == W.disk && !q.root.empty() && exists(q.root + L"Recovery\\WindowsRE\\Winre.wim")) src = q.root + L"Recovery\\WindowsRE\\Winre.wim";
        if (src.empty() && exists(wl + L"\\Windows\\System32\\Recovery\\Winre.wim")) src = wl + L"\\Windows\\System32\\Recovery\\Winre.wim";
        if (!src.empty()) {
            CreateDirectoryW((rl + L"\\Recovery").c_str(), nullptr);
            CreateDirectoryW((rl + L"\\Recovery\\WindowsRE").c_str(), nullptr);
            if (CopyFileW(src.c_str(), (rl + L"\\Recovery\\WindowsRE\\Winre.wim").c_str(), FALSE)) {
                wstr reagent = exists(wl + L"\\Windows\\System32\\reagentc.exe") ? wl + L"\\Windows\\System32\\reagentc.exe" : findSystemTool(L"reagentc.exe");
                ToolResult a = runTool(reagent, L"/setreimage /path " + rl + L"\\Recovery\\WindowsRE /target " + wl + L"\\Windows");
                o.log += L"> reagentc /setreimage\r\n" + a.output;
                reOk = a.code == 0;
            }
        }
        unmount(rl);
    }
    // 7. Clear the migrated Windows' saved drive-letter map, so it gives itself C: cleanly on the
    //    new disk's layout instead of keeping the old disk's letter (which can leave it on D:).
    step(T(L"Clearing the saved drive letters so Windows assigns C: cleanly"), 0.98f);
    {
        wstr sysHive = wl + L"\\Windows\\System32\\config\\SYSTEM", e2;
        LoadedHive lh;
        if (lh.load(sysHive, false, e2)) {
            HKEY k = nullptr;
            if (RegOpenKeyExW(lh.root(), L"MountedDevices", 0, KEY_READ | KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
                LONG rc = RegDeleteValueW(k, L"\\DosDevices\\C:");
                o.log += rc == ERROR_SUCCESS ? T(L"Removed \\DosDevices\\C: from MountedDevices; Windows reassigns C: on first boot.\r\n")
                         : rc == ERROR_FILE_NOT_FOUND ? T(L"MountedDevices had no \\DosDevices\\C: entry (nothing to clear).\r\n")
                                                      : TF(L"MountedDevices: \\DosDevices\\C: could not be removed (code {0}).\r\n", std::to_wstring(rc));
                RegCloseKey(k);
            } else
                o.log += T(L"MountedDevices key not found in the migrated SYSTEM hive.\r\n");
        } else
            o.log += T(L"Could not open the migrated SYSTEM hive to clear drive letters: ") + e2 + L"\r\n";
    }
    unmount(wl);
    wstr result = TF(L"Windows was copied to {0}.", t->name());
    result += bootOk ? T(L" Its start-up files are written.") : T(L" Its start-up files could not be written: scan again and use \"Rebuild the start-up files\" for it.");
    result += reOk ? T(L" The troubleshoot options are on its recovery partition.") : T(L" The troubleshoot options could not be set up; use \"Recreate the recovery partition\" for it later.");
    if (!badRanges.empty())
        result += TF(L"\n\nWarning: {0} of the old disk could not be read ({1} unreadable areas) and was copied as blank. The old disk is failing. Check Windows afterwards (chkdsk, sfc), and recover anything important from the old disk while it still turns on. The unreadable places are listed in the console.",
                     sizeText(badBytes), std::to_wstring(badRanges.size()));
    result += T(L"\n\nTo start from the new disk: choose it in the PC's boot menu (often F12, F11, F8 or Esc while the PC starts), or shut down and disconnect the old disk. The old disk is unchanged; wipe or reuse it only after the new one has started fine.");
    return done(bootOk, result);
}

}  // namespace bd

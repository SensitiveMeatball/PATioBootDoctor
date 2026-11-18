#include "Layout.h"
#include "Lang.h"
#include "DiskTool.h"
#include "core/Platform.h"
#include <map>

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

bool overlaps(uint64_t a, uint64_t al, uint64_t b, uint64_t bl) { return a < b + bl && b < a + al; }

wstr mb(uint64_t bytes) { return sizeText(bytes); }

ToolResult diskpartScript(const wstr& script, wstr& log) {
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    wstr file = wstr(tmp) + L"PATioBD-layout.txt";
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

}  // namespace

// ================================================================== planning
LayoutPlan planLayout(const Report& r, int install, bool uefi, bool bootFilesDue) {
    LayoutPlan plan;
    plan.install = install;
    plan.uefi = uefi;
    if (install < 0 || install >= (int)r.installs.size() || r.installs[install].linux || !r.installs[install].atRoot) {
        plan.why = T(L"Choose a Windows that sits at the top of its drive.");
        return plan;
    }
    const Install& in = r.installs[install];
    const Partition& W = r.m.parts[in.part];
    const Disk* d = diskByNum(r, W.disk);
    plan.disk = W.disk;
    if (!d) {
        plan.why = T(L"The disk was not found.");
        return plan;
    }
    wstr refuse = workingDiskRefusal(r, d->number);
    if (!refuse.empty()) {
        plan.why = refuse;
        return plan;
    }
    if (uefi && d->style != 2) {
        plan.why = T(L"A UEFI layout needs a GPT disk, and this disk is MBR. Change it first with Disks and partitions > the disk > \"Change to GPT, keeping Windows and files\".");
        return plan;
    }
    if (!uefi && d->style != 1) {
        plan.why = T(L"A legacy BIOS layout needs an MBR disk, and this disk is GPT. Use the UEFI layout instead (almost every PC made since 2012 starts in UEFI mode).");
        return plan;
    }
    if (!W.readable || W.fs != L"NTFS") {
        plan.why = T(L"The Windows partition must be readable NTFS.");
        return plan;
    }
    if (W.bitlocker) {
        plan.why = T(L"The Windows partition uses BitLocker. Turn BitLocker off first (Disks and partitions > the partition > Turn BitLocker off) and wait until it has finished decrypting; partitions encrypted with BitLocker are not moved here.");
        return plan;
    }
    // The part of the disk that is rearranged: between the nearest other partitions before and after Windows
    std::vector<int> parts;
    for (int i : d->parts)
        if (r.m.parts[i].role != R_EXTENDED) parts.push_back(i);
    std::sort(parts.begin(), parts.end(), [&](int a, int b) { return r.m.parts[a].offset < r.m.parts[b].offset; });
    auto kindOf = [&](int i) {
        const Partition& p = r.m.parts[i];
        if (i == in.part) return (int)SK_WIN;
        if (p.role == R_ESP) return uefi ? (int)SK_ESP : (int)SK_OTHER;
        if (p.role == R_MSR) return uefi ? (int)SK_MSR : (int)SK_OTHER;
        if (p.role == R_RECOVERY) return (int)SK_RECOVERY;
        if (!uefi && p.role == R_SYSTEM && p.length <= (1200ull << 20)) return (int)SK_SYSRES;
        return (int)SK_OTHER;
    };
    uint64_t start = kMB, end = downMB(d->size - (d->style == 2 ? kMB : 0));
    for (int i : parts) {
        const Partition& p = r.m.parts[i];
        if (kindOf(i) != SK_OTHER) continue;
        if (p.offset < W.offset) start = std::max(start, upMB(p.offset + p.length));
        else end = std::min(end, downMB(p.offset));
    }
    int esps = 0, recs = 0, sysres = 0;
    uint64_t espLen = 260 * kMB, sysLen = 500 * kMB, recLen = 1024 * kMB;
    for (int i : parts) {
        const Partition& p = r.m.parts[i];
        if (p.offset < start || p.offset >= end) continue;
        int k = kindOf(i);
        if (k == SK_OTHER) continue;
        if ((k != SK_WIN) && (p.locked || p.bitlocker)) {
            plan.why = TF(L"{0} uses BitLocker; it is not moved here.", p.name());
            return plan;
        }
        plan.now.push_back(Slot{k, i, p.offset, p.length});
        if (k == SK_ESP) {
            ++esps;
            espLen = upMB(p.length);
        }
        if (k == SK_SYSRES) {
            ++sysres;
            sysLen = upMB(p.length);
        }
        if (k == SK_RECOVERY) {
            ++recs;
            if (p.length >= (750ull << 20) && p.length <= (2048ull << 20)) recLen = upMB(p.length);
        }
    }
    if (esps > 1 || sysres > 1) {
        plan.why = T(L"There are two start-up partitions next to this Windows. Remove the one that is not used first (Disks and partitions), then try again.");
        return plan;
    }
    // The order Windows Setup makes
    uint64_t o = start;
    Slot tEsp{SK_ESP, -1, 0, 0}, tMsr{SK_MSR, -1, 0, 0}, tSys{SK_SYSRES, -1, 0, 0};
    if (uefi) {
        tEsp.off = o; tEsp.len = espLen; o += espLen;
        tMsr.off = o; tMsr.len = 16 * kMB; o += 16 * kMB;
    } else {
        tSys.off = o; tSys.len = sysLen; o += sysLen;
    }
    Slot tRec{SK_RECOVERY, -1, end - recLen, recLen};
    Slot tWin{SK_WIN, in.part, o, tRec.off > o ? tRec.off - o : 0};
    uint64_t used = W.totalBytes > W.freeBytes ? W.totalBytes - W.freeBytes : W.length;
    if (tWin.len < used + (2ull << 30)) {
        plan.why = TF(L"There is not enough room: Windows uses {0}, and the space between the other partitions leaves it only {1} (2 GB more than it uses are needed).", mb(used), mb(tWin.len));
        return plan;
    }
    for (Slot& s : plan.now)
        if (s.kind == SK_ESP) tEsp.part = s.part;
        else if (s.kind == SK_SYSRES) tSys.part = s.part;
    if (uefi) {
        plan.target = {tEsp, tMsr, tWin, tRec};
    } else
        plan.target = {tSys, tWin, tRec};
    // ---- the steps, worked out on a copy of the layout
    std::vector<Slot> cur = plan.now;
    auto target = [&](int kind) -> const Slot* {
        for (const Slot& t : plan.target)
            if (t.kind == kind) return &t;
        return nullptr;
    };
    auto freeFor = [&](uint64_t a, uint64_t len, int self) {
        for (const Slot& s : cur)
            if (s.part != self || self < 0)
                if (!(self >= 0 && s.part == self) && overlaps(a, len, s.off, s.len)) return false;
        return true;
    };
    bool winreSaved = exists(in.winDir + L"\\System32\\Recovery\\Winre.wim"), touchedBoot = bootFilesDue, newRecovery = false;
    for (int guard = 0; guard < 24; ++guard) {
        LayoutStep st;
        bool found = false;
        // 1. a recovery or reserved partition in the wrong place goes (its repair tools are kept first)
        for (size_t k = 0; k < cur.size() && !found; ++k) {
            Slot& s = cur[k];
            const Slot* t = target(s.kind);
            if ((s.kind != SK_RECOVERY && s.kind != SK_MSR) || (t && s.off == t->off && s.len == t->len)) continue;
            const Partition& p = r.m.parts[s.part];
            if (s.kind == SK_RECOVERY && !winreSaved && !p.root.empty() && exists(p.root + L"Recovery\\WindowsRE\\Winre.wim")) {
                st.kind = ST_SAVE_WINRE;
                st.part = s.part;
                st.title = T(L"Keep a copy of Windows' repair tools");
                st.what = TF(L"Copies the repair tools (Winre.wim) from {0} into Windows (System32\\Recovery), so they can be put on the new recovery partition later.", p.name());
                winreSaved = true;
            } else {
                st.kind = ST_DELETE;
                st.part = s.part;
                st.title = TF(L"Remove {0}", p.name());
                st.what = s.kind == SK_RECOVERY ? T(L"This recovery partition is in the wrong place or too small. A new one is made at the end of the disk later.")
                                                : T(L"The reserved partition (MSR) is in the wrong place. A new one is made in the right place later; it holds nothing.");
                st.danger = true;
                cur.erase(cur.begin() + k);
                newRecovery = newRecovery || s.kind == SK_RECOVERY;
            }
            found = true;
        }
        // 2. Windows smaller first, when it must end before its new end
        if (!found)
            for (Slot& s : cur)
                if (s.kind == SK_WIN && s.len > tWin.len + kMB) {
                    st.kind = ST_SHRINK;
                    st.part = s.part;
                    st.size = upMB(s.len - tWin.len);
                    st.title = TF(L"Make Windows {0} smaller", mb(st.size));
                    st.what = T(L"Gives up free space at the end of the Windows partition, so the other partitions fit in their places. Windows' own files are not moved by this step.");
                    st.danger = true;
                    s.len -= st.size;
                    found = true;
                    break;
                }
        // 3. moves: rightwards from the right, leftwards from the left
        if (!found) {
            int best = -1;
            for (size_t k = 0; k < cur.size(); ++k) {
                const Slot& s = cur[k];
                const Slot* t = target(s.kind);
                if (!t || s.kind == SK_RECOVERY || s.kind == SK_MSR || s.off == t->off) continue;
                if (!freeFor(t->off, s.len, s.part)) continue;
                if (best < 0) best = (int)k;
                else {
                    const Slot& b = cur[best];
                    bool sRight = t->off > s.off, bRight = target(b.kind)->off > b.off;
                    if ((sRight && (!bRight || s.off > b.off)) || (!sRight && !bRight && s.off < b.off)) best = (int)k;
                }
            }
            if (best >= 0) {
                Slot& s = cur[best];
                const Slot* t = target(s.kind);
                const Partition& p = r.m.parts[s.part];
                st.kind = ST_MOVE;
                st.part = s.part;
                st.to = t->off;
                st.size = s.len;
                st.slow = s.len > (2ull << 30);
                st.danger = true;
                bool right = t->off > s.off;
                st.title = right ? TF(L"Move {0} {1} towards the end", p.name(), mb(t->off - s.off)) : TF(L"Move {0} {1} towards the start", p.name(), mb(s.off - t->off));
                st.what = TF(L"Copies all {0} of the partition, block by block, to its new place. {1}", mb(s.len),
                             st.slow ? T(L"This takes long (roughly 10 to 30 minutes for every 100 GB) and must not be interrupted: keep the PC on and plugged in. If the power fails anyway, this program finishes the move when it is started again.")
                                     : T(L"This takes a moment."));
                s.off = t->off;
                if (s.kind == SK_WIN || s.kind == SK_ESP || s.kind == SK_SYSRES) touchedBoot = true;
                found = true;
            }
        }
        // 4. new start-up and reserved partitions
        auto have = [&](int kind) {
            for (const Slot& s : cur)
                if (s.kind == kind) return true;
            return false;
        };
        if (!found)
            for (const Slot& t : plan.target) {
                if (t.kind == SK_WIN || t.kind == SK_RECOVERY || have(t.kind) || !freeFor(t.off, t.len, -1)) continue;
                st.kind = t.kind == SK_ESP ? ST_CREATE_ESP : t.kind == SK_MSR ? ST_CREATE_MSR : ST_CREATE_SYSRES;
                st.to = t.off;
                st.size = t.len;
                st.title = t.kind == SK_ESP ? TF(L"Create the start-up partition (EFI, {0})", mb(t.len)) : t.kind == SK_MSR ? T(L"Create the reserved partition (MSR, 16 MB)")
                                                                                                                    : TF(L"Create the System Reserved partition ({0})", mb(t.len));
                st.what = t.kind == SK_MSR ? T(L"Windows keeps this small partition free for its own use on GPT disks.")
                                           : T(L"The partition the PC starts from. Fresh start-up files are written to it in a later step.");
                cur.push_back(Slot{t.kind, -1, t.off, t.len});
                if (t.kind != SK_MSR) touchedBoot = true;
                found = true;
                break;
            }
        // 5. Windows grows into what is left
        if (!found)
            for (Slot& s : cur)
                if (s.kind == SK_WIN && s.off == tWin.off && s.len + kMB < tWin.len && freeFor(s.off + s.len, tWin.len - s.len, s.part)) {
                    st.kind = ST_EXTEND;
                    st.part = s.part;
                    st.size = downMB(tWin.len - s.len);
                    st.title = TF(L"Make Windows {0} bigger", mb(st.size));
                    st.what = T(L"Adds the unused space after the Windows partition to it, up to where the recovery partition goes.");
                    s.len += st.size;
                    found = true;
                    break;
                }
        // 6. the recovery partition at the end
        if (!found && !have(SK_RECOVERY) && freeFor(tRec.off, tRec.len, -1)) {
            st.kind = ST_CREATE_RECOVERY;
            st.to = tRec.off;
            st.size = tRec.len;
            st.title = TF(L"Create the recovery partition ({0})", mb(tRec.len));
            st.what = T(L"The hidden partition for Windows' troubleshoot options, at the end of the Windows part of the disk.");
            cur.push_back(tRec);
            newRecovery = true;
            found = true;
        }
        if (!found) break;
        plan.steps.push_back(st);
    }
    // Anything still out of place that no step could fix?
    for (const Slot& t : plan.target) {
        bool ok = false;
        for (const Slot& s : cur)
            if (s.kind == t.kind && s.off == t.off && (s.kind == SK_WIN ? s.len + 2 * kMB >= t.len && s.len <= t.len : true)) ok = true;
        if (!ok) {
            plan.why = T(L"The partitions cannot be put in order from here: something is in the way that this program does not move. Look at the plan below and arrange the disk by hand in Disks and partitions.");
            return plan;
        }
    }
    // Start-up files and repair tools last
    bool espHasFiles = false;
    for (const Slot& s : plan.now)
        if ((s.kind == SK_ESP || s.kind == SK_SYSRES) && s.part >= 0 && !r.m.parts[s.part].root.empty())
            espHasFiles = exists(r.m.parts[s.part].root + (uefi ? L"EFI\\Microsoft\\Boot\\bootmgfw.efi" : L"bootmgr"));
    if (touchedBoot || !espHasFiles) {
        LayoutStep b;
        b.kind = ST_BOOTFILES;
        b.title = T(L"Write fresh start-up files");
        b.what = T(L"Copies the start-up files from this Windows to the start-up partition and writes its start-up entry, so the PC finds Windows in its new place.");
        plan.steps.push_back(b);
    }
    bool recHasTools = false;
    for (const Slot& s : plan.now)
        if (s.kind == SK_RECOVERY && s.off == tRec.off && !r.m.parts[s.part].root.empty()) recHasTools = exists(r.m.parts[s.part].root + L"Recovery\\WindowsRE\\Winre.wim");
    if (newRecovery || !recHasTools) {
        LayoutStep w;
        w.kind = ST_WINRE;
        w.title = T(L"Put the troubleshoot options on the recovery partition");
        w.what = T(L"Copies Windows' repair tools onto the recovery partition and switches the troubleshoot options on.");
        plan.steps.push_back(w);
    }
    plan.possible = true;
    plan.done = plan.steps.empty();
    return plan;
}

// ================================================================== moving a partition
namespace {

wstr statePath(const wstr& folder) { return folder + L"\\partition-move.ini"; }

void writeState(const wstr& path, int disk, uint64_t from, uint64_t to, uint64_t len, uint64_t done, const wstr& volume, const wstr& name) {
    wstr t = L"disk=" + std::to_wstring(disk) + L"\nfrom=" + std::to_wstring(from) + L"\nto=" + std::to_wstring(to) + L"\nlength=" + std::to_wstring(len) +
             L"\ndone=" + std::to_wstring(done) + L"\nvolume=" + volume + L"\nname=" + name + L"\n";
    std::string u = wideToUtf8(t);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD wr = 0;
    WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr);
    FlushFileBuffers(h);
    CloseHandle(h);
}

bool readState(const wstr& path, std::map<wstr, wstr>& kv) {
    std::string s;
    if (!readFileText(path, s)) return false;
    wstr t = utf8ToWide(s);
    size_t a = 0;
    while (a < t.size()) {
        size_t b = t.find(L'\n', a);
        if (b == wstr::npos) b = t.size();
        wstr line = t.substr(a, b - a);
        size_t eq = line.find(L'=');
        if (eq != wstr::npos) kv[line.substr(0, eq)] = line.substr(eq + 1);
        a = b + 1;
    }
    return kv.count(L"disk") && kv.count(L"from") && kv.count(L"to") && kv.count(L"length") && kv.count(L"done");
}

uint32_t le32(const std::string& s, size_t o) { return (uint8_t)s[o] | (uint8_t)s[o + 1] << 8 | (uint8_t)s[o + 2] << 16 | (uint32_t)(uint8_t)s[o + 3] << 24; }
void put32(std::string& s, size_t o, uint32_t v) {
    for (int i = 0; i < 4; ++i) s[o + i] = (char)(v >> (8 * i));
}

}  // namespace

// NTFS and FAT write the partition's start into their first sector ("hidden sectors"); older PCs start from it.
void fixHiddenSectors(HANDLE h, uint64_t start, wstr& log) {
    uint32_t sector = diskSectorSize(h);
    std::string b;
    if (!readDiskAt(h, start, sector, b)) return;
    wstr fs = sniffFs(b);
    if (fs != L"NTFS" && fs != L"FAT32" && fs != L"FAT") return;
    uint32_t hs = (uint32_t)(start / sector);
    put32(b, 0x1C, hs);
    writeDiskAt(h, start, b);
    if (fs == L"NTFS") {
        uint64_t total = (uint64_t)le32(b, 0x28) | (uint64_t)le32(b, 0x2C) << 32;
        uint16_t bps = (uint16_t)((uint8_t)b[0x0B] | (uint8_t)b[0x0C] << 8);
        std::string c;
        uint64_t at = start + total * bps;
        if (bps && readDiskAt(h, at, sector, c) && sniffFs(c) == L"NTFS") {
            put32(c, 0x1C, hs);
            writeDiskAt(h, at, c);
        }
    } else if (fs == L"FAT32") {
        uint16_t bk = (uint16_t)((uint8_t)b[0x32] | (uint8_t)b[0x33] << 8), bps = (uint16_t)((uint8_t)b[0x0B] | (uint8_t)b[0x0C] << 8);
        std::string c;
        if (bk && bk < 32 && readDiskAt(h, start + (uint64_t)bk * bps, sector, c)) {
            put32(c, 0x1C, hs);
            writeDiskAt(h, start + (uint64_t)bk * bps, c);
        }
    }
    log += L"Start written into the file system's first sector: sector " + std::to_wstring(hs) + L"\r\n";
}

namespace {

bool copySectors(int disk, uint64_t from, uint64_t to, uint64_t len, uint64_t done, const wstr& volume, const wstr& name, const wstr& state,
                 const std::function<void(const wstr&, float)>& progress, wstr& err, wstr& log) {
    HANDLE h = openDiskHandle(disk, true);
    if (h == INVALID_HANDLE_VALUE) {
        err = T(L"The disk could not be opened for writing.");
        return false;
    }
    uint32_t sector = diskSectorSize(h);
    uint64_t delta = to > from ? to - from : from - to;
    // A block never larger than the distance moved: then a block is never written over what is still to be read,
    // and a broken-off move can safely go on from its last finished block.
    uint32_t chunk = (uint32_t)std::min<uint64_t>(8 * kMB, delta) / sector * sector;
    bool right = to > from, ok = chunk > 0;
    if (!ok) err = T(L"The partition does not need to move.");
    auto t0 = GetTickCount64();
    while (ok && done < len) {
        uint32_t n = (uint32_t)std::min<uint64_t>(chunk, len - done);
        uint64_t src = right ? from + len - done - n : from + done;
        uint64_t dst = right ? to + len - done - n : to + done;
        std::string b;
        if (!readDiskAt(h, src, n, b) || !writeDiskAt(h, dst, b)) {
            err = T(L"Reading or writing the disk failed (error ") + std::to_wstring(GetLastError()) + T(L"). The move can be finished later: start this program again.");
            ok = false;
            break;
        }
        done += n;
        writeState(state, disk, from, to, len, done, volume, name);
        if (progress && (done == len || (done / n) % 16 == 0)) {
            uint64_t ms = GetTickCount64() - t0;
            wstr left;
            if (ms > 5000 && done) {
                uint64_t rest = (uint64_t)((double)ms / done * (len - done) / 1000);
                left = L" · " + std::to_wstring(rest / 60) + T(L" min left");
            }
            progress(TF(L"Moving {0}: {1} of {2}", name, sizeText(done), sizeText(len)) + left, (float)done / len);
        }
    }
    FlushFileBuffers(h);
    if (ok) fixHiddenSectors(h, to, log);
    CloseHandle(h);
    return ok;
}

}  // namespace

bool movePartition(int disk, const Partition& p, uint64_t newOffset, const wstr& stateFolder, const std::function<void(const wstr&, float)>& progress, wstr& err,
                   wstr& log, uint64_t resumeDone) {
    toolLog(L"[move] " + p.name() + L": " + std::to_wstring(p.offset) + L" -> " + std::to_wstring(newOffset));
    HANDLE vol = INVALID_HANDLE_VALUE;
    if (!p.volume.empty()) {
        vol = lockVolume(p.volume, err);
        if (vol == INVALID_HANDLE_VALUE && resumeDone == 0) return false;  // in use: nothing was touched yet
        err.clear();
    }
    wstr state = statePath(stateFolder);
    writeState(state, disk, p.offset, newOffset, p.length, resumeDone, p.volume, p.name());
    bool ok = copySectors(disk, p.offset, newOffset, p.length, resumeDone, p.volume, p.name(), state, progress, err, log);
    if (ok) ok = setPartitionOffset(disk, p.offset, newOffset, err);
    if (ok) {
        // The old place still holds a copy of the file system's first sector (and NTFS's backup of it in
        // its last sector). Left there, a later search for lost partitions finds a partition that is not
        // real. Clear them where they are outside the partition's new place.
        HANDLE h = openDiskHandle(disk, true);
        if (h != INVALID_HANDLE_VALUE) {
            uint32_t sector = diskSectorSize(h);
            for (uint64_t at : {p.offset, p.offset + p.length - sector}) {
                std::string b;
                if (at >= newOffset && at < newOffset + p.length) continue;
                if (sector && readDiskAt(h, at, sector, b) && !sniffFs(b).empty() && writeDiskAt(h, at, std::string(sector, '\0')))
                    log += L"Old first-sector copy cleared at " + std::to_wstring(at) + L"\r\n";
            }
            FlushFileBuffers(h);
            CloseHandle(h);
        }
    }
    if (vol != INVALID_HANDLE_VALUE) CloseHandle(vol);
    if (ok) DeleteFileW(state.c_str());
    return ok;
}

bool pendingMove(const wstr& stateFolder, wstr& what) {
    std::map<wstr, wstr> kv;
    if (stateFolder.empty() || !readState(statePath(stateFolder), kv)) return false;
    uint64_t len = _wcstoui64(kv[L"length"].c_str(), nullptr, 10), done = _wcstoui64(kv[L"done"].c_str(), nullptr, 10);
    what = TF(L"Moving {0} on Disk {1} was broken off after {2} of {3}.", kv[L"name"], kv[L"disk"], sizeText(done), sizeText(len));
    return true;
}

FixOutcome resumeMove(const wstr& stateFolder, const std::function<void(const wstr&, float)>& progress) {
    FixOutcome o;
    std::map<wstr, wstr> kv;
    if (!readState(statePath(stateFolder), kv)) {
        o.result = T(L"There is no broken-off move to finish.");
        return o;
    }
    Partition p;
    p.disk = _wtoi(kv[L"disk"].c_str());
    p.offset = _wcstoui64(kv[L"from"].c_str(), nullptr, 10);
    p.length = _wcstoui64(kv[L"length"].c_str(), nullptr, 10);
    p.volume = kv[L"volume"];
    uint64_t to = _wcstoui64(kv[L"to"].c_str(), nullptr, 10), done = _wcstoui64(kv[L"done"].c_str(), nullptr, 10);
    wstr err;
    // The volume at the old place may have been mounted again: take it away first if it is there.
    o.ok = movePartition(p.disk, p, to, stateFolder, progress, err, o.log, done);
    o.result = o.ok ? T(L"The move is finished. Scan again and go on with the next step.") : err;
    return o;
}

// ================================================================== carrying out a step
FixOutcome runLayoutStep(const LayoutPlan& plan, Report& r, Journal& j, const wstr& stateFolder, const std::function<void(const wstr&, float)>& progress,
                         volatile bool* cancel) {
    FixOutcome o;
    if (!plan.possible || plan.steps.empty()) {
        o.result = plan.why.empty() ? T(L"Nothing to do.") : plan.why;
        return o;
    }
    wstr refuse = workingDiskRefusal(r, plan.disk);
    if (!refuse.empty()) {
        o.result = T(L"Nothing was changed. ") + refuse;
        return o;
    }
    const LayoutStep& st = plan.steps[0];
    const Disk* d = diskByNum(r, plan.disk);
    if (!d) {
        o.result = T(L"The disk was not found.");
        return o;
    }
    if (st.kind == ST_BOOTFILES || st.kind == ST_WINRE) {
        Fix f = makeFix(st.kind == ST_BOOTFILES ? FX_REBUILD_BOOT : FX_REBUILD_WINRE);
        f.install = plan.install;
        f.part = r.installs[plan.install].part;
        f.disk = plan.disk;
        return runFix(f, r, j, cancel, [&](const wstr& s) {
            if (progress) progress(s, -1.f);
        });
    }
    wstr act = j.begin(st.title, d->name());
    wstr err;
    bool canUndo = st.kind == ST_DELETE || st.kind == ST_CREATE_ESP || st.kind == ST_CREATE_MSR || st.kind == ST_CREATE_SYSRES || st.kind == ST_CREATE_RECOVERY;
    if (st.kind != ST_SAVE_WINRE && !j.saveDiskTables(act, *d, err)) {
        o.result = T(L"Nothing was changed, because the backup failed. ") + err;
        j.end(act, false, o.result, L"", false);
        return o;
    }
    const Partition* p = st.part >= 0 && st.part < (int)r.m.parts.size() ? &r.m.parts[st.part] : nullptr;
    wstr sel = L"select disk " + std::to_wstring(d->number) + L"\r\n";
    wstr selp = p ? L"select partition " + std::to_wstring(p->number) + L"\r\n" : wstr();
    auto dp = [&](const wstr& script) {
        ToolResult t = diskpartScript(sel + script, o.log);
        o.ok = t.started && t.code == 0;
        if (!o.ok) o.result = T(L"Windows did not allow it. Diskpart said:\n") + t.output.substr(t.output.size() > 400 ? t.output.size() - 400 : 0);
        return o.ok;
    };
    wstr offKB = std::to_wstring(st.to / 1024), sizeMB = std::to_wstring(st.size / kMB);
    switch (st.kind) {
        case ST_SAVE_WINRE: {
            const Install& in = r.installs[plan.install];
            wstr root = p ? p->root : wstr(), letter;
            if (p && root.empty() && !p->volume.empty()) {
                letter = freeLetter();
                if (!letter.empty() && mountAt(p->volume, letter, err)) root = letter + L"\\";
            }
            wstr dir = in.winDir + L"\\System32\\Recovery";
            CreateDirectoryW(dir.c_str(), nullptr);
            o.ok = !root.empty() && CopyFileW((root + L"Recovery\\WindowsRE\\Winre.wim").c_str(), (dir + L"\\Winre.wim").c_str(), FALSE);
            if (!letter.empty()) unmount(letter);
            o.result = o.ok ? T(L"A copy of the repair tools is kept in Windows (System32\\Recovery).") : T(L"The repair tools could not be copied: ") + errorText(GetLastError());
            break;
        }
        case ST_DELETE:
            if (dp(selp + L"delete partition override\r\n")) o.result = T(L"The partition was removed.");
            break;
        case ST_SHRINK:
            if (dp(selp + L"shrink desired=" + sizeMB + L" minimum=" + sizeMB + L"\r\n")) o.result = TF(L"Windows is {0} smaller.", sizeText(st.size));
            else o.result += T(L"\n\nWindows can only give up space that is free at the end of its partition. Free up space, run \"Check for errors\" on it, and try again.");
            break;
        case ST_EXTEND:
            if (dp(selp + L"extend size=" + sizeMB + L"\r\n")) o.result = TF(L"Windows is {0} bigger.", sizeText(st.size));
            break;
        case ST_CREATE_ESP:
            if (dp(L"create partition efi size=" + sizeMB + L" offset=" + offKB + L"\r\nformat quick fs=fat32 label=\"System\"\r\n")) o.result = T(L"The start-up partition was made.");
            break;
        case ST_CREATE_MSR:
            if (dp(L"create partition msr size=16 offset=" + offKB + L"\r\n")) o.result = T(L"The reserved partition (MSR) was made.");
            break;
        case ST_CREATE_SYSRES:
            if (dp(L"create partition primary size=" + sizeMB + L" offset=" + offKB + L"\r\nformat quick fs=ntfs label=\"System Reserved\"\r\nactive\r\n"))
                o.result = T(L"The System Reserved partition was made and marked active.");
            break;
        case ST_CREATE_RECOVERY:
            if (dp(L"create partition primary size=" + sizeMB + L" offset=" + offKB + L"\r\nformat quick fs=ntfs label=\"Recovery\"\r\n" +
                   (d->style == 2 ? wstr(L"set id=de94bba4-06d1-4d40-a16a-bfd50179d6ac override\r\ngpt attributes=0x8000000000000001\r\n") : wstr(L"set id=27 override\r\n"))))
                o.result = T(L"The recovery partition was made.");
            break;
        case ST_MOVE:
            if (!p) {
                o.result = T(L"The partition was not found.");
                break;
            }
            o.ok = movePartition(d->number, *p, st.to, stateFolder, progress, err, o.log);
            o.result = o.ok ? TF(L"{0} is in its new place.", p->name()) : err;
            break;
    }
    o.actionDir = act;
    o.undoable = canUndo && o.ok;
    j.end(act, o.ok, o.result, o.log, o.undoable);
    return o;
}

}  // namespace bd

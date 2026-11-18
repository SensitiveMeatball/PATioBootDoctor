#include "Recover.h"
#include "Lang.h"
#include "DiskTool.h"
#include <map>
#include <set>

namespace bd {

namespace {

uint16_t u16(const std::string& s, size_t o) { return o + 2 <= s.size() ? (uint16_t)((uint8_t)s[o] | (uint8_t)s[o + 1] << 8) : 0; }
uint32_t u32(const std::string& s, size_t o) { return o + 4 <= s.size() ? (uint32_t)u16(s, o) | (uint32_t)u16(s, o + 2) << 16 : 0; }
uint64_t u64(const std::string& s, size_t o) { return o + 8 <= s.size() ? (uint64_t)u32(s, o) | (uint64_t)u32(s, o + 4) << 32 : 0; }
uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

wstr asciiField(const std::string& s, size_t o, size_t n) {
    wstr out;
    for (size_t i = o; i < o + n && i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (!c) break;
        out += (wchar_t)c;
    }
    while (!out.empty() && out.back() == L' ') out.pop_back();
    return out;
}

const uint64_t kMB = 1ull << 20;

}  // namespace

// ================================================================== lost partitions
bool parseBootSector(const std::string& s, wstr& fs, uint64_t& size, wstr& label) {
    fs.clear();
    label.clear();
    size = 0;
    if (s.size() < 512) return false;
    uint16_t bps = u16(s, 0x0B);
    bool bpsOk = bps == 512 || bps == 1024 || bps == 2048 || bps == 4096;
    if (!memcmp(&s[3], "NTFS    ", 8) && bpsOk) {
        uint8_t v = (uint8_t)s[0x0D];
        uint32_t spc = v > 0x80 ? 1u << (256 - v) : v;
        uint64_t total = u64(s, 0x28);
        if (!spc || !total) return false;
        fs = L"NTFS";
        size = (total + 1) * bps;
        return true;
    }
    if (!memcmp(&s[3], "EXFAT   ", 8)) {
        uint8_t shift = (uint8_t)s[0x6C];
        if (shift < 9 || shift > 12) return false;
        fs = L"exFAT";
        size = u64(s, 0x48) << shift;
        return size != 0;
    }
    if (!memcmp(&s[3], "-FVE-FS-", 8)) {
        fs = L"BitLocker";
        uint64_t total = u64(s, 0x28);
        size = bpsOk && total ? (total + 1) * bps : 0;
        return true;
    }
    if (u16(s, 0x1FE) == 0xAA55 && bpsOk && !memcmp(&s[0x52], "FAT32   ", 8)) {
        fs = L"FAT32";
        size = (uint64_t)u32(s, 0x20) * bps;
        label = asciiField(s, 0x47, 11);
        if (label == L"NO NAME") label.clear();
        return size != 0;
    }
    if (u16(s, 0x1FE) == 0xAA55 && bpsOk && (!memcmp(&s[0x36], "FAT16   ", 8) || !memcmp(&s[0x36], "FAT12   ", 8))) {
        fs = L"FAT";
        uint32_t t = u16(s, 0x13);
        size = (uint64_t)(t ? t : u32(s, 0x20)) * bps;
        label = asciiField(s, 0x2B, 11);
        if (label == L"NO NAME") label.clear();
        return size != 0;
    }
    if (s.size() >= 2048 && u16(s, 1024 + 0x38) == 0xEF53) {
        uint32_t logBlock = u32(s, 1024 + 0x18);
        if (logBlock > 6) return false;
        uint64_t blocks = u32(s, 1024 + 0x04);
        if (u32(s, 1024 + 0x60) & 0x80) blocks |= (uint64_t)u32(s, 1024 + 0x150) << 32;
        uint32_t compat = u32(s, 1024 + 0x5C), incompat = u32(s, 1024 + 0x60);
        fs = (incompat & 0x40) ? L"ext4" : (compat & 0x4) ? L"ext3" : L"ext2";
        size = blocks * (1024ull << logBlock);
        label = asciiField(s, 1024 + 0x78, 16);
        return blocks != 0;
    }
    return false;
}

namespace {

// A second look, so a stray sector that only looks like a start is not offered.
bool confirmFs(const ReadFn& rd, uint64_t start, const std::string& boot, const wstr& fs) {
    if (fs == L"NTFS") {
        uint16_t bps = u16(boot, 0x0B);
        uint8_t v = (uint8_t)boot[0x0D];
        uint32_t spc = v > 0x80 ? 1u << (256 - v) : v;
        uint64_t mft = u64(boot, 0x30) * spc * bps, mirr = u64(boot, 0x38) * spc * bps;
        std::string r;
        if (rd(start + mft, 512, r) && !memcmp(r.data(), "FILE", 4)) return true;
        return rd(start + mirr, 512, r) && !memcmp(r.data(), "FILE", 4);
    }
    if (fs == L"FAT32") {
        std::string r;
        uint16_t bps = u16(boot, 0x0B);
        return rd(start + bps * (uint64_t)std::max<uint16_t>(1, u16(boot, 0x30)), 512, r) && !memcmp(r.data(), "RRaA", 4);
    }
    return true;
}

struct Region {
    uint64_t a, b;
};

}  // namespace

std::vector<LostPartition> findLostPartitions(const Report& r, int diskNum, bool full, const ScanProgress& progress, volatile bool* cancel) {
    std::vector<LostPartition> out;
    const Disk* d = nullptr;
    for (const Disk& x : r.m.disks)
        if (x.number == diskNum) d = &x;
    if (!d) return out;
    uint64_t dsize = d->size;
    ReadFn rd = diskReader(diskNum, &dsize);
    if (!rd) return out;
    if (!dsize) dsize = d->size;
    std::vector<Region> regions;
    bool noTable = d->style == 0 || d->parts.empty();
    if (noTable) regions.push_back({0, dsize});
    else
        for (const auto& g : d->gaps) regions.push_back({g.first, g.first + g.second});
    uint64_t total = 0, done = 0;
    for (const Region& g : regions) total += g.b - g.a;
    std::map<uint64_t, LostPartition> found;
    auto consider = [&](uint64_t start, const std::string& boot, bool backup) {
        wstr fs, label;
        uint64_t size = 0;
        if (!parseBootSector(boot, fs, size, label)) return false;
        if (start >= dsize || found.count(start)) return false;
        if (size && start + size > dsize + kMB) return false;  // longer than the disk: not real
        if (!confirmFs(rd, start, boot, fs)) return false;
        LostPartition lp;
        lp.offset = start;
        lp.length = size;
        lp.fs = fs;
        lp.label = label;
        lp.fromBackup = backup;
        found[start] = lp;
        toolLog(L"[recover] found " + fs + L" at " + std::to_wstring(start) + (backup ? L" (backup copy)" : L""));
        return true;
    };
    // The backup copies: NTFS keeps one in its last sector, FAT32 at sector 6, exFAT at sector 12.
    auto considerBackup = [&](uint64_t at, const std::string& sec) {
        wstr fs, label;
        uint64_t size = 0;
        if (!parseBootSector(sec, fs, size, label)) return;
        uint16_t bps = u16(sec, 0x0B);
        if (fs == L"NTFS" && size > bps && at + bps >= size) consider(at + bps - size, sec, true);
        if (fs == L"FAT32" && at >= 6ull * bps) consider(at - 6ull * bps, sec, true);
        if (fs == L"exFAT" && at >= 12ull * 512) consider(at - 12ull * (1ull << (uint8_t)sec[0x6C]), sec, true);
    };
    for (const Region& g : regions) {
        if (cancel && *cancel) break;
        if (!full) {
            std::vector<uint64_t> spots;
            if (g.a % kMB) spots.push_back(g.a);
            if (g.a <= 63 * 512 && g.b > 63 * 512) spots.push_back(63 * 512);
            for (uint64_t p = (g.a + kMB - 1) / kMB * kMB; p + 4096 <= g.b; p += kMB) spots.push_back(p);
            size_t k = 0;
            for (uint64_t p : spots) {
                if (cancel && *cancel) break;
                if (++k % 256 == 0 && progress) progress(T(L"Looking for lost partitions"), (float)(done + (p - g.a)) / std::max<uint64_t>(1, total));
                auto it = found.upper_bound(p);
                if (it != found.begin()) {
                    --it;
                    if (it->second.length && p < it->first + it->second.length) continue;  // inside one already found
                }
                std::string b;
                uint64_t from = p >= 4096 ? p - 4096 : 0;
                if (!rd(from, (uint32_t)(p - from + 4096), b)) continue;
                std::string here = b.substr((size_t)(p - from));
                if (!consider(p, here, false) && p >= 512 && p - from >= 512) considerBackup(p - 512, b.substr((size_t)(p - from - 512), 512));
            }
        } else {
            const uint32_t chunk = 4 * (uint32_t)kMB;
            for (uint64_t p = g.a / 512 * 512; p < g.b; p += chunk) {
                if (cancel && *cancel) break;
                if (progress) progress(T(L"Looking for lost partitions, sector by sector"), (float)(done + (p - g.a)) / std::max<uint64_t>(1, total));
                uint32_t n = (uint32_t)std::min<uint64_t>(chunk, g.b - p);
                n = n / 512 * 512;
                std::string b;
                if (!n || !rd(p, n, b)) continue;
                for (uint32_t o = 0; o + 512 <= n; o += 512) {
                    const char* s = b.data() + o;
                    bool sig = !memcmp(s + 3, "NTFS    ", 8) || !memcmp(s + 3, "EXFAT   ", 8) || !memcmp(s + 3, "-FVE-FS-", 8) || !memcmp(s + 0x52, "FAT32   ", 8) ||
                               !memcmp(s + 0x36, "FAT16   ", 8);
                    bool ext = ((p + o) % 1024 == 0) && (uint8_t)s[0x38] == 0x53 && (uint8_t)s[0x39] == 0xEF && p + o >= 1024;
                    if (sig) {
                        std::string four;
                        if (rd(p + o, 4096, four) && !consider(p + o, four, false)) considerBackup(p + o, four.substr(0, 512));
                    } else if (ext) {
                        std::string four;
                        if (rd(p + o - 1024, 4096, four)) consider(p + o - 1024, four, false);
                    }
                }
            }
        }
        done += g.b - g.a;
    }
    // Can each be put back where it was?
    for (auto& kv : found) {
        LostPartition lp = kv.second;
        const Region* in = nullptr;
        for (const Region& g : regions)
            if (lp.offset >= g.a && lp.offset < g.b) in = &g;
        if (!lp.length && in) {
            lp.length = (in->b - lp.offset) / kMB * kMB;
            lp.why = T(L"Its size is not written in its first sector, so all unused space after it is used.");
        }
        if (!in) {
            lp.restorable = false;
            lp.why = T(L"It starts inside a partition that exists now.");
        } else if (lp.offset + lp.length > in->b) {
            lp.restorable = false;
            lp.why = T(L"It runs into a partition that exists now, so it was partly overwritten or that partition was made over it.");
        } else if (lp.offset % 512) {
            lp.restorable = false;
            lp.why = T(L"It does not start on a sector boundary.");
        }
        out.push_back(lp);
    }
    // Leave out partitions found inside another found one (for example a disk image file)
    std::vector<LostPartition> keep;
    for (const LostPartition& lp : out) {
        bool inside = false;
        for (const LostPartition& o : out)
            if (&o != &lp && o.length && lp.offset > o.offset && lp.offset < o.offset + o.length) inside = true;
        if (!inside) keep.push_back(lp);
    }
    if (progress) progress(T(L"Done"), 1.f);
    return keep;
}

FixOutcome restorePartition(const LostPartition& lp, int diskNum, const Report& r, Journal& j) {
    FixOutcome o;
    const Disk* d = nullptr;
    for (const Disk& x : r.m.disks)
        if (x.number == diskNum) d = &x;
    wstr title = T(L"Restore a lost partition");
    wstr act = j.begin(title, T(L"Disk ") + std::to_wstring(diskNum) + L" · " + lp.fs + L" · " + sizeText(lp.length));
    wstr err;
    wstr refuse = workingDiskRefusal(r, diskNum);
    if (!refuse.empty()) {
        o.result = T(L"Nothing was changed. ") + refuse;
    } else if (!d || !lp.restorable) {
        o.result = !d ? T(L"The disk was not found.") : lp.why;
    } else if (!j.saveDiskTables(act, *d, err)) {
        o.result = T(L"Nothing was changed, because the backup failed. ") + err;
    } else {
        GUID basic, linux;
        parseGuid(L"{EBD0A0A2-B9E5-4433-87C0-68B6B72699C7}", basic);
        parseGuid(L"{0FC63DAF-8483-4772-8E79-3D69D8477DE4}", linux);
        bool isLinux = lp.fs.rfind(L"ext", 0) == 0;
        uint8_t mbr = isLinux ? 0x83 : lp.fs == L"FAT32" ? 0x0C : lp.fs == L"FAT" ? 0x0E : 0x07;
        o.ok = addPartitionEntry(diskNum, lp.offset, lp.length, r.m.fw.uefi || d->size > (2ull << 40), isLinux ? linux : basic, mbr,
                                 lp.label.empty() ? wstr(L"Recovered") : lp.label, err);
        o.result = o.ok ? T(L"The partition is back in the partition table. Its files were not touched. If it has no drive letter, give it one in Disks and partitions.")
                        : T(L"The partition could not be put back. ") + err;
    }
    j.end(act, o.ok, o.result, o.log, true);
    o.actionDir = act;
    o.undoable = true;
    return o;
}

// ================================================================== deleted files
int fileKindOf(const wstr& name) {
    static const wchar_t* hives[] = {L"SYSTEM", L"SOFTWARE", L"SAM", L"SECURITY", L"DEFAULT", L"COMPONENTS", L"DRIVERS", L"NTUSER.DAT", L"UsrClass.dat", L"BCD"};
    for (auto hv : hives)
        if (iequals(name, hv)) return FK_REGISTRY;
    size_t dot = name.find_last_of(L'.');
    if (dot == wstr::npos) return FK_OTHER;
    wstr e = name.substr(dot + 1);
    for (wchar_t& c : e) c = (wchar_t)towlower(c);
    static const wchar_t* pic[] = {L"jpg", L"jpeg", L"png", L"gif", L"bmp", L"tif", L"tiff", L"heic", L"webp", L"raw", L"cr2", L"cr3", L"nef", L"arw", L"dng", L"psd", L"svg"};
    static const wchar_t* doc[] = {L"doc", L"docx", L"xls", L"xlsx", L"ppt", L"pptx", L"pdf", L"txt", L"rtf", L"odt", L"ods", L"odp", L"csv", L"md", L"pages", L"numbers", L"key", L"one", L"pst", L"eml", L"msg"};
    static const wchar_t* med[] = {L"mp3", L"wav", L"flac", L"m4a", L"aac", L"ogg", L"wma", L"mp4", L"mov", L"avi", L"mkv", L"wmv", L"m4v", L"3gp", L"mts", L"webm"};
    static const wchar_t* arc[] = {L"zip", L"rar", L"7z", L"gz", L"tar", L"iso", L"cab", L"vhd", L"vhdx", L"bak"};
    if (e == L"hve" || e == L"dat" && (icontains(name, L"ntuser") || icontains(name, L"usrclass"))) return FK_REGISTRY;
    for (auto x : pic) if (e == x) return FK_PICTURE;
    for (auto x : doc) if (e == x) return FK_DOCUMENT;
    for (auto x : med) if (e == x) return FK_MEDIA;
    for (auto x : arc) if (e == x) return FK_ARCHIVE;
    return FK_OTHER;
}

bool isWindowsEssential(const FoundFile& f) {
    if (f.kind == FK_REGISTRY) return true;
    const wstr& n = f.name;
    wstr folder = f.folder;
    static const wchar_t* hives[] = {L"SYSTEM", L"SOFTWARE", L"SAM", L"SECURITY", L"DEFAULT", L"COMPONENTS", L"DRIVERS"};
    for (auto hv : hives) {
        wstr base = n;
        size_t dot = base.find(L'.');
        if (dot != wstr::npos) base = base.substr(0, dot);  // SYSTEM.LOG1, SOFTWARE.LOG2 ...
        if (iequals(base, hv) && (folder.size() >= 6 && iequals(folder.substr(folder.size() - 6), L"config") || icontains(folder, L"RegBack"))) return true;
    }
    if (iequals(n, L"NTUSER.DAT") || iequals(n, L"UsrClass.dat")) return true;
    // the keys that unlock saved passwords, certificates and credentials
    if (icontains(folder, L"Microsoft\\Protect") || icontains(folder, L"Microsoft\\Credentials") || icontains(folder, L"Microsoft\\Vault") ||
        icontains(folder, L"SystemCertificates") || icontains(folder, L"Crypto\\RSA"))
        return true;
    // mail
    size_t dot = n.find_last_of(L'.');
    wstr ext = dot == wstr::npos ? wstr() : toLower(n.substr(dot + 1));
    if (ext == L"pst" || ext == L"ost") return true;
    // browser profiles: logins, bookmarks, history
    static const wchar_t* chromium[] = {L"Login Data", L"Bookmarks", L"Cookies", L"History", L"Web Data", L"Local State"};
    if (icontains(folder, L"\\User Data"))
        for (auto c : chromium)
            if (iequals(n, c)) return true;
    static const wchar_t* firefox[] = {L"logins.json", L"key4.db", L"places.sqlite", L"cookies.sqlite", L"formhistory.sqlite"};
    if (icontains(folder, L"Firefox\\Profiles"))
        for (auto c : firefox)
            if (iequals(n, c)) return true;
    // the user folders
    if (istartsWith(folder, L"Users\\") && (icontains(folder, L"\\Desktop") || icontains(folder, L"\\Documents") || icontains(folder, L"\\Pictures"))) return true;
    return false;
}

wstr hiveNameFromHeader(const std::string& h) {
    if (h.size() < 0x70 || memcmp(h.data(), "regf", 4)) return L"";
    wstr path((const wchar_t*)(h.data() + 0x30), 32);
    path = path.c_str();  // up to the first zero
    size_t s = path.find_last_of(L'\\');
    wstr name = s == wstr::npos ? path : path.substr(s + 1);
    for (wchar_t& c : name)
        if (c < 32) c = L'_';
    return name;
}

bool ntfsApplyFixups(std::string& rec, uint32_t sector) {
    if (rec.size() < 48 || memcmp(rec.data(), "FILE", 4)) return false;
    uint16_t off = u16(rec, 4), cnt = u16(rec, 6);
    if (!cnt || off + cnt * 2u > rec.size() || sector < 256) return false;
    uint16_t usn = u16(rec, off);
    for (uint16_t i = 1; i < cnt; ++i) {
        size_t end = (size_t)i * sector - 2;
        if (end + 2 > rec.size()) break;
        if (u16(rec, end) != usn) return false;  // torn write: not trustworthy
        rec[end] = rec[off + i * 2];
        rec[end + 1] = rec[off + i * 2 + 1];
    }
    return true;
}

std::vector<std::pair<int64_t, uint64_t>> ntfsRuns(const uint8_t* p, size_t n) {
    std::vector<std::pair<int64_t, uint64_t>> out;
    int64_t lcn = 0;
    size_t i = 0;
    while (i < n && p[i]) {
        uint8_t h = p[i++];
        int lb = h & 0x0F, ob = h >> 4;
        if (!lb || lb > 8 || ob > 8 || i + lb + ob > n) break;
        uint64_t len = 0;
        for (int k = 0; k < lb; ++k) len |= (uint64_t)p[i + k] << (8 * k);
        i += lb;
        if (!ob) {
            out.push_back({-1, len});  // a hole (sparse)
            continue;
        }
        int64_t delta = 0;
        for (int k = 0; k < ob; ++k) delta |= (int64_t)p[i + k] << (8 * k);
        if (p[i + ob - 1] & 0x80) delta |= -((int64_t)1 << (8 * ob));  // negative
        i += ob;
        lcn += delta;
        if (lcn < 0) break;
        out.push_back({lcn, len});
    }
    return out;
}

bool ntfsParseRecord(const std::string& rec, FoundFile& f, uint64_t& parent, bool& inUse, bool& isDir, bool& isBase) {
    if (rec.size() < 56 || memcmp(rec.data(), "FILE", 4)) return false;
    uint16_t flags = u16(rec, 0x16);
    inUse = (flags & 1) != 0;
    isDir = (flags & 2) != 0;
    isBase = u64(rec, 0x20) == 0;
    size_t a = u16(rec, 0x14);
    int bestNs = -1;
    bool haveData = false;
    parent = 0;
    while (a + 16 <= rec.size()) {
        uint32_t type = u32(rec, a), len = u32(rec, a + 4);
        if (type == 0xFFFFFFFF || len < 16 || a + len > rec.size()) break;
        bool nonRes = rec[a + 8] != 0;
        uint8_t nameLen = (uint8_t)rec[a + 9];
        if (!nonRes) {
            uint32_t clen = u32(rec, a + 0x10);
            uint16_t coff = u16(rec, a + 0x14);
            if (a + coff + clen > rec.size()) break;
            if (type == 0x10 && clen >= 0x20) f.modified = {u32(rec, a + coff + 0x08), u32(rec, a + coff + 0x0C)};
            if (type == 0x30 && clen >= 0x42) {
                uint8_t n = (uint8_t)rec[a + coff + 0x40], ns = (uint8_t)rec[a + coff + 0x41];
                int rank = ns == 1 ? 3 : ns == 3 ? 3 : ns == 0 ? 2 : 1;  // Win32 names before DOS 8.3 ones
                if (rank > bestNs && 0x42u + n * 2 <= clen) {
                    bestNs = rank;
                    parent = u64(rec, a + coff) & 0xFFFFFFFFFFFFull;
                    f.name.assign((const wchar_t*)(rec.data() + a + coff + 0x42), n);
                    if (!haveData) f.size = u64(rec, a + coff + 0x30);
                    if (!f.modified.dwLowDateTime && !f.modified.dwHighDateTime) f.modified = {u32(rec, a + coff + 0x10), u32(rec, a + coff + 0x14)};
                }
            }
            if (type == 0x80 && nameLen == 0) {
                f.resident = true;
                f.data = rec.substr(a + coff, clen);
                f.size = clen;
                haveData = true;
            }
        } else if (type == 0x80 && nameLen == 0 && u64(rec, a + 0x10) == 0) {
            uint16_t roff = u16(rec, a + 0x20);
            if (a + roff < a + len) f.runs = ntfsRuns((const uint8_t*)rec.data() + a + roff, len - roff);
            f.size = u64(rec, a + 0x30);
            f.resident = false;
            haveData = true;
        }
        a += len;
    }
    return bestNs >= 0;
}

namespace {

struct Ntfs {
    uint32_t bps = 512, cluster = 4096, rec = 1024;
    uint64_t mftLcn = 0, clusters = 0;
};

bool readNtfsBoot(const ReadFn& rd, Ntfs& n) {
    std::string b;
    if (!rd(0, 512, b) || memcmp(&b[3], "NTFS    ", 8)) return false;
    n.bps = u16(b, 0x0B);
    uint8_t v = (uint8_t)b[0x0D];
    uint32_t spc = v > 0x80 ? 1u << (256 - v) : v;
    n.cluster = n.bps * spc;
    n.clusters = u64(b, 0x28) / std::max<uint32_t>(1, spc);
    n.mftLcn = u64(b, 0x30);
    int8_t c = (int8_t)b[0x40];
    n.rec = c < 0 ? 1u << (-c) : (uint32_t)c * n.cluster;
    return n.bps >= 512 && n.cluster && n.rec >= 512 && n.rec <= 65536;
}

// Reads a non-resident stream (by its runs) up to size bytes.
bool readRuns(const ReadFn& rd, uint32_t cluster, const std::vector<std::pair<int64_t, uint64_t>>& runs, uint64_t size, std::string& out) {
    out.clear();
    for (const auto& r : runs) {
        if (out.size() >= size) break;
        uint64_t bytes = std::min<uint64_t>(r.second * cluster, size - out.size());
        if (r.first < 0) {
            out.append((size_t)bytes, '\0');
            continue;
        }
        for (uint64_t done = 0; done < bytes;) {
            uint32_t n = (uint32_t)std::min<uint64_t>(bytes - done, 8 * kMB);
            std::string part;
            uint32_t rn = (n + 511) / 512 * 512;
            if (!rd((uint64_t)r.first * cluster + done, rn, part)) return false;
            out.append(part, 0, n);
            done += n;
        }
    }
    return true;
}

bool bit(const std::string& bm, uint64_t i) { return i / 8 < bm.size() && ((uint8_t)bm[(size_t)(i / 8)] >> (i % 8) & 1); }

// The length of a picture or PDF that starts at off, found from its own structure (0 = not valid).
uint64_t carveLength(const ReadFn& rd, uint64_t off, uint64_t limit, int type) {
    std::string buf;
    uint64_t bufAt = 0;
    auto at = [&](uint64_t i, int& c) {
        if (i >= limit) return false;
        if (i < bufAt || i >= bufAt + buf.size()) {
            bufAt = i / 4096 * 4096;
            uint32_t n = (uint32_t)std::min<uint64_t>(kMB, limit - bufAt);
            n = (n + 511) / 512 * 512;
            if (!rd(off + bufAt, n, buf)) return false;
        }
        c = (uint8_t)buf[(size_t)(i - bufAt)];
        return true;
    };
    int c = 0, c2 = 0;
    if (type == 0) {  // JPEG: walk the segments, then the picture data up to the end marker
        uint64_t i = 2;
        for (int guard = 0; guard < 100000; ++guard) {
            if (!at(i, c) || c != 0xFF || !at(i + 1, c2)) return 0;
            if (c2 == 0xD9) return i + 2;
            if (c2 == 0xD8 || c2 == 0x01 || (c2 >= 0xD0 && c2 <= 0xD7) || c2 == 0xFF) {
                i += c2 == 0xFF ? 1 : 2;
                continue;
            }
            int h = 0, l = 0;
            if (!at(i + 2, h) || !at(i + 3, l)) return 0;
            uint64_t seg = (uint64_t)(h << 8 | l);
            if (seg < 2) return 0;
            i += 2 + seg;
            if (c2 == 0xDA) {  // picture data: runs until a marker that is not a fill or restart
                for (;; ++i) {
                    if (!at(i, c)) return 0;
                    if (c != 0xFF) continue;
                    if (!at(i + 1, c2)) return 0;
                    if (c2 == 0x00 || (c2 >= 0xD0 && c2 <= 0xD7)) continue;
                    break;
                }
            }
        }
        return 0;
    }
    if (type == 1) {  // PNG: chunks up to IEND
        uint64_t i = 8;
        for (int guard = 0; guard < 200000; ++guard) {
            uint8_t h[8];
            for (int k = 0; k < 8; ++k) {
                if (!at(i + k, c)) return 0;
                h[k] = (uint8_t)c;
            }
            uint32_t len = be32(h);
            if (len > 0x7FFFFFFF) return 0;
            for (int k = 4; k < 8; ++k)
                if (!isalpha(h[k])) return 0;
            i += 12 + (uint64_t)len;
            if (!memcmp(h + 4, "IEND", 4)) return i;
        }
        return 0;
    }
    // PDF: up to the last "%%EOF" that follows soon after another
    uint64_t end = 0;
    const char* pat = "%%EOF";
    for (uint64_t i = 5; i + 5 < limit; ++i) {
        if (end && i > end + 256 * 1024) break;
        if (!at(i, c)) break;
        if (c != '%') continue;
        bool m = true;
        for (int k = 1; k < 5 && m; ++k) m = at(i + k, c2) && c2 == pat[k];
        if (m) end = i + 5;
    }
    if (end) {
        if (at(end, c) && (c == '\r' || c == '\n')) ++end;
        if (at(end, c) && c == '\n') ++end;
    }
    return end;
}

wstr carvedName(int type, size_t n) {
    wchar_t b[48];
    swprintf(b, 48, type == 0 ? L"photo %05zu.jpg" : type == 1 ? L"picture %05zu.png" : L"document %05zu.pdf", n);
    return b;
}

}  // namespace

FileScan scanDeletedFiles(const Partition& p, bool full, const ScanProgress& progress, volatile bool* cancel) {
    FileScan s;
    ReadFn rd = partitionReader(p);
    if (!rd) {
        s.note = T(L"The partition could not be opened.");
        return s;
    }
    if (p.locked || p.rawFs == L"BitLocker" && !p.readable) {
        s.note = T(L"This drive is locked by BitLocker. Unlock it first: its contents cannot be read while it is locked.");
        return s;
    }
    // A file deleted moments ago may still be only in Windows' memory: have Windows write what it
    // holds for this drive, so the file list read from the disk is the current one.
    if (p.root.size() >= 3) {
        wstr vol = p.root.substr(0, p.root.size() - 1);
        if (vol.size() == 2 && vol[1] == L':') vol = L"\\\\.\\" + vol;
        HANDLE hv = CreateFileW(vol.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hv != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(hv);
            CloseHandle(hv);
        }
    }
    Ntfs n;
    s.ntfs = readNtfsBoot(rd, n);
    std::string bitmap;
    std::vector<std::pair<uint64_t, uint64_t>> mftSpans;  // bytes the file list covers
    if (s.ntfs) {
        s.cluster = n.cluster;
        std::string r0;
        uint64_t mftAt = n.mftLcn * n.cluster;
        FoundFile mft, bm;
        uint64_t par = 0;
        bool inUse = false, dir = false, base = false;
        if (!rd(mftAt, n.rec, r0) || !ntfsApplyFixups(r0, n.bps) || !ntfsParseRecord(r0, mft, par, inUse, dir, base) || mft.runs.empty()) {
            s.note = T(L"The file list of this drive is damaged. The full scan can still find files by their contents.");
            s.ntfs = false;
        } else {
            std::string r6;
            if (rd(mftAt + 6ull * n.rec, n.rec, r6) && ntfsApplyFixups(r6, n.bps) && ntfsParseRecord(r6, bm, par, inUse, dir, base) && !bm.runs.empty() &&
                bm.size < (512ull << 20))
                readRuns(rd, n.cluster, bm.runs, bm.size, bitmap);
            uint64_t records = mft.size / n.rec, idx = 0;
            struct DirInfo {
                wstr name;
                uint64_t parent;
            };
            std::map<uint64_t, DirInfo> dirs;
            std::vector<std::pair<uint64_t, FoundFile>> cand;  // parent, file
            for (const auto& run : mft.runs) {
                if (run.first < 0) {
                    idx += run.second * n.cluster / n.rec;
                    continue;
                }
                uint64_t bytes = run.second * n.cluster, base0 = (uint64_t)run.first * n.cluster;
                mftSpans.push_back({base0, bytes});
                for (uint64_t done = 0; done < bytes && idx < records;) {
                    if (cancel && *cancel) break;
                    uint32_t chunk = (uint32_t)std::min<uint64_t>(bytes - done, 4 * kMB);
                    std::string b;
                    if (!rd(base0 + done, chunk, b)) {
                        done += chunk;
                        idx += chunk / n.rec;
                        continue;
                    }
                    for (uint32_t o = 0; o + n.rec <= chunk && idx < records; o += n.rec, ++idx) {
                        std::string rec = b.substr(o, n.rec);
                        if (!ntfsApplyFixups(rec, n.bps)) continue;
                        FoundFile f;
                        uint64_t parent = 0;
                        bool use = false, isDir = false, isBase = false;
                        if (!ntfsParseRecord(rec, f, parent, use, isDir, isBase) || !isBase) continue;
                        ++s.scanned;
                        if (isDir) dirs[idx] = DirInfo{f.name, parent};
                        else if (!use && idx >= 24) cand.push_back({parent, std::move(f)});
                    }
                    done += chunk;
                    if (progress) progress(T(L"Reading the file list"), full ? 0.15f * idx / std::max<uint64_t>(1, records) : (float)idx / std::max<uint64_t>(1, records));
                }
            }
            for (auto& c : cand) {
                FoundFile& f = c.second;
                wstr path;
                uint64_t at = c.first;
                bool known = true;
                for (int depth = 0; depth < 64 && at != 5; ++depth) {
                    auto it = dirs.find(at);
                    if (it == dirs.end()) {
                        known = false;
                        break;
                    }
                    path = it->second.name + (path.empty() ? L"" : L"\\" + path);
                    at = it->second.parent;
                }
                f.folder = known ? path : (path.empty() ? T(L"(folder unknown)") : T(L"(folder unknown)\\") + path);
                f.kind = fileKindOf(f.name);
                uint64_t used = 0, all = 0;
                for (const auto& run : f.runs)
                    if (run.first >= 0)
                        for (uint64_t k = 0; k < run.second && k < 1 << 20; ++k) {
                            ++all;
                            if (bit(bitmap, (uint64_t)run.first + k)) ++used;
                        }
                f.state = (!f.resident && f.size && f.runs.empty()) ? 2 : bitmap.empty() || !used ? 0 : used == all ? 2 : 1;
                s.files.push_back(std::move(f));
            }
        }
    } else if (!full) {
        s.note = T(L"A quick search only works on NTFS drives (most Windows drives). Use the full scan: it finds pictures and PDFs by their contents on any drive.");
        return s;
    }
    if (full && !(cancel && *cancel)) {
        // Read every cluster: file-list records of an older file list, and pictures / PDFs by their contents
        uint64_t len = p.length / 512 * 512, cl = s.ntfs ? s.cluster : 512;
        std::set<std::wstring> seen;
        for (const FoundFile& f : s.files) seen.insert(f.name + L"|" + std::to_wstring(f.size));
        size_t carved[3] = {0, 0, 0}, hives = 0;
        uint64_t skipTo = 0;
        for (uint64_t at = 0; at < len; at += 4 * kMB) {
            if (cancel && *cancel) break;
            if (progress) progress(T(L"Reading the whole drive"), 0.15f + 0.85f * at / std::max<uint64_t>(1, len));
            uint32_t chunk = (uint32_t)std::min<uint64_t>(4 * kMB, len - at);
            std::string b;
            if (!rd(at, chunk, b)) continue;
            s.scanned += chunk;
            for (uint32_t o = 0; o + 512 <= chunk; o += 512) {
                uint64_t abs = at + o;
                bool inMft = false;
                for (const auto& m : mftSpans)
                    if (abs >= m.first && abs < m.first + m.second) inMft = true;
                const char* q = b.data() + o;
                if (s.ntfs && !inMft && !memcmp(q, "FILE", 4) && o + n.rec <= chunk) {
                    std::string rec(q, n.rec);
                    FoundFile f;
                    uint64_t parent = 0;
                    bool use = false, isDir = false, isBase = false;
                    if (ntfsApplyFixups(rec, n.bps) && ntfsParseRecord(rec, f, parent, use, isDir, isBase) && isBase && !isDir && !f.name.empty() && f.name[0] != L'$' &&
                        seen.insert(f.name + L"|" + std::to_wstring(f.size)).second) {
                        f.folder = T(L"(found by the deep scan)");
                        f.kind = fileKindOf(f.name);
                        f.state = 1;
                        s.files.push_back(std::move(f));
                    }
                    continue;
                }
                if (abs % cl || abs < skipTo) continue;
                if (s.ntfs && bit(bitmap, abs / cl)) continue;  // in use by an existing file
                if (!memcmp(q, "regf", 4) && o + 4096 <= chunk) {
                    // a registry file: its header says how big its data is (4 KB header + the bins)
                    std::string head(q, 4096);
                    uint32_t bins = (uint8_t)q[0x28] | (uint8_t)q[0x29] << 8 | (uint8_t)q[0x2A] << 16 | (uint32_t)(uint8_t)q[0x2B] << 24;
                    std::string first;
                    if (bins && bins < (1u << 30) && abs + 4096 + bins <= len && rd(abs + 4096, 512, first) && !memcmp(first.data(), "hbin", 4)) {
                        FoundFile f;
                        f.carved = true;
                        f.carvedOffset = abs;
                        f.size = 4096ull + bins;
                        wstr nm = hiveNameFromHeader(head);
                        f.name = (nm.empty() ? wstr(L"registry") : nm) + L" " + std::to_wstring(++hives) + L".hive";
                        f.folder = T(L"(found by contents)\\Registry");
                        f.kind = FK_REGISTRY;
                        // the bins follow each other; a broken chain means the file was in pieces
                        uint64_t off = 4096;
                        int guard = 0;
                        f.state = 0;
                        while (off < f.size && guard++ < 200000) {
                            std::string b;
                            if (!rd(abs + off, 512, b) || memcmp(b.data(), "hbin", 4)) {
                                f.state = 1;
                                break;
                            }
                            uint32_t bs = (uint8_t)b[8] | (uint8_t)b[9] << 8 | (uint8_t)b[10] << 16 | (uint32_t)(uint8_t)b[11] << 24;
                            if (bs < 4096 || bs % 4096) {
                                f.state = 1;
                                break;
                            }
                            off += bs;
                        }
                        s.files.push_back(std::move(f));
                        skipTo = abs + 4096ull + bins;
                        continue;
                    }
                }
                int type = -1;
                const uint8_t* u = (const uint8_t*)q;
                if (u[0] == 0xFF && u[1] == 0xD8 && u[2] == 0xFF && (u[3] == 0xE0 || u[3] == 0xE1 || u[3] == 0xDB || u[3] == 0xEE)) type = 0;
                else if (!memcmp(q, "\x89PNG\r\n\x1a\n", 8)) type = 1;
                else if (!memcmp(q, "%PDF-", 5)) type = 2;
                if (type < 0) continue;
                uint64_t limit = std::min<uint64_t>(type == 2 ? 256 * kMB : 64 * kMB, len - abs);
                uint64_t size = carveLength(rd, abs, limit, type);
                if (size < 512) continue;
                FoundFile f;
                f.carved = true;
                f.carvedOffset = abs;
                f.size = size;
                f.name = carvedName(type, ++carved[type]);
                f.folder = type == 2 ? T(L"(found by contents)\\Documents") : T(L"(found by contents)\\Pictures");
                f.kind = type == 2 ? FK_DOCUMENT : FK_PICTURE;
                f.state = 0;
                s.files.push_back(std::move(f));
                skipTo = abs + size;
            }
        }
    }
    std::sort(s.files.begin(), s.files.end(), [](const FoundFile& a, const FoundFile& b) {
        int c = _wcsicmp(a.folder.c_str(), b.folder.c_str());
        return c ? c < 0 : _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    if (s.note.empty())
        s.note = s.files.empty() ? T(L"No deleted files were found.")
                                 : TF(L"{0} files found. Files marked \"may be damaged\" had part of their space used again since they were deleted.", std::to_wstring(s.files.size()));
    if (progress) progress(T(L"Done"), 1.f);
    return s;
}

namespace {

wstr safeName(const wstr& n) {
    wstr o;
    for (wchar_t c : n) o += (c < 32 || wcschr(L"<>:\"/\\|?*", c)) ? L'_' : c;
    while (!o.empty() && (o.back() == L'.' || o.back() == L' ')) o.pop_back();
    return o.empty() ? wstr(L"_") : o;
}

bool makeDirs(const wstr& dir) {
    if (GetFileAttributesW(dir.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    size_t s = dir.find_last_of(L'\\');
    if (s != wstr::npos && s > 2) makeDirs(dir.substr(0, s));
    return CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

}  // namespace

int recoverFiles(const Partition& p, const FileScan& s, const std::vector<int>& which, const wstr& toDirIn, const ScanProgress& progress, volatile bool* cancel,
                 int& failed, wstr& log) {
    failed = 0;
    int ok = 0;
    ReadFn rd = partitionReader(p);
    wstr toDir = toDirIn;
    while (!toDir.empty() && toDir.back() == L'\\') toDir.pop_back();
    if (!rd) {
        failed = (int)which.size();
        log = T(L"The partition could not be opened.");
        return 0;
    }
    int k = 0;
    for (int i : which) {
        if (cancel && *cancel) break;
        if (i < 0 || i >= (int)s.files.size()) continue;
        const FoundFile& f = s.files[i];
        if (progress) progress(f.name, (float)k++ / std::max<size_t>(1, which.size()));
        wstr dir = toDir;
        for (size_t a = 0; a < f.folder.size();) {
            size_t b = f.folder.find(L'\\', a);
            if (b == wstr::npos) b = f.folder.size();
            if (b > a) dir += L"\\" + safeName(f.folder.substr(a, b - a));
            a = b + 1;
        }
        makeDirs(dir);
        wstr name = safeName(f.name), path = dir + L"\\" + name;
        for (int n = 2; GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES && n < 1000; ++n) {
            size_t dot = name.find_last_of(L'.');
            path = dir + L"\\" + (dot == wstr::npos ? name + L" (" + std::to_wstring(n) + L")" : name.substr(0, dot) + L" (" + std::to_wstring(n) + L")" + name.substr(dot));
        }
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            ++failed;
            log += T(L"Could not create ") + path + L"\r\n";
            continue;
        }
        bool good = true;
        uint64_t written = 0;
        auto put = [&](const std::string& d) {
            DWORD wr = 0;
            if (!WriteFile(h, d.data(), (DWORD)d.size(), &wr, nullptr) || wr != d.size()) good = false;
            written += wr;
        };
        if (f.resident) put(f.data);
        else if (f.carved) {
            for (uint64_t done = 0; done < f.size && good;) {
                uint32_t n = (uint32_t)std::min<uint64_t>(f.size - done, 4 * kMB);
                std::string b;
                if (!rd(f.carvedOffset + done, (n + 511) / 512 * 512, b)) {
                    good = false;
                    break;
                }
                b.resize(n);
                put(b);
                done += n;
            }
        } else {
            for (const auto& run : f.runs) {
                if (written >= f.size || !good) break;
                uint64_t bytes = std::min<uint64_t>(run.second * s.cluster, f.size - written);
                for (uint64_t done = 0; done < bytes && good;) {
                    uint32_t n = (uint32_t)std::min<uint64_t>(bytes - done, 4 * kMB);
                    std::string b;
                    if (run.first < 0) b.assign(n, '\0');
                    else if (!rd((uint64_t)run.first * s.cluster + done, (n + 511) / 512 * 512, b)) {
                        good = false;
                        break;
                    } else b.resize(n);
                    put(b);
                    done += n;
                }
            }
            if (written < f.size) good = false;
        }
        if (f.modified.dwLowDateTime || f.modified.dwHighDateTime) SetFileTime(h, nullptr, nullptr, &f.modified);
        CloseHandle(h);
        if (good) ++ok;
        else {
            ++failed;
            log += T(L"Only partly recovered: ") + path + L"\r\n";
        }
    }
    if (progress) progress(T(L"Done"), 1.f);
    return ok;
}

// ---------------------------------------------------------------- which files sit on unreadable sectors
bool isStartupCritical(const wstr& pathIn) {
    wstr p = L"\\" + pathIn;
    for (wchar_t& c : p) c = (wchar_t)towlower(c);
    if (p.size() > 2 && p[1] == L'$') return true;  // the file system's own files: $MFT, $LogFile, $Bitmap ...
    static const wchar_t* const folders[] = {L"\\windows\\system32\\config\\", L"\\windows\\system32\\drivers\\", L"\\boot\\", L"\\efi\\"};
    for (const wchar_t* k : folders)
        if (p.find(k) != wstr::npos) return true;
    static const wchar_t* const files[] = {L"\\windows\\system32\\ntoskrnl.exe", L"\\windows\\system32\\hal.dll",     L"\\windows\\system32\\winload.exe",
                                           L"\\windows\\system32\\winload.efi",  L"\\windows\\system32\\ci.dll",      L"\\windows\\system32\\kdcom.dll",
                                           L"\\windows\\system32\\bootvid.dll",  L"\\windows\\system32\\smss.exe",    L"\\windows\\system32\\csrss.exe",
                                           L"\\windows\\system32\\wininit.exe",  L"\\windows\\system32\\winlogon.exe", L"\\windows\\system32\\services.exe",
                                           L"\\windows\\system32\\lsass.exe",    L"\\bootmgr"};
    for (const wchar_t* k : files) {
        size_t n = wcslen(k);
        if (p.size() >= n && p.compare(p.size() - n, n, k) == 0) return true;
    }
    return false;
}

std::vector<FileOnBad> filesOnBadRanges(const Partition& p, const std::vector<std::pair<uint64_t, uint64_t>>& diskRanges, wstr& note, const ScanProgress& progress,
                                        volatile bool* cancel) {
    std::vector<FileOnBad> out;
    note.clear();
    std::vector<std::pair<uint64_t, uint64_t>> rel;  // unreadable bytes inside this partition: [from, to)
    for (const auto& r : diskRanges) {
        uint64_t a = std::max(r.first, p.offset), b = std::min(r.first + r.second, p.offset + p.length);
        if (a < b) rel.push_back({a - p.offset, b - p.offset});
    }
    if (rel.empty()) {
        note = T(L"No unreadable area falls inside this partition.");
        return out;
    }
    auto hit = [&](uint64_t a, uint64_t len) {
        uint64_t t = 0;
        for (const auto& r : rel) {
            uint64_t s = std::max(a, r.first), e = std::min(a + len, r.second);
            if (s < e) t += e - s;
        }
        return t;
    };
    ReadFn rd = partitionReader(p);
    Ntfs n;
    if (!rd || !readNtfsBoot(rd, n)) {
        note = T(L"Only NTFS partitions can be traced to files; this one is not NTFS, or its first sector could not be read.");
        return out;
    }
    std::string r0;
    FoundFile mft;
    uint64_t par = 0;
    bool use = false, isDir = false, isBase = false;
    if (!rd(n.mftLcn * n.cluster, n.rec, r0) || !ntfsApplyFixups(r0, n.bps) || !ntfsParseRecord(r0, mft, par, use, isDir, isBase) || mft.runs.empty()) {
        note = T(L"The file list ($MFT) could not be read: it may itself sit on an unreadable sector, so no file can be named.");
        out.push_back({L"$MFT", true, 0});
        return out;
    }
    uint64_t mftHit = 0;
    for (const auto& run : mft.runs)
        if (run.first >= 0) mftHit += hit((uint64_t)run.first * n.cluster, run.second * n.cluster);
    struct Rec {
        uint64_t parent;
        wstr name;
    };
    std::map<uint64_t, Rec> names;
    std::vector<std::pair<uint64_t, uint64_t>> hits;  // record, bytes on unreadable sectors
    uint64_t records = mft.size / n.rec, idx = 0, lost = 0;
    auto take = [&](const std::string& recIn, uint64_t at) {
        std::string rec = recIn;
        FoundFile f;
        uint64_t parent = 0;
        bool inUse = false, dir = false, baseRec = false;
        if (!ntfsApplyFixups(rec, n.bps) || !ntfsParseRecord(rec, f, parent, inUse, dir, baseRec) || !baseRec || !inUse) return;
        names[idx] = Rec{parent, f.name};
        uint64_t b = hit(at, n.rec);  // the file's own entry in the list
        if (!f.resident)
            for (const auto& run : f.runs)
                if (run.first >= 0) b += hit((uint64_t)run.first * n.cluster, run.second * n.cluster);
        if (b) hits.push_back({idx, b});
    };
    for (const auto& run : mft.runs) {
        if (run.first < 0) {
            idx += run.second * n.cluster / n.rec;
            continue;
        }
        uint64_t bytes = run.second * n.cluster, base0 = (uint64_t)run.first * n.cluster;
        for (uint64_t done = 0; done < bytes && idx < records;) {
            if (cancel && *cancel) break;
            uint32_t chunk = (uint32_t)std::min<uint64_t>(bytes - done, 4 * kMB);
            std::string b;
            bool whole = rd(base0 + done, chunk, b);
            for (uint32_t o = 0; o + n.rec <= chunk && idx < records; o += n.rec, ++idx) {
                if (whole) take(b.substr(o, n.rec), base0 + done + o);
                else {  // a damaged part of the list: one entry at a time, so only the unreadable ones are lost
                    std::string one;
                    if (rd(base0 + done + o, n.rec, one)) take(one, base0 + done + o);
                    else ++lost;
                }
            }
            done += chunk;
            if (progress) progress(T(L"Reading the file list"), (float)idx / std::max<uint64_t>(1, records));
        }
    }
    for (const auto& h : hits) {
        wstr path;
        uint64_t at = h.first;
        for (int depth = 0; depth < 64 && at != 5; ++depth) {
            auto it = names.find(at);
            if (it == names.end()) {
                path = L"?\\" + path;
                break;
            }
            path = it->second.name + (path.empty() ? L"" : L"\\" + path);
            at = it->second.parent;
        }
        out.push_back({path, isStartupCritical(path), h.second});
    }
    if (mftHit) out.push_back({L"$MFT", true, mftHit});
    std::sort(out.begin(), out.end(), [](const FileOnBad& a, const FileOnBad& b) { return a.critical != b.critical ? a.critical : a.path < b.path; });
    if (lost) note = TF(L"{0} entries of the file list could not be read; the files they describe cannot be named and are likely damaged too.", std::to_wstring(lost));
    else if (out.empty())
        note = T(L"The unreadable areas hold no file: they are in free space, so nothing is lost there.");
    return out;
}

}  // namespace bd

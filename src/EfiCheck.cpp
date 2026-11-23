#include "EfiCheck.h"
#include "Hive.h"
#include <vector>
#include <utility>
#include <cstring>

namespace bd {

namespace {

uint16_t rd16(const std::string& s, size_t o) { return o + 2 <= s.size() ? (uint16_t)((uint8_t)s[o] | (uint8_t)s[o + 1] << 8) : 0; }
uint32_t rd32(const std::string& s, size_t o) {
    return o + 4 <= s.size() ? (uint32_t)((uint8_t)s[o] | (uint8_t)s[o + 1] << 8 | (uint8_t)s[o + 2] << 16 | (uint32_t)(uint8_t)s[o + 3] << 24) : 0;
}

// FAT32 end-of-chain / bad / free markers (the top 4 bits are reserved).
bool isEoc(uint32_t c) { return (c & 0x0FFFFFFF) >= 0x0FFFFFF8; }
bool isBad(uint32_t c) { return (c & 0x0FFFFFFF) == 0x0FFFFFF7; }

// One directory entry worth keeping.
struct Entry {
    wstr name;           // long name when there was one, else the 8.3 short name, upper case
    uint32_t cluster = 0;
    uint64_t size = 0;
    bool isDir = false;
};

// A FAT32 volume read straight from the partition's sectors.
struct Vol {
    const ReadFn& rd;
    uint64_t partLen;
    uint32_t bps = 0, spc = 0, rsc = 0, nFat = 0, fatSz = 0, rootClus = 0;
    uint32_t clusBytes = 0, countClus = 0;
    uint64_t dataStart = 0;      // bytes from the partition start to cluster 2
    std::string fat;             // the first FAT, whole
    std::vector<uint8_t> used;   // clusters already seen, to catch sharing/loops
    EspFault fault = EspFault::None;
    wstr detail;

    explicit Vol(const ReadFn& r, uint64_t len) : rd(r), partLen(len) {}

    void fail(EspFault f, const wstr& d) {
        if (fault == EspFault::None) {
            fault = f;
            detail = d;
        }
    }

    uint32_t next(uint32_t c) const { return rd32(fat, (size_t)c * 4) & 0x0FFFFFFF; }

    bool readClusterBytes(uint32_t c, std::string& out) {
        if (c < 2 || c >= countClus + 2) return false;
        return rd(dataStart + (uint64_t)(c - 2) * clusBytes, clusBytes, out);
    }

    // Walks a cluster chain. Marks every cluster used. Returns the clusters in order
    // (capped) and sets a fault on a loop, a shared cluster, a bad cluster or an
    // out-of-range link. maxClus limits how far a (possibly huge) chain is followed.
    bool walkChain(uint32_t first, std::vector<uint32_t>& chain, const wstr& who) {
        chain.clear();
        if (first == 0) return true;  // empty file: no chain
        uint32_t c = first;
        uint32_t guard = countClus + 2;
        while (!isEoc(c)) {
            if (c < 2 || c >= countClus + 2) {
                fail(EspFault::ClusterChain, who + L": cluster " + std::to_wstring(c) + L" out of range");
                return false;
            }
            if (isBad(c)) {
                fail(EspFault::ClusterChain, who + L": chain runs into a bad cluster");
                return false;
            }
            if (used[c]) {
                fail(EspFault::ClusterChain, who + L": cluster " + std::to_wstring(c) + L" is shared or loops");
                return false;
            }
            used[c] = 1;
            chain.push_back(c);
            if (chain.size() > guard) {
                fail(EspFault::ClusterChain, who + L": chain does not end");
                return false;
            }
            c = next(c);
        }
        return true;
    }

    // Reads a file's whole contents by following its chain (capped at limit bytes).
    bool readFile(uint32_t first, uint64_t size, std::string& out, const wstr& who) {
        out.clear();
        std::vector<uint32_t> chain;
        if (!walkChain(first, chain, who)) return false;
        for (uint32_t c : chain) {
            std::string b;
            if (!readClusterBytes(c, b)) {
                fail(EspFault::ClusterChain, who + L": a cluster could not be read");
                return false;
            }
            out += b;
            if (out.size() >= size && size) break;
        }
        if (size && out.size() > size) out.resize((size_t)size);
        return true;
    }
};

// Pulls the 13 UTF-16 characters out of one long-file-name entry.
void lfnChars(const std::string& rec, size_t base, wstr& out) {
    auto take = [&](size_t o, int n) {
        for (int i = 0; i < n; ++i) {
            uint16_t ch = rd16(rec, base + o + i * 2);
            if (ch == 0 || ch == 0xFFFF) return;
            out += (wchar_t)ch;
        }
    };
    take(0x01, 5);
    take(0x0E, 6);
    take(0x1C, 2);
}

wstr shortName(const std::string& rec, size_t base) {
    wstr name, ext;
    for (int i = 0; i < 8; ++i) {
        char ch = rec[base + i];
        if (ch == ' ') break;
        name += (wchar_t)(uint8_t)ch;
    }
    for (int i = 8; i < 11; ++i) {
        char ch = rec[base + i];
        if (ch == ' ') break;
        ext += (wchar_t)(uint8_t)ch;
    }
    if (!ext.empty()) name += L"." + ext;
    for (wchar_t& ch : name)
        if (ch >= L'a' && ch <= L'z') ch = ch - L'a' + L'A';
    return name;
}

wstr upper(wstr s) {
    for (wchar_t& ch : s)
        if (ch >= L'a' && ch <= L'z') ch = ch - L'a' + L'A';
    return s;
}

// Reads a directory (following its own chain) into a flat list of entries, long
// names assembled. "." and ".." are left out.
bool readDir(Vol& v, uint32_t firstClus, std::vector<Entry>& out, const wstr& who) {
    out.clear();
    std::vector<uint32_t> chain;
    if (!v.walkChain(firstClus, chain, who)) return false;
    std::string bytes;
    for (uint32_t c : chain) {
        std::string b;
        if (!v.readClusterBytes(c, b)) {
            v.fail(EspFault::ClusterChain, who + L": a directory cluster could not be read");
            return false;
        }
        bytes += b;
        if (bytes.size() > (8u << 20)) break;  // a sane cap for one directory
    }
    // Long-name parts are stored just before the short entry, highest sequence first.
    std::vector<std::pair<int, wstr>> lfn;
    for (size_t p = 0; p + 32 <= bytes.size(); p += 32) {
        uint8_t first = (uint8_t)bytes[p];
        if (first == 0x00) break;      // end of directory
        uint8_t attr = (uint8_t)bytes[p + 0x0B];
        if (first == 0xE5) {           // deleted
            lfn.clear();
            continue;
        }
        if ((attr & 0x0F) == 0x0F) {   // long-name part
            wstr piece;
            lfnChars(bytes, p, piece);
            lfn.emplace_back((int)(first & 0x1F), piece);
            continue;
        }
        if (attr & 0x08) {             // volume label
            lfn.clear();
            continue;
        }
        wstr name;
        if (!lfn.empty()) {
            int maxSeq = 0;
            for (const auto& kv : lfn) maxSeq = std::max(maxSeq, kv.first);
            for (int s = 1; s <= maxSeq; ++s)
                for (const auto& kv : lfn)
                    if (kv.first == s) name += kv.second;
            name = upper(name);
        }
        lfn.clear();
        if (name.empty()) name = shortName(bytes, p);
        if (name == L"." || name == L"..") continue;
        Entry e;
        e.name = name;
        e.cluster = (uint32_t)rd16(bytes, p + 0x1A) | (uint32_t)rd16(bytes, p + 0x14) << 16;
        e.size = rd32(bytes, p + 0x1C);
        e.isDir = (attr & 0x10) != 0;
        out.push_back(e);
    }
    return true;
}

const Entry* findChild(const std::vector<Entry>& dir, const wchar_t* name) {
    wstr want = upper(name);
    for (const Entry& e : dir)
        if (e.name == want) return &e;
    return nullptr;
}

// Walks the whole directory tree, validating every cluster chain and that each
// file's chain length matches its size.
void validateTree(Vol& v, uint32_t dirClus, int depth, const wstr& path, int& budget) {
    if (depth > 24 || budget <= 0 || v.fault != EspFault::None) return;
    std::vector<Entry> entries;
    if (!readDir(v, dirClus, entries, path.empty() ? wstr(L"\\") : path)) return;
    for (const Entry& e : entries) {
        if (--budget <= 0) return;
        wstr full = path + L"\\" + e.name;
        if (e.isDir) {
            // The subdirectory's own readDir walks (and marks) its chain, so it is not walked here.
            validateTree(v, e.cluster, depth + 1, full, budget);
            if (v.fault != EspFault::None) return;
        } else {
            std::vector<uint32_t> chain;
            if (!v.walkChain(e.cluster, chain, full)) return;
            uint64_t have = (uint64_t)chain.size() * v.clusBytes;
            uint64_t need = e.size;
            // The chain must hold the file and be no more than one cluster longer.
            if (need > have || (need + v.clusBytes <= have && have > 0)) {
                v.fail(EspFault::ClusterChain, full + L": " + std::to_wstring(need) + L" bytes in " +
                                                   std::to_wstring(chain.size()) + L" clusters");
                return;
            }
        }
    }
}

bool isX64Pe(const std::string& d) {
    if (d.size() < 0x40 || d[0] != 'M' || d[1] != 'Z') return false;
    uint32_t pe = rd32(d, 0x3C);
    if (pe + 6 > d.size() || d[pe] != 'P' || d[pe + 1] != 'E' || d[pe + 2] != 0 || d[pe + 3] != 0) return false;
    return rd16(d, pe + 4) == 0x8664;
}

}  // namespace

EspCheck checkEsp(const ReadFn& rd, uint64_t partLength) {
    EspCheck out;
    if (!rd) return out;
    Vol v(rd, partLength);

    // 1. The boot sector.
    std::string boot;
    if (!rd(0, 512, boot) || boot.size() < 512) {
        out.ran = false;
        out.fault = EspFault::NotChecked;
        return out;
    }
    out.ran = true;
    bool sig = rd16(boot, 0x1FE) == 0xAA55;
    bool fat32Label = memcmp(&boot[0x52], "FAT32   ", 8) == 0;
    v.bps = rd16(boot, 0x0B);
    v.spc = (uint8_t)boot[0x0D];
    v.rsc = rd16(boot, 0x0E);
    v.nFat = (uint8_t)boot[0x10];
    v.fatSz = rd32(boot, 0x24);
    v.rootClus = rd32(boot, 0x2C);
    uint32_t totalSec = rd32(boot, 0x20);
    bool bpsOk = v.bps == 512 || v.bps == 1024 || v.bps == 2048 || v.bps == 4096;
    bool spcOk = v.spc && (v.spc & (v.spc - 1)) == 0 && v.spc <= 128;
    if (!fat32Label) {
        out.fault = EspFault::NotFat32;
        return out;
    }
    out.fat32 = true;
    if (!sig || !bpsOk || !spcOk || !v.rsc || !v.nFat || !v.fatSz || v.rootClus < 2 || !totalSec) {
        out.fault = EspFault::BootSector;
        out.detail = L"invalid FAT32 BPB";
        return out;
    }
    v.clusBytes = v.bps * v.spc;
    uint64_t fatBytes = (uint64_t)v.fatSz * v.bps;
    uint64_t dataSector = (uint64_t)v.rsc + (uint64_t)v.nFat * v.fatSz;
    if (dataSector >= totalSec || fatBytes > (64ull << 20) || (uint64_t)totalSec * v.bps > partLength + v.clusBytes) {
        out.fault = EspFault::BootSector;
        out.detail = L"FAT32 geometry does not fit the partition";
        return out;
    }
    v.countClus = (uint32_t)((totalSec - dataSector) / v.spc);
    v.dataStart = dataSector * v.bps;

    // 2. The backup boot sector (sector 6, or wherever the BPB says).
    uint16_t backupSec = rd16(boot, 0x32);
    if (backupSec == 0 || backupSec >= v.rsc) backupSec = 6;
    std::string backup;
    if (!rd((uint64_t)backupSec * v.bps, 512, backup) || backup.size() < 512) {
        out.fault = EspFault::BackupBootSector;
        out.detail = L"the backup boot sector could not be read";
        return out;
    }
    // The BPB numbers and the signature must match the main boot sector.
    if (rd16(backup, 0x1FE) != 0xAA55 || memcmp(&backup[0x0B], &boot[0x0B], 0x3F - 0x0B) != 0 ||
        memcmp(&backup[0x52], "FAT32   ", 8) != 0) {
        out.fault = EspFault::BackupBootSector;
        out.detail = L"the backup boot sector does not match";
        return out;
    }

    // 3. The two FAT copies must agree, and the first FAT is kept for the chain checks.
    if (!rd((uint64_t)v.rsc * v.bps, (uint32_t)fatBytes, v.fat) || v.fat.size() < fatBytes) {
        out.fault = EspFault::FatCopies;
        out.detail = L"the first FAT could not be read";
        return out;
    }
    if (v.nFat >= 2) {
        std::string fat2;
        if (!rd((uint64_t)(v.rsc + v.fatSz) * v.bps, (uint32_t)fatBytes, fat2) || fat2.size() < fatBytes) {
            out.fault = EspFault::FatCopies;
            out.detail = L"the second FAT could not be read";
            return out;
        }
        if (memcmp(v.fat.data(), fat2.data(), (size_t)fatBytes) != 0) {
            out.fault = EspFault::FatCopies;
            out.detail = L"the two FAT copies differ";
            return out;
        }
    }

    // 4. Every file's cluster chain.
    v.used.assign((size_t)v.countClus + 2, 0);
    int budget = 20000;  // at most this many entries, against a corrupt tree
    validateTree(v, v.rootClus, 0, wstr(), budget);
    if (v.fault != EspFault::None) {
        out.fault = v.fault;
        out.detail = v.detail;
        return out;
    }

    // 5. The two boot loaders must read in full and be 64-bit programs.
    // The chains were already checked above; reading the files walks some of them again, so clear
    // the "seen" map first, or a re-read would look like a shared cluster.
    std::fill(v.used.begin(), v.used.end(), (uint8_t)0);
    std::vector<Entry> root;
    readDir(v, v.rootClus, root, L"\\");
    const Entry* efi = findChild(root, L"EFI");
    std::vector<Entry> efiDir, msDir, bootDir, msBootDir;
    if (efi) readDir(v, efi->cluster, efiDir, L"\\EFI");
    const Entry* ms = efi ? findChild(efiDir, L"Microsoft") : nullptr;
    if (ms) readDir(v, ms->cluster, msDir, L"\\EFI\\Microsoft");
    const Entry* msBoot = ms ? findChild(msDir, L"Boot") : nullptr;
    if (msBoot) readDir(v, msBoot->cluster, msBootDir, L"\\EFI\\Microsoft\\Boot");
    const Entry* bmgr = msBoot ? findChild(msBootDir, L"bootmgfw.efi") : nullptr;
    std::string data;
    if (!bmgr || !v.readFile(bmgr->cluster, bmgr->size, data, L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi") || !isX64Pe(data)) {
        out.fault = v.fault != EspFault::None ? v.fault : EspFault::Bootmgr;
        out.detail = v.fault != EspFault::None ? v.detail : (bmgr ? wstr(L"bootmgfw.efi is not a 64-bit program") : wstr(L"bootmgfw.efi is missing"));
        return out;
    }
    const Entry* efiBoot = efi ? findChild(efiDir, L"Boot") : nullptr;
    std::vector<Entry> fbDir;
    if (efiBoot) readDir(v, efiBoot->cluster, fbDir, L"\\EFI\\Boot");
    const Entry* bx64 = efiBoot ? findChild(fbDir, L"bootx64.efi") : nullptr;
    if (!bx64 || !v.readFile(bx64->cluster, bx64->size, data, L"\\EFI\\Boot\\bootx64.efi") || !isX64Pe(data)) {
        out.fault = v.fault != EspFault::None ? v.fault : EspFault::FallbackLoader;
        out.detail = v.fault != EspFault::None ? v.detail : (bx64 ? wstr(L"bootx64.efi is not a 64-bit program") : wstr(L"bootx64.efi is missing"));
        return out;
    }

    // 6. The BCD store must load as a registry hive.
    const Entry* bcd = findChild(msBootDir, L"BCD");
    std::string bcdData;
    if (!bcd || !v.readFile(bcd->cluster, bcd->size, bcdData, L"\\EFI\\Microsoft\\Boot\\BCD")) {
        out.fault = v.fault != EspFault::None ? v.fault : EspFault::Bcd;
        out.detail = v.fault != EspFault::None ? v.detail : wstr(L"the BCD store is missing");
        return out;
    }
    wstr why;
    if (hiveBytesHealth(bcdData.substr(0, 4096), why) == Health::Bad) {
        out.fault = EspFault::Bcd;
        out.detail = why.empty() ? wstr(L"the BCD store does not load") : why;
        return out;
    }

    out.fault = EspFault::None;
    return out;
}

bool espWritable(const Partition& p) {
    wstr path = !p.volume.empty() ? p.volume : (!p.letter.empty() ? L"\\\\.\\" + p.letter : wstr());
    if (path.empty()) return true;  // nothing to open: do not report a problem
    while (!path.empty() && path.back() == L'\\') path.pop_back();  // \\?\Volume{..}\ -> \\?\Volume{..}
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

const wchar_t* espFaultWord(EspFault f) {
    switch (f) {
        case EspFault::None: return L"ok";
        case EspFault::NotChecked: return L"not checked";
        case EspFault::NotFat32: return L"not FAT32";
        case EspFault::BootSector: return L"boot sector";
        case EspFault::BackupBootSector: return L"backup boot sector";
        case EspFault::FatCopies: return L"FAT copies";
        case EspFault::ClusterChain: return L"cluster chain";
        case EspFault::Bootmgr: return L"bootmgfw.efi";
        case EspFault::FallbackLoader: return L"bootx64.efi";
        case EspFault::Bcd: return L"BCD";
        case EspFault::ReadOnly: return L"read-only";
    }
    return L"";
}

}  // namespace bd

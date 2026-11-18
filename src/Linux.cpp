#include "Linux.h"
#include "Lang.h"

namespace bd {

static uint16_t u16(const std::string& s, size_t at) { return at + 2 <= s.size() ? (uint16_t)((uint8_t)s[at] | ((uint8_t)s[at + 1] << 8)) : 0; }
static uint32_t u32(const std::string& s, size_t at) {
    if (at + 4 > s.size()) return 0;
    uint32_t v;
    memcpy(&v, &s[at], 4);
    return v;
}
static bool fileExists(const wstr& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

// ---------------------------------------------------------------- ext2/3/4 reader
bool ExtReader::open(const ReadFn& rd, wstr& err) {
    rd_ = rd;
    std::string sb;
    if (!rd_ || !rd_(1024, 1024, sb)) {
        err = T(L"The partition could not be read.");
        return false;
    }
    if (u16(sb, 0x38) != 0xEF53) {
        err = T(L"This is not an ext2, ext3 or ext4 file system.");
        return false;
    }
    uint32_t log = u32(sb, 0x18);
    if (log > 6) {
        err = T(L"Unusual block size.");
        return false;
    }
    bs_ = 1024u << log;
    ipg_ = u32(sb, 0x28);
    inodeSize_ = u32(sb, 0x4C) >= 1 ? u16(sb, 0x58) : 128;
    if (inodeSize_ < 128) inodeSize_ = 128;
    uint32_t incompat = u32(sb, 0x60);
    is64_ = (incompat & 0x80) != 0;
    descSize_ = is64_ && u16(sb, 0xFE) >= 32 ? u16(sb, 0xFE) : 32;
    gdt_ = u32(sb, 0x14) + 1;
    blocksCount_ = u32(sb, 0x04) | (is64_ ? (uint64_t)u32(sb, 0x150) << 32 : 0);
    if (!ipg_) {
        err = T(L"The file system header is damaged.");
        return false;
    }
    return true;
}

bool ExtReader::inode(uint32_t ino, Inode& out) {
    if (!ino) return false;
    uint32_t g = (ino - 1) / ipg_, idx = (ino - 1) % ipg_;
    std::string d;
    if (!rd_(gdt_ * bs_ + (uint64_t)g * descSize_, descSize_, d)) return false;
    uint64_t table = u32(d, 8) | (is64_ && descSize_ >= 64 ? (uint64_t)u32(d, 0x28) << 32 : 0);
    if (!table || table >= blocksCount_) return false;
    std::string raw;
    if (!rd_(table * bs_ + (uint64_t)idx * inodeSize_, inodeSize_, raw)) return false;
    out.mode = u16(raw, 0);
    out.size = u32(raw, 4) | ((uint64_t)u32(raw, 0x6C) << 32);
    out.flags = u32(raw, 0x20);
    out.block = raw.substr(0x28, 60);
    return true;
}

bool ExtReader::extentNode(const std::string& node, std::vector<uint64_t>& out, size_t maxBlocks, int depth) {
    if (depth > 6 || u16(node, 0) != 0xF30A) return false;
    uint16_t n = u16(node, 2), level = u16(node, 6);
    for (uint16_t e = 0; e < n && out.size() < maxBlocks; ++e) {
        size_t at = 12 + 12u * e;
        if (at + 12 > node.size()) return false;
        if (level == 0) {
            uint32_t lblk = u32(node, at);
            uint32_t len = u16(node, at + 4);
            bool uninit = len > 32768;
            if (uninit) len -= 32768;
            uint64_t start = ((uint64_t)u16(node, at + 6) << 32) | u32(node, at + 8);
            while (out.size() < lblk && out.size() < maxBlocks) out.push_back(0);  // a hole
            for (uint32_t k = 0; k < len && out.size() < maxBlocks; ++k) out.push_back(uninit ? 0 : start + k);
        } else {
            uint64_t leaf = u32(node, at + 4) | ((uint64_t)u16(node, at + 8) << 32);
            std::string child;
            if (!leaf || leaf >= blocksCount_ || !rd_(leaf * bs_, bs_, child) || !extentNode(child, out, maxBlocks, depth + 1)) return false;
        }
    }
    return true;
}

bool ExtReader::mapBlock(uint64_t blk, int level, std::vector<uint64_t>& out, size_t maxBlocks) {
    uint32_t per = bs_ / 4;
    if (!blk) {  // a hole the size of this whole branch
        uint64_t span = level == 1 ? per : level == 2 ? (uint64_t)per * per : (uint64_t)per * per * per;
        for (uint64_t k = 0; k < span && out.size() < maxBlocks; ++k) out.push_back(0);
        return true;
    }
    std::string b;
    if (blk >= blocksCount_ || !rd_(blk * bs_, bs_, b)) return false;
    for (uint32_t i = 0; i < per && out.size() < maxBlocks; ++i) {
        uint32_t p = u32(b, i * 4);
        if (level == 1) out.push_back(p);
        else if (!mapBlock(p, level - 1, out, maxBlocks)) return false;
    }
    return true;
}

bool ExtReader::blocks(const Inode& in, std::vector<uint64_t>& out, size_t maxBlocks) {
    if (in.flags & 0x80000) return extentNode(in.block, out, maxBlocks, 0);
    for (int i = 0; i < 12 && out.size() < maxBlocks; ++i) out.push_back(u32(in.block, i * 4));
    for (int l = 1; l <= 3 && out.size() < maxBlocks; ++l)
        if (!mapBlock(u32(in.block, (11 + l) * 4), l, out, maxBlocks)) return false;
    return true;
}

bool ExtReader::data(const Inode& in, std::string& out, size_t maxBytes) {
    out.clear();
    uint64_t want = std::min<uint64_t>(in.size, maxBytes);
    if (in.flags & 0x10000000) {  // inline data: kept inside the inode
        out = in.block.substr(0, (size_t)std::min<uint64_t>(want, 60));
        return true;
    }
    std::vector<uint64_t> list;
    size_t count = (size_t)((want + bs_ - 1) / bs_);
    if (!blocks(in, list, count)) return false;
    for (size_t i = 0; i < list.size() && out.size() < want;) {
        if (!list[i]) {
            out.append(bs_, '\0');
            ++i;
            continue;
        }
        size_t run = 1;
        while (i + run < list.size() && list[i + run] == list[i] + run && run < 256) ++run;
        std::string chunk;
        if (list[i] + run > blocksCount_ || !rd_(list[i] * bs_, (uint32_t)(run * bs_), chunk)) return false;
        out += chunk;
        i += run;
    }
    if (out.size() > want) out.resize((size_t)want);
    return true;
}

bool ExtReader::entries(const Inode& dir, std::vector<std::pair<std::string, uint32_t>>& out) {
    std::string d;
    if ((dir.mode & 0xF000) != 0x4000 || !data(dir, d, 8u << 20)) return false;
    for (size_t pos = 0; pos + 8 <= d.size();) {
        uint32_t ino = u32(d, pos);
        uint16_t rec = u16(d, pos + 4);
        uint8_t len = (uint8_t)d[pos + 6];
        if (rec < 8 || pos + rec > d.size()) break;
        if (ino && len && pos + 8 + len <= d.size()) out.push_back({d.substr(pos + 8, len), ino});
        pos += rec;
    }
    return true;
}

bool ExtReader::lookup(const std::string& path, uint32_t& ino, int depth) {
    if (depth > 8) return false;
    std::vector<std::string> parts;
    for (size_t a = 0; a <= path.size();) {
        size_t b = path.find('/', a);
        if (b == std::string::npos) b = path.size();
        if (b > a) parts.push_back(path.substr(a, b - a));
        a = b + 1;
    }
    std::vector<uint32_t> stack = {2};
    std::vector<std::string> names;
    for (size_t k = 0; k < parts.size(); ++k) {
        const std::string& c = parts[k];
        if (c == ".") continue;
        if (c == "..") {
            if (stack.size() > 1) {
                stack.pop_back();
                names.pop_back();
            }
            continue;
        }
        Inode dir;
        std::vector<std::pair<std::string, uint32_t>> list;
        if (!inode(stack.back(), dir) || !entries(dir, list)) return false;
        uint32_t child = 0;
        for (auto& e : list)
            if (e.first == c) child = e.second;
        if (!child) return false;
        Inode ci;
        if (!inode(child, ci)) return false;
        if ((ci.mode & 0xF000) == 0xA000) {  // a symbolic link: follow it
            std::string target;
            if (ci.size < 60 && !(ci.flags & 0x80000) && !(ci.flags & 0x10000000)) target = ci.block.substr(0, (size_t)ci.size);
            else if (!data(ci, target, 4096)) return false;
            std::string rest;
            for (size_t j = k + 1; j < parts.size(); ++j) rest += "/" + parts[j];
            std::string base;
            for (const std::string& n : names) base += "/" + n;
            std::string next = (!target.empty() && target[0] == '/' ? target : base + "/" + target) + rest;
            return lookup(next, ino, depth + 1);
        }
        stack.push_back(child);
        names.push_back(c);
    }
    ino = stack.back();
    return true;
}

bool ExtReader::readFile(const std::string& path, std::string& out, size_t maxBytes) {
    uint32_t ino = 0;
    Inode in;
    if (!lookup(path, ino) || !inode(ino, in) || (in.mode & 0xF000) != 0x8000) return false;
    return data(in, out, maxBytes);
}

bool ExtReader::list(const std::string& dir, std::vector<std::string>& names) {
    uint32_t ino = 0;
    Inode in;
    std::vector<std::pair<std::string, uint32_t>> e;
    if (!lookup(dir, ino) || !inode(ino, in) || !entries(in, e)) return false;
    for (auto& x : e)
        if (x.first != "." && x.first != "..") names.push_back(x.first);
    return true;
}

bool ExtReader::exists(const std::string& path) {
    uint32_t ino = 0;
    return lookup(path, ino);
}

bool ExtReader::isDir(const std::string& path) {
    uint32_t ino = 0;
    Inode in;
    return lookup(path, ino) && inode(ino, in) && (in.mode & 0xF000) == 0x4000;
}

// ---------------------------------------------------------------- configuration files
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

static std::vector<std::string> lines(const std::string& t) {
    std::vector<std::string> out;
    for (size_t a = 0; a < t.size();) {
        size_t b = t.find('\n', a);
        if (b == std::string::npos) b = t.size();
        out.push_back(t.substr(a, b - a));
        a = b + 1;
    }
    return out;
}

static std::vector<std::string> words(const std::string& l) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : l) {
        if (c == ' ' || c == '\t' || c == '\r') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::map<std::string, std::string> parseOsRelease(const std::string& text) {
    std::map<std::string, std::string> m;
    for (std::string l : lines(text)) {
        l = trim(l);
        size_t eq = l.find('=');
        if (l.empty() || l[0] == '#' || eq == std::string::npos) continue;
        std::string v = trim(l.substr(eq + 1));
        if (v.size() >= 2 && (v[0] == '"' || v[0] == '\'') && v.back() == v[0]) v = v.substr(1, v.size() - 2);
        m[l.substr(0, eq)] = v;
    }
    return m;
}

std::vector<FstabLine> parseFstab(const std::string& text) {
    std::vector<FstabLine> out;
    for (const std::string& l : lines(text)) {
        std::string t = trim(l);
        if (t.empty() || t[0] == '#') continue;
        std::vector<std::string> w = words(t);
        if (w.size() < 3) continue;
        out.push_back({w[0], w[1], w[2]});
    }
    return out;
}

static bool looksLikeId(const std::string& s) {
    if (s.size() < 8 || s.size() > 36) return false;
    for (char c : s)
        if (!isxdigit((unsigned char)c) && c != '-') return false;
    return true;
}

std::string grubUuid(const std::string& cfg) {
    for (const std::string& l : lines(cfg)) {
        std::vector<std::string> w = words(trim(l));
        bool search = false;
        for (size_t i = 0; i < w.size(); ++i) {
            if (w[i] == "search.fs_uuid" || w[i] == "--fs-uuid" || w[i] == "-u" || (w[i] == "search" && l.find("uuid") != std::string::npos)) search = true;
            else if (search && w[i][0] != '-' && looksLikeId(w[i])) {
                std::string v = w[i];
                for (char& c : v) c = (char)tolower((unsigned char)c);
                return v;
            }
        }
        size_t r = l.find("root=UUID=");
        if (r != std::string::npos) {
            std::string v = l.substr(r + 10);
            v = v.substr(0, v.find_first_of(" \t\r"));
            for (char& c : v) c = (char)tolower((unsigned char)c);
            if (looksLikeId(v)) return v;
        }
    }
    return "";
}

std::string grubPrefix(const std::string& cfg) {
    size_t p = cfg.find("prefix=");
    if (p == std::string::npos) return "";
    std::string v = cfg.substr(p + 7);
    v = v.substr(0, v.find_first_of("\r\n"));
    size_t close = v.find(')');
    if (close != std::string::npos) v = v.substr(close + 1);
    std::string out;
    for (char c : v)
        if (c != '\'' && c != '"' && c != ' ') out += c;
    return out;
}

// ---------------------------------------------------------------- finding Linux
static wstr wide(const std::string& s) { return utf8ToWide(s); }

static int partByUuid(const Report& r, const std::string& uuid) {
    if (uuid.empty()) return -1;
    wstr w = toLower(wide(uuid));
    for (size_t i = 0; i < r.m.parts.size(); ++i)
        if (!r.m.parts[i].fsUuid.empty() && iequals(r.m.parts[i].fsUuid, w)) return (int)i;
    return -1;
}

static std::vector<wstr> kernelsIn(ExtReader& x, const std::string& dir) {
    std::vector<std::string> names;
    std::vector<wstr> out;
    if (!x.list(dir, names)) return out;
    for (const std::string& n : names)
        if (n.rfind("vmlinuz", 0) == 0 || n.rfind("vmlinux", 0) == 0 || n.rfind("kernel-", 0) == 0 || n == "bzImage") {
            std::string v = n;
            for (const char* pre : {"vmlinuz-", "vmlinux-", "kernel-"})
                if (v.rfind(pre, 0) == 0) v = v.substr(strlen(pre));
            if (v.find(".old") != std::string::npos) continue;
            out.push_back(wide(v));
        }
    std::sort(out.begin(), out.end(), [](const wstr& a, const wstr& b) { return a > b; });
    return out;
}

static void findLoaders(Report& r) {
    static const wchar_t* const skip[] = {L"Microsoft", L"Boot", L"HP", L"Dell", L"Lenovo", L"ASUS", L"Acer", L"Toshiba", L"OEM", L"tools", L"PATioBootPicker",
                                          L"Intel", L"memtest86"};
    static const wchar_t* const files[] = {L"shimx64.efi", L"shimaa64.efi", L"shimia32.efi", L"grubx64.efi", L"grubaa64.efi", L"grubia32.efi",
                                           L"systemd-bootx64.efi", L"systemd-bootaa64.efi"};
    for (size_t i = 0; i < r.m.parts.size(); ++i) {
        const Partition& p = r.m.parts[i];
        if (!isEsp(p) || !p.readable || p.root.empty()) continue;
        WIN32_FIND_DATAW fd{};
        HANDLE f = FindFirstFileW((p.root + L"EFI\\*").c_str(), &fd);
        if (f == INVALID_HANDLE_VALUE) continue;
        do {
            wstr d = fd.cFileName;
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || d == L"." || d == L"..") continue;
            bool skipped = false;
            for (const wchar_t* s : skip) skipped |= iequals(d, s);
            if (skipped) continue;
            LinuxLoader l;
            l.esp = (int)i;
            l.dir = L"EFI\\" + d;
            for (const wchar_t* name : files)
                if (l.file.empty() && fileExists(p.root + l.dir + L"\\" + name)) {
                    l.file = l.dir + L"\\" + name;
                    l.shim = istartsWith(name, L"shim");
                    l.systemdBoot = istartsWith(name, L"systemd");
                }
            if (l.file.empty()) continue;
            std::string cfg;
            if (readFileText(p.root + l.dir + L"\\grub.cfg", cfg, 1u << 20)) {
                l.uuid = grubUuid(cfg);
                l.prefix = grubPrefix(cfg);
                l.fullConfig = cfg.find("menuentry") != std::string::npos || cfg.find("blscfg") != std::string::npos;
            }
            if (l.systemdBoot) {  // systemd-boot: the entries say where the root file system is
                WIN32_FIND_DATAW ed{};
                HANDLE g = FindFirstFileW((p.root + L"loader\\entries\\*.conf").c_str(), &ed);
                if (g != INVALID_HANDLE_VALUE) {
                    std::string e;
                    if (readFileText(p.root + L"loader\\entries\\" + ed.cFileName, e, 65536)) {
                        l.uuid = grubUuid(e);
                        l.fullConfig = true;
                    }
                    FindClose(g);
                }
            }
            r.linuxLoaders.push_back(l);
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
}

void findLinuxWith(Report& r, const std::function<ReadFn(int)>& readerFor) {
    findLoaders(r);
    std::vector<int> claimed;  // partitions that already belong to a Linux found
    // 1. Linux systems we can look into (ext2/3/4)
    for (size_t i = 0; i < r.m.parts.size(); ++i) {
        const Partition& p = r.m.parts[i];
        if (!istartsWith(p.rawFs, L"ext")) continue;
        ReadFn rd = readerFor((int)i);
        ExtReader x;
        wstr err;
        if (!rd || !x.open(rd, err)) continue;
        std::string osr;
        if (!x.readFile("/etc/os-release", osr, 65536) && !x.readFile("/usr/lib/os-release", osr, 65536)) continue;
        auto m = parseOsRelease(osr);
        Install in;
        in.linux = true;
        in.part = (int)i;
        in.name = wide(!m["PRETTY_NAME"].empty() ? m["PRETTY_NAME"] : !m["NAME"].empty() ? m["NAME"] : std::string("Linux"));
        in.distroId = wide(m["ID"]);
        in.kernels = kernelsIn(x, "/boot");
        std::string fstab;
        bool haveFstab = x.readFile("/etc/fstab", fstab, 262144);
        in.loopSigns.clear();
        for (const FstabLine& f : parseFstab(fstab)) {
            if (f.mount == "/boot" && f.spec.rfind("UUID=", 0) == 0) in.bootPart = partByUuid(r, f.spec.substr(5));
            // Kept for the chain: every file system Linux expects, as "spec|mount"
            in.loopSigns.push_back(wide(f.spec + "|" + f.mount + "|" + f.type));
        }
        if (!haveFstab) in.loopSigns.push_back(L"!nofstab");
        bool menu = x.exists("/boot/grub/grub.cfg") || x.exists("/boot/grub2/grub.cfg") || x.isDir("/boot/loader/entries");
        if (in.bootPart >= 0) {
            ExtReader b;
            ReadFn brd = readerFor(in.bootPart);
            if (brd && b.open(brd, err)) {
                if (in.kernels.empty()) in.kernels = kernelsIn(b, "/");
                menu = menu || b.exists("/grub/grub.cfg") || b.exists("/grub2/grub.cfg") || b.isDir("/loader/entries");
            }
            claimed.push_back(in.bootPart);
        }
        in.verdictText = menu ? L"menu" : L"";  // carried to the chain, replaced there
        claimed.push_back((int)i);
        r.installs.push_back(in);
    }
    // 2. Linux systems we cannot look into (btrfs, XFS, encrypted) when something points to them
    for (size_t i = 0; i < r.m.parts.size(); ++i) {
        const Partition& p = r.m.parts[i];
        if (std::find(claimed.begin(), claimed.end(), (int)i) != claimed.end()) continue;
        bool fsLinux = p.rawFs == L"btrfs" || p.rawFs == L"XFS" || p.rawFs == L"LUKS";
        if (!fsLinux) continue;
        bool pointed = false;
        for (const LinuxLoader& l : r.linuxLoaders) pointed |= !l.uuid.empty() && iequals(wide(l.uuid), p.fsUuid);
        bool bigLinux = p.role == R_LINUX && p.length > (8ull << 30);
        if (!pointed && !p.linuxRoot && !bigLinux) continue;
        Install in;
        in.linux = true;
        in.readable = false;
        in.part = (int)i;
        in.name = p.rawFs == L"LUKS" ? T(L"Linux (encrypted)") : T(L"Linux (") + p.rawFs + L")";
        r.installs.push_back(in);
    }
    // 3. Which boot loader starts which Linux
    for (Install& in : r.installs) {
        if (!in.linux) continue;
        const Partition& P = r.m.parts[in.part];
        wstr bootUuid = in.bootPart >= 0 ? r.m.parts[in.bootPart].fsUuid : wstr();
        int best = -1;
        for (size_t k = 0; k < r.linuxLoaders.size() && best < 0; ++k) {
            const LinuxLoader& l = r.linuxLoaders[k];
            wstr u = wide(l.uuid);
            if (!u.empty() && (iequals(u, P.fsUuid) || (!bootUuid.empty() && iequals(u, bootUuid)))) best = (int)k;
        }
        for (size_t k = 0; k < r.linuxLoaders.size() && best < 0; ++k) {
            const LinuxLoader& l = r.linuxLoaders[k];
            if (l.used || (!l.uuid.empty() && partByUuid(r, l.uuid) >= 0)) continue;  // points to another existing partition: not ours
            wstr dir = toLower(l.dir.substr(4));
            if ((!in.distroId.empty() && dir == toLower(in.distroId)) || (!in.distroId.empty() && icontains(in.name, dir))) best = (int)k;
        }
        if (best >= 0) {
            in.loader = best;
            r.linuxLoaders[best].used = true;
            if (!in.readable) in.name = systemKindFromPath(L"\\" + r.linuxLoaders[best].dir + L"\\x") + (r.m.parts[in.part].rawFs == L"LUKS" ? T(L", encrypted") : L"");
        }
    }
    // 4. Boot loaders whose Linux is gone
    for (size_t k = 0; k < r.linuxLoaders.size(); ++k) {
        const LinuxLoader& l = r.linuxLoaders[k];
        if (l.used) continue;
        int target = partByUuid(r, l.uuid);
        const Partition& esp = r.m.parts[l.esp];
        wstr kind = systemKindFromPath(L"\\" + l.dir + L"\\x");
        Finding f;
        f.lesson = T(L"What is a Linux boot loader? Linux is usually started by a program called GRUB (or systemd-boot). Its files sit in a folder on the start-up partition, next to Windows Boot Manager, and a small settings file tells it on which partition Linux lives. When Linux is deleted, this folder is often left behind.");
        if (!l.uuid.empty() && target < 0) {
            f.health = Health::Warn;
            f.title = T(L"A Linux boot loader leads nowhere: ") + kind;
            f.text = L"The folder \\" + l.dir + T(L" on the start-up partition (") + esp.where() + T(L") holds the boot loader of ") + kind +
                     T(L", but the partition it starts Linux from is not on any disk now. That Linux was probably deleted, or its partition was formatted. If you pick it in the PC's boot menu, you get a GRUB error or a \"grub rescue>\" prompt.\n\nIf you no longer use that Linux, removing the folder is safe: it only takes a few MB and nothing else needs it.");
            Fix d = makeFix(FX_DELETE_EFI_DIR);
            d.part = l.esp;
            d.disk = esp.disk;
            d.arg = l.dir;
            d.title = T(L"Remove the leftover ") + kind + T(L" boot loader");
            f.fixes.push_back(d);
            r.pc.push_back(f);
        } else if (target < 0 && l.uuid.empty() && !l.fullConfig) {
            f.health = Health::Info;
            f.title = T(L"A Linux boot loader was found: ") + kind;
            f.text = L"The folder \\" + l.dir + L" on " + esp.where() + T(L" holds a Linux boot loader, but no matching Linux was found on the disks. Its Linux may be on a disk that is not connected, or use a file system this program cannot look into.");
            r.pc.push_back(f);
        }
    }
}

void findLinux(Report& r) {
    findLinuxWith(r, [&r](int i) { return partitionReader(r.m.parts[i]); });
}

// ---------------------------------------------------------------- the chain
void buildLinuxChain(Report& r, Install& in) {
    const Partition& P = r.m.parts[in.part];
    const Disk* D = nullptr;
    for (const Disk& d : r.m.disks)
        if (d.number == P.disk) D = &d;
    bool uefi = r.m.fw.uefi;
    const LinuxLoader* L = in.loader >= 0 ? &r.linuxLoaders[in.loader] : nullptr;
    bool menuSeen = in.verdictText == L"menu";
    std::vector<wstr> mounts = in.loopSigns;
    in.loopSigns.clear();
    in.verdictText.clear();
    in.chain.clear();
    in.plan.clear();
    in.winDir = P.where() + L" (" + (P.rawFs.empty() ? wstr(T(L"Linux")) : P.rawFs) + L")";
    in.where = (P.letter.empty() ? wstr() : P.letter + L" · ") + P.where();
    in.edition = in.kernels.empty() ? wstr() : T(L"Linux kernel ") + in.kernels[0];
    in.sysPart = L ? L->esp : -1;
    auto fix = [&](int kind, const wstr& what = wstr()) {
        for (size_t i = 0; i < in.plan.size(); ++i)
            if (in.plan[i].kind == kind) {
                if (kind == FX_LINUX_USB && !what.empty() && in.plan[i].what.find(what) == wstr::npos) in.plan[i].what += L" " + what;
                return (int)i;
            }
        Fix f = makeFix(kind);
        if (!what.empty()) f.what = what;
        f.part = in.part;
        f.disk = P.disk;
        in.plan.push_back(f);
        return (int)in.plan.size() - 1;
    };
    auto link = [&](const char* icon, const wstr& name, Health h, const wstr& plain, const wstr& tech = wstr()) -> Link& {
        Link l;
        l.icon = icon;
        l.name = name;
        l.health = h;
        l.plain = plain;
        l.tech = tech;
        in.chain.push_back(l);
        return in.chain.back();
    };
    wstr usbWhy;

    // 1. Firmware
    {
        Link& l = link("bd_chip", N(L"PC firmware"), Health::Ok, uefi ? T(L"The PC starts in UEFI mode, the modern way of starting.") : T(L"The PC starts in legacy (BIOS) mode, the older way of starting."));
        l.aboutSecureBoot = uefi;
        if (uefi && r.m.fw.secureBootKnown) l.plain += r.m.fw.secureBoot ? T(L" Secure Boot is on.") : T(L" Secure Boot is off.");
        if (uefi && r.m.fw.secureBoot && L && !L->shim) {
                l.health = Health::Warn;
                l.plain = T(L"Secure Boot is on, but this Linux starts without the signed \"shim\" loader, so the PC may refuse to start it. Turn Secure Boot off in the PC's firmware settings, or reinstall GRUB with Secure Boot support from Linux.");
                fix(FX_FIRMWARE_SETTINGS);
            }
        if (!uefi && D && D->style == 2) {
            l.health = Health::Warn;
            l.plain = T(L"This Linux is on a GPT disk but the PC was started in legacy (BIOS) mode. It only starts this way if GRUB was set up for it (with a \"BIOS boot\" partition).");
        }
    }
    // 2. Disk
    {
        if (!D || !D->readable) link("disk_hw", N(L"Disk"), Health::Bad, T(L"The disk cannot be read. It may be failing or badly connected."));
        else {
            Link& l = link("disk_hw", N(L"Disk"), Health::Ok, T(L"The disk with this Linux (") + D->name() + T(L") can be read and its partition table is fine."),
                           D->name() + (D->style == 2 ? T(L", GPT") : T(L", MBR")));
            if (D->style == 2 && !D->gptPrimaryOk) {
                l.health = Health::Bad;
                l.plain = T(L"The main copy of the disk's partition table is damaged.");
            }
        }
    }
    if (uefi) {
        // 3. Start-up partition with the boot loader
        if (!L) {
            Link& l = link("partition", N(L"Start-up partition"), Health::Bad,
                           T(L"No Linux boot loader (GRUB) was found on any start-up partition. Something deleted it: often a Windows reinstall or repair, or formatting the start-up partition. Linux itself is still there."));
            usbWhy = T(L"Reinstall GRUB, the Linux boot loader.");
            l.fixes.push_back(fix(FX_LINUX_USB, usbWhy));
        } else {
            const Partition& E = r.m.parts[L->esp];
            Link& l = link("partition", N(L"Start-up partition"), Health::Ok, T(L"The start-up partition with the Linux boot loader is there and can be read."),
                           E.where() + L" (" + sizeText(E.length) + L", " + E.fs + L")");
            if (E.disk != P.disk) l.plain += T(L" It is on another disk (Disk ") + std::to_wstring(E.disk) + T(L"), so Linux only starts while that disk is connected.");
        }
        // 4. PC's start-up list
        {
            const FirmwareEntry* fe = nullptr;
            if (L && r.m.fw.varsReadable) {
                const Partition& E = r.m.parts[L->esp];
                for (const FirmwareEntry& e : r.m.fw.entries)
                    if (e.hasPart && E.gpt && IsEqualGUID(e.part, E.id) && icontains(e.path, L"\\" + L->dir + L"\\") && (!fe || (e.inOrder && !fe->inOrder))) fe = &e;
            }
            if (!L) link("bd_list", N(L"PC's start-up list"), Health::Unknown, T(L"Cannot be checked until the boot loader is back."));
            else if (!r.m.fw.varsReadable) link("bd_list", N(L"PC's start-up list"), Health::Unknown, T(L"The PC's start-up list could not be read here."));
            else if (!fe) {
                bool fallback = fileExists(r.m.parts[L->esp].root + L"EFI\\Boot\\grubx64.efi");
                Link& l = link("bd_list", N(L"PC's start-up list"), fallback ? Health::Warn : Health::Bad,
                               T(L"The PC's own start-up list has no entry for this Linux, so the PC never offers to start it. This often happens after a Windows update or a firmware update."));
                Fix& f = in.plan[fix(FX_ADD_FW_ENTRY)];
                f.part = L->esp;
                f.arg = L"\\" + L->file;
                f.entry = in.name;
                l.fixes.push_back((int)(&f - in.plan.data()));
            } else {
                wchar_t num[16];
                swprintf(num, 16, L"Boot%04X", fe->num);
                in.active = fe->inOrder;
                in.isDefault = !r.m.fw.order.empty() && r.m.fw.order[0] == fe->num;
                in.inMenu = true;
                wstr first;
                if (!in.isDefault && !r.m.fw.order.empty())
                    for (const FirmwareEntry& e : r.m.fw.entries)
                        if (e.num == r.m.fw.order[0]) first = e.description;
                Link& l = link("bd_list", N(L"PC's start-up list"), Health::Ok, T(L"The PC's own start-up list has this Linux as \"") + fe->description + T(L"\", and it starts first."),
                               wstr(num) + L", " + fe->path);
                if (!fe->inOrder) {
                    l.health = Health::Warn;
                    l.plain = T(L"The PC's start-up list has an entry for this Linux (\"") + fe->description + T(L"\"), but it is switched off, so the PC never tries it.");
                } else if (!in.isDefault) {
                    l.health = Health::Info;
                    l.plain = T(L"The PC's start-up list has this Linux (\"") + fe->description + T(L"\"), but ") + (first.empty() ? wstr(T(L"another system")) : L"\"" + first + L"\"") +
                              T(L" starts first. To start Linux, pick it in the PC's boot menu (often F12, F11, F8 or Esc while the PC starts), or make it start first. When GRUB starts first, it usually shows a menu with Windows in it too.");
                }
                if (!in.isDefault) {
                    Fix& f = in.plan[fix(FX_FW_FIRST)];
                    f.fwNum = fe->num;
                    f.entry = in.name;
                    l.fixes.push_back((int)(&f - in.plan.data()));
                }
            }
        }
        // 5. Boot loader files
        if (L) {
            wstr full = r.m.parts[L->esp].root + L->file;
            if (!looksLikeProgram(full)) {
                Link& l = link("bd_key", N(L"Linux boot loader"), Health::Bad, T(L"The Linux boot loader file is damaged."), L"\\" + L->file);
                usbWhy = T(L"Reinstall GRUB, the Linux boot loader.");
                l.fixes.push_back(fix(FX_LINUX_USB, usbWhy));
            } else
                link("bd_key", N(L"Linux boot loader"), Health::Ok,
                     wstr(T(L"The Linux boot loader is in place")) + (L->shim ? L" (with \"shim\", which lets it start while Secure Boot is on)." : L->systemdBoot ? L" (systemd-boot)." : L" (GRUB)."),
                     L"\\" + L->file);
        } else
            link("bd_key", N(L"Linux boot loader"), Health::Unknown, T(L"Missing (see the step before)."));
        // 6. GRUB settings: where it looks for Linux
        if (L) {
            int target = partByUuid(r, L->uuid);
            wstr bootUuid = in.bootPart >= 0 ? r.m.parts[in.bootPart].fsUuid : wstr();
            wstr u = utf8ToWide(L->uuid);
            if (L->uuid.empty())
                link("bd_list", N(L"GRUB settings"), Health::Info, T(L"The boot loader's settings are built into it, so this program cannot see where it looks for Linux. That is normal for some Linux versions."));
            else if (target >= 0 && (target == in.part || target == in.bootPart))
                link("bd_list", N(L"GRUB settings"), Health::Ok, T(L"The boot loader looks for Linux on the right partition (") + r.m.parts[target].where() + L").", L"search --fs-uuid " + u);
            else if (target >= 0) {
                Link& l = link("bd_list", N(L"GRUB settings"), Health::Warn,
                               T(L"The boot loader looks for Linux on another partition (") + r.m.parts[target].where() + T(L"), not on this Linux's partition. It may be starting a different Linux."),
                               L"search --fs-uuid " + u);
                (void)l;
            } else {
                Link& l = link("bd_list", N(L"GRUB settings"), Health::Bad,
                               T(L"The boot loader looks for Linux on a partition that is not on any disk now. Starting Linux ends at a \"grub rescue>\" prompt."),
                               L"search --fs-uuid " + u + L" (no such file system)" + (bootUuid.empty() ? L"" : T(L", /boot is ") + bootUuid));
                l.fixes.push_back(fix(FX_LINUX_USB, T(L"Point GRUB to the right partition again (Boot-Repair does this).")));
            }
        } else
            link("bd_list", N(L"GRUB settings"), Health::Unknown, T(L"Missing (see the step before)."));
    } else {
        // Legacy BIOS: GRUB lives in the disk's first sector
        if (D && D->grubMbr) link("bd_key", N(L"Disk start-up code"), Health::Ok, T(L"The disk's start-up code is GRUB, the Linux boot loader."), T(L"GRUB in the MBR"));
        else {
            Link& l = link("bd_key", N(L"Disk start-up code"), Health::Bad,
                           D && D->bootCode ? T(L"The disk's start-up code is not GRUB (usually Windows replaced it). The PC goes straight to Windows and never offers Linux.")
                                            : T(L"The disk's start-up code is empty, so the PC cannot start from this disk."),
                           T(L"MBR without GRUB"));
            l.fixes.push_back(fix(FX_LINUX_USB, T(L"Write GRUB to the disk again (Boot-Repair does this).")));
        }
    }
    // 7. Linux partition
    {
        wstr t = P.where() + L" (" + sizeText(P.length) + L", " + (P.rawFs.empty() ? wstr(T(L"unknown file system")) : P.rawFs) + L")";
        if (in.readable) link("drive", N(L"Linux partition"), Health::Ok, T(L"The partition with Linux is there and this program can look inside it."), t);
        else if (P.rawFs == L"LUKS")
            link("drive", N(L"Linux partition"), Health::Info, T(L"The Linux partition is encrypted (LUKS). Linux asks for its password while starting. Its contents cannot be checked from here."), t);
        else
            link("drive", N(L"Linux partition"), Health::Unknown,
                 T(L"The Linux partition is there, but it uses ") + P.rawFs + T(L", which this program cannot look inside. The next steps cannot be checked from here."), t);
    }
    // 8-10. Inside Linux
    if (!in.readable) {
        link("file_app", N(L"Linux kernel"), Health::Unknown, T(L"Cannot be checked: the file system cannot be read from Windows."));
        link("bd_list", N(L"GRUB menu"), Health::Unknown, T(L"Cannot be checked: the file system cannot be read from Windows."));
        link("settings", N(L"Linux settings"), Health::Unknown, T(L"Cannot be checked: the file system cannot be read from Windows."));
    } else {
        if (in.kernels.empty()) {
            Link& l = link("file_app", N(L"Linux kernel"), Health::Bad,
                           T(L"No Linux kernel (the heart of Linux) was found in /boot. An update probably failed halfway, or /boot is on a partition that is missing."));
            l.fixes.push_back(fix(FX_LINUX_USB, T(L"Reinstall the Linux kernel.")));
        } else {
            wstr all;
            for (const wstr& k : in.kernels) all += (all.empty() ? L"" : L", ") + k;
            link("file_app", N(L"Linux kernel"), Health::Ok,
                 T(L"The Linux kernel is in place") + (in.kernels.size() > 1 ? T(L", plus ") + std::to_wstring(in.kernels.size() - 1) + T(L" older one(s) to fall back on.") : wstr(L".")), all);
        }
        bool menu = menuSeen || (L && (L->fullConfig || L->systemdBoot));
        if (menu) link("bd_list", N(L"GRUB menu"), Health::Ok, T(L"The menu file the boot loader shows (with the Linux kernels to choose from) is in place."), L"/boot/grub/grub.cfg");
        else {
            Link& l = link("bd_list", N(L"GRUB menu"), Health::Bad, T(L"The GRUB menu file (/boot/grub/grub.cfg) is missing, so GRUB stops at a \"grub>\" prompt."));
            l.fixes.push_back(fix(FX_LINUX_USB, T(L"Rebuild the GRUB menu (\"sudo update-grub\").")));
        }
        bool noFstab = false;
        wstr missing, missingKey, tech;
        for (const wstr& m : mounts) {
            if (m == L"!nofstab") {
                noFstab = true;
                continue;
            }
            size_t a = m.find(L'|'), b = m.find(L'|', a + 1);
            wstr spec = m.substr(0, a), mount = m.substr(a + 1, b - a - 1), type = m.substr(b + 1);
            tech += (tech.empty() ? L"" : L"\n") + mount + L"  " + spec;
            if (!istartsWith(spec, L"UUID=")) continue;
            if (partByUuid(r, wideToUtf8(spec.substr(5))) >= 0) continue;
            bool key = mount == L"/" || mount == L"/boot" || mount == L"/boot/efi" || mount == L"/efi";
            (key ? missingKey : missing) += ((key ? missingKey : missing).empty() ? L"" : L", ") + (type == L"swap" ? wstr(L"swap") : mount);
        }
        if (noFstab) {
            Link& l = link("settings", N(L"Linux settings"), Health::Bad, T(L"Linux's list of partitions to use (/etc/fstab) is missing."));
            l.fixes.push_back(fix(FX_LINUX_USB, T(L"Recreate /etc/fstab.")));
        } else if (!missingKey.empty()) {
            Link& l = link("settings", N(L"Linux settings"), Health::Bad,
                           T(L"Linux expects partitions that are not on any disk now (") + missingKey + T(L"). It stops at an \"emergency mode\" prompt while starting."), tech);
            l.fixes.push_back(fix(FX_LINUX_USB, T(L"Correct /etc/fstab so it names the partitions that are there now.")));
        } else if (!missing.empty()) {
            Link& l = link("settings", N(L"Linux settings"), Health::Warn,
                           T(L"Linux expects partitions that are not on any disk now (") + missing + T(L"). It may wait a minute and a half while starting, or stop at an emergency prompt."), tech);
            l.fixes.push_back(fix(FX_LINUX_USB, T(L"Correct /etc/fstab so it names the partitions that are there now.")));
        } else
            link("settings", N(L"Linux settings"), Health::Ok, T(L"Every partition Linux expects to use (in /etc/fstab) is there."), tech);
    }
    // Verdict
    Health worst = Health::Ok;
    bool unknown = false;
    for (const Link& l : in.chain) {
        if (l.health == Health::Unknown) unknown = true;
        else if (l.health != Health::Info) worst = worse(worst, l.health);
    }
    for (Fix& f : in.plan) {
        f.install = (int)(&in - r.installs.data());
        if (f.kind == FX_LINUX_USB) {
            f.available = false;
            f.notAvailable =
                T(L"Windows cannot safely change Linux's own files. Start the PC from a Linux USB stick (for example Ubuntu's \"Try Ubuntu\"), connect to the internet, install the free tool \"Boot-Repair\" (sudo add-apt-repository ppa:yannubuntu/boot-repair, then sudo apt install boot-repair) and choose \"Recommended repair\". A Linux-savvy friend can also do it by hand.");
        }
    }
    // Order: what can be done here first, then what needs a Linux USB stick.
    // Sorting moves fixes, so each step's fix list is rebuilt by kind afterwards.
    std::stable_sort(in.plan.begin(), in.plan.end(), [](const Fix& a, const Fix& b) { return (a.kind == FX_LINUX_USB) < (b.kind == FX_LINUX_USB); });
    for (Link& l : in.chain) {
        l.fixes.clear();
        auto add = [&](int kind) {
            for (size_t i = 0; i < in.plan.size(); ++i)
                if (in.plan[i].kind == kind) l.fixes.push_back((int)i);
        };
        if (l.name == L"PC firmware" && l.health == Health::Warn) add(FX_FIRMWARE_SETTINGS);
        if (l.name == L"PC's start-up list") {
            add(FX_ADD_FW_ENTRY);
            add(FX_FW_FIRST);
        } else if (l.health == Health::Bad || l.health == Health::Warn) {
            if (l.name != L"PC firmware" && l.name != L"Disk" && l.name != L"Linux partition") add(FX_LINUX_USB);
        }
    }
    in.verdict = worst;
    in.active = in.active || (!uefi && D && D->grubMbr);
    if (worst == Health::Bad) {
        in.verdictTitle = T(L"Will not start");
        in.verdictText = T(L"Something this Linux needs to start is missing or damaged.");
    } else if (worst == Health::Warn) {
        in.verdictTitle = T(L"May have trouble starting");
        in.verdictText = T(L"It may start, but some things need attention.");
    } else if (uefi && !in.isDefault) {
        in.verdict = Health::Info;
        in.verdictTitle = T(L"Starts from the boot menu");
        in.verdictText = T(L"This Linux is fine. Another system starts first; to start Linux, pick it in the PC's boot menu, or make it start first.");
    } else if (unknown) {
        in.verdict = Health::Info;
        in.verdictTitle = T(L"Looks fine so far");
        in.verdictText = T(L"Everything that can be checked from Windows is in place. The inside of its partition could not be checked.");
    } else {
        in.verdictTitle = T(L"Should start normally");
        in.verdictText = T(L"Everything this Linux needs to start is in place.");
    }
}

}  // namespace bd

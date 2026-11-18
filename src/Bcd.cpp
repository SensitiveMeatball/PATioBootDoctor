#include "Bcd.h"
#include "Lang.h"
#include "Hive.h"

namespace bd {

const BcdEntry* BcdStore::find(const wstr& id) const {
    for (const BcdEntry& e : entries)
        if (iequals(e.id, id)) return &e;
    return nullptr;
}

static bool contains(const std::string& hay, const void* needle, size_t n) {
    if (hay.size() < n) return false;
    for (size_t i = 0; i + n <= hay.size(); ++i)
        if (!memcmp(hay.data() + i, needle, n)) return true;
    return false;
}

int matchDevice(const std::string& blob, const Machine& m) {
    if (blob.size() < 32) return -1;
    for (size_t i = 0; i < m.parts.size(); ++i) {
        const Partition& p = m.parts[i];
        if (p.gpt) {
            if (contains(blob, &p.id, 16)) return (int)i;
        } else {
            const Disk* d = nullptr;
            for (const Disk& x : m.disks)
                if (x.number == p.disk) d = &x;
            if (!d || !d->signature) continue;
            uint64_t off = p.offset;
            if (contains(blob, &d->signature, 4) && contains(blob, &off, 8)) return (int)i;
        }
    }
    return -1;
}

bool deviceIsFile(const std::string& blob) {
    wstr w;
    for (size_t i = 0; i + 1 < blob.size(); i += 2) w += (wchar_t)((uint8_t)blob[i] | ((uint8_t)blob[i + 1] << 8));
    w = toLower(w);
    return w.find(L".wim") != wstr::npos || w.find(L".vhd") != wstr::npos || w.find(L".sdi") != wstr::npos;
}

void parseStore(HKEY root, const Machine& m, BcdStore& out) {
    out.loaded = true;
    for (const wstr& id : regSubkeys(root, L"Objects")) {
        BcdEntry e;
        e.id = id;
        wstr base = L"Objects\\" + id;
        DWORD type = 0;
        regDword(root, base + L"\\Description", L"Type", type);
        e.type = type;
        e.app = (type >> 28) == 1 ? (int)(type & 0xFFFFF) : APP_OTHER;
        wstr el = base + L"\\Elements\\";
        e.description = regString(root, el + L"12000004", L"Element");
        e.path = regString(root, el + L"12000002", L"Element");
        e.hasDevice = regBinary(root, el + L"11000001", L"Element", e.device);
        if (e.hasDevice) {
            e.devPart = matchDevice(e.device, m);
            e.ramdisk = deviceIsFile(e.device);
        }
        if (e.app == APP_LOADER || e.app == APP_RESUME) {
            e.hasOsDevice = regBinary(root, el + L"21000001", L"Element", e.osDevice);
            if (e.hasOsDevice) {
                e.osPart = matchDevice(e.osDevice, m);
                e.ramdisk = e.ramdisk || deviceIsFile(e.osDevice);
            }
            e.systemRoot = regString(root, el + L"22000002", L"Element");
            std::string rec;
            if (regBinary(root, el + L"16000009", L"Element", rec) && !rec.empty()) e.recoveryEnabled = rec[0] != 0;
        }
        if (e.app == APP_BOOTMGR && iequals(id, L"{9dea862c-5cdd-4e70-acc1-f32b344d4795}")) {
            out.hasBootmgr = true;
            out.bootmgrPath = e.path;
            out.defaultId = regString(root, el + L"23000003", L"Element");
            out.order = regMulti(root, el + L"24000001", L"Element");
            std::string t;
            if (regBinary(root, el + L"25000004", L"Element", t) && t.size() >= 4) {
                uint32_t v;
                memcpy(&v, t.data(), 4);
                out.timeout = (int)v;
            }
        }
        out.entries.push_back(e);
    }
    for (BcdEntry& e : out.entries) {
        e.isDefault = iequals(e.id, out.defaultId);
        for (const wstr& o : out.order)
            if (iequals(o, e.id)) e.inMenu = true;
    }
    if (!out.hasBootmgr) {
        out.health = Health::Bad;
        out.problem = T(L"The start-up settings have no boot manager section, so nothing can be started from them.");
    } else if (out.order.empty() && out.defaultId.empty()) {
        // Nothing is broken here: the file is fine, it just names no system.
        out.empty = true;
        out.health = Health::Warn;
        out.problem = T(L"The start-up settings list no system to start.");
    } else out.health = Health::Ok;
}

wstr devicePathOf(const wstr& volume) {
    // \\?\Volume{...}\  ->  Volume{...}
    if (volume.size() < 6) return L"";
    wstr v = volume.substr(4);
    if (!v.empty() && v.back() == L'\\') v.pop_back();
    wchar_t buf[512] = {};
    if (!QueryDosDeviceW(v.c_str(), buf, 512)) return L"";
    return buf;
}

wstr liveStoreDevicePath() {
    return regString(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\hivelist", L"\\REGISTRY\\MACHINE\\BCD00000000");
}

bool readStore(const wstr& file, bool liveRegistry, const Machine& m, BcdStore& out) {
    out.file = file;
    if (liveRegistry) {
        HKEY k = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"BCD00000000", 0, KEY_READ, &k) == ERROR_SUCCESS) {
            out.live = true;
            parseStore(k, m, out);
            RegCloseKey(k);
            return true;
        }
    }
    wstr why;
    Health h = hiveFileHealth(file, why);
    if (h == Health::Bad) {
        out.health = Health::Bad;
        out.problem = T(L"The start-up settings file is damaged: ") + why;
        return false;
    }
    LoadedHive hive;
    wstr err;
    if (!hive.load(file, true, err)) {
        // In use: it is the running system's own store, open under HKLM\BCD00000000.
        HANDLE fh = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        DWORD e = GetLastError();
        if (fh != INVALID_HANDLE_VALUE) CloseHandle(fh);
        else if ((e == ERROR_SHARING_VIOLATION || e == ERROR_LOCK_VIOLATION) && !liveRegistry) return readStore(file, true, m, out);

        out.health = Health::Bad;
        out.problem = T(L"The start-up settings file could not be read. ") + err;
        return false;
    }
    parseStore(hive.root(), m, out);
    return true;
}

std::vector<BcdStore> readStores(const Machine& m) {
    std::vector<BcdStore> out;
    wstr live = liveStoreDevicePath();
    for (size_t i = 0; i < m.parts.size(); ++i) {
        const Partition& p = m.parts[i];
        if (p.root.empty() || !p.readable) continue;
        for (const wchar_t* rel : {L"EFI\\Microsoft\\Boot\\BCD", L"Boot\\BCD"}) {
            wstr file = p.root + rel;
            if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            bool isLive = false;
            if (!live.empty() && !p.volume.empty()) {
                wstr dev = devicePathOf(p.volume);
                isLive = !dev.empty() && iequals(live, dev + L"\\" + rel);
            }
            BcdStore s;
            s.part = (int)i;
            readStore(file, isLive, m, s);
            out.push_back(s);
        }
    }
    return out;
}

}  // namespace bd

#include "Disk.h"
#include "Lang.h"
#include "core/Platform.h"
#include <winioctl.h>

namespace bd {

static const GUID kEsp = {0xC12A7328, 0xF81F, 0x11D2, {0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B}};
static const GUID kMsr = {0xE3C9E316, 0x0B5C, 0x4DB8, {0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE}};
static const GUID kRecovery = {0xDE94BBA4, 0x06D1, 0x4D40, {0xA1, 0x6A, 0xBF, 0xD5, 0x01, 0x79, 0xD6, 0xAC}};
static const GUID kLdmMeta = {0x5808C8AA, 0x7E8F, 0x42E0, {0x85, 0xD2, 0xE1, 0xE9, 0x04, 0x34, 0xCF, 0xB3}};
static const GUID kLdmData = {0xAF9B60A0, 0x1431, 0x4F62, {0xBC, 0x68, 0x33, 0x11, 0x71, 0x4A, 0x69, 0xAD}};
static const GUID kLinuxFs = {0x0FC63DAF, 0x8483, 0x4772, {0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4}};
static const GUID kLinuxRootX64 = {0x4F68BCE3, 0xE8CD, 0x4DB1, {0x96, 0xE7, 0xFB, 0xCA, 0xF9, 0x84, 0xB7, 0x09}};
static const GUID kLinuxRootArm64 = {0xB921B045, 0x1DF0, 0x41C3, {0xAF, 0x44, 0x4C, 0x6F, 0x28, 0x0D, 0x3F, 0xAE}};
static const GUID kLinuxRootX86 = {0x44479540, 0xF297, 0x41B2, {0x9A, 0xF7, 0xD1, 0x31, 0xD5, 0xF0, 0x45, 0x8A}};
static const GUID kLinuxBoot = {0xBC13C2FF, 0x59E6, 0x4262, {0xA3, 0x52, 0xB2, 0x75, 0xFD, 0x6F, 0x71, 0x72}};
static const GUID kLinuxHome = {0x933AC7E1, 0x2EB4, 0x4F13, {0xB8, 0x44, 0x0E, 0x14, 0xE2, 0xAE, 0xF9, 0x15}};
static const GUID kLinuxSwap = {0x0657FD6D, 0xA4AB, 0x43C4, {0x84, 0xE5, 0x09, 0x33, 0xC8, 0x4B, 0x4F, 0x4F}};
static const GUID kLinuxLvm = {0xE6D6D379, 0xF507, 0x44C2, {0xA2, 0x3C, 0x23, 0x8F, 0x2A, 0x3D, 0xF9, 0x28}};
static const GUID kLinuxLuks = {0xCA7D7CCB, 0x63ED, 0x4C53, {0x86, 0x1C, 0x17, 0x42, 0x53, 0x60, 0x59, 0xCC}};
static const GUID kLinuxRaid = {0xA19D880F, 0x05FC, 0x4D3B, {0xA0, 0x06, 0x74, 0x3F, 0x0F, 0x84, 0x91, 0x1E}};
static const wchar_t* const kGlobalVar = L"{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}";

Health worse(Health a, Health b) {
    auto rank = [](Health h) { return h == Health::Bad ? 4 : h == Health::Warn ? 3 : h == Health::Unknown ? 2 : h == Health::Info ? 1 : 0; };
    return rank(a) >= rank(b) ? a : b;
}

const wchar_t* roleName(int role) {
    switch (role) {
        case R_ESP: return N(L"EFI system partition (start-up partition)");
        case R_MSR: return N(L"Reserved partition (MSR)");
        case R_RECOVERY: return N(L"Recovery partition");
        case R_SYSTEM: return N(L"Active partition (start-up partition)");
        case R_DATA: return N(L"Data partition");
        case R_EXTENDED: return N(L"Extended partition (holds other partitions)");
        case R_LINUX: return N(L"Linux partition");
        case R_DYNAMIC: return N(L"Dynamic disk partition");
        case R_EMPTY: return N(L"Empty partition");
    }
    return N(L"Unknown partition");
}

const wchar_t* roleShort(int role) {
    switch (role) {
        case R_ESP: return N(L"Start-up (EFI)");
        case R_MSR: return N(L"Reserved");
        case R_RECOVERY: return N(L"Recovery");
        case R_SYSTEM: return N(L"Start-up (active)");
        case R_DATA: return N(L"Data");
        case R_EXTENDED: return N(L"Extended");
        case R_LINUX: return N(L"Linux");
        case R_DYNAMIC: return N(L"Dynamic");
        case R_EMPTY: return N(L"Empty");
    }
    return N(L"Unknown");
}

bool isEsp(const Partition& p) { return p.role == R_ESP; }

wstr Partition::where() const { return T(L"Disk ") + std::to_wstring(disk) + T(L", partition ") + std::to_wstring(number); }

wstr Partition::name() const {
    wstr s = letter.empty() ? where() : letter;
    if (!label.empty()) s += L" · " + label;
    else if (role != R_DATA) s += wstr(L" · ") + T(roleShort(role));
    return s;
}

wstr Disk::id() const {
    if (style == 2) return L"gpt:" + guidText(guid);
    wchar_t b[24];
    swprintf(b, 24, L"mbr:%08X", signature);
    return b;
}

wstr Disk::name() const {
    wstr s = T(L"Disk ") + std::to_wstring(number);
    if (!model.empty()) s += L" · " + model;
    return s + L" · " + sizeText(size);
}

wstr guidText(const GUID& g, bool braces) {
    wchar_t b[48];
    swprintf(b, 48, L"%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2],
             g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return braces ? L"{" + wstr(b) + L"}" : wstr(b);
}

bool parseGuid(const wstr& in, GUID& g) {
    wstr s;
    for (wchar_t c : in)
        if (iswxdigit(c)) s += c;
    if (s.size() != 32) return false;
    auto hex = [&](size_t at, int n) {
        unsigned long long v = 0;
        for (int i = 0; i < n; ++i) v = v * 16 + (iswdigit(s[at + i]) ? s[at + i] - L'0' : (towlower(s[at + i]) - L'a' + 10));
        return v;
    };
    g.Data1 = (unsigned long)hex(0, 8);
    g.Data2 = (unsigned short)hex(8, 4);
    g.Data3 = (unsigned short)hex(12, 4);
    for (int i = 0; i < 8; ++i) g.Data4[i] = (unsigned char)hex(16 + i * 2, 2);
    return true;
}

wstr sizeText(uint64_t b) {
    wchar_t t[32];
    if (b >= (1ull << 40)) swprintf(t, 32, L"%.1f TB", b / 1099511627776.0);
    else if (b >= (1ull << 30)) swprintf(t, 32, b >= (100ull << 30) ? L"%.0f GB" : L"%.1f GB", b / 1073741824.0);
    else if (b >= (1ull << 20)) swprintf(t, 32, L"%.0f MB", b / 1048576.0);
    else if (b >= 1024) swprintf(t, 32, L"%.0f KB", b / 1024.0);
    else swprintf(t, 32, L"%llu bytes", (unsigned long long)b);
    return t;
}

uint32_t crc32(const void* data, size_t n, uint32_t crc) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    const uint8_t* p = (const uint8_t*)data;
    while (n--) crc = table[(crc ^ *p++) & 255] ^ (crc >> 8);
    return ~crc;
}

wstr sniffFs(const std::string& s) {
    if (s.size() < 512) return L"";
    if (!memcmp(&s[3], "-FVE-FS-", 8)) return L"BitLocker";
    if (!memcmp(&s[3], "NTFS    ", 8)) return L"NTFS";
    if (!memcmp(&s[3], "EXFAT   ", 8)) return L"exFAT";
    if (!memcmp(&s[3], "ReFS", 4)) return L"ReFS";
    if (!memcmp(&s[0x52], "FAT32   ", 8)) return L"FAT32";
    if (!memcmp(&s[0x36], "FAT1", 4) || !memcmp(&s[0x36], "FAT ", 4)) return L"FAT";
    if (!memcmp(&s[0], "XFSB", 4)) return L"XFS";
    if (!memcmp(&s[0], "LUKS\xBA\xBE", 6)) return L"LUKS";
    if (s.size() >= 1024 + 0x68 && (uint8_t)s[1024 + 0x38] == 0x53 && (uint8_t)s[1024 + 0x39] == 0xEF) {
        uint32_t compat, incompat;
        memcpy(&compat, &s[1024 + 0x5C], 4);
        memcpy(&incompat, &s[1024 + 0x60], 4);
        return incompat & 0x40 ? L"ext4" : compat & 0x4 ? L"ext3" : L"ext2";
    }
    if (s.size() >= 4096 && !memcmp(&s[4086], "SWAPSPACE2", 10)) return T(L"Linux swap");
    return L"";
}

wstr uuidText(const uint8_t* b) {
    wchar_t t[40];
    swprintf(t, 40, L"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10],
             b[11], b[12], b[13], b[14], b[15]);
    return t;
}

wstr fsUuidFrom(const wstr& fs, const std::string& s, const std::string& at64k) {
    auto u = [&](const std::string& src, size_t at) { return src.size() >= at + 16 ? uuidText((const uint8_t*)&src[at]) : wstr(); };
    wchar_t t[24];
    if (istartsWith(fs, L"ext")) return u(s, 1024 + 0x68);
    if (fs == L"XFS") return u(s, 32);
    if (fs == L"Linux swap") return u(s, 1024 + 0x0C);
    if (fs == L"btrfs") return u(at64k, 0x20);
    if (fs == L"LUKS" && s.size() >= 208) {
        std::string id(&s[168], 40);
        id.resize(strlen(id.c_str()));
        return toLower(utf8ToWide(id));
    }
    if (fs == L"FAT32" && s.size() >= 0x47) {  // Linux shows the volume serial as ABCD-1234
        swprintf(t, 24, L"%02x%02x-%02x%02x", (uint8_t)s[0x46], (uint8_t)s[0x45], (uint8_t)s[0x44], (uint8_t)s[0x43]);
        return t;
    }
    if (fs == L"FAT" && s.size() >= 0x2B) {
        swprintf(t, 24, L"%02x%02x-%02x%02x", (uint8_t)s[0x2A], (uint8_t)s[0x29], (uint8_t)s[0x28], (uint8_t)s[0x27]);
        return t;
    }
    if (fs == L"NTFS" && s.size() >= 0x50) {
        swprintf(t, 24, L"%02x%02x%02x%02x%02x%02x%02x%02x", (uint8_t)s[0x4F], (uint8_t)s[0x4E], (uint8_t)s[0x4D], (uint8_t)s[0x4C], (uint8_t)s[0x4B],
                 (uint8_t)s[0x4A], (uint8_t)s[0x49], (uint8_t)s[0x48]);
        return t;
    }
    return L"";
}

// ---------------------------------------------------------------- raw access
static HANDLE openDisk(int disk, bool write) {
    wchar_t p[40];
    swprintf(p, 40, L"\\\\.\\PhysicalDrive%d", disk);
    return CreateFileW(p, GENERIC_READ | (write ? GENERIC_WRITE : 0), FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
}

static uint32_t sectorOf(HANDLE h) {
    DISK_GEOMETRY geo{};
    DWORD got = 0;
    if (DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY, nullptr, 0, &geo, sizeof geo, &got, nullptr) && geo.BytesPerSector)
        return geo.BytesPerSector;
    return 512;
}

static bool readAt(HANDLE h, uint32_t sector, uint64_t offset, uint32_t length, std::string& out) {
    uint64_t start = offset / sector * sector;
    uint64_t end = (offset + length + sector - 1) / sector * sector;
    size_t n = (size_t)(end - start);
    void* buf = VirtualAlloc(nullptr, n, MEM_COMMIT, PAGE_READWRITE);
    if (!buf) return false;
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)start;
    DWORD got = 0;
    bool ok = SetFilePointerEx(h, li, nullptr, FILE_BEGIN) && ReadFile(h, buf, (DWORD)n, &got, nullptr) && got == n;
    if (ok) out.assign((char*)buf + (offset - start), length);
    VirtualFree(buf, 0, MEM_RELEASE);
    return ok;
}

ReadFn partitionReader(const Partition& p) {
    HANDLE h = openDisk(p.disk, false);
    if (h == INVALID_HANDLE_VALUE) return nullptr;
    uint32_t sector = sectorOf(h);
    std::shared_ptr<void> hold(h, [](void* x) { CloseHandle((HANDLE)x); });
    uint64_t base = p.offset, len = p.length;
    return [hold, sector, base, len](uint64_t off, uint32_t n, std::string& out) {
        if (off + n > len) return false;
        return readAt((HANDLE)hold.get(), sector, base + off, n, out);
    };
}

ReadFn diskReader(int disk, uint64_t* size) {
    HANDLE h = openDisk(disk, false);
    if (h == INVALID_HANDLE_VALUE) return nullptr;
    uint32_t sector = sectorOf(h);
    GET_LENGTH_INFORMATION gl{};
    DWORD n = 0;
    uint64_t len = DeviceIoControl(h, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0, &gl, sizeof gl, &n, nullptr) ? (uint64_t)gl.Length.QuadPart : 0;
    if (size) *size = len;
    std::shared_ptr<void> hold(h, [](void* x) { CloseHandle((HANDLE)x); });
    return [hold, sector, len](uint64_t off, uint32_t cnt, std::string& out) {
        if (len && off + cnt > len) return false;
        return readAt((HANDLE)hold.get(), sector, off, cnt, out);
    };
}

bool readDisk(int disk, uint64_t offset, uint32_t length, std::string& out) {
    HANDLE h = openDisk(disk, false);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = readAt(h, sectorOf(h), offset, length, out);
    CloseHandle(h);
    return ok;
}

bool writeDisk(int disk, uint64_t offset, const std::string& data, wstr& err) {
    toolLog(L"[disk] write " + std::to_wstring(data.size()) + T(L" bytes at ") + std::to_wstring(offset) + T(L" on disk ") + std::to_wstring(disk));
    HANDLE h = openDisk(disk, true);
    if (h == INVALID_HANDLE_VALUE) {
        err = T(L"The disk could not be opened for writing.");
        return false;
    }
    uint32_t sector = sectorOf(h);
    if (offset % sector || data.size() % sector) {
        CloseHandle(h);
        err = T(L"The saved copy does not match this disk's sector size.");
        return false;
    }
    void* buf = VirtualAlloc(nullptr, data.size(), MEM_COMMIT, PAGE_READWRITE);
    bool ok = false;
    if (buf) {
        memcpy(buf, data.data(), data.size());
        LARGE_INTEGER li;
        li.QuadPart = (LONGLONG)offset;
        DWORD wr = 0;
        ok = SetFilePointerEx(h, li, nullptr, FILE_BEGIN) && WriteFile(h, buf, (DWORD)data.size(), &wr, nullptr) && wr == data.size();
        if (!ok) err = T(L"Windows refused to write to the disk (error ") + std::to_wstring(GetLastError()) + L").";
        VirtualFree(buf, 0, MEM_RELEASE);
    }
    DWORD got = 0;
    DeviceIoControl(h, IOCTL_DISK_UPDATE_PROPERTIES, nullptr, 0, nullptr, 0, &got, nullptr);
    CloseHandle(h);
    return ok;
}

static bool getLayout(HANDLE h, std::vector<uint8_t>& buf) {
    buf.assign(sizeof(DRIVE_LAYOUT_INFORMATION_EX) + 255 * sizeof(PARTITION_INFORMATION_EX), 0);
    DWORD got = 0;
    return DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, nullptr, 0, buf.data(), (DWORD)buf.size(), &got, nullptr) != 0;
}

static bool setLayout(HANDLE h, std::vector<uint8_t>& buf, wstr& err) {
    auto* L = (DRIVE_LAYOUT_INFORMATION_EX*)buf.data();
    for (DWORD i = 0; i < L->PartitionCount; ++i) L->PartitionEntry[i].RewritePartition = TRUE;
    DWORD size = (DWORD)(offsetof(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry) + L->PartitionCount * sizeof(PARTITION_INFORMATION_EX));
    DWORD got = 0;
    if (!DeviceIoControl(h, IOCTL_DISK_SET_DRIVE_LAYOUT_EX, L, size, nullptr, 0, &got, nullptr)) {
        err = T(L"Windows refused to change the partition table (error ") + std::to_wstring(GetLastError()) + L").";
        return false;
    }
    DeviceIoControl(h, IOCTL_DISK_UPDATE_PROPERTIES, nullptr, 0, nullptr, 0, &got, nullptr);
    return true;
}

bool rewritePartitionTable(int disk, wstr& err) {
    toolLog(L"[disk] write the partition table of disk " + std::to_wstring(disk) + T(L" again"));
    HANDLE h = openDisk(disk, true);
    if (h == INVALID_HANDLE_VALUE) {
        err = T(L"The disk could not be opened for writing.");
        return false;
    }
    std::vector<uint8_t> buf;
    bool ok = getLayout(h, buf) && setLayout(h, buf, err);
    if (!ok && err.empty()) err = T(L"The partition table could not be read.");
    CloseHandle(h);
    return ok;
}

bool addPartitionEntry(int disk, uint64_t offset, uint64_t length, bool gptIfRaw, const GUID& gptType, uint8_t mbrType, const wstr& name, wstr& err) {
    toolLog(L"[disk] add a partition entry on disk " + std::to_wstring(disk) + L": offset " + std::to_wstring(offset) + L", length " + std::to_wstring(length));
    HANDLE h = openDisk(disk, true);
    if (h == INVALID_HANDLE_VALUE) {
        err = T(L"The disk could not be opened for writing.");
        return false;
    }
    std::vector<uint8_t> buf;
    bool ok = getLayout(h, buf);
    auto* L = (DRIVE_LAYOUT_INFORMATION_EX*)buf.data();
    DWORD got = 0;
    if (ok && L->PartitionStyle == PARTITION_STYLE_RAW) {
        // No partition table at all: write an empty one first (only the table sectors are written)
        CREATE_DISK cd{};
        cd.PartitionStyle = gptIfRaw ? PARTITION_STYLE_GPT : PARTITION_STYLE_MBR;
        if (gptIfRaw) {
            cd.Gpt.MaxPartitionCount = 128;
            for (int i = 0; i < 16; ++i) ((uint8_t*)&cd.Gpt.DiskId)[i] = (uint8_t)(GetTickCount() * (i + 7) + i * 31);
        } else cd.Mbr.Signature = GetTickCount() ^ 0x5A5A1234;
        if (!DeviceIoControl(h, IOCTL_DISK_CREATE_DISK, &cd, sizeof cd, nullptr, 0, &got, nullptr)) {
            err = T(L"A partition table could not be written to the disk (error ") + std::to_wstring(GetLastError()) + L").";
            CloseHandle(h);
            return false;
        }
        DeviceIoControl(h, IOCTL_DISK_UPDATE_PROPERTIES, nullptr, 0, nullptr, 0, &got, nullptr);
        ok = getLayout(h, buf);
        L = (DRIVE_LAYOUT_INFORMATION_EX*)buf.data();
    }
    if (!ok) {
        err = T(L"The partition table could not be read.");
        CloseHandle(h);
        return false;
    }
    PARTITION_INFORMATION_EX* slot = nullptr;
    if (L->PartitionStyle == PARTITION_STYLE_MBR) {
        if (L->PartitionCount < 4) L->PartitionCount = 4;
        for (DWORD i = 0; i < 4 && !slot; ++i)
            if (L->PartitionEntry[i].PartitionLength.QuadPart == 0) slot = &L->PartitionEntry[i];
        if (!slot) err = T(L"This disk already has 4 main partitions, the most an MBR disk can have. Delete or move one first.");
        if (offset + length > (2ull << 40)) {
            err = T(L"An MBR disk cannot hold partitions beyond 2 TB.");
            slot = nullptr;
        }
        if (slot) {
            memset(slot, 0, sizeof *slot);
            slot->PartitionStyle = PARTITION_STYLE_MBR;
            slot->Mbr.PartitionType = mbrType;
            slot->Mbr.RecognizedPartition = TRUE;
            slot->Mbr.HiddenSectors = (DWORD)(offset / 512);
        }
    } else if (L->PartitionStyle == PARTITION_STYLE_GPT) {
        if (L->PartitionCount >= 128) err = T(L"The partition table is full.");
        else {
            slot = &L->PartitionEntry[L->PartitionCount++];
            memset(slot, 0, sizeof *slot);
            slot->PartitionStyle = PARTITION_STYLE_GPT;
            slot->Gpt.PartitionType = gptType;
            typedef BOOLEAN(WINAPI * RandFn)(PVOID, ULONG);
            auto rnd = (RandFn)GetProcAddress(GetModuleHandleW(L"advapi32.dll"), "SystemFunction036");
            if (!rnd || !rnd(&slot->Gpt.PartitionId, sizeof(GUID)))
                for (int i = 0; i < 16; ++i) ((uint8_t*)&slot->Gpt.PartitionId)[i] = (uint8_t)(GetTickCount() * (i + 3) ^ (i * 77));
            slot->Gpt.PartitionId.Data3 = (slot->Gpt.PartitionId.Data3 & 0x0FFF) | 0x4000;
            slot->Gpt.PartitionId.Data4[0] = (slot->Gpt.PartitionId.Data4[0] & 0x3F) | 0x80;
            wcsncpy_s(slot->Gpt.Name, name.c_str(), _TRUNCATE);
        }
    }
    if (slot) {
        slot->StartingOffset.QuadPart = (LONGLONG)offset;
        slot->PartitionLength.QuadPart = (LONGLONG)length;
        ok = setLayout(h, buf, err);
    } else ok = false;
    CloseHandle(h);
    return ok;
}

// ---------------------------------------------------------------- raw access for moving partitions
HANDLE openDiskHandle(int disk, bool write) { return openDisk(disk, write); }
uint32_t diskSectorSize(HANDLE h) { return sectorOf(h); }
bool readDiskAt(HANDLE h, uint64_t offset, uint32_t length, std::string& out) { return readAt(h, sectorOf(h), offset, length, out); }

bool writeDiskAt(HANDLE h, uint64_t offset, const std::string& data) {
    uint32_t sector = sectorOf(h);
    if (offset % sector || data.size() % sector) return false;
    void* buf = VirtualAlloc(nullptr, data.size(), MEM_COMMIT, PAGE_READWRITE);
    if (!buf) return false;
    memcpy(buf, data.data(), data.size());
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)offset;
    DWORD wr = 0;
    bool ok = SetFilePointerEx(h, li, nullptr, FILE_BEGIN) && WriteFile(h, buf, (DWORD)data.size(), &wr, nullptr) && wr == data.size();
    VirtualFree(buf, 0, MEM_RELEASE);
    return ok;
}

bool setPartitionOffset(int disk, uint64_t oldOffset, uint64_t newOffset, wstr& err) {
    toolLog(L"[disk] partition on disk " + std::to_wstring(disk) + L" now starts at " + std::to_wstring(newOffset) + L" (was " + std::to_wstring(oldOffset) + L")");
    HANDLE h = openDisk(disk, true);
    if (h == INVALID_HANDLE_VALUE) {
        err = T(L"The disk could not be opened for writing.");
        return false;
    }
    std::vector<uint8_t> buf;
    bool ok = getLayout(h, buf);
    auto* L = (DRIVE_LAYOUT_INFORMATION_EX*)buf.data();
    bool found = false;
    if (ok)
        for (DWORD i = 0; i < L->PartitionCount; ++i)
            if ((uint64_t)L->PartitionEntry[i].StartingOffset.QuadPart == oldOffset && L->PartitionEntry[i].PartitionLength.QuadPart) {
                L->PartitionEntry[i].StartingOffset.QuadPart = (LONGLONG)newOffset;
                if (L->PartitionStyle == PARTITION_STYLE_MBR) L->PartitionEntry[i].Mbr.HiddenSectors = (DWORD)(newOffset / sectorOf(h));
                found = true;
            }
    if (!found) err = T(L"The partition was not found in the partition table.");
    ok = ok && found && setLayout(h, buf, err);
    CloseHandle(h);
    return ok;
}

// Takes a volume away from Windows while its sectors are moved (kept until the handle is closed).
HANDLE lockVolume(const wstr& volume, wstr& err) {
    wstr v = volume;
    while (!v.empty() && v.back() == L'\\') v.pop_back();
    HANDLE h = CreateFileW(v.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        err = T(L"The partition could not be opened (error ") + std::to_wstring(GetLastError()) + L").";
        return h;
    }
    DWORD n = 0;
    if (!DeviceIoControl(h, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &n, nullptr)) {
        err = T(L"The partition is in use (a program or a window has files open on it). Close everything that uses it and try again.");
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    DeviceIoControl(h, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0, &n, nullptr);
    return h;
}

bool setActivePartition(int disk, int number, wstr& err) {
    toolLog(L"[disk] mark partition " + std::to_wstring(number) + T(L" of disk ") + std::to_wstring(disk) + L" active");
    HANDLE h = openDisk(disk, true);
    if (h == INVALID_HANDLE_VALUE) {
        err = T(L"The disk could not be opened for writing.");
        return false;
    }
    std::vector<uint8_t> buf;
    bool ok = getLayout(h, buf);
    auto* L = (DRIVE_LAYOUT_INFORMATION_EX*)buf.data();
    if (ok && L->PartitionStyle != PARTITION_STYLE_MBR) {
        err = T(L"Only MBR disks have an active partition.");
        ok = false;
    }
    if (ok) {
        bool found = false;
        for (DWORD i = 0; i < L->PartitionCount; ++i) {
            auto& e = L->PartitionEntry[i];
            if (!e.PartitionNumber) continue;
            e.Mbr.BootIndicator = (int)e.PartitionNumber == number;
            found |= (int)e.PartitionNumber == number;
        }
        if (!found) {
            err = T(L"That partition was not found on the disk.");
            ok = false;
        } else ok = setLayout(h, buf, err);
    }
    CloseHandle(h);
    return ok;
}

// ---------------------------------------------------------------- drive letters
wstr freeLetter() {
    DWORD used = GetLogicalDrives();
    for (wchar_t c = L'S'; c <= L'Z'; ++c)
        if (!(used & (1u << (c - L'A')))) return wstr(1, c) + L":";
    for (wchar_t c = L'D'; c < L'S'; ++c)
        if (!(used & (1u << (c - L'A')))) return wstr(1, c) + L":";
    return L"";
}

bool mountAt(const wstr& volume, const wstr& letter, wstr& err) {
    if (!SetVolumeMountPointW((letter + L"\\").c_str(), volume.c_str())) {
        err = T(L"A drive letter could not be given to the partition (error ") + std::to_wstring(GetLastError()) + L").";
        return false;
    }
    return true;
}

void unmount(const wstr& letter) {
    if (!letter.empty()) DeleteVolumeMountPointW((letter + L"\\").c_str());
}

// ---------------------------------------------------------------- privileges
bool enablePrivilege(const wchar_t* name) {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) return false;
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    bool ok = LookupPrivilegeValueW(nullptr, name, &tp.Privileges[0].Luid) && AdjustTokenPrivileges(tok, FALSE, &tp, 0, nullptr, nullptr) &&
              GetLastError() == ERROR_SUCCESS;
    CloseHandle(tok);
    return ok;
}

void enableAllPrivileges() {
    enablePrivilege(L"SeBackupPrivilege");
    enablePrivilege(L"SeRestorePrivilege");
    enablePrivilege(L"SeSystemEnvironmentPrivilege");
    enablePrivilege(L"SeTakeOwnershipPrivilege");
}

// ---------------------------------------------------------------- firmware
static bool getVar(const wchar_t* name, std::vector<uint8_t>& out) {
    out.assign(32768, 0);
    DWORD n = GetFirmwareEnvironmentVariableW(name, kGlobalVar, out.data(), (DWORD)out.size());
    out.resize(n);
    return n != 0;
}

static std::function<void(const wstr&)> g_logger;
void setToolLogger(std::function<void(const wstr&)> fn) { g_logger = std::move(fn); }
void toolLog(const wstr& line) {
    if (!g_logger) return;
    wstr s = line;
    size_t k = s.find(L"-RecoveryPassword ");  // never show a BitLocker key
    if (k != wstr::npos)
        for (size_t i = k + 18; i < s.size() && (iswdigit(s[i]) || s[i] == L'-'); ++i)
            if (iswdigit(s[i])) s[i] = L'*';
    // ...wherever it appears (tool output too): 8 groups of 6 digits joined by dashes
    for (size_t i = 0; i + 55 <= s.size(); ++i) {
        bool key = true;
        for (size_t j = 0; j < 55 && key; ++j) key = (j % 7 == 6) ? s[i + j] == L'-' : (s[i + j] >= L'0' && s[i + j] <= L'9');
        if (key) {
            for (size_t j = 0; j < 55; ++j)
                if (j % 7 != 6) s[i + j] = L'*';
            i += 54;
        }
    }
    g_logger(s);
}

bool readFwVar(const wstr& name, std::string& out) {
    enablePrivilege(L"SeSystemEnvironmentPrivilege");
    std::vector<uint8_t> v;
    if (!getVar(name.c_str(), v)) return false;
    out.assign((const char*)v.data(), v.size());
    return true;
}

bool writeFwVar(const wstr& name, const std::string& data) {
    enablePrivilege(L"SeSystemEnvironmentPrivilege");
    toolLog(data.empty() ? L"[firmware] delete variable " + name : L"[firmware] write variable " + name + L" (" + std::to_wstring(data.size()) + T(L" bytes)"));
    return SetFirmwareEnvironmentVariableW(name.c_str(), kGlobalVar, data.empty() ? nullptr : (PVOID)data.data(), (DWORD)data.size()) != 0;
}

namespace {
// Runs a program and waits a little for it; true when it started and did not fail at once.
bool runQuiet(const wstr& cmd, DWORD* exitCode) {
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> b(cmd.begin(), cmd.end());
    b.push_back(0);
    if (!CreateProcessW(nullptr, b.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
    DWORD code = STILL_ACTIVE;
    if (WaitForSingleObject(pi.hProcess, 15000) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (exitCode) *exitCode = code;
    return code == 0 || code == STILL_ACTIVE;
}

// Shuts down or restarts, trying the ways that work in Windows, Windows PE and WinRE in turn.
bool powerOff(bool restart, wstr& err) {
    enablePrivilege(L"SeShutdownPrivilege");
    enablePrivilege(L"SeRemoteShutdownPrivilege");
    wchar_t sys[MAX_PATH] = {};
    GetSystemDirectoryW(sys, MAX_PATH);
    wstr what = restart ? L"restart" : L"shut down";
    DWORD code = 0;
    if (g_sys.winPE) {
        // Windows PE / WinRE: the usual calls report success but do nothing; wpeutil does it.
        wstr cmd = L"\"" + wstr(sys) + L"\\wpeutil.exe\" " + (restart ? L"reboot" : L"shutdown");
        toolLog(L"> " + cmd);
        if (runQuiet(cmd, &code)) return true;
        toolLog(L"  wpeutil failed (exit code " + std::to_wstring(code) + L")");
    }
    // The way shutdown.exe does it: other programs are closed (they were warned), this one too.
    const DWORD kForceOthers = 0x00000001, kPowerOff = 0x00000008, kRestart = 0x00000004, kGraceOverride = 0x00000020;
    DWORD flags = kForceOthers | kGraceOverride | (restart ? kRestart : kPowerOff);
    DWORD reason = SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER | SHTDN_REASON_FLAG_PLANNED;
    typedef DWORD(WINAPI * InitiateShutdownFn)(LPWSTR, LPWSTR, DWORD, DWORD, DWORD);
    auto initiate = (InitiateShutdownFn)GetProcAddress(GetModuleHandleW(L"advapi32.dll"), "InitiateShutdownW");
    if (initiate) {
        DWORD e = initiate(nullptr, nullptr, 0, flags, reason);
        toolLog(L"[power] InitiateShutdown (" + what + L"): " + (e == ERROR_SUCCESS ? wstr(L"accepted") : L"error " + std::to_wstring(e)));
        if (e == ERROR_SUCCESS) return true;
        if (e == ERROR_SHUTDOWN_IN_PROGRESS) return true;
    }
    if (InitiateSystemShutdownExW(nullptr, nullptr, 0, TRUE, restart ? TRUE : FALSE, reason)) {
        toolLog(L"[power] InitiateSystemShutdownEx (" + what + L"): accepted");
        return true;
    }
    toolLog(L"[power] InitiateSystemShutdownEx: error " + std::to_wstring(GetLastError()));
    wstr cmd = L"\"" + wstr(sys) + L"\\shutdown.exe\" " + (restart ? L"/r" : L"/s") + L" /f /t 0";
    toolLog(L"> " + cmd);
    if (runQuiet(cmd, &code)) return true;
    if (ExitWindowsEx((restart ? EWX_REBOOT : EWX_POWEROFF) | EWX_FORCE, reason)) {
        toolLog(L"[power] ExitWindowsEx: accepted");
        return true;
    }
    err = (restart ? T(L"The PC could not be restarted (error ") : T(L"The PC could not be shut down (error ")) + std::to_wstring(GetLastError()) +
          T(L"). Do it by hand; the console (Settings) shows every way that was tried.");
    return false;
}
}  // namespace

bool restartPc(wstr& err) { return powerOff(true, err); }
bool shutdownPc(wstr& err) { return powerOff(false, err); }

bool protectedFileExists(const wstr& path) {
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    enablePrivilege(L"SeBackupPrivilege");
    HANDLE h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

bool copyProtectedFile(const wstr& from, const wstr& to, wstr& err) {
    if (CopyFileW(from.c_str(), to.c_str(), FALSE)) return true;
    enablePrivilege(L"SeBackupPrivilege");
    HANDLE in = CreateFileW(from.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (in == INVALID_HANDLE_VALUE) {
        err = T(L"Could not read ") + from + L" (error " + std::to_wstring(GetLastError()) + L").";
        return false;
    }
    HANDLE out = CreateFileW(to.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    bool ok = out != INVALID_HANDLE_VALUE;
    std::vector<char> buf(4 << 20);
    while (ok) {
        DWORD rd = 0, wr = 0;
        if (!ReadFile(in, buf.data(), (DWORD)buf.size(), &rd, nullptr)) ok = false;
        else if (!rd) break;
        else ok = WriteFile(out, buf.data(), rd, &wr, nullptr) && wr == rd;
    }
    if (!ok) err = T(L"Could not copy ") + from + L" (error " + std::to_wstring(GetLastError()) + L").";
    CloseHandle(in);
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (!ok) DeleteFileW(to.c_str());
    return ok;
}

HMODULE systemDll(const wchar_t* name) {
    wchar_t dir[MAX_PATH] = {};
    UINT n = GetSystemDirectoryW(dir, MAX_PATH);
    if (!n || n >= MAX_PATH) return nullptr;
    return LoadLibraryW((wstr(dir) + L"\\" + name).c_str());
}

namespace {
typedef BOOLEAN(WINAPI* SetSuspendFn)(BOOLEAN, BOOLEAN, BOOLEAN);
typedef BOOLEAN(WINAPI* PwrCapsFn)(PSYSTEM_POWER_CAPABILITIES);
HMODULE powrprof() {
    static HMODULE m = systemDll(L"powrprof.dll");
    return m;
}
}  // namespace

bool canHibernate() {
    if (g_sys.winPE) return false;
    auto caps = powrprof() ? (PwrCapsFn)GetProcAddress(powrprof(), "GetPwrCapabilities") : nullptr;
    SYSTEM_POWER_CAPABILITIES c{};
    return caps && caps(&c) && c.SystemS4 && c.HiberFilePresent;
}

bool hibernatePc(wstr& err) {
    enablePrivilege(L"SeShutdownPrivilege");
    toolLog(L"[power] hibernate");
    auto fn = powrprof() ? (SetSuspendFn)GetProcAddress(powrprof(), "SetSuspendState") : nullptr;
    if (fn && fn(TRUE, FALSE, FALSE)) return true;
    err = T(L"The PC could not go into hibernation (error ") + std::to_wstring(GetLastError()) + T(L").");
    return false;
}

bool restartIntoFirmware(wstr& err) {
    toolLog(L"[firmware] set OsIndications: open the firmware settings at the next start, then restart");
    enablePrivilege(L"SeSystemEnvironmentPrivilege");
    std::vector<uint8_t> v;
    uint64_t ind = 0;
    if (getVar(L"OsIndications", v) && v.size() >= 8) memcpy(&ind, v.data(), 8);
    ind |= 1;  // EFI_OS_INDICATIONS_BOOT_TO_FW_UI
    // NV + BS + RT attributes: plain SetFirmwareEnvironmentVariable uses exactly these
    if (!SetFirmwareEnvironmentVariableW(L"OsIndications", kGlobalVar, &ind, sizeof ind)) {
        err = T(L"The PC did not accept the request (error ") + std::to_wstring(GetLastError()) + T(L"). Open the firmware settings with its key while the PC starts instead.");
        return false;
    }
    return restartPc(err);
}

Firmware readFirmware() {
    Firmware f;
    typedef BOOL(WINAPI * GetFirmwareTypeFn)(int*);
    auto gft = (GetFirmwareTypeFn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetFirmwareType");  // Windows 8+
    int type = 0;
    if (gft && gft(&type)) f.uefi = type == 2;
    else {
        SetLastError(0);
        GetFirmwareEnvironmentVariableW(L"", L"{00000000-0000-0000-0000-000000000000}", nullptr, 0);
        f.uefi = GetLastError() != ERROR_INVALID_FUNCTION;
    }
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        DWORD v = 0, n = sizeof v;
        if (RegQueryValueExW(k, L"UEFISecureBootEnabled", nullptr, nullptr, (BYTE*)&v, &n) == ERROR_SUCCESS) {
            f.secureBoot = v != 0;
            f.secureBootKnown = true;
        }
        RegCloseKey(k);
    }
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        wchar_t b[128];
        DWORD n = sizeof b;
        if (RegQueryValueExW(k, L"SystemManufacturer", nullptr, nullptr, (BYTE*)b, &n) == ERROR_SUCCESS) f.maker = b;
        n = sizeof b;
        if (RegQueryValueExW(k, L"SystemProductName", nullptr, nullptr, (BYTE*)b, &n) == ERROR_SUCCESS) f.model = b;
        RegCloseKey(k);
    }
    if (!f.uefi) return f;
    enablePrivilege(L"SeSystemEnvironmentPrivilege");
    {
        std::vector<uint8_t> v;
        if (getVar(L"SecureBoot", v) && !v.empty()) {
            f.secureBoot = v[0] == 1;
            f.secureBootKnown = true;
        }
        if (getVar(L"SetupMode", v) && !v.empty()) f.setupMode = v[0] == 1;
        if (getVar(L"OsIndicationsSupported", v) && !v.empty()) f.canOpenSettings = (v[0] & 1) != 0;
    }
    std::vector<uint8_t> ord;
    if (!getVar(L"BootOrder", ord)) return f;
    f.varsReadable = true;
    for (size_t i = 0; i + 1 < ord.size(); i += 2) f.order.push_back(ord[i] | (ord[i + 1] << 8));
    // Look at the entries in the boot order, plus the usual first few numbers.
    std::vector<int> nums = f.order;
    for (int n = 0; n < 16; ++n)
        if (std::find(nums.begin(), nums.end(), n) == nums.end()) nums.push_back(n);
    for (int n : nums) {
        wchar_t name[16];
        swprintf(name, 16, L"Boot%04X", n);
        std::vector<uint8_t> o;
        if (!getVar(name, o) || o.size() < 8) continue;
        FirmwareEntry e;
        e.num = n;
        e.inOrder = std::find(f.order.begin(), f.order.end(), n) != f.order.end();
        size_t p = 6;
        while (p + 1 < o.size() && (o[p] || o[p + 1])) {
            e.description += (wchar_t)(o[p] | (o[p + 1] << 8));
            p += 2;
        }
        p += 2;
        uint16_t listLen = (uint16_t)(o[4] | (o[5] << 8));
        size_t end = std::min(o.size(), p + listLen);
        while (p + 4 <= end) {
            size_t len = o[p + 2] | (o[p + 3] << 8);
            if (len < 4 || p + len > end || o[p] == 0x7F) break;
            if (o[p] == 4 && o[p + 1] == 1 && len >= 42 && o[p + 41] == 2) {
                memcpy(&e.part, &o[p + 24], 16);
                e.hasPart = true;
            } else if (o[p] == 4 && o[p + 1] == 4) {
                for (size_t q = p + 4; q + 1 < p + len; q += 2) {
                    wchar_t c = (wchar_t)(o[q] | (o[q + 1] << 8));
                    if (!c) break;
                    e.path += c;
                }
            }
            p += len;
        }
        f.entries.push_back(e);
    }
    return f;
}

// ---------------------------------------------------------------- reading the machine
static wstr trimRight(wstr s) {
    while (!s.empty() && (s.back() == L' ' || s.back() == 0)) s.pop_back();
    return s;
}

static void deviceInfo(HANDLE h, Disk& d) {
    STORAGE_PROPERTY_QUERY q{};
    q.PropertyId = StorageDeviceProperty;
    q.QueryType = PropertyStandardQuery;
    std::vector<uint8_t> buf(4096, 0);
    DWORD got = 0;
    if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof q, buf.data(), (DWORD)buf.size(), &got, nullptr)) return;
    auto* dd = (STORAGE_DEVICE_DESCRIPTOR*)buf.data();
    auto str = [&](DWORD off) -> wstr {
        if (!off || off >= got) return L"";
        std::string s((const char*)buf.data() + off);
        while (!s.empty() && s.back() == ' ') s.pop_back();
        size_t a = s.find_first_not_of(' ');
        return a == std::string::npos ? wstr() : utf8ToWide(s.substr(a));
    };
    wstr vendor = str(dd->VendorIdOffset), product = str(dd->ProductIdOffset);
    d.model = vendor.empty() || icontains(product, vendor) ? product : vendor + L" " + product;
    d.removable = dd->RemovableMedia != 0 || dd->BusType == BusTypeUsb || dd->BusType == BusTypeSd;
    d.removableMedia = dd->RemovableMedia != 0;
    d.busType = (int)dd->BusType;
    switch (dd->BusType) {
        case BusTypeUsb: d.bus = L"USB"; break;
        case BusTypeSata: d.bus = L"SATA"; break;
        case BusTypeNvme: d.bus = L"NVMe"; break;
        case BusTypeSas: d.bus = L"SAS"; break;
        case BusTypeRAID: d.bus = L"RAID"; break;
        case BusTypeSd: d.bus = L"SD"; break;
        case BusTypeiScsi: d.bus = L"iSCSI"; break;
        case BusTypeFibre: d.bus = L"Fibre Channel"; break;
        case BusTypeSpaces: d.bus = L"Storage Spaces"; break;
        case BusTypeVirtual:
        case BusTypeFileBackedVirtual: d.bus = L"Virtual"; break;
        default: break;
    }
    readDiskKind(h, d);
}

static int roleOf(const Partition& p) {
    if (p.gpt) {
        if (IsEqualGUID(p.type, kEsp)) return R_ESP;
        if (IsEqualGUID(p.type, kMsr)) return R_MSR;
        if (IsEqualGUID(p.type, kRecovery)) return R_RECOVERY;
        if (IsEqualGUID(p.type, kLdmMeta) || IsEqualGUID(p.type, kLdmData)) return R_DYNAMIC;
        for (const GUID* g : {&kLinuxFs, &kLinuxRootX64, &kLinuxRootArm64, &kLinuxRootX86, &kLinuxBoot, &kLinuxHome, &kLinuxSwap, &kLinuxLvm, &kLinuxLuks, &kLinuxRaid})
            if (IsEqualGUID(p.type, *g)) return R_LINUX;
        return R_DATA;
    }
    switch (p.mbrType) {
        case 0: return R_EMPTY;
        case 0x05: case 0x0F: case 0x85: return R_EXTENDED;
        case 0xEF: return R_ESP;
        case 0x27: return R_RECOVERY;
        case 0x42: return R_DYNAMIC;
        case 0x83: case 0x82: case 0x8E: return R_LINUX;
    }
    return p.active ? R_SYSTEM : R_DATA;
}

static void checkGpt(HANDLE h, Disk& d) {
    std::string hdr;
    if (!readAt(h, d.sector, d.sector, d.sector, hdr) || hdr.size() < 92 || memcmp(hdr.data(), "EFI PART", 8)) return;
    uint32_t hsize, hcrc;
    memcpy(&hsize, &hdr[12], 4);
    memcpy(&hcrc, &hdr[16], 4);
    if (hsize < 92 || hsize > d.sector) return;
    std::string z = hdr.substr(0, hsize);
    memset(&z[16], 0, 4);
    d.gptPrimaryOk = crc32(z.data(), z.size()) == hcrc;
    uint64_t alt = 0, entriesLba = 0;
    uint32_t count = 0, esize = 0, ecrc = 0;
    memcpy(&alt, &hdr[32], 8);
    memcpy(&entriesLba, &hdr[72], 8);
    memcpy(&count, &hdr[80], 4);
    memcpy(&esize, &hdr[84], 4);
    memcpy(&ecrc, &hdr[88], 4);
    if (count && esize && (uint64_t)count * esize <= (1u << 20)) {
        std::string ent;
        if (readAt(h, d.sector, entriesLba * d.sector, count * esize, ent) && crc32(ent.data(), ent.size()) != ecrc) d.gptPrimaryOk = false;
    }
    uint64_t last = d.size / d.sector - 1;
    d.gptBackupMisplaced = alt != last;
    std::string bk;
    if (alt && alt <= last && readAt(h, d.sector, alt * d.sector, d.sector, bk) && !memcmp(bk.data(), "EFI PART", 8)) {
        uint32_t bsize, bcrc;
        memcpy(&bsize, &bk[12], 4);
        memcpy(&bcrc, &bk[16], 4);
        if (bsize >= 92 && bsize <= d.sector) {
            std::string zb = bk.substr(0, bsize);
            memset(&zb[16], 0, 4);
            d.gptBackupOk = crc32(zb.data(), zb.size()) == bcrc;
        }
    }
}

static void readVolumes(Machine& m) {
    wchar_t name[MAX_PATH];
    HANDLE find = FindFirstVolumeW(name, MAX_PATH);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        wstr path = name;
        if (path.size() < 5 || path.back() != L'\\') continue;
        wstr device = path.substr(0, path.size() - 1);
        HANDLE h = CreateFileW(device.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        std::vector<uint8_t> buf(sizeof(VOLUME_DISK_EXTENTS) + 8 * sizeof(DISK_EXTENT), 0);
        DWORD got = 0;
        BOOL ok = DeviceIoControl(h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, buf.data(), (DWORD)buf.size(), &got, nullptr);
        CloseHandle(h);
        if (!ok) continue;
        auto* ext = (VOLUME_DISK_EXTENTS*)buf.data();
        if (ext->NumberOfDiskExtents < 1) continue;
        for (Partition& p : m.parts) {
            if (p.disk != (int)ext->Extents[0].DiskNumber || p.offset != (uint64_t)ext->Extents[0].StartingOffset.QuadPart) continue;
            p.volume = path;
            wchar_t names[512] = {};
            DWORD len = 0;
            if (GetVolumePathNamesForVolumeNameW(path.c_str(), names, 512, &len))
                for (const wchar_t* n = names; *n; n += wcslen(n) + 1)
                    if (wcslen(n) == 3 && n[1] == L':') {
                        p.letter = wstr(n, 2);
                        break;
                    }
            p.root = p.letter.empty() ? path : p.letter + L"\\";
            wchar_t label[MAX_PATH] = {}, fs[64] = {};
            UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS);
            if (GetVolumeInformationW(path.c_str(), label, MAX_PATH, nullptr, nullptr, nullptr, fs, 64)) {
                p.fs = fs;
                p.label = label;
                p.readable = true;
                ULARGE_INTEGER avail{}, total{};
                if (GetDiskFreeSpaceExW(path.c_str(), &avail, &total, nullptr)) {
                    p.freeBytes = avail.QuadPart;
                    p.totalBytes = total.QuadPart;
                }
            } else {
                DWORD e = GetLastError();
                p.locked = p.bitlocker || e == 0x80310000 /* FVE_E_LOCKED_VOLUME */ || e == ERROR_NOT_READY;
            }
            SetErrorMode(oldMode);
            HANDLE vh = CreateFileW(device.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (vh != INVALID_HANDLE_VALUE) {
                ULONG dirty = 0;
                if (p.readable && DeviceIoControl(vh, FSCTL_IS_VOLUME_DIRTY, nullptr, 0, &dirty, sizeof dirty, &got, nullptr))
                    p.dirty = (dirty & VOLUME_IS_DIRTY) != 0;
                CloseHandle(vh);
            }
            if (!p.readable && !p.bitlocker) p.root.clear();
        }
    } while (FindNextVolumeW(find, name, MAX_PATH));
    FindVolumeClose(find);
}

Machine readMachine() {
    Machine m;
    m.fw = readFirmware();
    m.others = readOtherDrives();
    int misses = 0;
    for (int n = 0; n < 64 && misses < 8; ++n) {
        HANDLE h = openDisk(n, false);
        if (h == INVALID_HANDLE_VALUE) {
            ++misses;
            continue;
        }
        misses = 0;
        Disk d;
        d.number = n;
        d.sector = sectorOf(h);
        DWORD got = 0;
        GET_LENGTH_INFORMATION len{};
        if (DeviceIoControl(h, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0, &len, sizeof len, &got, nullptr)) d.size = (uint64_t)len.Length.QuadPart;
        deviceInfo(h, d);
        if (!d.size) {  // card readers and empty drives
            CloseHandle(h);
            continue;
        }
        std::string s0;
        d.readable = readAt(h, d.sector, 0, d.sector, s0);
        if (d.readable) {
            d.sig55aa = (uint8_t)s0[510] == 0x55 && (uint8_t)s0[511] == 0xAA;
            for (int i = 0; i < 440 && !d.bootCode; ++i) d.bootCode = s0[i] != 0;
            d.grubMbr = s0.substr(0, 440).find("GRUB") != std::string::npos;
        }
        std::vector<uint8_t> lay;
        if (getLayout(h, lay)) {
            auto* L = (DRIVE_LAYOUT_INFORMATION_EX*)lay.data();
            if (L->PartitionStyle == PARTITION_STYLE_MBR && d.sig55aa) {
                d.style = 1;
                d.signature = L->Mbr.Signature;
            } else if (L->PartitionStyle == PARTITION_STYLE_GPT) {
                d.style = 2;
                d.guid = L->Gpt.DiskId;
                checkGpt(h, d);
            }
            for (DWORD i = 0; i < L->PartitionCount && d.style; ++i) {
                const PARTITION_INFORMATION_EX& e = L->PartitionEntry[i];
                if (!e.PartitionLength.QuadPart) continue;
                if (e.PartitionStyle == PARTITION_STYLE_MBR && e.Mbr.PartitionType == 0) continue;
                Partition p;
                p.disk = n;
                p.number = (int)e.PartitionNumber;
                p.offset = (uint64_t)e.StartingOffset.QuadPart;
                p.length = (uint64_t)e.PartitionLength.QuadPart;
                p.gpt = e.PartitionStyle == PARTITION_STYLE_GPT;
                if (p.gpt) {
                    p.type = e.Gpt.PartitionType;
                    p.id = e.Gpt.PartitionId;
                    p.gptAttrs = e.Gpt.Attributes;
                    p.gptName = trimRight(wstr(e.Gpt.Name, 36));
                } else {
                    p.mbrType = e.Mbr.PartitionType;
                    p.active = e.Mbr.BootIndicator != 0;
                }
                p.role = roleOf(p);
                p.pastEnd = p.offset + p.length > d.size;
                if (p.role != R_EXTENDED && p.role != R_MSR) {
                    std::string first;
                    uint32_t n4 = (uint32_t)std::max<uint64_t>(d.sector, std::min<uint64_t>(4096, p.length) / d.sector * d.sector);
                    if (!p.pastEnd && readAt(h, d.sector, p.offset, n4, first)) {
                        if (first.size() < 4096) first.resize(4096, '\0');
                        p.rawFs = sniffFs(first);
                        p.bitlocker = p.rawFs == L"BitLocker";
                        std::string b64;
                        if (p.rawFs.empty() && p.length > 0x20000 && readAt(h, d.sector, p.offset + 0x10000, 4096, b64) && !memcmp(&b64[0x40], "_BHRfS_M", 8))
                            p.rawFs = L"btrfs";
                        p.fsUuid = fsUuidFrom(p.rawFs, first, b64);
                    }
                    p.linuxRoot = p.gpt && (IsEqualGUID(p.type, kLinuxRootX64) || IsEqualGUID(p.type, kLinuxRootArm64) || IsEqualGUID(p.type, kLinuxRootX86));
                }
                wchar_t key[96];
                swprintf(key, 96, L"@%llu", (unsigned long long)p.offset);
                d.parts.push_back((int)m.parts.size());
                m.parts.push_back(p);
                m.parts.back().key = d.id() + key;
            }
        }
        CloseHandle(h);
        // Gaps, overlaps and partitions that run past the end
        std::vector<int> idx = d.parts;
        std::sort(idx.begin(), idx.end(), [&](int a, int b) { return m.parts[a].offset < m.parts[b].offset; });
        uint64_t cursor = d.style == 2 ? 34ull * d.sector : d.sector;
        for (int i : idx) {
            Partition& p = m.parts[i];
            if (p.role == R_EXTENDED) continue;
            if (p.offset > cursor) {
                d.largestGap = std::max(d.largestGap, p.offset - cursor);
                if (p.offset - cursor >= (1ull << 20)) d.gaps.push_back({cursor, p.offset - cursor});
            }
            if (p.offset < cursor && cursor > (d.style == 2 ? 34ull * d.sector : d.sector)) p.overlaps = true;
            cursor = std::max(cursor, p.offset + p.length);
        }
        uint64_t usableEnd = d.style == 2 ? d.size - 33ull * d.sector : d.size;
        if (usableEnd > cursor) {
            d.largestGap = std::max(d.largestGap, usableEnd - cursor);
            if (usableEnd - cursor >= (1ull << 20)) d.gaps.push_back({cursor, usableEnd - cursor});
        }
        m.disks.push_back(d);
    }
    readVolumes(m);
    // Plain-language problems per disk
    for (Disk& d : m.disks) {
        if (!d.readable) d.problems.push_back(T(L"The disk cannot be read. It may be failing or badly connected."));
        else if (d.style == 0) d.problems.push_back(T(L"The disk has no partition table. It is empty, brand new, or its table was wiped."));
        if (d.style == 1 && !d.bootCode && !d.removable)
            d.problems.push_back(T(L"The disk's start-up code (in the first sector) is empty, so an older PC cannot start from it."));
        if (d.style == 2 && !d.gptPrimaryOk) d.problems.push_back(T(L"The main copy of the partition table is damaged."));
        if (d.style == 2 && !d.gptBackupOk) d.problems.push_back(T(L"The backup copy of the partition table is missing or damaged."));
        else if (d.style == 2 && d.gptBackupMisplaced)
            d.problems.push_back(T(L"The backup copy of the partition table is not at the end of the disk. The disk was probably copied to a bigger one."));
        for (int i : d.parts) {
            Partition& p = m.parts[i];
            if (p.pastEnd) d.problems.push_back(p.where() + T(L" runs past the end of the disk, so part of it is missing. The disk was probably copied to a smaller one."));
            if (p.overlaps) d.problems.push_back(p.where() + T(L" overlaps another partition. One of them will be damaged when written to."));
            if (p.role == R_DATA && !p.readable && !p.bitlocker && p.rawFs.empty())
                d.problems.push_back(p.where() + T(L" has no readable file system. It is empty, or its file system is damaged."));
            if (p.locked) d.problems.push_back(p.where() + T(L" is locked by BitLocker. It needs its recovery key before it can be checked."));
            if (p.dirty) d.problems.push_back((p.letter.empty() ? p.where() : p.letter) + T(L" is marked as needing a disk check."));
            if (p.role == R_DYNAMIC) d.problems.push_back(T(L"This is a dynamic disk. Windows can start from it, but its parts can only be checked roughly here."));
        }
    }
    return m;
}

}  // namespace bd

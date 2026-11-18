// What kind of storage each disk is, for the groups under Disks and partitions:
// fixed disks (hard disk, SATA SSD, NVMe SSD, hybrid; 2.5", 3.5", M.2 when the drive tells),
// removable devices (USB disks and sticks, memory cards, floppy / Zip / Jaz),
// optical drives (CD, DVD, Blu-ray), and enterprise and virtual storage (SAS, RAID, iSCSI,
// Fibre Channel, Storage Spaces, virtual disks, network drives).
#include "Disk.h"
#include "Lang.h"
#include <winioctl.h>

namespace bd {

const wchar_t* diskGroupName(int g) {
    switch (g) {
        case DG_FIXED: return N(L"Fixed disks");
        case DG_REMOVABLE: return N(L"Removable devices");
        case DG_OPTICAL: return N(L"Optical drives");
        default: return N(L"Enterprise and virtual storage");
    }
}

// Pure: from what the drive reported. rpm: 0 unknown, 1 no spinning parts, else turns per minute.
void classifyDisk(Disk& d) {
    const wstr& m = d.model;
    bool hybrid = icontains(m, L"SSHD") || icontains(m, L"Hybrid") || icontains(m, L"FireCuda") && !icontains(m, L"SSD") && d.rpm > 1;
    bool ssd = d.rpm == 1 || (d.rpm == 0 && d.noSeekPenalty);
    wstr media = hybrid ? wstr(T(L"hybrid disk (SSHD)")) : ssd ? wstr(L"SSD") : (d.rpm > 1 || d.seekKnown) ? wstr(T(L"hard disk")) : wstr(T(L"disk"));
    wstr ff = d.formFactor.empty() ? wstr() : L" · " + d.formFactor;
    switch (d.busType) {
        case 7:   // USB
            d.group = DG_REMOVABLE;
            if (icontains(m, L"ZIP ") || icontains(m, L"JAZ") || icontains(m, L"Floppy")) d.typeText = T(L"Legacy device (Zip, Jaz or floppy)");
            else if (icontains(m, L"Card") || icontains(m, L"SD/") || icontains(m, L"MMC") || icontains(m, L"CFexpress") || icontains(m, L"CompactFlash") || icontains(m, L"Reader"))
                d.typeText = T(L"Memory card (in a USB reader)");
            else d.typeText = d.removableMedia ? wstr(T(L"USB stick")) : T(L"External USB ") + media;
            break;
        case 12:  // SD
        case 13:  // MMC
            d.group = DG_REMOVABLE;
            d.typeText = T(L"Memory card (SD / microSD)");
            break;
        case 17:  // NVMe
            d.group = DG_FIXED;
            d.typeText = T(L"NVMe SSD") + (d.formFactor.empty() ? wstr(L" · M.2") : ff);
            break;
        case 11:  // SATA
        case 3:   // ATA
            d.group = DG_FIXED;
            d.typeText = (ssd && !hybrid ? wstr(T(L"SATA SSD")) : hybrid ? wstr(T(L"Hybrid disk (SSHD)")) : wstr(T(L"Internal hard disk"))) + ff +
                         (d.rpm > 1 ? L" · " + std::to_wstring(d.rpm) + T(L" rpm") : wstr());
            break;
        case 10:  // SAS
        case 6:   // Fibre Channel
        case 8:   // RAID
        case 9:   // iSCSI
        case 16:  // Storage Spaces
            d.group = DG_ENTERPRISE;
            d.typeText = d.busType == 10 ? wstr(T(L"SAS disk (direct attached)")) : d.busType == 6 ? wstr(T(L"Fibre Channel (SAN)")) : d.busType == 8 ? wstr(T(L"RAID volume")) :
                         d.busType == 9 ? wstr(T(L"iSCSI disk (SAN / NAS)")) : wstr(T(L"Storage Spaces"));
            break;
        case 14:  // virtual
        case 15:  // file-backed virtual (VHD)
            d.group = DG_ENTERPRISE;
            d.typeText = T(L"Virtual disk");
            break;
        default:
            d.group = d.removable ? DG_REMOVABLE : DG_FIXED;
            d.typeText = d.removable ? wstr(T(L"Removable disk")) : (ssd ? wstr(L"SSD") : wstr(T(L"Internal disk"))) + ff;
    }
}

// What the drive itself says: no seek penalty (SSD), and for ATA drives the turning speed and size.
void readDiskKind(HANDLE h, Disk& d) {
    DWORD n = 0;
    STORAGE_PROPERTY_QUERY q{};
    q.PropertyId = StorageDeviceSeekPenaltyProperty;
    q.QueryType = PropertyStandardQuery;
    DEVICE_SEEK_PENALTY_DESCRIPTOR sp{};
    if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof q, &sp, sizeof sp, &n, nullptr)) {
        d.seekKnown = true;
        d.noSeekPenalty = !sp.IncursSeekPenalty;
    }
    if (d.busType == 11 || d.busType == 3) {
        // ATA IDENTIFY DEVICE: word 217 = turning speed, word 168 = nominal size
        SENDCMDINPARAMS in{};
        in.cBufferSize = 512;
        in.irDriveRegs.bSectorCountReg = 1;
        in.irDriveRegs.bSectorNumberReg = 1;
        in.irDriveRegs.bDriveHeadReg = 0xA0;
        in.irDriveRegs.bCommandReg = ID_CMD;
        std::vector<uint8_t> o(sizeof(SENDCMDOUTPARAMS) + 512, 0);
        if (DeviceIoControl(h, SMART_RCV_DRIVE_DATA, &in, sizeof in - 1, o.data(), (DWORD)o.size(), &n, nullptr)) {
            const uint8_t* id = o.data() + FIELD_OFFSET(SENDCMDOUTPARAMS, bBuffer);
            auto word = [&](int w) { return (int)(id[w * 2] | id[w * 2 + 1] << 8); };
            int rate = word(217);
            if (rate == 1) d.rpm = 1;
            else if (rate >= 0x0401 && rate < 0xFFFF) d.rpm = rate;
            switch (word(168) & 0xF) {
                case 1: d.formFactor = L"5.25\""; break;
                case 2: d.formFactor = L"3.5\""; break;
                case 3: d.formFactor = L"2.5\""; break;
                case 4: d.formFactor = L"1.8\""; break;
                case 5: d.formFactor = T(L"smaller than 1.8\""); break;
                case 6: d.formFactor = L"mSATA"; break;
                case 7: d.formFactor = L"M.2"; break;
            }
        }
    }
    classifyDisk(d);
}

// Drives that are not disks Windows can partition: optical drives, floppy drives, network drives.
std::vector<OtherDrive> readOtherDrives() {
    std::vector<OtherDrive> out;
    DWORD drives = GetLogicalDrives();
    UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS);
    for (wchar_t c = L'A'; c <= L'Z'; ++c) {
        if (!(drives & (1u << (c - L'A')))) continue;
        wstr root = wstr(1, c) + L":\\";
        UINT t = GetDriveTypeW(root.c_str());
        bool floppy = t == DRIVE_REMOVABLE && (c == L'A' || c == L'B');
        if (t != DRIVE_CDROM && t != DRIVE_REMOTE && !floppy) continue;
        OtherDrive o;
        o.letter = wstr(1, c) + L":";
        wchar_t label[MAX_PATH] = {}, fs[64] = {};
        o.hasMedia = GetVolumeInformationW(root.c_str(), label, MAX_PATH, nullptr, nullptr, nullptr, fs, 64) != 0;
        o.label = label;
        o.fs = fs;
        ULARGE_INTEGER total{};
        if (o.hasMedia && GetDiskFreeSpaceExW(root.c_str(), nullptr, &total, nullptr)) o.size = total.QuadPart;
        if (t == DRIVE_REMOTE) {
            o.group = DG_ENTERPRISE;
            o.typeText = T(L"Network drive (NAS or server share)");
        } else if (floppy) {
            o.group = DG_REMOVABLE;
            o.typeText = T(L"Legacy device (floppy drive)");
        } else {
            o.group = DG_OPTICAL;
            // the drive's own name, and what the disc is by its size and file system
            HANDLE h = CreateFileW((L"\\\\.\\" + o.letter).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                STORAGE_PROPERTY_QUERY q{};
                std::vector<uint8_t> buf(1024, 0);
                DWORD got = 0;
                if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof q, buf.data(), (DWORD)buf.size(), &got, nullptr)) {
                    auto* dd = (STORAGE_DEVICE_DESCRIPTOR*)buf.data();
                    if (dd->ProductIdOffset && dd->ProductIdOffset < got) {
                        std::string s((const char*)buf.data() + dd->ProductIdOffset);
                        while (!s.empty() && s.back() == ' ') s.pop_back();
                        o.model = utf8ToWide(s);
                    }
                }
                CloseHandle(h);
            }
            wstr disc = !o.hasMedia ? wstr(T(L"no disc in it")) : o.size > (9ull << 30) ? wstr(T(L"Blu-ray disc")) : o.size > (750ull << 20) ? wstr(T(L"DVD")) : wstr(T(L"CD"));
            wstr drive = icontains(o.model, L"BD") || icontains(o.model, L"Blu") ? T(L"Blu-ray drive") : icontains(o.model, L"DVD") ? T(L"DVD drive") : T(L"CD / DVD drive");
            o.typeText = drive + L" · " + disc;
        }
        out.push_back(o);
    }
    SetErrorMode(oldMode);
    return out;
}

}  // namespace bd

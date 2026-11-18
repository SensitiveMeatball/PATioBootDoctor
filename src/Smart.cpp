// Disk health: S.M.A.R.T. attributes of SATA/ATA drives and the health log of NVMe
// drives, read straight from the disk (kernel32 DeviceIoControl only), turned into
// plain warnings. USB adapters and RAID controllers often hide it; then it is unknown.
#include "Disk.h"
#include "Lang.h"
#include <winioctl.h>

namespace bd {

namespace {

uint64_t raw48(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 5; i >= 0; --i) v = v << 8 | p[i];
    return v;
}

uint64_t le64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = v << 8 | p[i];
    return v;
}

const wchar_t* attrName(int id) {
    switch (id) {
        case 1: return N(L"read errors");
        case 5: return N(L"replaced (reallocated) sectors");
        case 10: return N(L"spin-up retries");
        case 184: return N(L"end-to-end errors");
        case 187: return N(L"errors it could not correct");
        case 196: return N(L"reallocation events");
        case 197: return N(L"sectors waiting to be replaced");
        case 198: return N(L"sectors that cannot be read");
        case 231: return N(L"life left");
        case 233: return N(L"wear");
    }
    return nullptr;
}

void finish(DiskHealth& h) {
    if (!h.known) return;
    wstr s = h.level == 2 ? wstr(T(L"Failing")) : h.level == 1 ? wstr(T(L"Warning")) : wstr(T(L"Good"));
    if (h.hours >= 0) s += L" · " + TF(L"{0} hours in use", std::to_wstring(h.hours));
    if (h.tempC > 0 && h.tempC < 120) s += L" · " + std::to_wstring(h.tempC) + L" °C";
    if (h.percentUsed >= 0) s += L" · " + TF(L"{0} % worn", std::to_wstring(h.percentUsed));
    h.summary = s;
}

}  // namespace

void parseAtaSmart(const uint8_t* attrs, const uint8_t* thresh, DiskHealth& h) {
    h.known = true;
    h.kind = L"S.M.A.R.T.";
    for (int k = 0; k < 30; ++k) {
        const uint8_t* a = attrs + 2 + k * 12;
        int id = a[0];
        if (!id) continue;
        int value = a[3];
        uint64_t raw = raw48(a + 5);
        int limit = 0;
        if (thresh)
            for (int t = 0; t < 30; ++t)
                if (thresh[2 + t * 12] == id) limit = thresh[2 + t * 12 + 1];
        if (limit > 0 && value <= limit && value > 0) {
            const wchar_t* n = attrName(id);
            h.problems.push_back(TF(L"The disk itself reports a failing value ({0}).", n ? T(n) : L"#" + std::to_wstring(id)));
            h.level = 2;
        }
        switch (id) {
            case 5: h.reallocated = (int64_t)raw; break;
            case 197: h.pending = (int64_t)raw; break;
            case 198: h.uncorrectable = (int64_t)raw; break;
            case 187: if (raw) h.problems.push_back(TF(L"{0} errors it could not correct.", std::to_wstring(raw))), h.level = std::max(h.level, 1); break;
            case 9: h.hours = (int64_t)(raw & 0xFFFFFFFF); break;
            case 194: h.tempC = (int)(raw & 0xFF); break;
            case 231: if (value <= 10) h.problems.push_back(TF(L"Only {0} % of its life is left.", std::to_wstring(value))), h.level = std::max(h.level, 1); break;
        }
    }
    if (h.reallocated > 0) {
        h.problems.push_back(TF(L"{0} damaged sectors were already replaced by spare ones.", std::to_wstring(h.reallocated)));
        h.level = std::max(h.level, h.reallocated > 100 ? 2 : 1);
    }
    if (h.pending > 0) {
        h.problems.push_back(TF(L"{0} sectors could not be read and wait to be replaced.", std::to_wstring(h.pending)));
        h.level = 2;
    }
    if (h.uncorrectable > 0) {
        h.problems.push_back(TF(L"{0} sectors cannot be read at all.", std::to_wstring(h.uncorrectable)));
        h.level = 2;
    }
    if (h.tempC >= 60 && h.tempC < 120) {
        h.problems.push_back(TF(L"It is very hot ({0} °C).", std::to_wstring(h.tempC)));
        h.level = std::max(h.level, 1);
    }
    finish(h);
}

void parseNvmeLog(const uint8_t* log, DiskHealth& h) {
    h.known = true;
    h.kind = L"NVMe";
    int warn = log[0];
    h.tempC = (int)(log[1] | log[2] << 8) - 273;
    h.spare = log[3];
    h.spareThreshold = log[4];
    h.percentUsed = log[5];
    h.hours = (int64_t)le64(log + 128);
    h.mediaErrors = (int64_t)le64(log + 160);
    if (warn & 0x01) h.problems.push_back(TF(L"Its spare space is almost used up ({0} % left).", std::to_wstring(h.spare)));
    if (warn & 0x02) h.problems.push_back(T(L"It is too hot or too cold."));
    if (warn & 0x04) h.problems.push_back(T(L"The disk itself reports that it is no longer reliable."));
    if (warn & 0x08) h.problems.push_back(T(L"It has switched itself to read-only: nothing can be saved on it any more."));
    if (warn & 0x10) h.problems.push_back(T(L"Its power-loss protection has failed."));
    if (warn & 0x1D) h.level = 2;
    else if (warn) h.level = 1;
    if (h.percentUsed >= 100) {
        h.problems.push_back(T(L"It has reached the end of its rated life."));
        h.level = 2;
    } else if (h.percentUsed >= 90) {
        h.problems.push_back(TF(L"It is {0} % worn.", std::to_wstring(h.percentUsed)));
        h.level = std::max(h.level, 1);
    }
    if (h.mediaErrors > 0) {
        h.problems.push_back(TF(L"{0} times data could not be read correctly.", std::to_wstring(h.mediaErrors)));
        h.level = std::max(h.level, 1);
    }
    finish(h);
}

DiskHealth readDiskHealth(int disk) {
    DiskHealth h;
    wchar_t path[40];
    swprintf(path, 40, L"\\\\.\\PhysicalDrive%d", disk);
    HANDLE f = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return h;
    DWORD n = 0;
    // 1. NVMe health log (Windows 10 and later)
    {
        const DWORD len = 512;
        std::vector<uint8_t> buf(FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters) + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) + len, 0);
        auto* q = (STORAGE_PROPERTY_QUERY*)buf.data();
        auto* d = (STORAGE_PROTOCOL_SPECIFIC_DATA*)q->AdditionalParameters;
        q->PropertyId = StorageDeviceProtocolSpecificProperty;
        q->QueryType = PropertyStandardQuery;
        d->ProtocolType = ProtocolTypeNvme;
        d->DataType = NVMeDataTypeLogPage;
        d->ProtocolDataRequestValue = 2;  // SMART / health information
        d->ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
        d->ProtocolDataLength = len;
        if (DeviceIoControl(f, IOCTL_STORAGE_QUERY_PROPERTY, buf.data(), (DWORD)buf.size(), buf.data(), (DWORD)buf.size(), &n, nullptr)) {
            auto* desc = (STORAGE_PROTOCOL_DATA_DESCRIPTOR*)buf.data();
            auto* out = &desc->ProtocolSpecificData;
            if (out->ProtocolDataLength >= 512 && out->ProtocolDataOffset)
                parseNvmeLog((const uint8_t*)out + out->ProtocolDataOffset, h);
        }
    }
    // 2. ATA S.M.A.R.T.
    if (!h.known) {
        GETVERSIONINPARAMS ver{};
        if (DeviceIoControl(f, SMART_GET_VERSION, nullptr, 0, &ver, sizeof ver, &n, nullptr) && (ver.fCapabilities & CAP_SMART_CMD)) {
            auto ask = [&](BYTE feature, std::vector<uint8_t>& out) {
                SENDCMDINPARAMS in{};
                in.cBufferSize = READ_ATTRIBUTE_BUFFER_SIZE;
                in.irDriveRegs.bFeaturesReg = feature;
                in.irDriveRegs.bSectorCountReg = 1;
                in.irDriveRegs.bSectorNumberReg = 1;
                in.irDriveRegs.bCylLowReg = SMART_CYL_LOW;
                in.irDriveRegs.bCylHighReg = SMART_CYL_HI;
                in.irDriveRegs.bDriveHeadReg = 0xA0;
                in.irDriveRegs.bCommandReg = SMART_CMD;
                std::vector<uint8_t> o(sizeof(SENDCMDOUTPARAMS) + READ_ATTRIBUTE_BUFFER_SIZE, 0);
                if (!DeviceIoControl(f, SMART_RCV_DRIVE_DATA, &in, sizeof in - 1, o.data(), (DWORD)o.size(), &n, nullptr)) return false;
                out.assign(o.begin() + FIELD_OFFSET(SENDCMDOUTPARAMS, bBuffer), o.begin() + FIELD_OFFSET(SENDCMDOUTPARAMS, bBuffer) + 512);
                return true;
            };
            std::vector<uint8_t> attrs, th;
            if (ask(READ_ATTRIBUTES, attrs)) {
                bool haveTh = ask(READ_THRESHOLDS, th);
                parseAtaSmart(attrs.data(), haveTh ? th.data() : nullptr, h);
            }
        }
    }
    // 3. At least the drive's own verdict
    STORAGE_PREDICT_FAILURE pf{};
    if (DeviceIoControl(f, IOCTL_STORAGE_PREDICT_FAILURE, nullptr, 0, &pf, sizeof pf, &n, nullptr)) {
        if (!h.known) {
            h.known = true;
            h.kind = L"S.M.A.R.T.";
        }
        if (pf.PredictFailure) {
            h.problems.insert(h.problems.begin(), T(L"The disk itself predicts that it will fail."));
            h.level = 2;
        }
        finish(h);
    }
    CloseHandle(f);
    return h;
}

}  // namespace bd

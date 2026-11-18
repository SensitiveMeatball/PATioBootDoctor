// The hardware of this PC, for a technician's records: from the firmware's SMBIOS tables (maker, model,
// serial number, BIOS, mainboard, case, processor, every memory module), from Windows (processor name,
// memory), the devices found now (graphics, network, storage controllers), the disks with their health,
// and the systems installed. Works in Windows and on the rescue stick (it needs only kernel32 and the
// registry). Each collection says who collected it, when, and from where.
#pragma once
#include "Diagnose.h"

namespace bd {

struct SmbiosInfo {
    bool found = false;
    wstr biosVendor, biosVersion, biosDate;
    wstr maker, model, version, serial, sku, family, uuid;
    wstr boardMaker, boardModel, boardSerial;
    wstr chassisMaker, chassisType, chassisSerial;
    struct Cpu {
        wstr socket, maker, name;
        int maxMHz = 0, curMHz = 0, cores = 0, threads = 0;
    };
    std::vector<Cpu> cpus;
    struct Mem {
        wstr slot, bank, maker, part, serial, type;
        uint64_t mb = 0;
        int mhz = 0;
    };
    std::vector<Mem> mems;  // installed modules (empty slots left out)
    int memSlots = 0;
    uint64_t memMaxMb = 0;
};

// The SMBIOS structures (the table after the 8-byte RawSMBIOSData header). For the self-test too.
bool parseSmbios(const std::string& table, SmbiosInfo& out);
SmbiosInfo readSmbios();  // this PC's, through GetSystemFirmwareTable

struct PciDevice {
    wstr name, venDev, service;
};
std::vector<PciDevice> livePciDevices(const wchar_t* classGuid);  // present devices of one class

// The full hardware report of this PC as text. technician: who collected it.
wstr hardwareReport(const Report& r, const wstr& technician, const wstr& version);
// "Dell Inc. Latitude E6420 (serial 7XYZ12)" - the name a collection is filed under
wstr hardwareTitle(const SmbiosInfo& s, const wstr& fallback);

}  // namespace bd

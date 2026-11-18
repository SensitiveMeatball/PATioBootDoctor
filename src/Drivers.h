// Drivers: loading a storage driver so a hidden disk appears (Windows PE: drvload; Windows:
// pnputil), finding such drivers on removable disks, and listing the drivers a Windows loads
// while it starts, so one that is not Microsoft's can be switched off without starting it.
#pragma once
#include "Diagnose.h"

namespace bd {

struct DriverInf {
    wstr path;      // the .inf file
    wstr title;     // "Intel RST VMD Controller (iaStorVD.inf)"
    wstr provider;
};
// Looks on the given drive roots (4 folders deep) for storage controller drivers (.inf of class SCSIAdapter or HDC).
std::vector<DriverInf> findStorageDrivers(const std::vector<wstr>& roots, volatile bool* cancel);
bool infIsStorageDriver(const std::string& infBytes, wstr& provider);  // for the self-test
bool loadDriver(const wstr& inf, wstr& result);

struct BootDriver {
    wstr name, display, file, company;  // service name, its description, the .sys path, who made it
    int start = 0;                      // 0 boot, 1 system
    bool microsoft = false, missing = false;
    wstr group;
};
// Drivers and file-system filters that start with Windows (Start 0 or 1), other makers' first.
std::vector<BootDriver> listBootDrivers(const Install& in, const Partition& winPart, wstr& err);
wstr fileCompany(const wstr& path);  // CompanyName from a program file's version information

}  // namespace bd

// A rescue USB stick that starts PATio Boot Doctor. It is made from the
// Windows Recovery Environment image already on this PC (Winre.wim) or from a
// Windows install stick's boot.wim: the image gets this program and starts it
// by itself; the stick gets start-up files for UEFI and legacy BIOS PCs.
// The PC's own start-up settings and firmware list are never touched.
#pragma once
#include "Diagnose.h"
#include "Repair.h"

namespace bd {

struct RescueSource {
    wstr path;    // the .wim file
    wstr label;   // "Windows Recovery Environment on Disk 0, partition 4"
    uint64_t size = 0;
};

std::vector<RescueSource> rescueSources(const Report& r);
// PATio Commander next to this program's PatSuite folder (it goes on the stick too), or empty.
wstr findCommander(const wstr& exe);
// Disks a rescue stick can be written to: removable USB disks of 1 GB or more (virtual disks too when allowVirtual).
std::vector<int> rescueTargets(const Report& r, bool allowVirtual = false);
// Builds the stick on the given disk: everything on it is erased. exe: this program.
// How much room a rescue stick needs, worked out before anything starts.
struct RescueNeeds {
    uint64_t image = 0;        // the start-up image (sources\boot.wim) with the programs in it
    uint64_t repairCopy = 0;   // the clean copy of the repair tools (when made from a Winre.wim)
    uint64_t data = 0;         // saved keys, settings, data kept from the stick
    uint64_t bootFiles = 0;    // start-up files for UEFI and BIOS
    uint64_t onStick = 0;      // all of it, with a margin
    uint64_t stickSize = 0;    // the smallest usual stick size that holds it: 1, 2, 4, 8, 16 GB ...
    uint64_t work = 0;         // free space needed on this PC while it is made
    uint64_t imageFiles = 0;   // a disk image instead: the .vhd and the .img together
};
RescueNeeds rescueNeeds(const Report& r, const RescueSource& src, const wstr& exe, int targetDisk = -1);

// This program's data to put on the stick, besides what the stick already holds (which is kept):
// the password of the saved keys (to merge the stick's keys with this PC's) and the settings file.
struct RescueExtras {
    wstr keysPassword;
    wstr settingsFile;
    wstr stickName;  // the technician's own name for this stick ("Rescue 2 - workshop")
};

// The marker in a rescue stick's root (PATioBootDoctor.stick): line 1 the version that made it,
// then "name=<the stick's own name>". Sticks of earlier versions have the version only.
struct StickMarker {
    bool found = false;
    wstr version, name;
};
StickMarker readStickMarker(const wstr& root);
bool writeStickMarker(const wstr& root, const wstr& version, const wstr& name);
StickMarker parseStickMarker(const std::string& text);  // for the self-test
wstr stickNameOnDisk(const Report& r, int disk);          // the name of a rescue stick on that disk, or empty
FixOutcome makeRescueStick(const Report& r, int disk, const RescueSource& src, const wstr& exe, const wstr& workFolder,
                           const std::function<void(const wstr&, float)>& progress, volatile bool* cancel, const RescueExtras& extras = RescueExtras());

// The same rescue disk as image files in outFolder: a virtual disk (.vhd) and a plain image (.img).
FixOutcome makeRescueImage(const RescueSource& src, const wstr& exe, const wstr& outFolder, const wstr& workFolder,
                           const std::function<void(const wstr&, float)>& progress, volatile bool* cancel, const RescueExtras& extras = RescueExtras());

}  // namespace bd

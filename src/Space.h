// Free space on a Windows drive: the folders that can safely be emptied
// (temporary files, Windows Update downloads, crash reports, browser caches),
// measured first, emptied on request. Works on the running Windows and on one
// seen from Windows PE (paths are taken from the drive, not from this system).
#pragma once
#include "Disk.h"
#include <functional>

namespace bd {

struct CleanSpot {
    wstr title, tip;
    std::vector<wstr> patterns;  // relative to the Windows drive root; '*' matches one folder name
    bool browser = false;        // optional: browser caches (sites load a little slower once)
    bool on = true;
    uint64_t bytes = 0;
    uint32_t files = 0;
};

std::vector<CleanSpot> cleanupSpots();
// Expands "Users\*\AppData\Local\Temp" under root into the folders that exist.
std::vector<wstr> expandPattern(const wstr& root, const wstr& pattern);
void measureSpots(const wstr& root, std::vector<CleanSpot>& spots, volatile bool* cancel, const std::function<void(const wstr&, float)>& progress);
// Empties the chosen folders (the folders themselves stay). Files in use are skipped.
uint64_t cleanSpots(const wstr& root, const std::vector<CleanSpot>& spots, volatile bool* cancel, const std::function<void(const wstr&, float)>& progress,
                    uint32_t& removed, uint32_t& skipped);
// Free space now (bytes); 0 when unknown.
uint64_t freeSpace(const wstr& root);
// Below this the Windows drive is "almost full".
bool lowSpace(uint64_t freeBytes, uint64_t totalBytes);

}  // namespace bd

#include "Space.h"
#include "Lang.h"

namespace bd {

std::vector<CleanSpot> cleanupSpots() {
    std::vector<CleanSpot> s;
    auto add = [&](const wstr& title, const wstr& tip, std::vector<wstr> p, bool browser = false) {
        CleanSpot c;
        c.title = title;
        c.tip = tip;
        c.patterns = std::move(p);
        c.browser = browser;
        s.push_back(c);
    };
    add(T(L"Temporary files"), T(L"Files programs and Windows leave behind while they work. Safe to remove."),
        {L"Windows\\Temp", L"Users\\*\\AppData\\Local\\Temp"});
    add(T(L"Windows Update downloads"), T(L"Updates that were already downloaded. Windows downloads again whatever it still needs."),
        {L"Windows\\SoftwareDistribution\\Download", L"Windows\\ServiceProfiles\\NetworkService\\AppData\\Local\\Microsoft\\Windows\\DeliveryOptimization\\Cache"});
    add(T(L"Crash reports"), T(L"Memory dumps and error reports written after a crash. Only a technician looking into a crash needs them."),
        {L"Windows\\Minidump", L"Windows\\LiveKernelReports", L"ProgramData\\Microsoft\\Windows\\WER\\ReportArchive", L"ProgramData\\Microsoft\\Windows\\WER\\ReportQueue",
         L"Users\\*\\AppData\\Local\\CrashDumps", L"Windows\\MEMORY.DMP"});
    add(T(L"Browser caches"), T(L"Copies of web pages the browsers keep to load sites faster. Removing them signs no one out and keeps bookmarks and passwords; sites load a little slower once."),
        {L"Users\\*\\AppData\\Local\\Google\\Chrome\\User Data\\*\\Cache", L"Users\\*\\AppData\\Local\\Google\\Chrome\\User Data\\*\\Code Cache",
         L"Users\\*\\AppData\\Local\\Microsoft\\Edge\\User Data\\*\\Cache", L"Users\\*\\AppData\\Local\\Microsoft\\Edge\\User Data\\*\\Code Cache",
         L"Users\\*\\AppData\\Local\\BraveSoftware\\Brave-Browser\\User Data\\*\\Cache", L"Users\\*\\AppData\\Local\\Mozilla\\Firefox\\Profiles\\*\\cache2",
         L"Users\\*\\AppData\\Local\\Opera Software\\Opera Stable\\Cache", L"Users\\*\\AppData\\Local\\Opera Software\\Opera Stable\\Default\\Cache",
         L"Users\\*\\AppData\\Local\\Vivaldi\\User Data\\*\\Cache"},
        true);
    return s;
}

static bool isDirW(const wstr& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT);
}

std::vector<wstr> expandPattern(const wstr& rootIn, const wstr& pattern) {
    wstr root = rootIn;
    if (!root.empty() && root.back() != L'\\') root += L'\\';
    std::vector<wstr> cur = {root.substr(0, root.size() - 1)};
    size_t a = 0;
    while (a <= pattern.size() && !cur.empty()) {
        size_t b = pattern.find(L'\\', a);
        if (b == wstr::npos) b = pattern.size();
        wstr part = pattern.substr(a, b - a);
        a = b + 1;
        std::vector<wstr> next;
        for (const wstr& c : cur) {
            if (part == L"*") {
                WIN32_FIND_DATAW fd{};
                HANDLE h = FindFirstFileExW((c + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchLimitToDirectories, nullptr, 0);
                if (h == INVALID_HANDLE_VALUE) continue;
                do {
                    wstr n = fd.cFileName;
                    if (n == L"." || n == L".." || !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
                    next.push_back(c + L"\\" + n);
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            } else if (GetFileAttributesW((c + L"\\" + part).c_str()) != INVALID_FILE_ATTRIBUTES)
                next.push_back(c + L"\\" + part);
        }
        cur.swap(next);
        if (b == pattern.size()) break;
    }
    return cur;
}

namespace {

// Walks a folder (not following links). del: remove files and empty sub-folders.
void walk(const wstr& dir, bool del, uint64_t& bytes, uint32_t& files, uint32_t& skipped, volatile bool* cancel, int depth = 0) {
    if (depth > 40 || (cancel && *cancel)) return;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        wstr n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        wstr p = dir + L"\\" + n;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                if (del) RemoveDirectoryW(p.c_str());  // removes only the link
                continue;
            }
            walk(p, del, bytes, files, skipped, cancel, depth + 1);
            if (del) RemoveDirectoryW(p.c_str());
            continue;
        }
        uint64_t sz = (uint64_t)fd.nFileSizeHigh << 32 | fd.nFileSizeLow;
        if (del) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
            if (DeleteFileW(p.c_str())) {
                bytes += sz;
                ++files;
            } else ++skipped;
        } else {
            bytes += sz;
            ++files;
        }
    } while (FindNextFileW(h, &fd) && !(cancel && *cancel));
    FindClose(h);
}

}  // namespace

void measureSpots(const wstr& root, std::vector<CleanSpot>& spots, volatile bool* cancel, const std::function<void(const wstr&, float)>& progress) {
    for (size_t i = 0; i < spots.size(); ++i) {
        CleanSpot& c = spots[i];
        c.bytes = 0;
        c.files = 0;
        if (progress) progress(c.title, (float)i / spots.size());
        uint32_t skipped = 0;
        for (const wstr& pat : c.patterns)
            for (const wstr& p : expandPattern(root, pat)) {
                if (isDirW(p)) walk(p, false, c.bytes, c.files, skipped, cancel);
                else {
                    WIN32_FILE_ATTRIBUTE_DATA a{};
                    if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) {
                        c.bytes += (uint64_t)a.nFileSizeHigh << 32 | a.nFileSizeLow;
                        ++c.files;
                    }
                }
            }
    }
}

uint64_t cleanSpots(const wstr& root, const std::vector<CleanSpot>& spots, volatile bool* cancel, const std::function<void(const wstr&, float)>& progress,
                    uint32_t& removed, uint32_t& skipped) {
    uint64_t freed = 0;
    removed = skipped = 0;
    for (size_t i = 0; i < spots.size(); ++i) {
        const CleanSpot& c = spots[i];
        if (!c.on) continue;
        if (progress) progress(c.title, (float)i / spots.size());
        toolLog(L"[clean] " + c.title);
        for (const wstr& pat : c.patterns)
            for (const wstr& p : expandPattern(root, pat)) {
                if (isDirW(p)) walk(p, true, freed, removed, skipped, cancel);
                else {
                    WIN32_FILE_ATTRIBUTE_DATA a{};
                    if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a) && DeleteFileW(p.c_str())) {
                        freed += (uint64_t)a.nFileSizeHigh << 32 | a.nFileSizeLow;
                        ++removed;
                    } else ++skipped;
                }
            }
    }
    toolLog(L"[clean] freed " + sizeText(freed) + L", " + std::to_wstring(removed) + L" files removed, " + std::to_wstring(skipped) + L" in use");
    return freed;
}

uint64_t freeSpace(const wstr& root) {
    ULARGE_INTEGER avail{}, total{}, freeB{};
    if (GetDiskFreeSpaceExW(root.c_str(), &avail, &total, &freeB)) return freeB.QuadPart;
    return 0;
}

bool lowSpace(uint64_t freeBytes, uint64_t totalBytes) {
    if (!totalBytes) return false;
    return freeBytes < (2ull << 30) || (freeBytes < totalBytes / 100 && freeBytes < (10ull << 30));  // under 2 GB, or under 1 % (up to 10 GB)
}

}  // namespace bd

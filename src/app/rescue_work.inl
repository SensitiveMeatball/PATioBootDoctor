// Part of class DoctorApp (included inside the class in main.cpp).
    // ---------------------------------------------------------------- where a rescue stick is made
    // The data drive with the most room that is not the stick itself (Windows PE's temp folder is in memory).
    wstr rescueWorkFolder(int stickDisk) {
        wstr work;
        uint64_t best = 0;
        for (const Partition& p : rep_.m.parts)
            if (p.readable && p.disk != stickDisk && p.fs == L"NTFS" && p.role == R_DATA && !p.letter.empty() && p.freeBytes > best) {
                best = p.freeBytes;
                work = p.letter;
            }
        if (work.empty()) {
            wchar_t tmp[MAX_PATH] = {};
            GetTempPathW(MAX_PATH, tmp);
            work = tmp;
            while (!work.empty() && work.back() == L'\\') work.pop_back();
        }
        return work;
    }

    static uint64_t driveFree(const wstr& dir) {
        ULARGE_INTEGER avail{};
        wstr d = dir.size() == 2 ? dir + L"\\" : dir;
        return GetDiskFreeSpaceExW(d.c_str(), &avail, nullptr, nullptr) ? avail.QuadPart : 0;
    }

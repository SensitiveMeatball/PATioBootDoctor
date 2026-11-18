#include "Backup.h"
#include "Lang.h"
#include "core/Platform.h"

namespace bd {

static bool exists(const wstr& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

static void makeDirs(const wstr& path) {
    size_t start = istartsWith(path, L"\\\\?\\") ? path.find(L'\\', 4) : 3;
    for (size_t p = path.find(L'\\', start); p != wstr::npos; p = path.find(L'\\', p + 1)) CreateDirectoryW(path.substr(0, p).c_str(), nullptr);
    CreateDirectoryW(path.c_str(), nullptr);
}

static wstr parentOf(const wstr& p) {
    size_t s = p.find_last_of(L'\\');
    return s == wstr::npos ? wstr() : p.substr(0, s);
}

static bool writeText(const wstr& path, const wstr& text, bool append) {
    HANDLE h = CreateFileW(path.c_str(), append ? FILE_APPEND_DATA : GENERIC_WRITE, FILE_SHARE_READ, nullptr, append ? OPEN_ALWAYS : CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::string u = wideToUtf8(text);
    DWORD wr = 0;
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    if (!size.QuadPart) WriteFile(h, "\xEF\xBB\xBF", 3, &wr, nullptr);
    BOOL ok = WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr);
    CloseHandle(h);
    return ok != 0;
}

static bool writeBytes(const wstr& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    BOOL ok = data.empty() || WriteFile(h, data.data(), (DWORD)data.size(), &wr, nullptr);
    CloseHandle(h);
    return ok && wr == data.size();
}

static wstr stamp(bool forFolder) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t b[64];
    if (forFolder) swprintf(b, 64, L"%04d-%02d-%02d %02d-%02d-%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    else swprintf(b, 64, L"%04d-%02d-%02d %02d:%02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return b;
}

static wstr safeName(wstr s) {
    for (wchar_t& c : s)
        if (wcschr(L"\\/:*?\"<>|", c)) c = L'-';
    if (s.size() > 60) s.resize(60);
    return s;
}

static wstr systemTool(const wchar_t* name) {
    wchar_t win[MAX_PATH] = {};
    GetWindowsDirectoryW(win, MAX_PATH);
    wstr native = wstr(win) + L"\\Sysnative\\" + name;  // a 32-bit app on 64-bit Windows
    if (exists(native)) return native;
    return wstr(win) + L"\\System32\\" + name;
}

wstr bcdeditPath() { return systemTool(L"bcdedit.exe"); }

// ---------------------------------------------------------------- running tools
ToolResult runTool(const wstr& exe, const wstr& args, volatile bool* cancel, const std::string& stdinText) {
    ToolResult r;
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE outR = nullptr, outW = nullptr, inR = nullptr, inW = nullptr;
    if (!CreatePipe(&outR, &outW, &sa, 0)) return r;
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    if (!CreatePipe(&inR, &inW, &sa, 0)) {
        CloseHandle(outR);
        CloseHandle(outW);
        return r;
    }
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = outW;
    si.hStdError = outW;
    si.hStdInput = inR;
    PROCESS_INFORMATION pi{};
    wstr cmd = L"\"" + exe + L"\" " + args;
    toolLog(L"> " + exe.substr(exe.find_last_of(L'\\') + 1) + L" " + args);
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    r.started = CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi) != 0;
    CloseHandle(outW);
    CloseHandle(inR);
    if (!r.started) {
        r.output = T(L"Could not start ") + exe + L": " + errorText(GetLastError());
        CloseHandle(outR);
        CloseHandle(inW);
        return r;
    }
    if (!stdinText.empty()) {
        DWORD wr = 0;
        WriteFile(inW, stdinText.data(), (DWORD)stdinText.size(), &wr, nullptr);
    }
    CloseHandle(inW);
    std::string raw;
    char chunk[4096];
    for (;;) {
        DWORD avail = 0;
        if (PeekNamedPipe(outR, nullptr, 0, nullptr, &avail, nullptr) && avail) {
            DWORD got = 0;
            if (ReadFile(outR, chunk, std::min<DWORD>(avail, sizeof chunk), &got, nullptr) && got) raw.append(chunk, got);
            continue;
        }
        if (WaitForSingleObject(pi.hProcess, 100) == WAIT_OBJECT_0) {
            DWORD got = 0;
            while (PeekNamedPipe(outR, nullptr, 0, nullptr, &avail, nullptr) && avail && ReadFile(outR, chunk, sizeof chunk, &got, nullptr) && got)
                raw.append(chunk, got);
            break;
        }
        if (cancel && *cancel) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 3000);
            raw += "\r\n[Stopped]";
            break;
        }
    }
    GetExitCodeProcess(pi.hProcess, &r.code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(outR);
    // Some tools (sfc) write UTF-16, most write the OEM code page
    size_t zeros = 0;
    for (size_t i = 1; i < raw.size(); i += 2) zeros += raw[i] == 0;
    if (raw.size() > 8 && zeros * 3 > raw.size() / 2) {
        r.output.assign((const wchar_t*)raw.data(), raw.size() / 2);
        r.output.erase(std::remove(r.output.begin(), r.output.end(), L'\0'), r.output.end());
    } else if (!raw.empty()) {
        int n = MultiByteToWideChar(CP_OEMCP, 0, raw.data(), (int)raw.size(), nullptr, 0);
        r.output.assign(n, L'\0');
        MultiByteToWideChar(CP_OEMCP, 0, raw.data(), (int)raw.size(), &r.output[0], n);
    }
    {
        wstr o = r.output;
        while (!o.empty() && (o.back() == L'\n' || o.back() == L'\r' || o.back() == L' ')) o.pop_back();
        if (!o.empty()) toolLog(o);
        toolLog(L"  (exit code " + std::to_wstring((long)r.code) + L")");
    }
    return r;
}

// ---------------------------------------------------------------- files
static bool copyTree(const wstr& src, const wstr& dst, bool clearDest, int& count, wstr& err) {
    makeDirs(dst);
    WIN32_FIND_DATAW fd{};
    HANDLE f = FindFirstFileW((src + L"\\*").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return true;
    bool ok = true;
    do {
        wstr n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        wstr s = src + L"\\" + n, d = dst + L"\\" + n;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ok = copyTree(s, d, clearDest, count, err) && ok;
        } else {
            if (clearDest && exists(d)) SetFileAttributesW(d.c_str(), FILE_ATTRIBUTE_NORMAL);
            if (CopyFileW(s.c_str(), d.c_str(), FALSE)) ++count;
            else if (GetLastError() == ERROR_SHARING_VIOLATION || GetLastError() == ERROR_LOCK_VIOLATION) {
                // In use by the running system (its start-up settings): those are saved by "bcdedit /export".
                err += T(L"Skipped ") + n + L" (in use)\n";
            } else {
                ok = false;
                err += T(L"Could not copy ") + n + L": " + errorText(GetLastError()) + L"\n";
            }
        }
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return ok;
}

bool deleteFolderTree(const wstr& dir) {
    WIN32_FIND_DATAW fd{};
    HANDLE f = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            wstr n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            wstr p = dir + L"\\" + n;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) deleteFolderTree(p);
            else {
                SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(p.c_str());
            }
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    return RemoveDirectoryW(dir.c_str()) != 0;
}

uint64_t folderBytes(const wstr& dir) {
    uint64_t n = 0;
    WIN32_FIND_DATAW fd{};
    HANDLE f = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return 0;
    do {
        wstr name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) n += folderBytes(dir + L"\\" + name);
        else n += ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return n;
}

bool folderWritable(const wstr& dir) {
    makeDirs(dir);
    wstr probe = dir + L"\\.write-test-" + std::to_wstring(GetCurrentProcessId());
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

wstr defaultBackupRoot(const Machine& m) {
    wstr exeDir = g_sys.exeDir;
    while (!exeDir.empty() && exeDir.back() == L'\\') exeDir.pop_back();
    bool ramOrCd = false;
    if (exeDir.size() >= 2 && exeDir[1] == L':') {
        UINT t = GetDriveTypeW((exeDir.substr(0, 2) + L"\\").c_str());
        ramOrCd = t == DRIVE_CDROM || t == DRIVE_RAMDISK || (g_sys.winPE && towupper(exeDir[0]) == L'X');
    }
    if (!ramOrCd && folderWritable(exeDir + L"\\PATioBootDoctor Backups")) return exeDir + L"\\PATioBootDoctor Backups";
    // Running from a DVD or Windows PE's RAM disk: pick the data drive with the most room, so backups survive a restart.
    const Partition* best = nullptr;
    for (const Partition& p : m.parts)
        if (p.readable && p.role == R_DATA && p.fs == L"NTFS" && !p.root.empty() && (!best || p.freeBytes > best->freeBytes)) best = &p;
    if (best && folderWritable(best->root + L"PATioBootDoctor Backups")) return best->root + L"PATioBootDoctor Backups";
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    return wstr(tmp) + L"PATioBootDoctor Backups";
}

// ---------------------------------------------------------------- journal
bool Journal::open(const wstr& root, const wstr& host, wstr& err) {
    root_ = root;
    while (!root_.empty() && root_.back() == L'\\') root_.pop_back();
    makeDirs(root_);
    folder_ = root_ + L"\\" + stamp(true);
    makeDirs(folder_);
    if (!folderWritable(folder_)) {
        err = T(L"The backup folder ") + root_ + T(L" cannot be written to.");
        folder_.clear();
        return false;
    }
    next_ = 1;
    WritePrivateProfileStringW(L"session", L"host", host.c_str(), (folder_ + L"\\session.ini").c_str());
    note(T(L"Session started on ") + host);
    return true;
}

void Journal::note(const wstr& text) {
    if (onNote) onNote(text);
    if (folder_.empty()) return;
    writeText(folder_ + L"\\session.log", stamp(false) + L"  " + text + L"\r\n", true);
}

void Journal::saveReport(const wstr& text) {
    if (!folder_.empty()) writeText(folder_ + L"\\report.txt", text, false);
}

wstr Journal::begin(const wstr& title, const wstr& target) {
    if (folder_.empty()) return L"";
    wchar_t num[8];
    swprintf(num, 8, L"%03d ", next_++);
    wstr dir = folder_ + L"\\" + num + safeName(title);
    makeDirs(dir + L"\\files");
    wstr ini = dir + L"\\action.ini";
    WritePrivateProfileStringW(L"action", L"title", title.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"action", L"target", target.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"action", L"time", stamp(false).c_str(), ini.c_str());
    WritePrivateProfileStringW(L"action", L"finished", L"0", ini.c_str());
    WritePrivateProfileStringW(L"action", L"items", L"0", ini.c_str());
    note(T(L"Started: ") + title + (target.empty() ? L"" : L" (" + target + L")"));
    return dir;
}

void Journal::addItem(const wstr& action, const wstr& line) {
    wstr ini = action + L"\\action.ini";
    int n = (int)GetPrivateProfileIntW(L"action", L"items", 0, ini.c_str());
    WritePrivateProfileStringW(L"items", std::to_wstring(n).c_str(), line.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"action", L"items", std::to_wstring(n + 1).c_str(), ini.c_str());
}

bool Journal::saveFile(const wstr& action, const Partition& p, const wstr& rel, wstr& err) {
    if (action.empty()) return true;
    wstr src = p.root + rel;
    int n = (int)GetPrivateProfileIntW(L"action", L"items", 0, (action + L"\\action.ini").c_str());
    wstr stored = L"files\\" + std::to_wstring(n) + L"-" + safeName(rel.substr(rel.find_last_of(L'\\') == wstr::npos ? 0 : rel.find_last_of(L'\\') + 1));
    DWORD attrs = GetFileAttributesW(src.c_str());
    bool existed = attrs != INVALID_FILE_ATTRIBUTES;
    if (existed && !CopyFileW(src.c_str(), (action + L"\\" + stored).c_str(), FALSE)) {
        err = T(L"Could not back up ") + rel + L": " + errorText(GetLastError());
        return false;
    }
    if (existed) SetFileAttributesW((action + L"\\" + stored).c_str(), FILE_ATTRIBUTE_NORMAL);
    addItem(action, L"file|" + p.key + L"|" + rel + L"|" + stored + L"|" + (existed ? L"1" : L"0") + L"|" + std::to_wstring(existed ? attrs : 0));
    return true;
}

bool Journal::saveDir(const wstr& action, const Partition& p, const wstr& relDir, wstr& err) {
    if (action.empty()) return true;
    wstr src = p.root + relDir;
    int n = (int)GetPrivateProfileIntW(L"action", L"items", 0, (action + L"\\action.ini").c_str());
    wstr stored = L"files\\" + std::to_wstring(n) + L"-dir";
    if (!exists(src)) {
        addItem(action, L"dir|" + p.key + L"|" + relDir + L"|" + stored + L"|0");
        return true;
    }
    int count = 0;
    wstr e;
    if (!copyTree(src, action + L"\\" + stored, false, count, e)) {
        err = T(L"Could not back up the folder ") + relDir + L":\n" + e;
        return false;
    }
    addItem(action, L"dir|" + p.key + L"|" + relDir + L"|" + stored + L"|1");
    return true;
}

bool Journal::saveSectors(const wstr& action, const Disk& d, uint64_t offset, uint32_t length, wstr& err) {
    if (action.empty()) return true;
    std::string data;
    if (!readDisk(d.number, offset, length, data)) {
        err = T(L"Could not read the disk to back it up.");
        return false;
    }
    int n = (int)GetPrivateProfileIntW(L"action", L"items", 0, (action + L"\\action.ini").c_str());
    wstr stored = L"files\\" + std::to_wstring(n) + L"-disk" + std::to_wstring(d.number) + L"-at-" + std::to_wstring(offset) + L".bin";
    if (!writeBytes(action + L"\\" + stored, data)) {
        err = T(L"Could not write the backup file.");
        return false;
    }
    addItem(action, L"sectors|" + d.id() + L"|" + std::to_wstring(d.number) + L"|" + std::to_wstring(d.size) + L"|" + std::to_wstring(offset) + L"|" + stored);
    return true;
}

bool Journal::saveDiskTables(const wstr& action, const Disk& d, wstr& err) {
    if (!d.readable) return true;
    if (!saveSectors(action, d, 0, d.style == 2 ? 34 * d.sector : d.sector, err)) return false;
    if (d.style == 2 && d.size > 67ull * d.sector) return saveSectors(action, d, d.size - 33ull * d.sector, 33 * d.sector, err);
    return true;
}

bool Journal::saveLiveBcd(const wstr& action, wstr& err) {
    if (action.empty()) return true;
    int n = (int)GetPrivateProfileIntW(L"action", L"items", 0, (action + L"\\action.ini").c_str());
    wstr stored = L"files\\" + std::to_wstring(n) + L"-BCD-export";
    ToolResult t = runTool(bcdeditPath(), L"/export \"" + action + L"\\" + stored + L"\"");
    if (!t.started || t.code != 0) {
        err = T(L"Could not back up the start-up settings: ") + t.output;
        return false;
    }
    addItem(action, L"livebcd|" + stored);
    return true;
}

bool Journal::saveFwVar(const wstr& action, const wstr& name, wstr& err) {
    if (action.empty()) return true;
    std::string data;
    bool existed = readFwVar(name, data);
    int n = (int)GetPrivateProfileIntW(L"action", L"items", 0, (action + L"\\action.ini").c_str());
    wstr stored = L"files\\" + std::to_wstring(n) + L"-" + name + L".var";
    if (existed && !writeBytes(action + L"\\" + stored, data)) {
        err = T(L"Could not write the backup of ") + name + L".";
        return false;
    }
    addItem(action, L"fwvar|" + name + L"|" + stored + L"|" + (existed ? L"1" : L"0"));
    return true;
}

void Journal::recordMove(const wstr& action, const Partition& p, const wstr& fromRel, const wstr& toRel) {
    if (!action.empty()) addItem(action, L"move|" + p.key + L"|" + fromRel + L"|" + toRel);
}

void Journal::end(const wstr& action, bool ok, const wstr& result, const wstr& log, bool canUndo) {
    if (action.empty()) return;
    wstr ini = action + L"\\action.ini";
    WritePrivateProfileStringW(L"action", L"finished", L"1", ini.c_str());
    WritePrivateProfileStringW(L"action", L"ok", ok ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"action", L"canUndo", canUndo ? L"1" : L"0", ini.c_str());
    wstr oneLine = result;
    for (wchar_t& c : oneLine)
        if (c == L'\r' || c == L'\n') c = L' ';
    WritePrivateProfileStringW(L"action", L"result", oneLine.c_str(), ini.c_str());
    if (!log.empty()) writeText(action + L"\\log.txt", log, false);
    note((ok ? T(L"Done: ") : T(L"Failed: ")) + oneLine);
}

// ---------------------------------------------------------------- history
static std::vector<wstr> split(const wstr& s, wchar_t sep) {
    std::vector<wstr> out;
    size_t a = 0;
    for (size_t b; (b = s.find(sep, a)) != wstr::npos; a = b + 1) out.push_back(s.substr(a, b - a));
    out.push_back(s.substr(a));
    return out;
}

static wstr iniGet(const wstr& ini, const wchar_t* sec, const wchar_t* key) {
    std::vector<wchar_t> b(4096);
    GetPrivateProfileStringW(sec, key, L"", b.data(), (DWORD)b.size(), ini.c_str());
    return b.data();
}

ActionInfo readAction(const wstr& dir) {
    ActionInfo a;
    a.dir = dir;
    wstr ini = dir + L"\\action.ini";
    a.title = iniGet(ini, L"action", L"title");
    a.target = iniGet(ini, L"action", L"target");
    a.time = iniGet(ini, L"action", L"time");
    a.result = iniGet(ini, L"action", L"result");
    a.ok = GetPrivateProfileIntW(L"action", L"ok", 0, ini.c_str()) != 0;
    a.finished = GetPrivateProfileIntW(L"action", L"finished", 0, ini.c_str()) != 0;
    a.undone = GetPrivateProfileIntW(L"action", L"undone", 0, ini.c_str()) != 0;
    a.canUndo = GetPrivateProfileIntW(L"action", L"canUndo", 0, ini.c_str()) != 0;
    int n = (int)GetPrivateProfileIntW(L"action", L"items", 0, ini.c_str());
    for (int i = 0; i < n; ++i) {
        std::vector<wstr> f = split(iniGet(ini, L"items", std::to_wstring(i).c_str()), L'|');
        if (f.empty() || f[0].empty()) continue;
        BackupItem it;
        it.kind = f[0];
        it.f.assign(f.begin() + 1, f.end());
        a.items.push_back(it);
    }
    if (a.items.empty()) a.canUndo = false;
    return a;
}

std::vector<SessionInfo> listSessions(const wstr& root) {
    std::vector<SessionInfo> out;
    WIN32_FIND_DATAW fd{};
    HANDLE f = FindFirstFileW((root + L"\\*").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return out;
    do {
        wstr n = fd.cFileName;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || n.size() < 19 || !iswdigit(n[0])) continue;
        SessionInfo s;
        s.dir = root + L"\\" + n;
        s.name = n.substr(0, 10) + L" " + n.substr(11, 2) + L":" + n.substr(14, 2);
        s.host = iniGet(s.dir + L"\\session.ini", L"session", L"host");
        WIN32_FIND_DATAW ad{};
        HANDLE g = FindFirstFileW((s.dir + L"\\*").c_str(), &ad);
        if (g != INVALID_HANDLE_VALUE) {
            do {
                wstr an = ad.cFileName;
                if ((ad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && an.size() > 4 && iswdigit(an[0]) && exists(s.dir + L"\\" + an + L"\\action.ini"))
                    s.actions.push_back(readAction(s.dir + L"\\" + an));
            } while (FindNextFileW(g, &ad));
            FindClose(g);
        }
        std::sort(s.actions.begin(), s.actions.end(), [](const ActionInfo& a, const ActionInfo& b) { return a.dir < b.dir; });
        s.bytes = folderBytes(s.dir);
        out.push_back(s);
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    std::sort(out.begin(), out.end(), [](const SessionInfo& a, const SessionInfo& b) { return a.dir > b.dir; });
    return out;
}

static const Partition* partByKey(const Machine& m, const wstr& key) {
    for (const Partition& p : m.parts)
        if (iequals(p.key, key)) return &p;
    return nullptr;
}

void Journal::recordScript(const wstr& action, const wstr& script) {
    wstr one = script;
    for (wchar_t& c : one)
        if (c == L'\n') c = L';';
    one.erase(std::remove(one.begin(), one.end(), L'\r'), one.end());
    if (!action.empty()) addItem(action, L"dp|" + one);
}

void Journal::recordLabel(const wstr& action, const Partition& p, const wstr& oldLabel) {
    if (!action.empty()) addItem(action, L"label|" + p.key + L"|" + oldLabel);
}

bool undoAction(const ActionInfo& a, const Machine& m, Journal* j, wstr& log, wstr* newDir) {
    bool ok = true;
    wstr undoDir = j ? j->begin(T(L"Undo ") + a.title, a.time) : wstr();
    if (newDir) *newDir = undoDir;
    // Put things back in reverse order
    for (auto it = a.items.rbegin(); it != a.items.rend(); ++it) {
        const BackupItem& b = *it;
        const std::vector<wstr>& f = b.f;
        if (b.kind == L"file" && f.size() >= 4) {
            const Partition* p = partByKey(m, f[0]);
            if (!p || p->root.empty()) {
                log += T(L"Skipped ") + f[1] + T(L": its partition is not there now.\n");
                ok = false;
                continue;
            }
            wstr dst = p->root + f[1];
            wstr e;
            if (j) j->saveFile(undoDir, *p, f[1], e);
            if (f[3] == L"1") {
                makeDirs(parentOf(dst));
                if (exists(dst)) SetFileAttributesW(dst.c_str(), FILE_ATTRIBUTE_NORMAL);
                if (CopyFileW((a.dir + L"\\" + f[2]).c_str(), dst.c_str(), FALSE)) {
                    if (f.size() >= 5) SetFileAttributesW(dst.c_str(), (DWORD)_wtoi64(f[4].c_str()));
                    log += T(L"Put back ") + dst + L"\n";
                } else {
                    log += T(L"Could not put back ") + dst + L": " + errorText(GetLastError()) + L"\n";
                    ok = false;
                }
            } else if (exists(dst)) {
                SetFileAttributesW(dst.c_str(), FILE_ATTRIBUTE_NORMAL);
                if (DeleteFileW(dst.c_str())) log += T(L"Removed ") + dst + L" (it was not there before)\n";
            }
        } else if (b.kind == L"dir" && f.size() >= 4) {
            const Partition* p = partByKey(m, f[0]);
            if (!p || p->root.empty()) {
                log += T(L"Skipped the folder ") + f[1] + T(L": its partition is not there now.\n");
                ok = false;
                continue;
            }
            if (f[3] != L"1") continue;
            wstr e;
            if (j) j->saveDir(undoDir, *p, f[1], e);
            int count = 0;
            if (copyTree(a.dir + L"\\" + f[2], p->root + f[1], true, count, e)) log += T(L"Put back ") + std::to_wstring(count) + T(L" files in ") + p->root + f[1] + L"\n";
            else {
                log += e;
                ok = false;
            }
        } else if (b.kind == L"sectors" && f.size() >= 5) {
            const Disk* d = nullptr;
            for (const Disk& x : m.disks)
                if (iequals(x.id(), f[0])) d = &x;
            if (!d)  // the disk's id lives in the table being restored; fall back to the same number and size
                for (const Disk& x : m.disks)
                    if (std::to_wstring(x.number) == f[1] && std::to_wstring(x.size) == f[2]) d = &x;
            if (!d) {
                log += T(L"Skipped the partition table of disk ") + f[1] + T(L": that disk is not connected now.\n");
                ok = false;
                continue;
            }
            std::string data;
            if (!readFileText(a.dir + L"\\" + f[4], data, 1u << 20)) {
                log += T(L"The saved copy of the partition table is missing.\n");
                ok = false;
                continue;
            }
            wstr e;
            if (j) j->saveSectors(undoDir, *d, _wcstoui64(f[3].c_str(), nullptr, 10), (uint32_t)data.size(), e);
            if (writeDisk(d->number, _wcstoui64(f[3].c_str(), nullptr, 10), data, e)) log += T(L"Put back ") + std::to_wstring(data.size()) + T(L" bytes at the start or end of Disk ") + f[1] + L"\n";
            else {
                log += e + L"\n";
                ok = false;
            }
        } else if (b.kind == L"livebcd" && !f.empty()) {
            wstr e;
            if (j) j->saveLiveBcd(undoDir, e);
            ToolResult t = runTool(bcdeditPath(), L"/import \"" + a.dir + L"\\" + f[0] + L"\"");
            log += t.output;
            if (t.code != 0) ok = false;
        } else if (b.kind == L"fwvar" && f.size() >= 3) {
            wstr e;
            if (j) j->saveFwVar(undoDir, f[0], e);
            std::string data;
            if (f[2] == L"1" && !readFileText(a.dir + L"\\" + f[1], data, 1u << 16)) {
                log += T(L"The saved copy of ") + f[0] + T(L" is missing.\n");
                ok = false;
                continue;
            }
            if (writeFwVar(f[0], f[2] == L"1" ? data : std::string())) log += L"Put back the PC's start-up list entry " + f[0] + L"\n";
            else {
                log += T(L"Could not put back ") + f[0] + L" (error " + std::to_wstring(GetLastError()) + L").\n";
                ok = false;
            }
        } else if (b.kind == L"dp" && !f.empty()) {
            wstr script = f[0];
            for (size_t i = 1; i < f.size(); ++i) script += L"|" + f[i];
            for (wchar_t& c : script)
                if (c == L';') c = L'\n';
            // Record the opposite first, so this undo can itself be undone (Redo)
            if (j) {
                auto num = [&](const wstr& key) {
                    size_t at = script.find(key);
                    return at == wstr::npos ? -1 : _wtoi(script.c_str() + at + key.size());
                };
                int dn = num(L"select disk "), pn = num(L"select partition ");
                wstr sel = L"select disk " + std::to_wstring(dn) + L"\n" + (pn >= 0 ? L"select partition " + std::to_wstring(pn) + L"\n" : wstr());
                if (script.find(L"online disk") != wstr::npos) j->recordScript(undoDir, sel + L"offline disk\n");
                else if (script.find(L"offline disk") != wstr::npos) j->recordScript(undoDir, sel + L"online disk\n");
                else if (script.find(L"letter=") != wstr::npos && dn >= 0 && pn >= 0) {
                    wstr cur, add;
                    for (const Partition& p : m.parts)
                        if (p.disk == dn && p.number == pn) cur = p.letter;
                    size_t at = script.find(L"assign letter=");
                    if (at != wstr::npos) add = script.substr(at + 14, 1);
                    wstr inv = sel + (add.empty() ? wstr() : L"remove letter=" + add + L" noerr\n") + (cur.empty() ? wstr() : L"assign letter=" + cur.substr(0, 1) + L"\n");
                    j->recordScript(undoDir, inv);
                }
            }
            wchar_t tmp[MAX_PATH] = {};
            GetTempPathW(MAX_PATH, tmp);
            wstr file = wstr(tmp) + L"PATioBD-undo.txt";
            std::string u = wideToUtf8(script + L"\nexit\n");
            HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            DWORD wr = 0;
            if (h != INVALID_HANDLE_VALUE) {
                WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr);
                CloseHandle(h);
            }
            ToolResult t = runTool(systemTool(L"diskpart.exe"), L"/s \"" + file + L"\"");
            DeleteFileW(file.c_str());
            log += t.output;
            if (!t.started || t.code != 0) ok = false;
        } else if (b.kind == L"label" && f.size() >= 2) {
            const Partition* p = partByKey(m, f[0]);
            if (p && j) j->recordLabel(undoDir, *p, p->label);
            if (!p || p->volume.empty() || !SetVolumeLabelW(p->volume.c_str(), f[1].c_str())) {
                log += T(L"Could not put the old name back.\n");
                ok = false;
            } else log += T(L"Put back the name \"") + f[1] + L"\"\n";
        } else if (b.kind == L"move" && f.size() >= 3) {
            const Partition* p = partByKey(m, f[0]);
            if (!p || p->root.empty()) {
                ok = false;
                continue;
            }
            wstr from = p->root + f[2], to = p->root + f[1];
            makeDirs(parentOf(to));
            if (MoveFileExW(from.c_str(), to.c_str(), 0)) {
                log += T(L"Moved ") + from + T(L" back to ") + to + L"\n";
                if (j) j->recordMove(undoDir, *p, f[2], f[1]);
            } else {
                log += T(L"Could not move ") + from + T(L" back: ") + errorText(GetLastError()) + L"\n";
                ok = false;
            }
        }
    }
    if (ok) WritePrivateProfileStringW(L"action", L"undone", L"1", (a.dir + L"\\action.ini").c_str());
    if (j) j->end(undoDir, ok, ok ? T(L"Put back what \"") + a.title + T(L"\" changed.") : T(L"Some things could not be put back."), log, true);
    return ok;
}

}  // namespace bd

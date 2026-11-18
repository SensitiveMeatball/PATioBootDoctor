// Updating this program: from the update address (an HTTPS text file that names the newest
// version, where to get it and its SHA-256 checksum), or from an .exe the user picked.
// The new program is checked (a 64-bit Windows program, a newer version, the checksum)
// before it replaces this one; the old one is kept as <name>.old until the next start.
//
// The update file, for example https://www.muspc.com/updates/patio-boot-doctor.txt:
//   version=0.3.0
//   url=https://www.muspc.com/updates/PATioBootDoctor_x64.exe
//   sha256=0123...cdef        (64 hex digits of the .exe)
//   notes=What is new, in one line
#pragma once
#include "Disk.h"

namespace bd {

extern const wchar_t* const kDefaultUpdateUrl;

struct UpdateInfo {
    bool ok = false;
    wstr error;               // why the check failed
    wstr version, url, sha256, notes;
    wstr sig;                 // signature of the checksum, made with MusPC's private key
};

int compareVersions(const wstr& a, const wstr& b);    // <0, 0, >0
UpdateInfo parseUpdateFile(const std::string& text);
UpdateInfo checkForUpdate(const wstr& updateUrl);
// Downloads the new program next to this one (as .new) and checks it. Returns its path, or empty with err.
wstr downloadUpdate(const UpdateInfo& u, const std::function<void(float)>& progress, volatile bool* cancel, wstr& err);
// Checks a new program file: 64-bit Windows program, its version (out), and the checksum when given.
bool checkProgramFile(const wstr& path, const wstr& sha256, wstr& version, wstr& err);
// Puts the new program in place of the running one (this one becomes <name>.old) and starts it.
bool installUpdate(const wstr& newExe, wstr& err);
void removeOldVersion();  // at start: the <name>.old left by an update
std::string sha256Hex(const std::string& data);

// Signed updates (ECDSA P-256). The public key is built into the program (res\update-key.pub);
// the private key stays with the maker. A program with a key built in only accepts update
// files whose checksum is signed with it.
bool updateKeyBuiltIn();
bool makeUpdateKey(const wstr& privateFile, const wstr& publicFile, wstr& err);
wstr signChecksum(const wstr& sha256, const wstr& privateFile, wstr& err);              // hex signature
bool verifyChecksum(const wstr& sha256, const wstr& sig, const std::string& publicKey);  // publicKey: hex, as in the .pub file
bool verifyChecksum(const wstr& sha256, const wstr& sig);                               // with the built-in key
// For the maker: writes <exe>.update.txt (version, url, sha256, sig, notes) for a new program file.
bool writeUpdateFile(const wstr& exe, const wstr& privateFile, const wstr& url, const wstr& notes, wstr& out, wstr& err);

}  // namespace bd

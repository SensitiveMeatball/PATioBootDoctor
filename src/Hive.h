// Registry hive files (BCD stores, a Windows installation's SYSTEM file) read
// without starting that Windows: loaded under HKLM for a moment, then unloaded.
#pragma once
#include "Disk.h"

namespace bd {

// Checks a hive file's header: "regf" signature, checksum, and whether it was saved cleanly.
// why: a plain-language reason when not Ok.
Health hiveFileHealth(const wstr& path, wstr& why);
Health hiveBytesHealth(const std::string& head, wstr& why);  // the same on the first 4 KB

// A hive loaded under HKLM. With copy = true a copy in %TEMP% is loaded, so the
// original file is never changed or locked. Unloads when destroyed.
class LoadedHive {
public:
    LoadedHive() = default;
    ~LoadedHive() { unload(); }
    LoadedHive(const LoadedHive&) = delete;
    LoadedHive& operator=(const LoadedHive&) = delete;
    bool load(const wstr& file, bool copy, wstr& err);
    void unload();
    HKEY root() const { return root_; }
    bool ok() const { return root_ != nullptr; }

private:
    wstr name_, temp_;
    HKEY root_ = nullptr;
};

void unloadStaleHives();  // left over by a crash

// Small registry readers (missing values give the default)
bool regDword(HKEY root, const wstr& sub, const wchar_t* value, DWORD& out);
wstr regString(HKEY root, const wstr& sub, const wchar_t* value);
std::vector<wstr> regMulti(HKEY root, const wstr& sub, const wchar_t* value);
bool regBinary(HKEY root, const wstr& sub, const wchar_t* value, std::string& out);
std::vector<wstr> regSubkeys(HKEY root, const wstr& sub);
bool regSetDword(HKEY root, const wstr& sub, const wchar_t* value, DWORD v);
bool regSetString(HKEY root, const wstr& sub, const wchar_t* value, const wstr& v);
bool regSetBinary(HKEY root, const wstr& sub, const wchar_t* value, const std::string& v);
bool regEnsureKey(HKEY root, const wstr& sub);  // creates the key (and its parents) when missing
bool regDefaultValueType(HKEY root, const wstr& sub, DWORD& type);  // the type field of a key's (Default) value (SAM keeps the RID there)

}  // namespace bd

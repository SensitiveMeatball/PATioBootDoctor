// BitLocker recovery keys: checking, formatting (123456-123456-...), and the
// keys the user chose to remember on this PC. Saved keys are encrypted with
// Windows' own data protection (DPAPI), so only the same Windows user account
// on the same PC can read them back.
#pragma once
#include "Disk.h"
#include <functional>

namespace bd {

struct SavedKey {
    wstr id;     // the partition's lasting key (Partition::key)
    wstr name;   // what to show: "C: · Disk 0, partition 3"
    wstr when;   // saved on (YYYY-MM-DD HH:MM)
    wstr key;    // 48 digits with dashes
    wstr pc;     // the PC it was saved on: Windows' computer name (empty in Windows PE)
    wstr model;  // the PC's maker and model, as its firmware reports them
    wstr owner;  // a name the user gives it ("Ann's laptop")
    bool archived = false;  // kept, but listed apart (an old PC, a disk given away)
};

wstr keyDigits(const wstr& s);                 // only the digits
wstr formatRecoveryKey(const wstr& digits);    // groups of 6 joined by dashes
bool validRecoveryKey(const wstr& s);          // 48 digits; each group of 6 a multiple of 11 below 720896
// The formatter for the input box: keeps digits, inserts dashes, at most 48 digits.
wstr keyInputFormat(const wstr& before, const wstr& after, int& caret);

// The saved keys live in a file next to this program (so they travel with it, for example on
// the rescue USB stick), encrypted with a password the user chooses: AES-256 (CBC) with a key
// made from the password by PBKDF2-SHA256, and an HMAC-SHA256 seal against tampering.
// Running from the rescue stick: its folder for this program's files (keys, settings), else empty.
wstr stickFolder();
wstr rescueStickRoot();  // running from the rescue stick: its root ("E:\\"), else empty
// The one folder for everything this program keeps: PATioBootDoctorData (see Keys.cpp).
wstr dataFolder();
wstr keyStorePath();
bool keyStoreExists();
enum KeyLoad { KL_OK, KL_NONE, KL_BADPASS, KL_ERROR };
int loadKeys(const wstr& password, std::vector<SavedKey>& out);
int loadKeysFile(const wstr& path, const wstr& password, std::vector<SavedKey>& out);
bool storeKeysFile(const wstr& path, const std::vector<SavedKey>& keys, const wstr& password, wstr& err);
// Puts the keys of `from` into `into` (both opened with the same password). Returns how many
// were new, or -1 when one of them cannot be opened with it.
int mergeKeyFiles(const wstr& into, const wstr& from, const wstr& password);
const SavedKey* findKey(const std::vector<SavedKey>& keys, const wstr& id);
bool saveKey(const SavedKey& k, const wstr& password, wstr& err);   // replaces a key saved for the same id
bool deleteKey(const wstr& id, const wstr& password);
bool deleteAllKeys();
// Changes saved keys in one go (rename the owner, archive, ...); edit gets them all.
bool editKeys(const wstr& password, const std::function<void(std::vector<SavedKey>&)>& edit, wstr& err);
wstr thisPcName();  // Windows' computer name, empty in Windows PE
bool changeKeyPassword(const wstr& oldPassword, const wstr& newPassword, wstr& err);
// Keys saved by an earlier version (Windows data protection, in %APPDATA%) are moved into the file.
int migrateOldKeys(const wstr& password);

// For the self-test
bool encryptBlob(const std::string& plain, const wstr& password, std::string& out, uint32_t iterations = 200000);
bool decryptBlob(const std::string& blob, const wstr& password, std::string& out);
bool protectBytes(const std::string& in, std::string& out);
bool unprotectBytes(const std::string& in, std::string& out);

}  // namespace bd

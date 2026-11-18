// Local accounts of a Windows that is not running, read from and changed in its SAM
// registry file (System32\config\SAM) while that Windows is offline.
//
// What it can do, for the common "locked out of Windows" call-out:
//   - list the local accounts (name, whether disabled, locked, or without a password)
//   - enable a disabled account (clears the "account disabled" flag) — the usual way to
//     bring back the built-in Administrator (RID 500), which is disabled by default
//   - unlock an account locked after too many wrong passwords (clears the auto-lock flag)
//   - clear (blank) an account's password, so the next sign-in needs no password
//
// It only touches local accounts. It cannot help a Microsoft-account or Azure AD sign-in
// (the password is not on this PC), and clearing a password loses that account's
// EFS-encrypted files and its saved passwords (Windows protects them with the password).
//
// The caller backs up the SAM file (History) before any change, as the registry editor does.
#pragma once
#include "Disk.h"

namespace bd {

struct LocalAccount {
    wstr name, fullName;
    uint32_t rid = 0;
    bool disabled = false;       // the "account disabled" flag is set
    bool locked = false;         // auto-locked after wrong passwords
    bool blankPassword = false;  // no password hash stored
    bool pwNotRequired = false;  // a password is not required
    bool builtin = false;        // RID below 1000 (Administrator 500, Guest 501, ...)
    wstr lastLogon;              // local time "YYYY-MM-DD HH:MM", or empty for never
};

// Reads the local accounts from an offline Windows' SAM file. Empty with err on failure.
std::vector<LocalAccount> readLocalAccounts(const wstr& samFile, wstr& err);

enum AccountOp { AO_ENABLE, AO_UNLOCK, AO_CLEAR_PASSWORD };
// Carries out one change on one account in the SAM file. Writes the file. The caller backs it up first.
bool changeAccount(const wstr& samFile, uint32_t rid, int op, wstr& err);

// ---- the SAM structures on a plain buffer, for the self-test (no registry) ----
// The user's "F" value: account-control flags (ACB) are a 16-bit field at offset 0x38.
bool samParseF(const std::string& f, uint16_t& acb);
void samSetAcb(std::string& f, uint16_t acb);
// The user's "V" value: the name and full name, and the lengths of the LM and NT password hashes.
bool samParseV(const std::string& v, wstr& name, wstr& fullName, uint32_t& lmLen, uint32_t& ntLen);
bool samBlankV(std::string& v);  // sets both hash lengths to zero (no password)

// Account-control bits used here
enum { ACB_DISABLED = 0x0001, ACB_PWNOTREQ = 0x0004, ACB_AUTOLOCK = 0x0400 };

}  // namespace bd

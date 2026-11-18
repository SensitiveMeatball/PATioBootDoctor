// Secure Boot's certificate stores, read from the firmware, and the 2023-certificate change.
//
// Microsoft's 2011 Secure Boot certificates expire in 2026, so new 2023 ones are being rolled
// out (and the 2011 Windows boot certificate is being revoked after the "BlackLotus" fix,
// KB5025885). A PC can stop booting with "Secure Boot Violation / Invalid signature" when the
// firmware has revoked the old certificate but the boot files (or the firmware's trust list)
// have not caught up. This reads the three stores and explains where a PC stands.
//
//   db   (allowed)   – the certificates a boot file may be signed with
//   KEK  (keys)      – who may update db and dbx
//   dbx  (forbidden) – revoked certificates and file hashes
#pragma once
#include "Disk.h"

namespace bd {

struct SecureBootCerts {
    bool read = false;              // the stores could be read from the firmware
    std::vector<wstr> db, kek, dbx; // certificate names in each store
    int dbxHashes = 0;              // revoked file hashes in dbx (not certificates)
    bool dbHas2011 = false;         // db trusts the 2011 Windows boot certificate
    bool dbHas2023 = false;         // db trusts a 2023 certificate
    bool dbxRevokes2011 = false;    // dbx revokes the 2011 Windows boot certificate
    bool dbxLarge = false;          // dbx carries the large revocation list (the staged roll-out)
};

SecureBootCerts readSecureBootCerts();

// A plain-language paragraph about the 2023 change for this PC, or empty when there is nothing to say.
wstr secureBoot2023Note(const SecureBootCerts& c, const Firmware& fw);

// ---- parts, for the self-test ----
// The DER certificates and the count of SHA-256 hash entries in an EFI signature-list blob.
void efiSigListCerts(const std::string& var, std::vector<std::string>& ders, int& sha256Count);
// The common name of an X.509 certificate (DER). Uses crypt32 when present, else a plain scan.
wstr certCommonName(const std::string& der);

}  // namespace bd

// The disk and partition tool: create, delete, format, resize, letters, names,
// types, active flag, disk set-up, conversions and wiping. Every operation is
// explained in plain words, checked against what must not be touched (the
// running Windows, the backup folder), backs up the partition table first, and
// runs Windows' own diskpart (or a Windows call) to do the work.
#pragma once
#include "Diagnose.h"
#include "Backup.h"
#include "Repair.h"

namespace bd {

enum DiskOpKind {
    DO_CREATE, DO_DELETE, DO_FORMAT, DO_EXTEND, DO_SHRINK, DO_LETTER, DO_REMOVE_LETTER, DO_LABEL, DO_ACTIVE, DO_INACTIVE,
    DO_TYPE_DATA, DO_TYPE_ESP, DO_TYPE_RECOVERY, DO_CHKDSK, DO_INIT_GPT, DO_INIT_MBR, DO_CONVERT_GPT, DO_CONVERT_MBR, DO_MBR2GPT,
    DO_CLEAN, DO_ONLINE, DO_OFFLINE, DO_CLEAR_READONLY,
    DO_UNLOCK, DO_FIND_LOST, DO_RECOVER_FILES, DO_MERGE_NEXT,  // the first three open their own windows
    DO_BITLOCKER_OFF, DO_BITLOCKER_SUSPEND, DO_LAYOUT, DO_SURFACE,
    DO_COUNT
};

enum SelKind { SEL_NONE, SEL_DISK, SEL_PART, SEL_GAP };

struct DiskOpInfo {
    wstr title;    // a verb: "Delete partition"
    wstr what;     // what it does, in plain words
    wstr lesson;   // what the thing is, for someone new to it
    bool danger = false;
    bool undoable = true;   // Undo can put it back
    bool losesData = false; // files on it are gone
};

struct DiskOpArgs {
    int kind = DO_COUNT;
    int disk = -1, part = -1;     // disk number; partition index into Machine::parts
    int part2 = -1;               // merge: the partition after it, which is deleted
    uint64_t offset = 0;          // create: where the free space starts (bytes)
    uint64_t sizeMB = 0;          // create / extend / shrink
    int fs = 0;                   // 0 NTFS, 1 FAT32, 2 exFAT, 3 no file system
    int type = 0;                 // create: 0 data, 1 EFI start-up, 2 recovery, 3 reserved (MSR)
    bool quick = true;
    wstr label, letter;           // letter: "E:" or empty for none
};

DiskOpInfo diskOpInfo(int kind);
// Which operations fit the selection; why = why an operation is not possible here (empty = possible).
std::vector<std::pair<int, wstr>> diskOpsFor(const Report& r, int selKind, int disk, int part, int gap);
wstr diskpartScript(const DiskOpArgs& a, const Report& r);  // the script (for the confirmation's "for a technician" part)
FixOutcome runDiskOp(const DiskOpArgs& a, Report& r, Journal& j, volatile bool* cancel);
std::vector<wstr> freeLetters();
// The disk the running Windows (or this program) is on: its partitions are never changed.
wstr workingDiskReason(const Report& r, int disk);   // why it is in use, or empty
wstr workingDiskRefusal(const Report& r, int disk);  // the same, with what to do instead
int nextPartition(const Report& r, int part);      // the partition right after it on the same disk, or -1
uint64_t unusedAfter(const Report& r, int part);   // unused space right after it (bytes)
uint64_t unusedAfterEnd(const Report& r, int disk, uint64_t end);
const wchar_t* fsName(int fs);
void setProtectedFolder(const wstr& folder);  // the backup folder: its drive is never deleted or formatted

}  // namespace bd

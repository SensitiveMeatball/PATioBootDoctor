// Linux: a small read-only reader for ext2/ext3/ext4 (Windows cannot read
// them), finding Linux systems and their boot loaders (GRUB, shim,
// systemd-boot), and their boot chain. Nothing here ever writes to a Linux
// partition.
#pragma once
#include "Diagnose.h"
#include <map>

namespace bd {

class ExtReader {
public:
    bool open(const ReadFn& rd, wstr& err);
    bool readFile(const std::string& path, std::string& out, size_t maxBytes = 1u << 20);
    bool list(const std::string& dir, std::vector<std::string>& names);
    bool exists(const std::string& path);
    bool isDir(const std::string& path);

private:
    struct Inode {
        uint16_t mode = 0;
        uint64_t size = 0;
        uint32_t flags = 0;
        std::string block;  // i_block, 60 bytes
    };
    bool inode(uint32_t ino, Inode& out);
    bool blocks(const Inode& in, std::vector<uint64_t>& out, size_t maxBlocks);
    bool extentNode(const std::string& node, std::vector<uint64_t>& out, size_t maxBlocks, int depth);
    bool mapBlock(uint64_t blk, int level, std::vector<uint64_t>& out, size_t maxBlocks);
    bool data(const Inode& in, std::string& out, size_t maxBytes);
    bool entries(const Inode& dir, std::vector<std::pair<std::string, uint32_t>>& out);
    bool lookup(const std::string& path, uint32_t& ino, int depth = 0);
    ReadFn rd_;
    uint32_t bs_ = 1024, inodeSize_ = 128, ipg_ = 0, descSize_ = 32;
    uint64_t gdt_ = 2, blocksCount_ = 0;
    bool is64_ = false;
};

// Text helpers for Linux configuration files (exposed for the self-test)
std::map<std::string, std::string> parseOsRelease(const std::string& text);
struct FstabLine {
    std::string spec, mount, type;
};
std::vector<FstabLine> parseFstab(const std::string& text);
std::string grubUuid(const std::string& cfg);    // the file system id GRUB searches for, or ""
std::string grubPrefix(const std::string& cfg);  // /boot/grub, /grub, ... or ""

// Finds Linux systems on readable ext partitions and on unreadable Linux
// partitions that a boot loader points to; adds them to r.installs.
void findLinux(Report& r);
// For the self-test: the same work on given readers instead of real disks.
void findLinuxWith(Report& r, const std::function<ReadFn(int part)>& readerFor);
void buildLinuxChain(Report& r, Install& in);

}  // namespace bd

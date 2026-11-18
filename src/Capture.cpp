#include "Capture.h"
#include <cmath>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include "Disk.h"  // systemDll

namespace bd {

namespace {

// ---- checksums -------------------------------------------------------------
uint32_t g_crc[256];
bool g_crcReady = false;
void crcInit() {
    if (g_crcReady) return;
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        g_crc[n] = c;
    }
    g_crcReady = true;
}
uint32_t crc32(const uint8_t* p, size_t n, uint32_t c = 0xFFFFFFFFu) {
    crcInit();
    for (size_t i = 0; i < n; ++i) c = g_crc[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c;
}
uint32_t adler32(const uint8_t* p, size_t n) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; ++i) {
        a = (a + p[i]) % 65521;
        b = (b + a) % 65521;
    }
    return b << 16 | a;
}

void be32(std::string& s, uint32_t v) {
    s += (char)(v >> 24);
    s += (char)(v >> 16);
    s += (char)(v >> 8);
    s += (char)v;
}

void pngChunk(std::string& out, const char type[4], const std::string& data) {
    be32(out, (uint32_t)data.size());
    std::string td(type, 4);
    td += data;
    out.append(td);
    uint32_t c = crc32((const uint8_t*)td.data(), td.size()) ^ 0xFFFFFFFFu;
    be32(out, c);
}

}  // namespace

uint32_t crc32Of(const std::string& s) { return crc32((const uint8_t*)s.data(), s.size()) ^ 0xFFFFFFFFu; }

// ---- PNG (no compression library: deflate "stored" blocks) -----------------
bool writePng(const wstr& path, const uint32_t* bgra, int w, int h) {
    if (w <= 0 || h <= 0) return false;
    std::string raw;
    raw.reserve((size_t)(w * 3 + 1) * h);
    for (int y = 0; y < h; ++y) {
        raw += (char)0;  // filter: none
        const uint32_t* row = bgra + (size_t)y * w;
        for (int x = 0; x < w; ++x) {
            uint32_t c = row[x];
            raw += (char)(c >> 16);  // R
            raw += (char)(c >> 8);   // G
            raw += (char)c;          // B
        }
    }
    std::string zlib;
    zlib += (char)0x78;
    zlib += (char)0x01;
    for (size_t i = 0; i < raw.size();) {
        size_t n = raw.size() - i;
        if (n > 65535) n = 65535;
        bool last = i + n >= raw.size();
        zlib += (char)(last ? 1 : 0);
        zlib += (char)(n & 0xFF);
        zlib += (char)(n >> 8 & 0xFF);
        zlib += (char)(~n & 0xFF);
        zlib += (char)(~n >> 8 & 0xFF);
        zlib.append(raw, i, n);
        i += n;
    }
    be32(zlib, adler32((const uint8_t*)raw.data(), raw.size()));

    std::string png = "\x89PNG\r\n\x1a\n";
    std::string ihdr;
    be32(ihdr, (uint32_t)w);
    be32(ihdr, (uint32_t)h);
    ihdr += (char)8;  // bit depth
    ihdr += (char)2;  // colour type: RGB
    ihdr += (char)0;
    ihdr += (char)0;
    ihdr += (char)0;
    pngChunk(png, "IHDR", ihdr);
    pngChunk(png, "IDAT", zlib);
    pngChunk(png, "IEND", std::string());

    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    bool ok = WriteFile(f, png.data(), (DWORD)png.size(), &wr, nullptr) && wr == png.size();
    CloseHandle(f);
    return ok;
}

// ---- baseline JPEG (4:4:4) -------------------------------------------------
namespace {

const int kZigZag[64] = {0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

const int kLumQ[64] = {16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62,
                       18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};
const int kChrQ[64] = {17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
                       99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};

// Standard Huffman specification tables (JPEG Annex K)
const uint8_t kDcLumBits[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
const uint8_t kDcLumVal[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
const uint8_t kDcChrBits[16] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
const uint8_t kDcChrVal[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
const uint8_t kAcLumBits[16] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
const uint8_t kAcLumVal[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52,
    0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45,
    0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83,
    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
    0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8,
    0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};
const uint8_t kAcChrBits[16] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
const uint8_t kAcChrVal[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33,
    0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44,
    0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
    0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
    0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};

struct HuffCode {
    uint16_t code = 0;
    uint8_t len = 0;
};

void buildHuff(const uint8_t bits[16], const uint8_t* vals, HuffCode out[256]) {
    uint16_t code = 0;
    int k = 0;
    for (int len = 1; len <= 16; ++len) {
        for (int i = 0; i < bits[len - 1]; ++i) {
            out[vals[k]].code = code;
            out[vals[k]].len = (uint8_t)len;
            ++code;
            ++k;
        }
        code <<= 1;
    }
}

struct BitWriter {
    std::string& out;
    uint32_t acc = 0;
    int nbits = 0;
    explicit BitWriter(std::string& o) : out(o) {}
    void put(uint16_t code, int len) {
        acc = (acc << len) | (code & ((1u << len) - 1));
        nbits += len;
        while (nbits >= 8) {
            uint8_t b = (uint8_t)(acc >> (nbits - 8));
            out += (char)b;
            if (b == 0xFF) out += (char)0x00;  // byte stuffing
            nbits -= 8;
        }
    }
    void flush() {
        if (nbits > 0) put(0x7F, 8 - nbits);  // pad with 1-bits
    }
};

int category(int v) {
    v = v < 0 ? -v : v;
    int s = 0;
    while (v) {
        ++s;
        v >>= 1;
    }
    return s;
}

double g_cos[8][8];
bool g_cosReady = false;
void cosInit() {
    if (g_cosReady) return;
    for (int u = 0; u < 8; ++u)
        for (int x = 0; x < 8; ++x) g_cos[u][x] = (u == 0 ? 0.3535533905932738 : 0.5) * cos((2 * x + 1) * u * 3.14159265358979324 / 16.0);
    g_cosReady = true;
}

// One 8x8 block through DCT, quantise, and Huffman-encode. Returns the new DC for this component.
int encodeBlock(const double in[64], const int q[64], int prevDc, const HuffCode dc[256], const HuffCode ac[256], BitWriter& bw) {
    cosInit();
    double tmp[64], F[64];
    for (int y = 0; y < 8; ++y)
        for (int u = 0; u < 8; ++u) {
            double s = 0;
            for (int x = 0; x < 8; ++x) s += in[y * 8 + x] * g_cos[u][x];
            tmp[y * 8 + u] = s;
        }
    for (int u = 0; u < 8; ++u)
        for (int v = 0; v < 8; ++v) {
            double s = 0;
            for (int y = 0; y < 8; ++y) s += tmp[y * 8 + u] * g_cos[v][y];
            F[v * 8 + u] = s;
        }
    int blk[64];
    for (int i = 0; i < 64; ++i) {
        double val = F[i] / q[i];
        blk[i] = (int)(val < 0 ? val - 0.5 : val + 0.5);
    }
    // DC
    int diff = blk[0] - prevDc;
    int s = category(diff);
    bw.put(dc[s].code, dc[s].len);
    if (s) {
        int v = diff < 0 ? diff - 1 : diff;  // one's-complement for negatives
        bw.put((uint16_t)(v & ((1 << s) - 1)), s);
    }
    // AC in zig-zag order
    int run = 0;
    for (int k = 1; k < 64; ++k) {
        int v = blk[kZigZag[k]];
        if (v == 0) {
            ++run;
            continue;
        }
        while (run > 15) {
            bw.put(ac[0xF0].code, ac[0xF0].len);  // ZRL
            run -= 16;
        }
        int sc = category(v);
        int sym = (run << 4) | sc;
        bw.put(ac[sym].code, ac[sym].len);
        int vv = v < 0 ? v - 1 : v;
        bw.put((uint16_t)(vv & ((1 << sc) - 1)), sc);
        run = 0;
    }
    if (run > 0) bw.put(ac[0x00].code, ac[0x00].len);  // EOB
    return blk[0];
}

void u16(std::string& s, int v) {
    s += (char)(v >> 8 & 0xFF);
    s += (char)(v & 0xFF);
}

void scaleQ(const int base[64], int quality, int out[64]) {
    int s = quality < 50 ? 5000 / quality : 200 - 2 * quality;
    for (int i = 0; i < 64; ++i) {
        int q = (base[i] * s + 50) / 100;
        out[i] = q < 1 ? 1 : q > 255 ? 255 : q;
    }
}

}  // namespace

std::string encodeJpeg(const uint32_t* bgra, int w, int h, int quality) {
    if (w <= 0 || h <= 0) return {};
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;
    int lq[64], cq[64];
    scaleQ(kLumQ, quality, lq);
    scaleQ(kChrQ, quality, cq);
    HuffCode dcLum[256]{}, acLum[256]{}, dcChr[256]{}, acChr[256]{};
    buildHuff(kDcLumBits, kDcLumVal, dcLum);
    buildHuff(kAcLumBits, kAcLumVal, acLum);
    buildHuff(kDcChrBits, kDcChrVal, dcChr);
    buildHuff(kAcChrBits, kAcChrVal, acChr);

    std::string out;
    out += "\xFF\xD8";  // SOI
    // APP0 / JFIF
    out += "\xFF\xE0";
    u16(out, 16);
    out += "JFIF";
    out += (char)0;
    out += (char)1;
    out += (char)1;  // version 1.1
    out += (char)0;  // units
    u16(out, 1);
    u16(out, 1);
    out += (char)0;
    out += (char)0;
    // DQT (luma id 0, chroma id 1), values in zig-zag order
    auto dqt = [&](int id, const int q[64]) {
        out += "\xFF\xDB";
        u16(out, 67);
        out += (char)id;
        for (int i = 0; i < 64; ++i) out += (char)q[kZigZag[i]];
    };
    dqt(0, lq);
    dqt(1, cq);
    // SOF0
    out += "\xFF\xC0";
    u16(out, 17);
    out += (char)8;
    u16(out, h);
    u16(out, w);
    out += (char)3;
    out += (char)1;
    out += (char)0x11;
    out += (char)0;  // Y
    out += (char)2;
    out += (char)0x11;
    out += (char)1;  // Cb
    out += (char)3;
    out += (char)0x11;
    out += (char)1;  // Cr
    // DHT
    auto dht = [&](int cls_id, const uint8_t bits[16], const uint8_t* vals, int nval) {
        out += "\xFF\xC4";
        u16(out, 19 + nval);
        out += (char)cls_id;
        for (int i = 0; i < 16; ++i) out += (char)bits[i];
        for (int i = 0; i < nval; ++i) out += (char)vals[i];
    };
    dht(0x00, kDcLumBits, kDcLumVal, 12);
    dht(0x10, kAcLumBits, kAcLumVal, 162);
    dht(0x01, kDcChrBits, kDcChrVal, 12);
    dht(0x11, kAcChrBits, kAcChrVal, 162);
    // SOS
    out += "\xFF\xDA";
    u16(out, 12);
    out += (char)3;
    out += (char)1;
    out += (char)0x00;
    out += (char)2;
    out += (char)0x11;
    out += (char)3;
    out += (char)0x11;
    out += (char)0;
    out += (char)63;
    out += (char)0;

    BitWriter bw(out);
    int dcY = 0, dcB = 0, dcR = 0;
    auto at = [&](int x, int y) -> const uint32_t& {
        if (x >= w) x = w - 1;
        if (y >= h) y = h - 1;
        return bgra[(size_t)y * w + x];
    };
    for (int by = 0; by < h; by += 8)
        for (int bx = 0; bx < w; bx += 8) {
            double Y[64], Cb[64], Cr[64];
            for (int yy = 0; yy < 8; ++yy)
                for (int xx = 0; xx < 8; ++xx) {
                    uint32_t c = at(bx + xx, by + yy);
                    double r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
                    int i = yy * 8 + xx;
                    Y[i] = 0.299 * r + 0.587 * g + 0.114 * b - 128.0;
                    Cb[i] = -0.168736 * r - 0.331264 * g + 0.5 * b;
                    Cr[i] = 0.5 * r - 0.418688 * g - 0.081312 * b;
                }
            dcY = encodeBlock(Y, lq, dcY, dcLum, acLum, bw);
            dcB = encodeBlock(Cb, cq, dcB, dcChr, acChr, bw);
            dcR = encodeBlock(Cr, cq, dcR, dcChr, acChr, bw);
        }
    bw.flush();
    out += "\xFF\xD9";  // EOI
    return out;
}

// ---- Motion-JPEG AVI -------------------------------------------------------
bool AviWriter::put(const void* p, uint32_t n) {
    DWORD wr = 0;
    return WriteFile((HANDLE)h_, p, n, &wr, nullptr) && wr == n;
}
bool AviWriter::at(uint32_t pos, uint32_t v) {
    LARGE_INTEGER li;
    li.QuadPart = pos;
    if (!SetFilePointerEx((HANDLE)h_, li, nullptr, FILE_BEGIN)) return false;
    DWORD wr = 0;
    bool ok = WriteFile((HANDLE)h_, &v, 4, &wr, nullptr) && wr == 4;
    li.QuadPart = 0;
    SetFilePointerEx((HANDLE)h_, li, nullptr, FILE_END);
    return ok;
}

bool AviWriter::open(const wstr& path, int w, int h, int fps) {
    close();
    if (w <= 0 || h <= 0) return false;
    w_ = w;
    h_px_ = h;
    fps_ = fps < 1 ? 1 : fps;
    frames_ = 0;
    idx_.clear();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    h_ = f;
    auto fourcc = [&](const char* c) { put(c, 4); };
    auto dword = [&](uint32_t v) { put(&v, 4); };
    // RIFF header
    fourcc("RIFF");
    dword(0);  // RIFF size, patched at close  (offset 4)
    fourcc("AVI ");
    // hdrl: 'hdrl'(4) + avih chunk(8+56) + strl LIST(8 + 116) = 192
    fourcc("LIST");
    dword(4 + (8 + 56) + (8 + 116));
    fourcc("hdrl");
    // avih (56 bytes)
    fourcc("avih");
    dword(56);
    dword(1000000 / fps_);  // microseconds per frame
    dword(0);               // max bytes/sec
    dword(0);               // padding
    dword(0x10);            // flags: AVIF_HASINDEX
    dword(0);               // total frames, patched (this chunk field offset tracked below)
    dword(0);               // initial frames
    dword(1);               // streams
    dword(0);               // suggested buffer
    dword((uint32_t)w);
    dword((uint32_t)h);
    dword(0);
    dword(0);
    dword(0);
    dword(0);
    // strl
    fourcc("LIST");
    dword(4 + (8 + 56) + (8 + 40));  // 'strl' + strh + strf
    fourcc("strl");
    // strh (56 bytes)
    fourcc("strh");
    dword(56);
    fourcc("vids");
    fourcc("MJPG");
    dword(0);         // flags
    dword(0);         // priority/language
    dword(0);         // initial frames
    dword(1);         // scale
    dword((uint32_t)fps_);  // rate -> fps
    dword(0);         // start
    dword(0);         // length (frames), patched
    dword(0);         // suggested buffer
    dword(0xFFFFFFFF);// quality
    dword(0);         // sample size
    dword(0);         // rcFrame left/top
    dword(((uint32_t)h << 16) | (uint32_t)w);  // right|bottom (approx; readers ignore)
    // strf (BITMAPINFOHEADER, 40 bytes)
    fourcc("strf");
    dword(40);
    dword(40);
    dword((uint32_t)w);
    dword((uint32_t)h);
    put("\x01\x00\x18\x00", 4);  // planes 1, bit count 24
    fourcc("MJPG");
    dword((uint32_t)(w * h * 3));
    dword(0);
    dword(0);
    dword(0);
    dword(0);
    // movi
    fourcc("LIST");
    LARGE_INTEGER cur{};
    LARGE_INTEGER zero{};
    SetFilePointerEx(f, zero, &cur, FILE_CURRENT);
    moviSizePos_ = (uint32_t)cur.QuadPart;  // where the movi LIST size goes
    dword(0);                                // movi size, patched
    SetFilePointerEx(f, zero, &cur, FILE_CURRENT);
    moviPos_ = (uint32_t)cur.QuadPart;       // the 'movi' FourCC position
    fourcc("movi");
    return isOpen();
}

bool AviWriter::addFrame(const std::string& jpeg) {
    if (!isOpen() || jpeg.empty()) return false;
    LARGE_INTEGER cur{}, zero{};
    SetFilePointerEx((HANDLE)h_, zero, &cur, FILE_CURRENT);
    uint32_t off = (uint32_t)cur.QuadPart - moviPos_;  // from the 'movi' FourCC
    uint32_t sz = (uint32_t)jpeg.size();
    put("00dc", 4);
    put(&sz, 4);
    if (!put(jpeg.data(), sz)) return false;
    if (sz & 1) put("\x00", 1);  // chunks are word-aligned
    idx_.push_back({off, sz});
    ++frames_;
    return true;
}

bool AviWriter::close() {
    if (!isOpen()) {
        h_ = nullptr;
        return true;
    }
    HANDLE f = (HANDLE)h_;
    LARGE_INTEGER cur{}, zero{};
    SetFilePointerEx(f, zero, &cur, FILE_CURRENT);
    uint32_t moviEnd = (uint32_t)cur.QuadPart;
    // idx1
    put("idx1", 4);
    uint32_t idxBytes = (uint32_t)idx_.size() * 16;
    put(&idxBytes, 4);
    for (auto& e : idx_) {
        put("00dc", 4);
        uint32_t flags = 0x10;  // AVIIF_KEYFRAME
        put(&flags, 4);
        put(&e.first, 4);
        put(&e.second, 4);
    }
    SetFilePointerEx(f, zero, &cur, FILE_CURRENT);
    uint32_t fileEnd = (uint32_t)cur.QuadPart;
    // patch sizes
    at(4, fileEnd - 8);                         // RIFF size
    at(moviSizePos_, (moviEnd - moviPos_) + 4); // movi LIST size = 'movi' + frames
    at(0x30, (uint32_t)frames_);                // avih dwTotalFrames
    at(0x8C, (uint32_t)frames_);                // strh dwLength (frames)
    CloseHandle(f);
    h_ = nullptr;
    idx_.clear();
    return true;
}

// ---- MP4 (H.264) through Media Foundation, loaded at run time -------------
namespace {

// Media Foundation GUIDs (defined here so no Media Foundation import library is linked)
const GUID kMajorType = {0x48eba18e, 0xf8c9, 0x4687, {0xbf, 0x11, 0x0a, 0x74, 0xc9, 0xf9, 0x6a, 0x8f}};  // MF_MT_MAJOR_TYPE
const GUID kSubtype = {0xf7e34c9a, 0x42e8, 0x4714, {0xb7, 0x4b, 0xcb, 0x29, 0xd7, 0x2c, 0x35, 0xe5}};     // MF_MT_SUBTYPE
const GUID kBitrate = {0x20332624, 0xfb0d, 0x4d9e, {0xbd, 0x0d, 0xcb, 0xf6, 0x78, 0x6c, 0x10, 0x2e}};     // MF_MT_AVG_BITRATE
const GUID kInterlace = {0xe2724bb8, 0xe676, 0x4806, {0xb4, 0xb2, 0xa8, 0xd6, 0xef, 0xb4, 0x4c, 0xcd}};   // MF_MT_INTERLACE_MODE
const GUID kFrameSize = {0x1652c33d, 0xd6b2, 0x4012, {0xb8, 0x34, 0x72, 0x03, 0x08, 0x49, 0xa3, 0x7d}};   // MF_MT_FRAME_SIZE
const GUID kFrameRate = {0xc459a2e8, 0x3d2c, 0x4e44, {0xb1, 0x32, 0xfe, 0xe5, 0x15, 0x6c, 0x7b, 0xb0}};   // MF_MT_FRAME_RATE
const GUID kPixelAspect = {0xc6376a1e, 0x8d0a, 0x4027, {0xbe, 0x45, 0x6d, 0x9a, 0x0a, 0xd3, 0x9b, 0xb6}}; // MF_MT_PIXEL_ASPECT_RATIO
const GUID kVideoMajor = {0x73646976, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};  // MFMediaType_Video
const GUID kFmtRGB32 = {0x00000016, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};    // MFVideoFormat_RGB32
const GUID kFmtH264 = {0x34363248, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};     // MFVideoFormat_H264

typedef HRESULT(STDAPICALLTYPE* PStartup)(ULONG, DWORD);
typedef HRESULT(STDAPICALLTYPE* PShutdown)();
typedef HRESULT(STDAPICALLTYPE* PCreateMediaType)(IMFMediaType**);
typedef HRESULT(STDAPICALLTYPE* PCreateSample)(IMFSample**);
typedef HRESULT(STDAPICALLTYPE* PCreateMemoryBuffer)(DWORD, IMFMediaBuffer**);
typedef HRESULT(STDAPICALLTYPE* PCreateSinkWriterFromURL)(LPCWSTR, IMFByteStream*, IMFAttributes*, IMFSinkWriter**);
typedef HRESULT(STDAPICALLTYPE* PCoInit)(LPVOID, DWORD);
typedef void(STDAPICALLTYPE* PCoUninit)();

struct MF {
    PStartup startup = nullptr;
    PShutdown shutdown = nullptr;
    PCreateMediaType createType = nullptr;
    PCreateSample createSample = nullptr;
    PCreateMemoryBuffer createBuffer = nullptr;
    PCreateSinkWriterFromURL createWriter = nullptr;
    PCoInit coInit = nullptr;
    PCoUninit coUninit = nullptr;
    bool ok = false;
};

const MF& mf() {
    static MF m;
    static bool tried = false;
    if (tried) return m;
    tried = true;
    HMODULE plat = systemDll(L"mfplat.dll"), rw = systemDll(L"mfreadwrite.dll"), ole = systemDll(L"ole32.dll");
    if (!plat || !rw || !ole) return m;
    m.startup = (PStartup)GetProcAddress(plat, "MFStartup");
    m.shutdown = (PShutdown)GetProcAddress(plat, "MFShutdown");
    m.createType = (PCreateMediaType)GetProcAddress(plat, "MFCreateMediaType");
    m.createSample = (PCreateSample)GetProcAddress(plat, "MFCreateSample");
    m.createBuffer = (PCreateMemoryBuffer)GetProcAddress(plat, "MFCreateMemoryBuffer");
    m.createWriter = (PCreateSinkWriterFromURL)GetProcAddress(rw, "MFCreateSinkWriterFromURL");
    m.coInit = (PCoInit)GetProcAddress(ole, "CoInitializeEx");
    m.coUninit = (PCoUninit)GetProcAddress(ole, "CoUninitialize");
    m.ok = m.startup && m.shutdown && m.createType && m.createSample && m.createBuffer && m.createWriter && m.coInit && m.coUninit;
    return m;
}

void setSize(IMFMediaType* t, const GUID& key, uint32_t hi, uint32_t lo) { t->SetUINT64(key, ((uint64_t)hi << 32) | lo); }

}  // namespace

bool Mp4Writer::available() { return mf().ok; }

bool Mp4Writer::open(const wstr& path, int w, int h, int fps) {
    close();
    const MF& m = mf();
    if (!m.ok || w <= 0 || h <= 0) return false;
    w_ = w & ~1;
    h_ = h & ~1;
    fps_ = fps < 1 ? 1 : fps;
    frames_ = 0;
    t_ = 0;
    dur_ = 10000000LL / fps_;
    HRESULT hr = m.coInit(nullptr, 0x0);  // COINIT_MULTITHREADED = 0
    coInit_ = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    if (FAILED(m.startup(MF_VERSION, 1 /*MFSTARTUP_LITE*/))) return false;
    mfInit_ = true;
    IMFSinkWriter* writer = nullptr;
    if (FAILED(m.createWriter(path.c_str(), nullptr, nullptr, &writer)) || !writer) return false;
    uint32_t bitrate = (uint32_t)((double)w_ * h_ * fps_ * 0.12);
    if (bitrate < 1500000) bitrate = 1500000;
    if (bitrate > 16000000) bitrate = 16000000;
    IMFMediaType *out = nullptr, *in = nullptr;
    bool ok = SUCCEEDED(m.createType(&out)) && out;
    if (ok) {
        out->SetGUID(kMajorType, kVideoMajor);
        out->SetGUID(kSubtype, kFmtH264);
        out->SetUINT32(kBitrate, bitrate);
        out->SetUINT32(kInterlace, 2 /*progressive*/);
        setSize(out, kFrameSize, w_, h_);
        setSize(out, kFrameRate, fps_, 1);
        setSize(out, kPixelAspect, 1, 1);
        ok = SUCCEEDED(writer->AddStream(out, &stream_));
    }
    if (ok) ok = SUCCEEDED(m.createType(&in)) && in;
    if (ok) {
        in->SetGUID(kMajorType, kVideoMajor);
        in->SetGUID(kSubtype, kFmtRGB32);
        in->SetUINT32(kInterlace, 2);
        setSize(in, kFrameSize, w_, h_);
        setSize(in, kFrameRate, fps_, 1);
        setSize(in, kPixelAspect, 1, 1);
        ok = SUCCEEDED(writer->SetInputMediaType(stream_, in, nullptr));
    }
    if (ok) ok = SUCCEEDED(writer->BeginWriting());
    if (out) out->Release();
    if (in) in->Release();
    if (!ok) {
        writer->Release();
        return false;
    }
    writer_ = writer;
    return true;
}

bool Mp4Writer::addFrame(const uint32_t* bgra) {
    if (!writer_ || !bgra) return false;
    const MF& m = mf();
    IMFMediaBuffer* buf = nullptr;
    DWORD bytes = (DWORD)w_ * h_ * 4;
    if (FAILED(m.createBuffer(bytes, &buf)) || !buf) return false;
    BYTE* dst = nullptr;
    bool ok = SUCCEEDED(buf->Lock(&dst, nullptr, nullptr));
    if (ok) {
        // RGB32 default is bottom-up: write source row y to destination row (h-1-y)
        for (int y = 0; y < h_; ++y) memcpy(dst + (size_t)(h_ - 1 - y) * w_ * 4, bgra + (size_t)y * w_, (size_t)w_ * 4);
        buf->Unlock();
        buf->SetCurrentLength(bytes);
    }
    IMFSample* s = nullptr;
    if (ok) ok = SUCCEEDED(m.createSample(&s)) && s;
    if (ok) {
        s->AddBuffer(buf);
        s->SetSampleTime(t_);
        s->SetSampleDuration(dur_);
        ok = SUCCEEDED(((IMFSinkWriter*)writer_)->WriteSample(stream_, s));
    }
    if (s) s->Release();
    buf->Release();
    if (ok) {
        t_ += dur_;
        ++frames_;
    }
    return ok;
}

bool Mp4Writer::close() {
    if (writer_) {
        ((IMFSinkWriter*)writer_)->Finalize();
        ((IMFSinkWriter*)writer_)->Release();
        writer_ = nullptr;
    }
    const MF& m = mf();
    if (mfInit_ && m.shutdown) m.shutdown();
    mfInit_ = false;
    if (coInit_ && m.coUninit) m.coUninit();
    coInit_ = false;
    return true;
}

bool recordTestMp4(const wstr& path, int& frames, wstr& err) {
    frames = 0;
    if (!Mp4Writer::available()) {
        err = L"Media Foundation is not available here.";
        return false;
    }
    const int w = 160, h = 120, fps = 10, n = 30;
    Mp4Writer mv;
    if (!mv.open(path, w, h, fps)) {
        err = L"The MP4 could not be opened for writing.";
        return false;
    }
    std::vector<uint32_t> fr((size_t)w * h);
    for (int k = 0; k < n; ++k) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) fr[(size_t)y * w + x] = ((x + k * 4 & 0xFF) << 16) | ((y + k * 2 & 0xFF) << 8) | (k * 8 & 0xFF);
        if (!mv.addFrame(fr.data())) {
            err = L"A frame was refused at index " + std::to_wstring(k) + L".";
            mv.close();
            return false;
        }
    }
    frames = mv.frames();
    mv.close();
    return frames == n;
}

// ---- validators (self-test) ------------------------------------------------
bool pngLooksValid(const std::string& p, int& w, int& h) {
    if (p.size() < 8 + 12 + 13 || memcmp(p.data(), "\x89PNG\r\n\x1a\n", 8)) return false;
    if (memcmp(p.data() + 12, "IHDR", 4)) return false;
    auto rd = [&](size_t o) { return (uint32_t)((uint8_t)p[o] << 24 | (uint8_t)p[o + 1] << 16 | (uint8_t)p[o + 2] << 8 | (uint8_t)p[o + 3]); };
    w = (int)rd(16);
    h = (int)rd(20);
    // check the IHDR chunk's CRC
    uint32_t stored = rd(8 + 4 + 4 + 13);
    uint32_t calc = crc32Of(p.substr(12, 4 + 13));
    if (stored != calc) return false;
    return p.size() >= 8 && memcmp(p.data() + p.size() - 8, "IEND", 4) == 0;
}

bool mp4LooksValid(const std::string& m) {
    // an MP4 begins with an 'ftyp' box and (once finalised) carries 'moov' and 'mdat'
    return m.size() > 256 && memcmp(m.data() + 4, "ftyp", 4) == 0 && m.find("moov") != std::string::npos && m.find("mdat") != std::string::npos;
}

bool jpegLooksValid(const std::string& j) {
    if (j.size() < 4 || (uint8_t)j[0] != 0xFF || (uint8_t)j[1] != 0xD8) return false;
    if ((uint8_t)j[j.size() - 2] != 0xFF || (uint8_t)j[j.size() - 1] != 0xD9) return false;
    bool sof = false, sos = false;
    size_t sosEnd = 0;
    for (size_t i = 2; i + 1 < j.size();) {
        if ((uint8_t)j[i] != 0xFF) {
            ++i;
            continue;
        }
        uint8_t m = (uint8_t)j[i + 1];
        if (m == 0xC0) sof = true;
        if (m == 0xDA) {
            sos = true;
            int len = (uint8_t)j[i + 2] << 8 | (uint8_t)j[i + 3];
            sosEnd = i + 2 + len;
            break;
        }
        if (m == 0xD8 || m == 0xD9) {
            i += 2;
            continue;
        }
        int len = (uint8_t)j[i + 2] << 8 | (uint8_t)j[i + 3];
        i += 2 + len;
    }
    if (!sof || !sos || !sosEnd) return false;
    // every 0xFF in the entropy data must be stuffed (0x00) until the final EOI
    for (size_t i = sosEnd; i + 1 < j.size(); ++i)
        if ((uint8_t)j[i] == 0xFF) {
            uint8_t n = (uint8_t)j[i + 1];
            if (n == 0xD9) break;        // end
            if (n != 0x00) return false;  // an unstuffed 0xFF is a broken stream
            ++i;
        }
    return true;
}

}  // namespace bd

// Saving a picture of the screen, and recording the screen to a video file, with no extra
// system libraries, so it works the same on the rescue stick (WinPE) as in Windows.
//   - a screenshot is saved as a PNG (lossless, opens anywhere)
//   - a recording is saved as a Motion-JPEG .avi (opens in Windows Media Player, VLC, ...)
// No sound is ever captured: there is no audio track and no microphone is opened.
//
// The pixels come from what the program already drew (its own canvas), so nothing else on
// the screen is captured, and no screen-grab or recording permission is needed.
#pragma once
#include "Disk.h"

namespace bd {

// Writes a 24-bit PNG from top-down BGRA/BGRX pixels (the canvas' own format).
bool writePng(const wstr& path, const uint32_t* bgra, int w, int h);

// Encodes one frame as a baseline JPEG (4:4:4). quality 1..100. Empty on failure.
std::string encodeJpeg(const uint32_t* bgra, int w, int h, int quality);

// A Motion-JPEG .avi written one frame at a time to keep memory low. No audio stream.
class AviWriter {
public:
    ~AviWriter() { close(); }
    bool open(const wstr& path, int w, int h, int fps);
    bool addFrame(const std::string& jpeg);  // a JPEG from encodeJpeg at the opening size
    bool close();                            // writes the index and the final sizes
    bool isOpen() const { return h_ != nullptr && h_ != (void*)-1; }
    int frames() const { return frames_; }
    int width() const { return w_; }
    int height() const { return h_px_; }

private:
    void* h_ = nullptr;  // HANDLE
    int w_ = 0, h_px_ = 0, fps_ = 10, frames_ = 0;
    uint32_t moviPos_ = 0, moviSizePos_ = 0;          // file offsets of the 'movi' FourCC and its size field
    std::vector<std::pair<uint32_t, uint32_t>> idx_;  // each frame: offset from 'movi', size
    bool put(const void* p, uint32_t n);
    bool at(uint32_t pos, uint32_t v);               // patch a DWORD
};

// An MP4 (H.264) recording through Media Foundation, which is loaded at run time (it is part of
// Windows but not of the rescue environment, so recording falls back to the .avi there). No audio.
class Mp4Writer {
public:
    ~Mp4Writer() { close(); }
    bool open(const wstr& path, int w, int h, int fps);
    bool addFrame(const uint32_t* bgra);  // top-down BGRA, width*height, at the opening size
    bool close();
    bool isOpen() const { return writer_ != nullptr; }
    int frames() const { return frames_; }
    int width() const { return w_; }
    int height() const { return h_; }
    static bool available();  // Media Foundation can be loaded here

private:
    void* writer_ = nullptr;  // IMFSinkWriter*
    unsigned long stream_ = 0;
    int w_ = 0, h_ = 0, fps_ = 10, frames_ = 0;
    long long t_ = 0, dur_ = 0;
    bool coInit_ = false, mfInit_ = false;
};

// Records a short test clip (a moving gradient) to an .mp4, for the self-test / --mp4test.
bool recordTestMp4(const wstr& path, int& frames, wstr& err);

// For the self-test: that a PNG / JPEG / AVI buffer is well-formed.
bool pngLooksValid(const std::string& png, int& w, int& h);
bool jpegLooksValid(const std::string& jpeg);
bool mp4LooksValid(const std::string& mp4);

uint32_t crc32Of(const std::string& s);  // also used by the AVI index self-test

}  // namespace bd

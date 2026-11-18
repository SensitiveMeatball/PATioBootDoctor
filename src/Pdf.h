// A one-page PDF made from a picture of the page (see Pdf.cpp).
#pragma once
#include "core/Common.h"

namespace bd {

// bgra: w*h pixels, top row first (as a Canvas holds them). The page is A4.
bool writePagePdf(const wstr& path, const uint32_t* bgra, int w, int h, const std::string& title);
std::string packBits(const std::string& in);  // PDF RunLengthDecode data

}  // namespace bd

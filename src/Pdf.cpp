// A one-page PDF that holds a picture of the page (so any language and the logo look the
// same everywhere, with no fonts to embed). The picture is run-length packed.
#include "Pdf.h"

namespace bd {

std::string packBits(const std::string& in) {
    std::string out;
    size_t i = 0, n = in.size();
    while (i < n) {
        size_t run = 1;
        while (i + run < n && run < 128 && in[i + run] == in[i]) ++run;
        if (run >= 2) {
            out += (char)(257 - run);
            out += in[i];
            i += run;
            continue;
        }
        size_t lit = 1;
        while (i + lit < n && lit < 128 && !(i + lit + 1 < n && in[i + lit] == in[i + lit + 1])) ++lit;
        out += (char)(lit - 1);
        out.append(in, i, lit);
        i += lit;
    }
    out += (char)128;  // end of data
    return out;
}

bool writePagePdf(const wstr& path, const uint32_t* bgra, int w, int h, const std::string& title) {
    std::string rgb((size_t)w * h * 3, '\0');
    for (size_t p = 0, n = (size_t)w * h; p < n; ++p) {
        uint32_t c = bgra[p];
        rgb[p * 3] = (char)(c >> 16);
        rgb[p * 3 + 1] = (char)(c >> 8);
        rgb[p * 3 + 2] = (char)c;
    }
    std::string img = packBits(rgb);
    std::string content = "q 595 0 0 842 0 0 cm /Im0 Do Q\n";
    std::vector<std::string> obj = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /XObject << /Im0 4 0 R >> >> /Contents 5 0 R >>",
        "<< /Type /XObject /Subtype /Image /Width " + std::to_string(w) + " /Height " + std::to_string(h) +
            " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /RunLengthDecode /Length " + std::to_string(img.size()) + " >>\nstream\n" + img + "\nendstream",
        "<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "endstream",
        "<< /Title (" + title + ") /Producer (PATio Boot Doctor) >>",
    };
    std::string pdf = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    std::vector<size_t> at;
    for (size_t i = 0; i < obj.size(); ++i) {
        at.push_back(pdf.size());
        pdf += std::to_string(i + 1) + " 0 obj\n" + obj[i] + "\nendobj\n";
    }
    size_t xref = pdf.size();
    pdf += "xref\n0 " + std::to_string(obj.size() + 1) + "\n0000000000 65535 f \n";
    for (size_t o : at) {
        char b[24];
        snprintf(b, 24, "%010zu 00000 n \n", o);
        pdf += b;
    }
    pdf += "trailer\n<< /Size " + std::to_string(obj.size() + 1) + " /Root 1 0 R /Info 6 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    bool ok = WriteFile(f, pdf.data(), (DWORD)pdf.size(), &wr, nullptr) && wr == pdf.size();
    CloseHandle(f);
    return ok;
}

}  // namespace bd

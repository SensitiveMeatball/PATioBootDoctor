// See Suggest.h. No user interface here; the dialog lives in app/suggest.inl.
#include "Suggest.h"

namespace bd {

std::string urlEncode(const wstr& text) {
    static const char* hex = "0123456789ABCDEF";
    std::string utf8 = wideToUtf8(text), out;
    out.reserve(utf8.size() * 3);
    for (unsigned char c : utf8) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            out.push_back((char)c);
        else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

wstr suggestionRecord(const wstr& stamp, const wstr& kind, const wstr& version, const wstr& contact, const wstr& message) {
    wstr msg;
    msg.reserve(message.size());
    for (size_t i = 0; i < message.size(); ++i) {
        wchar_t c = message[i];
        if (c == L'\\')
            msg += L"\\\\";
        else if (c == L'\n')
            msg += L"\\n";
        else if (c == L'\r') {
            msg += L"\\n";
            if (i + 1 < message.size() && message[i + 1] == L'\n') ++i;  // treat CRLF as one newline
        } else
            msg += c;
    }
    return stamp + L" | " + kind + L" | app " + version + L" | " + (contact.empty() ? wstr(L"-") : contact) + L" | " + msg;
}

}  // namespace bd

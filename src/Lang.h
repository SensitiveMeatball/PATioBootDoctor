// Translations. Every text the user sees goes through T(L"English text"), which
// returns it in the chosen language, or in English when a pack has no
// translation for it. TF() fills in {0}, {1}, ... placeholders.
//
// Language packs are UTF-8 text files (.lang) in a simple gettext-like form:
//
//   @code tr
//   @name Türkçe
//   @rtl no
//
//   msgid "Scan again"
//   msgstr "Yeniden tara"
//
// Built-in packs are compiled into the exe (resources LANG_<CODE>). Packs in the
// "lang" folder next to the exe (or in %APPDATA%\PATio Boot Doctor\lang when that
// folder cannot be written) add languages or override built-in ones, so anyone
// can add a language or correct a text without rebuilding.
#pragma once
#include "core/Common.h"
#include <initializer_list>

namespace i18n {

struct LangInfo {
    wstr code;      // tr, de, ar, ...
    wstr name;      // in its own language: Türkçe, Deutsch, العربية
    wstr file;      // empty for built-in
    bool rtl = false;
    bool builtIn = false;
    size_t strings = 0;
};

void init(const wstr& exeDir);
std::vector<LangInfo> languages();
bool setLanguage(const wstr& code);  // "en" = English
const wstr& current();
bool rtl();
wstr userFolder();                   // where added packs go
bool importPack(const wstr& file, wstr& code, wstr& err);
// Writes a template with every text the program knows (for a new translation).
bool writeTemplate(const wstr& path, const wstr& code, const wstr& name, bool rtl, wstr& err);

const wstr& tr(const wchar_t* english);
const wstr& tr(const wstr& english);
wstr trf(const wchar_t* english, std::initializer_list<wstr> args);

// Parsing (exposed for the self-test)
struct Pack {
    wstr code, name;
    bool rtl = false;
    std::vector<std::pair<wstr, wstr>> entries;
};
bool parsePack(const std::string& utf8, Pack& out);

}  // namespace i18n

#define T(s) ::i18n::tr(s)
// Marks a text as translatable without translating it here (an id such as a step name): it is translated where it is shown.
#define N(s) s
#define TF(s, ...) ::i18n::trf(s, {__VA_ARGS__})

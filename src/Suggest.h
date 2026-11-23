// "Make a suggestion": the small pieces of logic behind the suggestion box.
// The box saves what the technician types to a local suggestions.txt first (so it
// works with no internet), then can open a pre-filled GitHub issue or a mailto link.
// These two free functions are the parts worth a self-test: the percent-encoder that
// builds those URLs, and the one-line record written to suggestions.txt.
#pragma once
#include "core/Common.h"
#include <string>

namespace bd {

// Percent-encodes text for a URL (query or mailto): the wide text is turned into
// UTF-8 bytes, then every byte except the unreserved set (A-Za-z0-9 - _ . ~) is
// written as %XX with upper-case hex. A space becomes %20, not '+'.
std::string urlEncode(const wstr& text);

// One suggestions.txt line, fields in a fixed order, separated by " | ":
//   <stamp> | <kind> | app <version> | <contact or -> | <message>
// New lines in the message are escaped (\\ -> \\\\, newline -> \n) so one
// suggestion is always one line. No trailing newline.
wstr suggestionRecord(const wstr& stamp, const wstr& kind, const wstr& version, const wstr& contact, const wstr& message);

}  // namespace bd

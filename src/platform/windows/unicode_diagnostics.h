#pragma once

#include <string>
#include <string_view>

namespace clipss2::platform::windows
{
[[nodiscard]] std::string utf8_from_utf16(std::wstring_view value);
// Invalid text is a programming error; unavailable diagnostic sinks are nonfatal.
// Console output is UTF-16, redirected output UTF-8; no handle ownership changes.
void write_stdout_utf8(std::string_view value);
void write_stderr_utf8(std::string_view value);
}

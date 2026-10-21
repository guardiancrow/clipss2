#pragma once
#include <string_view>

namespace clipss2::platform::windows
{
inline constexpr wchar_t portable_settings_marker_name[] = L"clipss2.portable";
// Exact ASCII bytes, LF, no BOM. A different version must not select another store.
inline constexpr std::string_view portable_settings_marker_content = "clipss2-portable-v1\n";
}

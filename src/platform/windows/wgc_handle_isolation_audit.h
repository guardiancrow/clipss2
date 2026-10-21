#pragma once

#include <string_view>

namespace clipss2::platform::windows
{
[[nodiscard]] int run_wgc_handle_isolation_audit(std::string_view probe);
}

#pragma once

#include <string_view>

namespace clipss2::platform::windows
{
[[nodiscard]] int run_wgc_item_reuse_audit(std::string_view probe);
}

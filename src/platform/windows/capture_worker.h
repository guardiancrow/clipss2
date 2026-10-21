#pragma once

#include <span>
#include <string_view>

namespace clipss2::platform::windows
{
int run_capture_worker(std::span<const std::string_view> arguments) noexcept;
}

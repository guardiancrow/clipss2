#pragma once
#include "platform/windows/display_color.h"

namespace clipss2::platform::windows
{
DisplayColorInfo fallback_display_color(std::uintptr_t monitor, ColorCheck check);
void require_single_monitor_window(std::uintptr_t window, std::uintptr_t monitor);
std::string monitor_color_identity(std::uintptr_t monitor);
}

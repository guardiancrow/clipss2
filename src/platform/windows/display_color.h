#pragma once

#include <string>
#include <cstdint>

#include "platform/windows/one_frame_capture.h"

namespace clipss2::platform::windows
{
enum class ColorCheck { precheck, postcheck };
struct DisplayColorInfo
{
    std::string acquisition_method;
    std::string diagnostic_note;
    std::string advanced_color_kind;
    bool sdr_available{};
    bool wcg_available{};
    bool hdr_available{};
    float sdr_white_level_in_nits{};
    float min_luminance_in_nits{};
    float max_luminance_in_nits{};
    float max_average_full_frame_luminance_in_nits{};
    // Opaque comparison token; must not be logged (contains display identities).
    std::string stability_token;
};

void require_unchanged_capture_color(const DisplayColorInfo& before, const DisplayColorInfo& after);

[[nodiscard]] DisplayColorInfo display_color_info_for_window(
    const WindowCaptureTarget& target, ColorCheck check = ColorCheck::precheck);

[[nodiscard]] DisplayColorInfo display_color_info_for_monitor(
    std::uintptr_t monitor_value, ColorCheck check = ColorCheck::precheck);
}

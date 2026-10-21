#pragma once

#include "platform/windows/window_candidates.h"
#include <stdexcept>
#include <string>
#include <string_view>

namespace clipss2::platform::windows
{
inline void require_sdr_capture_color(std::string_view kind)
{
    if (kind != "StandardDynamicRange")
        throw std::runtime_error("SDR BGRA8 output color policy pending/unsupported: " +
                                 std::string{kind});
}

// Small synchronous orchestration seam: cancellation cannot invoke any action;
// failed identity/color checks cannot enter the capture/encode action.
template<class Validate, class ReadColor, class Capture>
bool capture_confirmed_selection(const std::optional<WindowSelectionResult>& selected,
    Validate validate, ReadColor read_color, Capture capture)
{
    if (!selected) return false;
    if (!validate(selected->candidate))
        throw std::runtime_error("Selected target is stale or no longer selectable; capture not started.");
    require_sdr_capture_color(read_color(selected->candidate));
    // Color acquisition may take time: check again at the actual WGC boundary.
    if (!validate(selected->candidate))
        throw std::runtime_error("Selected target became stale before WGC capture.");
    capture(*selected);
    return true;
}
}

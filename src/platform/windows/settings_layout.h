#pragma once

#include <windows.h>
#include <algorithm>
#include <optional>
#include <utility>

namespace clipss2::platform::windows
{
// Geometry policy only: no monitor lookup, window mutation, or settings access.
inline RECT settings_transition_rect(RECT suggested, const RECT& work, int padding)
{
    const auto width = std::clamp(suggested.right - suggested.left, 1L,
        std::max(1L, work.right - work.left - 2 * padding));
    const auto height = std::clamp(suggested.bottom - suggested.top, 1L,
        std::max(1L, work.bottom - work.top - 2 * padding));
    // A work-area size limit must never become a top-left position constraint.
    return {suggested.left, suggested.top, suggested.left + width, suggested.top + height};
}

inline bool settings_same_rect(const RECT& left, const RECT& right) noexcept
{
    return left.left == right.left && left.top == right.top &&
        left.right == right.right && left.bottom == right.bottom;
}

struct SettingsLayoutRequest
{
    UINT dpi{96};
    std::optional<RECT> suggested;
    std::optional<RECT> work; // Populated only by the compiled-in audit fixture.
};

inline bool settings_same_request(const SettingsLayoutRequest& left, const SettingsLayoutRequest& right)
{
    const auto equal = [](const auto& a, const auto& b) {
        return a.has_value() == b.has_value() && (!a || settings_same_rect(*a, *b));
    };
    return left.dpi == right.dpi && equal(left.suggested, right.suggested) && equal(left.work, right.work);
}

// A synchronous window notification can enqueue a newer request, but cannot
// recursively mutate fonts, child rectangles, or scroll state of the active pass.
class SettingsLayoutQueue
{
public:
    bool busy() const noexcept { return active_.has_value(); }
    void submit(const SettingsLayoutRequest& request)
    {
        if (!pending_ && active_ && settings_same_request(*active_, request)) return;
        pending_ = request;
    }
    std::optional<SettingsLayoutRequest> take()
    {
        active_ = std::exchange(pending_, std::nullopt);
        return active_;
    }
    void finish() noexcept { active_.reset(); }
    void clear() noexcept { active_.reset(); pending_.reset(); }
private:
    std::optional<SettingsLayoutRequest> active_, pending_;
};

inline int settings_scroll_offset(int old_offset, UINT old_dpi, UINT new_dpi,
    int control_top, int control_height, int total_height, int viewport_height)
{
    const int offset = MulDiv(old_offset, static_cast<int>(new_dpi), static_cast<int>(old_dpi ? old_dpi : 96));
    return std::clamp(control_top + std::min(offset, std::max(0, control_height - 1)),
        0, std::max(0, total_height - viewport_height));
}
}

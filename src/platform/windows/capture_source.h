#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

namespace clipss2::platform::windows
{
// Half-open, physical pixels. int64 arithmetic prevents signed LONG overflow.
struct PixelRectangle
{
    std::int32_t left{}, top{}, right{}, bottom{};
    [[nodiscard]] std::int64_t width() const noexcept { return std::int64_t{right} - left; }
    [[nodiscard]] std::int64_t height() const noexcept { return std::int64_t{bottom} - top; }
    bool operator==(const PixelRectangle&) const = default;
};
inline void validate_pixel_bounds(const PixelRectangle& r)
{
    if (r.width() <= 0 || r.height() <= 0 || r.width() > INT32_MAX || r.height() > INT32_MAX)
        throw std::invalid_argument("Invalid physical rectangle bounds.");
}
struct WindowSource { std::uintptr_t hwnd_value{}; std::uint32_t process_id{}; std::wstring class_name; };
struct MonitorSource
{
    std::wstring device_name;
    PixelRectangle expected_bounds;
    std::optional<PixelRectangle> crop; // Monitor-local, not desktop coordinates.
    std::string expected_topology; // Ephemeral sorted display-mode snapshot; never settings/metadata.
    bool operator==(const MonitorSource&) const = default;
};
using CaptureSource = std::variant<WindowSource, MonitorSource>;
inline void validate_monitor_source(const MonitorSource& source)
{
    if (!source.device_name.starts_with(L"\\\\.\\DISPLAY") || source.device_name.size() <= 11 || source.device_name.size() > 31 ||
        !std::all_of(source.device_name.begin() + 11, source.device_name.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; }))
        throw std::invalid_argument("Malformed monitor device identity.");
    validate_pixel_bounds(source.expected_bounds);
    if (source.expected_topology.size()>4096 || !std::all_of(source.expected_topology.begin(),source.expected_topology.end(),[](unsigned char c) {
        return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='\\'||c=='.'||c==':'||c==';'||c==','||c=='-'; }))
        throw std::invalid_argument("Malformed display topology snapshot.");
    if (source.crop)
    {
        validate_pixel_bounds(*source.crop);
        if (source.crop->left < 0 || source.crop->top < 0 || source.crop->right > source.expected_bounds.width() ||
            source.crop->bottom > source.expected_bounds.height()) throw std::invalid_argument("Crop outside monitor bounds.");
    }
}
inline std::optional<PixelRectangle> region_crop(const PixelRectangle& bounds,
    std::int32_t anchor_x, std::int32_t anchor_y, std::int32_t pointer_x, std::int32_t pointer_y)
{
    validate_pixel_bounds(bounds);
    if (anchor_x < bounds.left || anchor_x >= bounds.right || anchor_y < bounds.top || anchor_y >= bounds.bottom)
        throw std::invalid_argument("Region anchor outside owner monitor.");
    const auto x = std::clamp(pointer_x, bounds.left, bounds.right);
    const auto y = std::clamp(pointer_y, bounds.top, bounds.bottom);
    PixelRectangle result{
        static_cast<std::int32_t>(std::int64_t{std::min(anchor_x,x)} - bounds.left),
        static_cast<std::int32_t>(std::int64_t{std::min(anchor_y,y)} - bounds.top),
        static_cast<std::int32_t>(std::int64_t{std::max(anchor_x,x)} - bounds.left),
        static_cast<std::int32_t>(std::int64_t{std::max(anchor_y,y)} - bounds.top)};
    if (!result.width() || !result.height()) return {};
    return result; // Includes 1x1, 1xN and Nx1; no inclusive-edge adjustment.
}
}

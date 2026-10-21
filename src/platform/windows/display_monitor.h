#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace clipss2::platform::windows
{
struct DisplayRectangle
{
    long left{};
    long top{};
    long right{};
    long bottom{};
};

struct DxgiOutputInfo
{
    std::wstring device_name;
    unsigned int color_space{};
    unsigned int bits_per_color{};
    float min_luminance{};
    float max_luminance{};
    float max_full_frame_luminance{};
};

struct DisplayMonitorInfo
{
    std::size_t index{};
    std::uintptr_t monitor_value{};
    std::wstring device_name;
    bool primary{};
    DisplayRectangle monitor_rectangle;
    DisplayRectangle work_rectangle;
    unsigned int dpi_x{};
    unsigned int dpi_y{};
    std::optional<DxgiOutputInfo> dxgi_output;
    std::string dxgi_note;
};

[[nodiscard]] std::vector<DisplayMonitorInfo> enumerate_display_monitors();
}

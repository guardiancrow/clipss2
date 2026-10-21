#pragma once

#include "image/cpu_image.h"
#include <cstddef>
#include <span>

namespace clipss2::codec::jxl
{
struct JxlInspection
{
    std::uint32_t width{}, height{}, depth{}, image_count{};
    std::size_t file_bytes{};
    bool has_alpha{};
};
[[nodiscard]] JxlInspection inspect_jxl_structure(std::span<const std::byte> bytes);
[[nodiscard]] JxlInspection validate_screenshot_jxl(
    std::span<const std::byte> bytes, const image::CpuImage& canonical_straight_bgra8);
}

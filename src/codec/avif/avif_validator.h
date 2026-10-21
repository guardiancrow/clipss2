#pragma once

#include "image/cpu_image.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace clipss2::codec::avif
{
struct AvifInspection
{
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t depth{};
    std::size_t file_bytes{};
    std::uint32_t image_count{};
    bool has_alpha{};
};

[[nodiscard]] AvifInspection inspect_avif_structure(std::span<const std::byte> bytes);
[[nodiscard]] AvifInspection validate_screenshot_avif(
    std::span<const std::byte> bytes, const image::CpuImage& canonical_straight_bgra8);
}

#pragma once

#include "image/cpu_image.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace clipss2::codec::webp
{
struct WebpInspection
{
    std::uint32_t width{};
    std::uint32_t height{};
    std::size_t file_bytes{};
    std::size_t image_payload_count{};
};

// Strict clipss2 v1 allowlist: one canonical VP8L still-image chunk only.
[[nodiscard]] WebpInspection inspect_webp_structure(std::span<const std::byte> bytes);
[[nodiscard]] WebpInspection validate_screenshot_webp(
    std::span<const std::byte> bytes, const image::CpuImage& canonical_straight_bgra8);
}

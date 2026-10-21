#pragma once

#include "image/cpu_image.h"

#include <cstddef>
#include <span>
#include <vector>

namespace clipss2::codec::webp
{
inline constexpr std::size_t maximum_webp_bytes = 256U * 1024U * 1024U;

struct WebpEncoderPolicy
{
    int lossless{1};
    int exact{1};
    int method{4};
    int thread_level{1};
    int image_hint{3}; // WEBP_HINT_GRAPH, kept header-independent for diagnostics.
    float quality{75.0F};
};

[[nodiscard]] constexpr WebpEncoderPolicy product_webp_policy() noexcept { return {}; }

// Input is canonical straight/unassociated BGRA8, including RGB=0 when A=0.
[[nodiscard]] std::vector<std::byte> encode_lossless_webp_bytes(
    const image::CpuImage& straight_bgra8);

[[nodiscard]] image::CpuImage decode_bgra8_webp(std::span<const std::byte> bytes);
}

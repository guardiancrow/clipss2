#pragma once

#include "image/cpu_image.h"
#include "codec/lossy_profile.h"
#include <cstddef>
#include <span>
#include <vector>

namespace clipss2::codec::jxl
{
inline constexpr std::size_t maximum_jxl_bytes = 256U * 1024U * 1024U;
inline constexpr int product_jxl_effort = 7;
// Canonical straight BGRA8: RGB must already be zero when alpha is zero.
[[nodiscard]] std::vector<std::byte> encode_lossless_jxl_bytes(const image::CpuImage& source);
// Strict product codestream policy also applies to this decoder.
[[nodiscard]] image::CpuImage decode_bgra8_jxl(std::span<const std::byte> bytes);
struct JxlLossyConfig { float distance{}, alpha_distance{}; int effort{}; bool uses_original_profile{}; };
[[nodiscard]] std::vector<std::byte> encode_lossy_jxl_bytes(const image::CpuImage& source,
    LossyProfile profile, JxlLossyConfig* applied = nullptr);
}

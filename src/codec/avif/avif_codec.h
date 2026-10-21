#pragma once

#include "image/cpu_image.h"
#include "codec/lossy_profile.h"

#include <cstddef>
#include <span>
#include <vector>

namespace clipss2::codec::avif
{
inline constexpr std::size_t maximum_avif_bytes = 256U * 1024U * 1024U;

struct AvifEncoderPolicy
{
    int quality_lossless{100};
    int quality_alpha_lossless{100};
    int speed{6};
    int depth{8};
};

[[nodiscard]] constexpr AvifEncoderPolicy product_avif_policy() noexcept { return {}; }

// Input is canonical straight/unassociated BGRA8, including RGB=0 when A=0.
[[nodiscard]] std::vector<std::byte> encode_lossless_avif_bytes(
    const image::CpuImage& straight_bgra8);
[[nodiscard]] image::CpuImage decode_bgra8_avif(std::span<const std::byte> bytes);
struct AvifLossyConfig
{
    int quality{}, quality_alpha{}, speed{}, threads{}, depth{}, matrix{};
};
// Optional synchronous telemetry records the actual applied encoder settings, not stream inference.
[[nodiscard]] std::vector<std::byte> encode_lossy_avif_bytes(const image::CpuImage& source,
    LossyProfile profile, AvifLossyConfig* applied = nullptr);
}

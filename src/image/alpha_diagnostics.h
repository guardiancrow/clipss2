#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "image/cpu_image.h"

namespace clipss2::image
{
enum class AlphaAssociation
{
    straight_unassociated,
    premultiplied,
    forced_opaque,
    unknown,
};

struct AlphaStatistics
{
    std::uint8_t minimum{255};
    std::uint8_t maximum{};
    std::size_t non_opaque_count{};
    std::size_t zero_count{};
    std::size_t zero_alpha_nonzero_rgb_count{};
    std::size_t premultiplied_rgb_exceeds_alpha_count{};
};

struct AlphaProbeSample
{
    Bgra8Pixel logical_straight;
    Bgra8Pixel fixture_premultiplied;
    Bgra8Pixel observed;
};

struct AlphaClassification
{
    AlphaAssociation association{AlphaAssociation::unknown};
    std::uint8_t maximum_straight_error{};
    std::uint8_t maximum_premultiplied_error{};
    std::uint8_t maximum_alpha_error{};
};

[[nodiscard]] std::uint8_t premultiply_channel(
    std::uint8_t straight,
    std::uint8_t alpha) noexcept;
[[nodiscard]] Bgra8Pixel premultiply_bgra8(Bgra8Pixel straight) noexcept;
[[nodiscard]] AlphaStatistics analyze_bgra8_alpha(const CpuImage& image);
[[nodiscard]] CpuImage unpremultiply_bgra8_for_lossless(const CpuImage& image);
[[nodiscard]] CpuImage unpremultiply_bgra8_for_png(const CpuImage& image);
[[nodiscard]] AlphaClassification classify_alpha_samples(
    std::span<const AlphaProbeSample> samples,
    std::uint8_t tolerance = 2) noexcept;
[[nodiscard]] const char* alpha_association_name(AlphaAssociation association) noexcept;
}

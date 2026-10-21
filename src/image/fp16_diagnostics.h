#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "image/cpu_image.h"

namespace clipss2::image
{
struct RgbaFloat
{
    float red{};
    float green{};
    float blue{};
    float alpha{};
};

struct Fp16ChannelStatistics
{
    float min_finite{};
    float max_finite{};
    std::size_t finite_count{};
    std::size_t nan_count{};
    std::size_t positive_infinity_count{};
    std::size_t negative_infinity_count{};
};

struct Fp16ImageStatistics
{
    std::array<Fp16ChannelStatistics, 4> channels;
    std::size_t finite_rgb_negative_count{};
    std::size_t finite_rgb_above_one_count{};
};

[[nodiscard]] float binary16_to_float(std::uint16_t bits) noexcept;

[[nodiscard]] RgbaFloat read_rgba16_float_pixel(
    const CpuImage& image,
    std::uint32_t x,
    std::uint32_t y);

[[nodiscard]] Fp16ImageStatistics analyze_rgba16_float(
    const CpuImage& image);
}

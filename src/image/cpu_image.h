#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace clipss2::image
{
enum class PixelFormat
{
    unknown,
    bgra8_unorm,
    rgba8_unorm,
    rgba16_float,
};

struct CpuImage
{
    std::uint32_t width{};
    std::uint32_t height{};
    PixelFormat format{};
    std::vector<std::byte> pixels;
};

struct Bgra8Pixel
{
    std::uint8_t blue{};
    std::uint8_t green{};
    std::uint8_t red{};
    std::uint8_t alpha{};
};

[[nodiscard]] Bgra8Pixel read_bgra8_pixel(
    const CpuImage& image,
    std::uint32_t x,
    std::uint32_t y);
}

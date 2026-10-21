#include "image/cpu_image.h"

#include <limits>
#include <stdexcept>

namespace clipss2::image
{
Bgra8Pixel read_bgra8_pixel(
    const CpuImage& image,
    std::uint32_t x,
    std::uint32_t y)
{
    constexpr std::size_t bytes_per_pixel = 4;

    if (image.format != PixelFormat::bgra8_unorm)
    {
        throw std::invalid_argument("CPU image is not BGRA8 UNORM.");
    }
    if (image.width == 0 || image.height == 0 || x >= image.width || y >= image.height)
    {
        throw std::out_of_range("Pixel coordinates are outside the CPU image.");
    }

    const auto width = static_cast<std::size_t>(image.width);
    const auto height = static_cast<std::size_t>(image.height);
    if (width > std::numeric_limits<std::size_t>::max() / height ||
        width * height > std::numeric_limits<std::size_t>::max() / bytes_per_pixel)
    {
        throw std::overflow_error("CPU image dimensions overflow the address space.");
    }

    const std::size_t expected_size = width * height * bytes_per_pixel;
    if (image.pixels.size() != expected_size)
    {
        throw std::invalid_argument("CPU image buffer size does not match its dimensions.");
    }

    const std::size_t offset =
        (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) *
        bytes_per_pixel;
    return Bgra8Pixel{
        std::to_integer<std::uint8_t>(image.pixels[offset]),
        std::to_integer<std::uint8_t>(image.pixels[offset + 1]),
        std::to_integer<std::uint8_t>(image.pixels[offset + 2]),
        std::to_integer<std::uint8_t>(image.pixels[offset + 3])};
}
}

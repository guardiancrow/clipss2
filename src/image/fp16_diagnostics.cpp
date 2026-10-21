#include "image/fp16_diagnostics.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace clipss2::image
{
namespace
{
constexpr std::size_t channel_count = 4;
constexpr std::size_t bytes_per_channel = 2;
constexpr std::size_t bytes_per_pixel = channel_count * bytes_per_channel;

std::size_t checked_image_bytes(const CpuImage& image)
{
    if (image.format != PixelFormat::rgba16_float)
    {
        throw std::invalid_argument("CPU image is not RGBA16 FLOAT.");
    }
    if (image.width == 0 || image.height == 0)
    {
        throw std::invalid_argument("FP16 image dimensions must be non-zero.");
    }

    const auto width = static_cast<std::size_t>(image.width);
    const auto height = static_cast<std::size_t>(image.height);
    if (width > std::numeric_limits<std::size_t>::max() / height ||
        width * height > std::numeric_limits<std::size_t>::max() / bytes_per_pixel)
    {
        throw std::overflow_error("FP16 image dimensions overflow the address space.");
    }
    return width * height * bytes_per_pixel;
}

std::uint16_t read_half_bits(const std::byte* source) noexcept
{
    std::uint16_t bits{};
    std::memcpy(&bits, source, sizeof(bits));
    return bits;
}

void observe(Fp16ChannelStatistics& statistics, float value)
{
    if (std::isnan(value))
    {
        ++statistics.nan_count;
        return;
    }
    if (std::isinf(value))
    {
        if (std::signbit(value))
        {
            ++statistics.negative_infinity_count;
        }
        else
        {
            ++statistics.positive_infinity_count;
        }
        return;
    }

    if (statistics.finite_count == 0)
    {
        statistics.min_finite = value;
        statistics.max_finite = value;
    }
    else
    {
        statistics.min_finite = std::min(statistics.min_finite, value);
        statistics.max_finite = std::max(statistics.max_finite, value);
    }
    ++statistics.finite_count;
}

RgbaFloat read_pixel_unchecked(const std::byte* pixel) noexcept
{
    return {
        binary16_to_float(read_half_bits(pixel)),
        binary16_to_float(read_half_bits(pixel + 2)),
        binary16_to_float(read_half_bits(pixel + 4)),
        binary16_to_float(read_half_bits(pixel + 6))};
}
}

float binary16_to_float(std::uint16_t bits) noexcept
{
    const std::uint32_t sign = static_cast<std::uint32_t>(bits & 0x8000u) << 16;
    std::uint32_t exponent = (bits >> 10) & 0x1fu;
    std::uint32_t fraction = bits & 0x03ffu;
    std::uint32_t float_bits{};

    if (exponent == 0)
    {
        if (fraction == 0)
        {
            float_bits = sign;
        }
        else
        {
            std::uint32_t shift = 0;
            while ((fraction & 0x0400u) == 0)
            {
                fraction <<= 1;
                ++shift;
            }
            fraction &= 0x03ffu;
            exponent = 127u - 14u - shift;
            float_bits = sign | (exponent << 23) | (fraction << 13);
        }
    }
    else if (exponent == 0x1fu)
    {
        float_bits = sign | 0x7f800000u | (fraction << 13);
    }
    else
    {
        exponent += 127u - 15u;
        float_bits = sign | (exponent << 23) | (fraction << 13);
    }

    return std::bit_cast<float>(float_bits);
}

RgbaFloat read_rgba16_float_pixel(
    const CpuImage& image,
    std::uint32_t x,
    std::uint32_t y)
{
    const std::size_t expected_bytes = checked_image_bytes(image);
    if (image.pixels.size() != expected_bytes)
    {
        throw std::invalid_argument("FP16 image buffer size does not match its dimensions.");
    }
    if (x >= image.width || y >= image.height)
    {
        throw std::out_of_range("Pixel coordinates are outside the FP16 image.");
    }

    const std::size_t offset =
        (static_cast<std::size_t>(y) * image.width + x) * bytes_per_pixel;
    return read_pixel_unchecked(image.pixels.data() + offset);
}

Fp16ImageStatistics analyze_rgba16_float(const CpuImage& image)
{
    const std::size_t expected_bytes = checked_image_bytes(image);
    if (image.pixels.size() != expected_bytes)
    {
        throw std::invalid_argument("FP16 image buffer size does not match its dimensions.");
    }

    Fp16ImageStatistics result;
    for (std::size_t offset = 0; offset < expected_bytes; offset += bytes_per_pixel)
    {
        const auto pixel = read_pixel_unchecked(image.pixels.data() + offset);
        const std::array values{pixel.red, pixel.green, pixel.blue, pixel.alpha};
        for (std::size_t channel = 0; channel < values.size(); ++channel)
        {
            observe(result.channels[channel], values[channel]);
            if (channel < 3 && std::isfinite(values[channel]))
            {
                if (values[channel] < 0.0f)
                {
                    ++result.finite_rgb_negative_count;
                }
                if (values[channel] > 1.0f)
                {
                    ++result.finite_rgb_above_one_count;
                }
            }
        }
    }

    for (auto& channel : result.channels)
    {
        if (channel.finite_count == 0)
        {
            channel.min_finite = std::numeric_limits<float>::quiet_NaN();
            channel.max_finite = std::numeric_limits<float>::quiet_NaN();
        }
    }
    return result;
}
}

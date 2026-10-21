#include "image/fp16_diagnostics.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <utility>
#include <vector>

namespace
{
void append_half(std::vector<std::byte>& bytes, std::uint16_t bits)
{
    const auto original_size = bytes.size();
    bytes.resize(original_size + sizeof(bits));
    std::memcpy(bytes.data() + original_size, &bits, sizeof(bits));
}

bool equal(float left, float right) noexcept
{
    return left == right;
}
}

int main()
{
    using clipss2::image::binary16_to_float;

    if (!equal(binary16_to_float(0x0000), 0.0f) ||
        std::signbit(binary16_to_float(0x0000)) ||
        !equal(binary16_to_float(0x3c00), 1.0f) ||
        !equal(binary16_to_float(0xbc00), -1.0f) ||
        !equal(binary16_to_float(0x4000), 2.0f) ||
        !equal(binary16_to_float(0x0001), 0x1p-24f) ||
        !std::isinf(binary16_to_float(0x7c00)) ||
        std::signbit(binary16_to_float(0x7c00)) ||
        !std::isinf(binary16_to_float(0xfc00)) ||
        !std::signbit(binary16_to_float(0xfc00)) ||
        !std::isnan(binary16_to_float(0x7e00)))
    {
        std::cerr << "Known binary16 conversion failed.\n";
        return 1;
    }

    std::vector<std::byte> pixels;
    constexpr std::array<std::uint16_t, 8> values{
        0xbc00, 0x4000, 0x7c00, 0x3c00,
        0x7e00, 0xb800, 0xfc00, 0x0000};
    for (const std::uint16_t value : values)
    {
        append_half(pixels, value);
    }

    const clipss2::image::CpuImage image{
        2,
        1,
        clipss2::image::PixelFormat::rgba16_float,
        std::move(pixels)};
    const auto first = clipss2::image::read_rgba16_float_pixel(image, 0, 0);
    if (!equal(first.red, -1.0f) || !equal(first.green, 2.0f) ||
        !std::isinf(first.blue) || !equal(first.alpha, 1.0f))
    {
        std::cerr << "Alignment-safe RGBA16F pixel extraction failed.\n";
        return 1;
    }

    const auto statistics = clipss2::image::analyze_rgba16_float(image);
    const auto& red = statistics.channels[0];
    const auto& green = statistics.channels[1];
    const auto& blue = statistics.channels[2];
    const auto& alpha = statistics.channels[3];
    if (red.finite_count != 1 || red.min_finite != -1.0f ||
        red.max_finite != -1.0f || red.nan_count != 1 ||
        green.finite_count != 2 || green.min_finite != -0.5f ||
        green.max_finite != 2.0f || blue.finite_count != 0 ||
        blue.positive_infinity_count != 1 || blue.negative_infinity_count != 1 ||
        alpha.finite_count != 2 || alpha.min_finite != 0.0f ||
        alpha.max_finite != 1.0f ||
        statistics.finite_rgb_negative_count != 2 ||
        statistics.finite_rgb_above_one_count != 1)
    {
        std::cerr << "RGBA16F statistics did not match expected values.\n";
        return 1;
    }

    std::cout << "FP16 conversion/extraction/statistics tests: passed\n";
    return 0;
}

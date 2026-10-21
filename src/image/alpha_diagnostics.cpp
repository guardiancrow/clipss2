#include "image/alpha_diagnostics.h"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace clipss2::image
{
namespace
{
std::uint8_t channel_error(std::uint8_t actual, std::uint8_t expected) noexcept
{
    return static_cast<std::uint8_t>(std::abs(
        static_cast<int>(actual) - static_cast<int>(expected)));
}

std::uint8_t color_error(Bgra8Pixel actual, Bgra8Pixel expected) noexcept
{
    return std::max({
        channel_error(actual.blue, expected.blue),
        channel_error(actual.green, expected.green),
        channel_error(actual.red, expected.red)});
}
}

std::uint8_t premultiply_channel(
    const std::uint8_t straight,
    const std::uint8_t alpha) noexcept
{
    return static_cast<std::uint8_t>(
        (static_cast<unsigned int>(straight) * alpha + 127U) / 255U);
}

Bgra8Pixel premultiply_bgra8(const Bgra8Pixel straight) noexcept
{
    return {
        premultiply_channel(straight.blue, straight.alpha),
        premultiply_channel(straight.green, straight.alpha),
        premultiply_channel(straight.red, straight.alpha),
        straight.alpha};
}

AlphaStatistics analyze_bgra8_alpha(const CpuImage& image)
{
    if (image.format != PixelFormat::bgra8_unorm)
        throw std::invalid_argument("Alpha statistics require a BGRA8 image.");

    const auto expected_size = static_cast<std::size_t>(image.width) * image.height * 4U;
    if (image.pixels.size() != expected_size)
        throw std::invalid_argument("BGRA8 image byte count does not match its dimensions.");

    AlphaStatistics result;
    if (image.pixels.empty())
    {
        result.minimum = 0;
        return result;
    }

    for (std::size_t offset = 3; offset < image.pixels.size(); offset += 4)
    {
        const auto alpha = std::to_integer<std::uint8_t>(image.pixels[offset]);
        result.minimum = std::min(result.minimum, alpha);
        result.maximum = std::max(result.maximum, alpha);
        result.non_opaque_count += alpha != 255;
        result.zero_count += alpha == 0;
        if (alpha == 0 &&
            (image.pixels[offset - 3] != std::byte{} ||
             image.pixels[offset - 2] != std::byte{} ||
             image.pixels[offset - 1] != std::byte{}))
            ++result.zero_alpha_nonzero_rgb_count;
        if (std::to_integer<std::uint8_t>(image.pixels[offset - 3]) > alpha ||
            std::to_integer<std::uint8_t>(image.pixels[offset - 2]) > alpha ||
            std::to_integer<std::uint8_t>(image.pixels[offset - 1]) > alpha)
            ++result.premultiplied_rgb_exceeds_alpha_count;
    }
    return result;
}

CpuImage unpremultiply_bgra8_for_lossless(const CpuImage& image)
{
    // PNG and lossless WebP store unassociated color samples, while the audited
    // WGC BGRA8 path is premultiplied. A=0 is canonicalized to RGB=0.
    (void)analyze_bgra8_alpha(image);
    CpuImage result = image;
    for (std::size_t offset = 0; offset < result.pixels.size(); offset += 4)
    {
        const auto alpha = std::to_integer<std::uint8_t>(result.pixels[offset + 3]);
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            const auto premultiplied =
                std::to_integer<std::uint8_t>(result.pixels[offset + channel]);
            const auto straight = alpha == 0
                ? 0U
                : std::min(255U,
                    (static_cast<unsigned int>(premultiplied) * 255U + alpha / 2U) /
                    alpha);
            result.pixels[offset + channel] = static_cast<std::byte>(straight);
        }
    }
    return result;
}

CpuImage unpremultiply_bgra8_for_png(const CpuImage& image)
{
    return unpremultiply_bgra8_for_lossless(image);
}

AlphaClassification classify_alpha_samples(
    const std::span<const AlphaProbeSample> samples,
    const std::uint8_t tolerance) noexcept
{
    AlphaClassification result;
    if (samples.empty())
        return result;

    bool all_straight = true;
    bool all_premultiplied = true;
    bool all_opaque = true;
    bool has_discriminating_sample = false;
    bool expects_non_opaque = false;

    for (const auto& sample : samples)
    {
        const auto straight_error = color_error(sample.observed, sample.logical_straight);
        const auto premultiplied_error =
            color_error(sample.observed, sample.fixture_premultiplied);
        const auto alpha_error =
            channel_error(sample.observed.alpha, sample.logical_straight.alpha);

        result.maximum_straight_error =
            std::max(result.maximum_straight_error, straight_error);
        result.maximum_premultiplied_error =
            std::max(result.maximum_premultiplied_error, premultiplied_error);
        result.maximum_alpha_error = std::max(result.maximum_alpha_error, alpha_error);

        all_straight &= straight_error <= tolerance && alpha_error <= tolerance;
        all_premultiplied &= premultiplied_error <= tolerance && alpha_error <= tolerance;
        all_opaque &= channel_error(sample.observed.alpha, 255) <= tolerance;
        has_discriminating_sample |= color_error(
            sample.logical_straight, sample.fixture_premultiplied) > tolerance;
        expects_non_opaque |= sample.logical_straight.alpha < 255 - tolerance;
    }

    if (all_opaque && expects_non_opaque)
        result.association = AlphaAssociation::forced_opaque;
    else if (has_discriminating_sample && all_straight && !all_premultiplied)
        result.association = AlphaAssociation::straight_unassociated;
    else if (has_discriminating_sample && all_premultiplied && !all_straight)
        result.association = AlphaAssociation::premultiplied;

    return result;
}

const char* alpha_association_name(const AlphaAssociation association) noexcept
{
    switch (association)
    {
    case AlphaAssociation::straight_unassociated:
        return "straight/unassociated";
    case AlphaAssociation::premultiplied:
        return "premultiplied";
    case AlphaAssociation::forced_opaque:
        return "opaque";
    default:
        return "unknown";
    }
}
}

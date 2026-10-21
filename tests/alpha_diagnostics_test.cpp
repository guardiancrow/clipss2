#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>

#include "image/alpha_diagnostics.h"

namespace
{
using clipss2::image::AlphaAssociation;
using clipss2::image::AlphaProbeSample;
using clipss2::image::Bgra8Pixel;

void require(const bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::array<AlphaProbeSample, 3> samples_for(AlphaAssociation association)
{
    const std::array logical{
        Bgra8Pixel{0, 0, 255, 128},
        Bgra8Pixel{0, 255, 0, 64},
        Bgra8Pixel{255, 255, 255, 192}};
    std::array<AlphaProbeSample, 3> result{};
    for (std::size_t index = 0; index < result.size(); ++index)
    {
        result[index].logical_straight = logical[index];
        result[index].fixture_premultiplied =
            clipss2::image::premultiply_bgra8(logical[index]);
        result[index].observed = association == AlphaAssociation::straight_unassociated
                                     ? logical[index]
                                     : result[index].fixture_premultiplied;
        if (association == AlphaAssociation::forced_opaque)
            result[index].observed.alpha = 255;
    }
    return result;
}
}

int main()
{
    try
    {
        require(clipss2::image::premultiply_channel(255, 128) == 128,
                "half red premultiply mismatch");
        require(clipss2::image::premultiply_channel(255, 64) == 64,
                "quarter blue premultiply mismatch");
        require(clipss2::image::premultiply_channel(255, 192) == 192,
                "three-quarter white premultiply mismatch");
        require(clipss2::image::premultiply_channel(0, 0) == 0,
                "transparent black premultiply mismatch");

        clipss2::image::CpuImage image{
            2, 2, clipss2::image::PixelFormat::bgra8_unorm,
            std::vector<std::byte>(16)};
        image.pixels[3] = std::byte{255};
        image.pixels[7] = std::byte{128};
        image.pixels[11] = std::byte{0};
        image.pixels[15] = std::byte{64};
        const auto statistics = clipss2::image::analyze_bgra8_alpha(image);
        require(statistics.minimum == 0 && statistics.maximum == 255,
                "alpha range mismatch");
        require(statistics.non_opaque_count == 3 && statistics.zero_count == 1,
                "alpha counts mismatch");

        clipss2::image::CpuImage opaque_image{
            1, 1, clipss2::image::PixelFormat::bgra8_unorm,
            {std::byte{17}, std::byte{31}, std::byte{47}, std::byte{255}}};
        const auto opaque_statistics = clipss2::image::analyze_bgra8_alpha(opaque_image);
        require(opaque_statistics.minimum == 255 && opaque_statistics.maximum == 255 &&
                    opaque_statistics.non_opaque_count == 0 &&
                    opaque_statistics.zero_count == 0,
                "opaque alpha statistics mismatch");

        image.pixels = {
            std::byte{0}, std::byte{0}, std::byte{128}, std::byte{128},
            std::byte{64}, std::byte{0}, std::byte{0}, std::byte{64},
            std::byte{192}, std::byte{192}, std::byte{192}, std::byte{192},
            std::byte{91}, std::byte{17}, std::byte{3}, std::byte{0}};
        const auto privacy_statistics = clipss2::image::analyze_bgra8_alpha(image);
        require(privacy_statistics.zero_alpha_nonzero_rgb_count == 1,
                "zero-alpha privacy count mismatch");
        require(privacy_statistics.premultiplied_rgb_exceeds_alpha_count == 1,
                "premultiplied invariant count mismatch");
        const auto straight_image = clipss2::image::unpremultiply_bgra8_for_png(image);
        require(clipss2::image::read_bgra8_pixel(straight_image, 0, 0).red == 255,
                "half red unpremultiply mismatch");
        require(clipss2::image::read_bgra8_pixel(straight_image, 1, 0).blue == 255,
                "quarter blue unpremultiply mismatch");
        const auto white = clipss2::image::read_bgra8_pixel(straight_image, 0, 1);
        require(white.red == 255 && white.green == 255 && white.blue == 255,
                "three-quarter white unpremultiply mismatch");
        const auto transparent = clipss2::image::read_bgra8_pixel(straight_image, 1, 1);
        require(transparent.red == 0 && transparent.green == 0 &&
                    transparent.blue == 0 && transparent.alpha == 0,
                "zero-alpha RGB was not sanitized");

        clipss2::image::CpuImage edge_cases{
            4, 2, clipss2::image::PixelFormat::bgra8_unorm,
            {
                std::byte{123}, std::byte{0}, std::byte{0}, std::byte{255},
                std::byte{96}, std::byte{0}, std::byte{0}, std::byte{192},
                std::byte{128}, std::byte{0}, std::byte{0}, std::byte{128},
                std::byte{64}, std::byte{0}, std::byte{0}, std::byte{64},
                std::byte{1}, std::byte{0}, std::byte{0}, std::byte{1},
                std::byte{9}, std::byte{0}, std::byte{0}, std::byte{0},
                std::byte{200}, std::byte{0}, std::byte{0}, std::byte{100},
                std::byte{50}, std::byte{0}, std::byte{0}, std::byte{128}}};
        const auto edge_output =
            clipss2::image::unpremultiply_bgra8_for_png(edge_cases);
        const std::array<std::uint8_t, 8> expected_blue{
            123, 128, 255, 255, 255, 0, 255, 100};
        for (std::size_t index = 0; index < expected_blue.size(); ++index)
        {
            const auto pixel = clipss2::image::read_bgra8_pixel(
                edge_output,
                static_cast<std::uint32_t>(index % 4),
                static_cast<std::uint32_t>(index / 4));
            require(pixel.blue == expected_blue[index],
                    "unpremultiply edge-case mismatch");
        }

        auto straight = samples_for(AlphaAssociation::straight_unassociated);
        require(clipss2::image::classify_alpha_samples(straight).association ==
                    AlphaAssociation::straight_unassociated,
                "straight classification mismatch");
        auto premultiplied = samples_for(AlphaAssociation::premultiplied);
        require(clipss2::image::classify_alpha_samples(premultiplied).association ==
                    AlphaAssociation::premultiplied,
                "premultiplied classification mismatch");
        auto opaque = samples_for(AlphaAssociation::forced_opaque);
        require(clipss2::image::classify_alpha_samples(opaque).association ==
                    AlphaAssociation::forced_opaque,
                "opaque classification mismatch");
        premultiplied[1].observed = {17, 31, 47, 83};
        require(clipss2::image::classify_alpha_samples(premultiplied).association ==
                    AlphaAssociation::unknown,
                "unknown classification mismatch");

        std::cout << "alpha diagnostics tests: passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "alpha diagnostics tests: failed: " << error.what() << '\n';
        return 1;
    }
}

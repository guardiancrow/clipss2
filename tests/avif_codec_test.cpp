#include "codec/avif/avif_codec.h"
#include "codec/avif/avif_validator.h"
#include "codec/png/png_codec.h"
#include "codec/webp/webp_codec.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace clipss2;

namespace
{
void require(const bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

template<class Function>
void rejects(Function&& function, const char* message)
{
    try { function(); }
    catch (const std::exception&) { return; }
    throw std::runtime_error(message);
}

image::CpuImage fixture(const std::uint32_t width, const std::uint32_t height)
{
    image::CpuImage result{width, height, image::PixelFormat::bgra8_unorm,
        std::vector<std::byte>(static_cast<std::size_t>(width) * height * 4)};
    for (std::uint32_t y = 0; y < height; ++y)
    for (std::uint32_t x = 0; x < width; ++x)
    {
        const auto offset = (static_cast<std::size_t>(y) * width + x) * 4;
        auto alpha = static_cast<std::uint8_t>((x * 17U + y * 31U) & 0xffU);
        if ((x + y) % 11 == 0) alpha = 0;
        result.pixels[offset] = alpha == 0 ? std::byte{} : static_cast<std::byte>((x * 3U + 197U) & 0xffU);
        result.pixels[offset + 1] = alpha == 0 ? std::byte{} : static_cast<std::byte>((y * 5U + 73U) & 0xffU);
        result.pixels[offset + 2] = alpha == 0 ? std::byte{} : static_cast<std::byte>((x + y * 7U + 19U) & 0xffU);
        result.pixels[offset + 3] = static_cast<std::byte>(alpha);
    }
    return result;
}

struct Measurement { std::size_t bytes{}; double milliseconds{}; };
template<class Function>
Measurement measure(Function&& function)
{
    const auto start = std::chrono::steady_clock::now();
    const auto bytes = function();
    return {bytes.size(), std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count()};
}
}

int main()
{
    try
    {
        const auto policy = codec::avif::product_avif_policy();
        require(policy.quality_lossless == 100 && policy.quality_alpha_lossless == 100 &&
            policy.speed == 6 && policy.depth == 8, "AVIF product policy changed unexpectedly.");

        const image::CpuImage one{1, 1, image::PixelFormat::bgra8_unorm,
            {std::byte{0x11}, std::byte{0x42}, std::byte{0xe3}, std::byte{0xff}}};
        const auto one_bytes = codec::avif::encode_lossless_avif_bytes(one);
        (void)codec::avif::validate_screenshot_avif(one_bytes, one);

        auto opaque = fixture(31, 13); // odd dimensions, opaque UI/text-like edges.
        for (std::size_t offset = 0; offset < opaque.pixels.size(); offset += 4)
        {
            const bool foreground = (offset / 4) % 7 < 2;
            opaque.pixels[offset] = foreground ? std::byte{0x09} : std::byte{0xe1};
            opaque.pixels[offset + 1] = foreground ? std::byte{0x31} : std::byte{0xd2};
            opaque.pixels[offset + 2] = foreground ? std::byte{0xf4} : std::byte{0x17};
            opaque.pixels[offset + 3] = std::byte{0xff};
        }
        const auto opaque_bytes = codec::avif::encode_lossless_avif_bytes(opaque);
        const auto opaque_info = codec::avif::validate_screenshot_avif(opaque_bytes, opaque);
        require(opaque_info.width == 31 && opaque_info.height == 13 && opaque_info.depth == 8 &&
            opaque_info.image_count == 1 && !opaque_info.has_alpha, "Opaque AVIF inspection failed.");

        auto alpha = fixture(17, 9); // asymmetric red/blue, A=0, partial alpha and odd dimensions.
        const auto alpha_bytes = codec::avif::encode_lossless_avif_bytes(alpha);
        const auto alpha_info = codec::avif::validate_screenshot_avif(alpha_bytes, alpha);
        require(alpha_info.has_alpha, "AVIF alpha presence was lost.");
        const auto decoded = codec::avif::decode_bgra8_avif(alpha_bytes);
        require(decoded.pixels == alpha.pixels, "AVIF partial-alpha/channel-order round trip failed.");
        require(decoded.pixels[0] == std::byte{} && decoded.pixels[1] == std::byte{} &&
            decoded.pixels[2] == std::byte{} && decoded.pixels[3] == std::byte{},
            "AVIF A=0 canonical pixel changed.");

        std::vector<std::byte> truncated{alpha_bytes.begin(), alpha_bytes.end() - 1};
        rejects([&] { (void)codec::avif::inspect_avif_structure(truncated); }, "Truncated AVIF accepted.");
        auto wrong_dimensions = alpha; ++wrong_dimensions.width;
        rejects([&] { (void)codec::avif::validate_screenshot_avif(alpha_bytes, wrong_dimensions); },
            "AVIF dimension mismatch accepted.");
        auto wrong_pixels = alpha; wrong_pixels.pixels[1] ^= std::byte{1};
        rejects([&] { (void)codec::avif::validate_screenshot_avif(alpha_bytes, wrong_pixels); },
            "AVIF pixel mismatch accepted.");
        image::CpuImage invalid{};
        rejects([&] { (void)codec::avif::encode_lossless_avif_bytes(invalid); }, "Invalid AVIF dimensions accepted.");

        const auto performance_source = fixture(768, 512);
        const auto avif = measure([&] { return codec::avif::encode_lossless_avif_bytes(performance_source); });
        const auto png = measure([&] { return codec::png::encode_srgb_png_bytes(performance_source); });
        const auto webp = measure([&] { return codec::webp::encode_lossless_webp_bytes(performance_source); });
        std::cout << "768x512 AVIF=" << avif.bytes << " bytes/" << avif.milliseconds
                  << " ms PNG=" << png.bytes << " bytes/" << png.milliseconds
                  << " ms WebP=" << webp.bytes << " bytes/" << webp.milliseconds << " ms\n";
        std::cout << "AVIF lossless exact/alpha/privacy validation tests: passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#include "codec/png/png_codec.h"
#include "codec/webp/webp_codec.h"
#include "codec/webp/webp_validator.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
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
        std::uint8_t alpha = static_cast<std::uint8_t>((x * 17U + y * 31U) & 0xffU);
        if ((x + y) % 11 == 0) alpha = 0;
        result.pixels[offset] = alpha == 0 ? std::byte{} : static_cast<std::byte>((x * 3U + 197U) & 0xffU);
        result.pixels[offset + 1] = alpha == 0 ? std::byte{} : static_cast<std::byte>((y * 5U + 73U) & 0xffU);
        result.pixels[offset + 2] = alpha == 0 ? std::byte{} : static_cast<std::byte>((x + y * 7U + 19U) & 0xffU);
        result.pixels[offset + 3] = static_cast<std::byte>(alpha);
    }
    return result;
}

void set_le32(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value)
{
    for (unsigned int index = 0; index != 4; ++index)
        bytes[offset + index] = static_cast<std::byte>((value >> (8U * index)) & 0xffU);
}

void replace_fourcc(std::vector<std::byte>& bytes, const std::string_view type)
{
    require(type.size() == 4, "Invalid test FOURCC.");
    std::copy(type.begin(), type.end(), reinterpret_cast<char*>(bytes.data() + 12));
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
        const auto policy = codec::webp::product_webp_policy();
        require(policy.lossless == 1 && policy.exact == 1 && policy.method == 4 &&
            policy.thread_level == 1 && policy.image_hint == 3 && policy.quality == 75.0F,
            "WebP product policy changed unexpectedly.");

        image::CpuImage one{1, 1, image::PixelFormat::bgra8_unorm,
            {std::byte{0x11}, std::byte{0x42}, std::byte{0xe3}, std::byte{0xff}}};
        const auto one_bytes = codec::webp::encode_lossless_webp_bytes(one);
        (void)codec::webp::validate_screenshot_webp(one_bytes, one);

        auto ui_edges = fixture(31, 13);
        for (std::size_t offset = 0; offset < ui_edges.pixels.size(); offset += 4)
        {
            const bool foreground = (offset / 4) % 7 < 2;
            ui_edges.pixels[offset] = foreground ? std::byte{0x09} : std::byte{0xe1};
            ui_edges.pixels[offset + 1] = foreground ? std::byte{0x31} : std::byte{0xd2};
            ui_edges.pixels[offset + 2] = foreground ? std::byte{0xf4} : std::byte{0x17};
            ui_edges.pixels[offset + 3] = std::byte{0xff};
        }
        const auto ui_bytes = codec::webp::encode_lossless_webp_bytes(ui_edges);
        (void)codec::webp::validate_screenshot_webp(ui_bytes, ui_edges);

        auto alpha = fixture(17, 9); // odd dimensions, partial alpha and A=0 canonical pixels.
        const auto alpha_bytes = codec::webp::encode_lossless_webp_bytes(alpha);
        const auto inspected = codec::webp::validate_screenshot_webp(alpha_bytes, alpha);
        require(inspected.width == 17 && inspected.height == 9 && inspected.image_payload_count == 1,
            "WebP inspection dimensions/payload count failed.");
        const auto decoded = codec::webp::decode_bgra8_webp(alpha_bytes);
        require(decoded.pixels == alpha.pixels, "WebP partial-alpha/channel-order round trip failed.");
        require(decoded.pixels[0] == std::byte{} && decoded.pixels[1] == std::byte{} &&
            decoded.pixels[2] == std::byte{} && decoded.pixels[3] == std::byte{},
            "WebP A=0 canonical pixel changed.");

        image::CpuImage transparent{2, 1, image::PixelFormat::bgra8_unorm,
            {std::byte{}, std::byte{}, std::byte{}, std::byte{},
             std::byte{0x10}, std::byte{0x20}, std::byte{0xe0}, std::byte{0x7f}}};
        const auto transparent_bytes = codec::webp::encode_lossless_webp_bytes(transparent);
        (void)codec::webp::validate_screenshot_webp(transparent_bytes, transparent);

        auto corrupted = alpha_bytes;
        corrupted.pop_back();
        rejects([&] { (void)codec::webp::inspect_webp_structure(corrupted); }, "Truncated RIFF accepted.");
        corrupted = alpha_bytes; set_le32(corrupted, 4, 0);
        rejects([&] { (void)codec::webp::inspect_webp_structure(corrupted); }, "Bad RIFF size accepted.");
        corrupted = alpha_bytes; set_le32(corrupted, 16, 0xffffffffU);
        rejects([&] { (void)codec::webp::inspect_webp_structure(corrupted); }, "Bad chunk size accepted.");
        corrupted = alpha_bytes;
        corrupted.insert(corrupted.end(), alpha_bytes.begin() + 12, alpha_bytes.end());
        set_le32(corrupted, 4, static_cast<std::uint32_t>(corrupted.size() - 8));
        rejects([&] { (void)codec::webp::inspect_webp_structure(corrupted); }, "Multiple VP8L payloads accepted.");
        corrupted = alpha_bytes; corrupted.push_back(std::byte{});
        set_le32(corrupted, 4, static_cast<std::uint32_t>(corrupted.size() - 8));
        rejects([&] { (void)codec::webp::inspect_webp_structure(corrupted); }, "Malformed trailing data accepted.");
        for (const auto type : {"JUNK", "EXIF", "XMP ", "ICCP", "ANIM", "ANMF", "VP8X"})
        {
            corrupted = alpha_bytes; replace_fourcc(corrupted, type);
            rejects([&] { (void)codec::webp::inspect_webp_structure(corrupted); },
                "Forbidden WebP chunk accepted.");
        }
        auto wrong_dimensions = alpha; ++wrong_dimensions.width;
        rejects([&] { (void)codec::webp::validate_screenshot_webp(alpha_bytes, wrong_dimensions); },
            "WebP dimension mismatch accepted.");
        auto wrong_pixels = alpha; wrong_pixels.pixels[1] ^= std::byte{1};
        rejects([&] { (void)codec::webp::validate_screenshot_webp(alpha_bytes, wrong_pixels); },
            "WebP pixel mismatch accepted.");
        image::CpuImage invalid{};
        rejects([&] { (void)codec::webp::encode_lossless_webp_bytes(invalid); },
            "Invalid WebP dimensions accepted.");

        for (const auto [width, height] : {std::pair{768U, 512U}, std::pair{2560U, 1440U}})
        {
            const auto source = fixture(width, height);
            const auto webp = measure([&] {
                auto encoded = codec::webp::encode_lossless_webp_bytes(source);
                (void)codec::webp::validate_screenshot_webp(encoded, source);
                return encoded;
            });
            const auto png = measure([&] { return codec::png::encode_srgb_png_bytes(source); });
            std::cout << width << 'x' << height << " WebP=" << webp.bytes << " bytes/"
                      << webp.milliseconds << " ms PNG=" << png.bytes << " bytes/"
                      << png.milliseconds << " ms\n";
        }
        std::cout << "WebP lossless exact/alpha/RIFF privacy validation tests: passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#include "codec/jpeg/jpeg_codec.h"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
template<class Function> void rejects(Function function)
{
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "Invalid flatten input was accepted.");
}
}

int main()
{
    using namespace clipss2;
    try
    {
        const image::CpuImage source{6, 1, image::PixelFormat::bgra8_unorm, {
            std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255},
            std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255},
            std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255},
            std::byte{99}, std::byte{88}, std::byte{77}, std::byte{0},
            std::byte{0}, std::byte{0}, std::byte{128}, std::byte{128},
            std::byte{11}, std::byte{22}, std::byte{33}, std::byte{255}}};
        const auto before = source.pixels;
        const auto rgb = codec::jpeg::flatten_wgc_premultiplied_bgra8_for_jpeg(source);
        const std::vector<std::byte> expected{
            std::byte{0}, std::byte{0}, std::byte{0},
            std::byte{255}, std::byte{255}, std::byte{255},
            std::byte{255}, std::byte{0}, std::byte{0},
            std::byte{255}, std::byte{255}, std::byte{255},
            std::byte{255}, std::byte{127}, std::byte{127},
            std::byte{33}, std::byte{22}, std::byte{11}};
        require(rgb.pixels == expected && source.pixels == before,
                "Exact white-matte/channel-order/immutability check failed.");

        // Every alpha: premultiplied red at full coverage of that alpha yields
        // red=255; empty green/blue become exactly 255-A, including A=0.
        for (unsigned int alpha = 0; alpha <= 255; ++alpha)
        {
            const image::CpuImage red{1, 1, image::PixelFormat::bgra8_unorm,
                {std::byte{0}, std::byte{0}, static_cast<std::byte>(alpha), static_cast<std::byte>(alpha)}};
            const auto result = codec::jpeg::flatten_wgc_premultiplied_bgra8_for_jpeg(red);
            require(result.pixels[0] == std::byte{255} &&
                std::to_integer<unsigned int>(result.pixels[1]) == 255U - alpha &&
                result.pixels[1] == result.pixels[2], "Alpha boundary sweep failed.");
        }
        const image::CpuImage malformed{1, 1, image::PixelFormat::bgra8_unorm,
            {std::byte{255}, std::byte{240}, std::byte{200}, std::byte{1}}};
        require(codec::jpeg::flatten_wgc_premultiplied_bgra8_for_jpeg(malformed).pixels ==
            std::vector<std::byte>(3, std::byte{255}), "Malformed RGB saturation wrapped.");
        auto invalid = source;
        invalid.format = image::PixelFormat::rgba16_float;
        rejects([&] { (void)codec::jpeg::flatten_wgc_premultiplied_bgra8_for_jpeg(invalid); });
        invalid = source; invalid.pixels.pop_back();
        rejects([&] { (void)codec::jpeg::flatten_wgc_premultiplied_bgra8_for_jpeg(invalid); });
        rejects([&] { (void)codec::jpeg::flatten_wgc_premultiplied_bgra8_for_jpeg(
            source, static_cast<codec::jpeg::AlphaFlattenPolicy>(99)); });
        for (const auto dimension : {0U, 65501U, std::numeric_limits<std::uint32_t>::max()})
            rejects([&] { (void)codec::jpeg::checked_rgb_image_bytes(dimension, 1); });
        rejects([] { (void)codec::jpeg::checked_rgb_image_bytes(65500, 65500); });
        std::cout << "JPEG lossless alpha flatten boundaries/rounding/safety: passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "JPEG alpha flatten test failed: " << error.what() << '\n';
        return 1;
    }
}

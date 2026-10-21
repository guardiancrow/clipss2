#include "platform/windows/clipboard_image.h"
#include "codec/png/png_codec.h"

#include <windows.h>

#include <cstddef>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace
{
template<class Function>
bool throws(Function&& function)
{
    try
    {
        function();
        return false;
    }
    catch (const std::exception&)
    {
        return true;
    }
}
}

int main()
{
    using clipss2::image::CpuImage;
    using clipss2::image::PixelFormat;
    using clipss2::platform::windows::make_dibv5_payload;
    using clipss2::platform::windows::prepare_image_for_clipboard;

    int failures = 0;
    const std::vector<std::byte> pixels{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
        std::byte{10}, std::byte{20}, std::byte{30}, std::byte{40},
        std::byte{50}, std::byte{60}, std::byte{70}, std::byte{80},
        std::byte{100}, std::byte{110}, std::byte{120}, std::byte{255}};
    const CpuImage image{2, 2, PixelFormat::bgra8_unorm, pixels};
    const auto payload = make_dibv5_payload(image);

    BITMAPV5HEADER header{};
    if (payload.bytes.size() >= sizeof(header))
        std::memcpy(&header, payload.bytes.data(), sizeof(header));
    else
        ++failures;

    if (header.bV5Size != sizeof(BITMAPV5HEADER) || header.bV5Width != 2 ||
        header.bV5Height != -2 || header.bV5Planes != 1 ||
        header.bV5BitCount != 32 || header.bV5Compression != BI_BITFIELDS ||
        header.bV5SizeImage != 16 || header.bV5RedMask != 0x00ff0000U ||
        header.bV5GreenMask != 0x0000ff00U || header.bV5BlueMask != 0x000000ffU ||
        header.bV5AlphaMask != 0xff000000U || header.bV5CSType != LCS_sRGB)
        ++failures;
    if (payload.row_bytes != 8 || payload.pixel_bytes != 16 ||
        payload.bytes.size() != sizeof(BITMAPV5HEADER) + pixels.size() ||
        std::memcmp(payload.bytes.data() + sizeof(BITMAPV5HEADER),
                    pixels.data(), pixels.size()) != 0)
        ++failures;

    auto memory_png = clipss2::codec::png::encode_srgb_png_bytes(image);
    const auto png_size = memory_png.size();
    const auto prepared = prepare_image_for_clipboard(
        image, image, std::move(memory_png));
    if (prepared.png.size() != png_size ||
        prepared.dibv5.bytes != payload.bytes)
        ++failures;

    if (!throws([] { (void)make_dibv5_payload(CpuImage{}); }))
        ++failures;
    if (!throws([] {
            (void)make_dibv5_payload(
                CpuImage{1, 1, PixelFormat::rgba8_unorm, std::vector<std::byte>(4)});
        }))
        ++failures;
    if (!throws([] {
            (void)make_dibv5_payload(CpuImage{
                static_cast<std::uint32_t>(std::numeric_limits<LONG>::max()),
                static_cast<std::uint32_t>(std::numeric_limits<LONG>::max()),
                PixelFormat::bgra8_unorm,
                {}});
        }))
        ++failures;
    if (!throws([&] {
            auto png = clipss2::codec::png::encode_srgb_png_bytes(image);
            (void)prepare_image_for_clipboard(
                image,
                CpuImage{1, 1, PixelFormat::bgra8_unorm,
                         {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255}}},
                std::move(png));
        }))
        ++failures;

    std::cout << "CF_DIBV5 serialization failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}

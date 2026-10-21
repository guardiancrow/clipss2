#include "codec/jpeg/jpeg_codec.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace clipss2::codec::jpeg
{
std::size_t checked_rgb_image_bytes(std::uint32_t width, std::uint32_t height)
{
    // libjpeg's baseline dimension limit; also bound allocations for untrusted
    // headers independently of the machine's available address space.
    constexpr std::uint32_t maximum_dimension = 65500;
    if (width == 0 || height == 0 || width > maximum_dimension ||
        height > maximum_dimension)
        throw std::invalid_argument("JPEG dimensions are outside the supported range.");
    const auto w = static_cast<std::size_t>(width);
    const auto h = static_cast<std::size_t>(height);
    if (w > std::numeric_limits<std::size_t>::max() / 3 ||
        w * 3 > std::numeric_limits<std::size_t>::max() / h)
        throw std::overflow_error("JPEG RGB buffer size overflowed.");
    const auto bytes = w * 3 * h;
    if (bytes > maximum_rgb_bytes)
        throw std::invalid_argument("JPEG RGB buffer exceeds the safety limit.");
    return bytes;
}

OpaqueRgb8Image flatten_wgc_premultiplied_bgra8_for_jpeg(
    const image::CpuImage& source, AlphaFlattenPolicy policy)
{
    if (policy != AlphaFlattenPolicy::opaque_srgb_white)
        throw std::invalid_argument("Unsupported JPEG alpha flatten policy.");
    if (source.format != image::PixelFormat::bgra8_unorm)
        throw std::invalid_argument("JPEG flatten requires premultiplied WGC BGRA8.");
    const auto rgb_bytes = checked_rgb_image_bytes(source.width, source.height);
    const auto count = rgb_bytes / 3;
    if (count > std::numeric_limits<std::size_t>::max() / 4)
        throw std::overflow_error("JPEG source buffer size overflowed.");
    if (source.pixels.size() != count * 4)
        throw std::invalid_argument("JPEG source buffer does not match its dimensions.");
    OpaqueRgb8Image result{source.width, source.height,
                           std::vector<std::byte>(rgb_bytes)};
    for (std::size_t pixel = 0; pixel < count; ++pixel)
    {
        const auto input = pixel * 4;
        const auto output = pixel * 3;
        const auto alpha = std::to_integer<unsigned int>(source.pixels[input + 3]);
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            const auto premultiplied =
                std::to_integer<unsigned int>(source.pixels[input + 2 - channel]);
            // General matte contribution is round(matte*(255-A)/255).
            // For white (255), this is exactly 255-A, no rounding needed.
            // Ignore hidden RGB at A=0; saturate malformed RGB>A, never wrap.
            const auto value = alpha == 0 ? 255U :
                std::min(255U, premultiplied + 255U - alpha);
            result.pixels[output + channel] = static_cast<std::byte>(value);
        }
    }
    return result;
}
}

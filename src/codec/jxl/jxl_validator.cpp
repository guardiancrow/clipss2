#include "codec/jxl/jxl_validator.h"
#include "codec/jxl/jxl_internal.h"
#include <stdexcept>

namespace clipss2::codec::jxl
{
JxlInspection inspect_jxl_structure(std::span<const std::byte> bytes)
{ return detail::decode_and_inspect(bytes).inspection; }

JxlInspection validate_screenshot_jxl(std::span<const std::byte> bytes, const image::CpuImage& source)
{
    const auto decoded = detail::decode_and_inspect(bytes);
    if (source.format != image::PixelFormat::bgra8_unorm || decoded.pixels.width != source.width ||
        decoded.pixels.height != source.height || decoded.pixels.pixels != source.pixels)
        throw std::runtime_error("JPEG XL exact canonical RGBA validation failed.");
    for (std::size_t i = 0; i < source.pixels.size(); i += 4)
        if (source.pixels[i + 3] == std::byte{} &&
            (source.pixels[i] != std::byte{} || source.pixels[i + 1] != std::byte{} || source.pixels[i + 2] != std::byte{}))
            throw std::runtime_error("JPEG XL source has noncanonical invisible RGB.");
    return decoded.inspection;
}
}

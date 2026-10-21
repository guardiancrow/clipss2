#include "codec/webp/webp_validator.h"

#include "codec/webp/webp_codec.h"

#include <webp/decode.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>

namespace clipss2::codec::webp
{
namespace
{
std::uint32_t read_le32(const std::byte* value) noexcept
{
    return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(value[0])) |
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(value[1])) << 8) |
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(value[2])) << 16) |
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(value[3])) << 24);
}

std::string_view fourcc(const std::byte* value) noexcept
{
    return {reinterpret_cast<const char*>(value), 4};
}

[[noreturn]] void reject(const std::string& reason)
{
    throw std::runtime_error("WebP structure/privacy validation failed: " + reason);
}
}

WebpInspection inspect_webp_structure(const std::span<const std::byte> bytes)
{
    if (bytes.size() < 12 || bytes.size() > maximum_webp_bytes) reject("encoded size outside safety limit");
    if (fourcc(bytes.data()) != "RIFF" || fourcc(bytes.data() + 8) != "WEBP")
        reject("missing RIFF/WEBP signature");
    const auto declared = static_cast<std::size_t>(read_le32(bytes.data() + 4));
    if (declared > bytes.size() - 8 || declared + 8 != bytes.size())
        reject("declared RIFF size does not exactly match file bounds");

    WebpInspection result{};
    result.file_bytes = bytes.size();
    std::size_t offset = 12;
    while (offset < bytes.size())
    {
        if (bytes.size() - offset < 8) reject("truncated chunk header");
        const auto type = fourcc(bytes.data() + offset);
        const auto payload = static_cast<std::size_t>(read_le32(bytes.data() + offset + 4));
        const auto remaining = bytes.size() - offset - 8;
        if (payload > remaining) reject("chunk exceeds file bounds");
        const auto padding = payload & 1U;
        if (padding > remaining - payload) reject("missing chunk padding");
        if (padding && bytes[offset + 8 + payload] != std::byte{}) reject("non-canonical chunk padding");

        if (type == "VP8L")
        {
            if (++result.image_payload_count != 1) reject("multiple still-image payloads");
        }
        else if (type == "ANIM" || type == "ANMF") reject("animation is forbidden");
        else if (type == "EXIF" || type == "XMP " || type == "ICCP") reject("metadata is forbidden");
        else reject("disallowed or unknown chunk: " + std::string{type});
        offset += 8 + payload + padding;
    }
    if (offset != bytes.size()) reject("malformed trailing data");
    if (result.image_payload_count != 1) reject("exactly one VP8L payload is required");

    int width{};
    int height{};
    if (!WebPGetInfo(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), &width, &height) ||
        width <= 0 || height <= 0)
        reject("official decoder rejected dimensions");
    result.width = static_cast<std::uint32_t>(width);
    result.height = static_cast<std::uint32_t>(height);
    return result;
}

WebpInspection validate_screenshot_webp(
    const std::span<const std::byte> bytes, const image::CpuImage& source)
{
    const auto result = inspect_webp_structure(bytes);
    if (source.format != image::PixelFormat::bgra8_unorm ||
        result.width != source.width || result.height != source.height)
        throw std::runtime_error("WebP dimensions or pixel format do not match the canonical source.");
    const auto decoded = decode_bgra8_webp(bytes);
    if (decoded.width != source.width || decoded.height != source.height ||
        decoded.format != source.format || decoded.pixels != source.pixels)
        throw std::runtime_error("WebP lossless pixel round-trip validation failed.");
    return result;
}
}

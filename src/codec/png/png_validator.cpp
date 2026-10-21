#include "codec/png/png_validator.h"

#include "codec/png/png_codec.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace clipss2::codec::png
{
namespace
{
constexpr std::array<std::byte, 8> png_signature{
    std::byte{0x89}, std::byte{0x50}, std::byte{0x4e}, std::byte{0x47},
    std::byte{0x0d}, std::byte{0x0a}, std::byte{0x1a}, std::byte{0x0a}};

constexpr std::array<std::uint32_t, 8> srgb_chromaticities{
    31270, 32900, 64000, 33000, 30000, 60000, 15000, 6000};

std::uint32_t read_big_endian_u32(const std::byte* bytes) noexcept
{
    return (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[0])) << 24) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[1])) << 16) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[2])) << 8) |
           static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[3]));
}

std::vector<std::byte> read_file(const std::filesystem::path& path)
{
    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if (!stream)
    {
        throw std::runtime_error("Could not open PNG for validation.");
    }

    const auto end_position = stream.tellg();
    if (end_position <= 0)
    {
        throw std::runtime_error("PNG file is empty.");
    }
    const auto file_size = static_cast<std::uintmax_t>(end_position);
    if (file_size > std::numeric_limits<std::size_t>::max() ||
        file_size > static_cast<std::uintmax_t>(
                        std::numeric_limits<std::streamsize>::max()))
    {
        throw std::overflow_error("PNG file is too large for the address space.");
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(file_size));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    if (!stream)
    {
        throw std::runtime_error("Could not read the complete PNG file.");
    }
    return bytes;
}

std::string_view chunk_type(const std::byte* type_bytes) noexcept
{
    return {reinterpret_cast<const char*>(type_bytes), 4};
}

void validate_round_trip(
    const image::CpuImage& source,
    const image::CpuImage& decoded)
{
    if (source.format != image::PixelFormat::bgra8_unorm ||
        decoded.format != image::PixelFormat::rgba8_unorm ||
        source.width != decoded.width || source.height != decoded.height ||
        source.pixels.size() != decoded.pixels.size())
    {
        throw std::runtime_error("Decoded PNG properties do not match the source image.");
    }

    for (std::size_t offset = 0; offset < source.pixels.size(); offset += 4)
    {
        if (decoded.pixels[offset] != source.pixels[offset + 2] ||
            decoded.pixels[offset + 1] != source.pixels[offset + 1] ||
            decoded.pixels[offset + 2] != source.pixels[offset] ||
            decoded.pixels[offset + 3] != source.pixels[offset + 3])
        {
            throw std::runtime_error("PNG pixel round-trip validation failed.");
        }
    }
}
}

PngValidationResult validate_screenshot_png(
    const std::filesystem::path& path,
    const image::CpuImage& source_bgra8)
{
    if (!std::filesystem::is_regular_file(path))
    {
        throw std::runtime_error("PNG output file does not exist.");
    }

    const auto bytes = read_file(path);
    return validate_screenshot_png(bytes, source_bgra8);
}

PngValidationResult validate_screenshot_png(
    std::span<const std::byte> bytes,
    const image::CpuImage& source_bgra8)
{
    if (bytes.size() < png_signature.size() ||
        !std::equal(png_signature.begin(), png_signature.end(), bytes.begin()))
    {
        throw std::runtime_error("PNG signature validation failed.");
    }

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t ihdr_count = 0;
    std::size_t srgb_count = 0;
    std::size_t gama_count = 0;
    std::size_t chrm_count = 0;
    std::size_t idat_count = 0;
    std::size_t iend_count = 0;
    std::size_t offset = png_signature.size();

    while (offset < bytes.size())
    {
        constexpr std::size_t chunk_overhead = 12;
        if (bytes.size() - offset < chunk_overhead)
        {
            throw std::runtime_error("PNG has a truncated chunk header.");
        }

        const std::uint32_t length = read_big_endian_u32(bytes.data() + offset);
        const std::size_t length_size = static_cast<std::size_t>(length);
        if (length_size > bytes.size() - offset - chunk_overhead)
        {
            throw std::runtime_error("PNG chunk length exceeds the file bounds.");
        }

        const auto type = chunk_type(bytes.data() + offset + 4);
        const std::byte* data = bytes.data() + offset + 8;
        if (type == "IHDR")
        {
            if (++ihdr_count != 1 || offset != png_signature.size() || length != 13)
            {
                throw std::runtime_error("PNG contains an invalid IHDR chunk.");
            }
            width = read_big_endian_u32(data);
            height = read_big_endian_u32(data + 4);
            if (std::to_integer<std::uint8_t>(data[8]) != 8 ||
                std::to_integer<std::uint8_t>(data[9]) != 6)
            {
                throw std::runtime_error("PNG is not RGBA8.");
            }
        }
        else if (type == "sRGB")
        {
            if (++srgb_count != 1 || length != 1 ||
                std::to_integer<std::uint8_t>(data[0]) != 0)
            {
                throw std::runtime_error("PNG contains invalid sRGB signaling.");
            }
        }
        else if (type == "gAMA")
        {
            if (++gama_count != 1 || length != 4 || read_big_endian_u32(data) != 45455)
            {
                throw std::runtime_error("PNG contains an invalid gAMA chunk.");
            }
        }
        else if (type == "cHRM")
        {
            if (++chrm_count != 1 || length != 32)
            {
                throw std::runtime_error("PNG contains an invalid cHRM chunk.");
            }
            for (std::size_t index = 0; index < srgb_chromaticities.size(); ++index)
            {
                if (read_big_endian_u32(data + index * 4) != srgb_chromaticities[index])
                {
                    throw std::runtime_error("PNG cHRM values are not the sRGB standard values.");
                }
            }
        }
        else if (type == "IDAT")
        {
            ++idat_count;
        }
        else if (type == "IEND")
        {
            if (++iend_count != 1 || length != 0)
            {
                throw std::runtime_error("PNG contains an invalid IEND chunk.");
            }
        }
        else
        {
            throw std::runtime_error("PNG contains a disallowed or unknown chunk: " +
                                     std::string{type});
        }

        offset += chunk_overhead + length_size;
        if (iend_count != 0 && offset != bytes.size())
        {
            throw std::runtime_error("PNG contains data after IEND.");
        }
    }

    if (ihdr_count != 1 || srgb_count != 1 || gama_count != 1 || chrm_count != 1 ||
        idat_count == 0 || iend_count != 1)
    {
        throw std::runtime_error("PNG is missing one or more required chunks.");
    }
    if (width != source_bgra8.width || height != source_bgra8.height)
    {
        throw std::runtime_error("PNG dimensions do not match the CPU image.");
    }

    const auto decoded = decode_rgba8_png(bytes);
    validate_round_trip(source_bgra8, decoded);
    return PngValidationResult{width, height, bytes.size()};
}
}

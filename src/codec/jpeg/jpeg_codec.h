#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "image/cpu_image.h"

namespace clipss2::codec::jpeg
{
enum class ChromaSubsampling { yuv444 };
enum class AlphaFlattenPolicy { opaque_srgb_white };

struct JpegOptions
{
    // Provisional controlled baseline, not a product default.
    int quality{90};
    ChromaSubsampling subsampling{ChromaSubsampling::yuv444};
    AlphaFlattenPolicy alpha_flatten{AlphaFlattenPolicy::opaque_srgb_white};
};

// Packed R,G,B bytes, opaque, caller-guaranteed SDR/sRGB content.
// This does not redefine CpuImage's alpha association or color model.
struct OpaqueRgb8Image
{
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::byte> pixels;
};

inline constexpr std::size_t maximum_jpeg_bytes = 256U * 1024U * 1024U;
inline constexpr std::size_t maximum_rgb_bytes = 256U * 1024U * 1024U;

[[nodiscard]] std::size_t checked_rgb_image_bytes(
    std::uint32_t width, std::uint32_t height);

// Only for the audited WGC product boundary (premultiplied BGRA8 in sRGB
// byte space), not arbitrary CpuImage values or HDR/Advanced Color input.
[[nodiscard]] OpaqueRgb8Image flatten_wgc_premultiplied_bgra8_for_jpeg(
    const image::CpuImage& source,
    AlphaFlattenPolicy policy = AlphaFlattenPolicy::opaque_srgb_white);

[[nodiscard]] std::vector<std::byte> encode_srgb_jpeg_bytes(
    const OpaqueRgb8Image& source, const JpegOptions& options = {});

// Strict metadata/header validation precedes allocation and decode.
[[nodiscard]] OpaqueRgb8Image decode_srgb_jpeg_bytes(
    std::span<const std::byte> bytes);

[[nodiscard]] const char* jpeg_backend_version() noexcept;
}

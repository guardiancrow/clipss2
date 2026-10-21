#pragma once

#include "image/cpu_image.h"
#include <stdexcept>

namespace clipss2::codec::detail
{
inline constexpr std::size_t maximum_lossy_decoded_bytes = 256U * 1024U * 1024U;
inline constexpr std::uint32_t maximum_lossy_dimension = 65536;

// The caller owns the trusted source for the entire synchronous validation call.
// This is an input invariant, NOT a rule for the lossy decoder's invisible RGB.
inline bool check_lossy_source(const image::CpuImage& source)
{
    if (source.format != image::PixelFormat::bgra8_unorm || !source.width || !source.height ||
        source.width > maximum_lossy_dimension || source.height > maximum_lossy_dimension ||
        static_cast<std::size_t>(source.width) > maximum_lossy_decoded_bytes / 4 / source.height ||
        source.pixels.size() != static_cast<std::size_t>(source.width) * source.height * 4)
        throw std::invalid_argument("Lossy validation requires bounded, dimension-matched canonical straight BGRA8.");
    bool alpha{};
    for (std::size_t i = 0; i < source.pixels.size(); i += 4)
    {
        alpha |= source.pixels[i + 3] != std::byte{255};
        if (source.pixels[i + 3] == std::byte{} &&
            (source.pixels[i] != std::byte{} || source.pixels[i + 1] != std::byte{} || source.pixels[i + 2] != std::byte{}))
            throw std::invalid_argument("Trusted canonical source requires RGB=0 at A=0.");
    }
    return alpha;
}

inline void check_lossy_alpha(const image::CpuImage& decoded, const image::CpuImage& source)
{
    if (decoded.format != source.format || decoded.width != source.width || decoded.height != source.height ||
        decoded.pixels.size() != source.pixels.size())
        throw std::runtime_error("Lossy decoded dimensions/buffer do not match trusted source.");
    for (std::size_t i = 3; i < source.pixels.size(); i += 4)
        if (decoded.pixels[i] != source.pixels[i])
            throw std::runtime_error("Lossy decoded alpha differs from trusted source.");
    // No source RGB equality, quality threshold, or post-decode normalization.
}
}

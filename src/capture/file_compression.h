#pragma once
#include "codec/lossy_profile.h"
#include "image/image_file_format.h"

namespace clipss2::capture
{
enum class FileCompressionMode { lossless, lossy };
struct FileCompression
{
    FileCompressionMode mode{FileCompressionMode::lossless};
    codec::LossyProfile profile{codec::LossyProfile::balanced};
    bool operator==(const FileCompression&) const = default;
};
inline void validate_file_compression(image::ImageFileFormat format, FileCompression compression)
{
    if (compression.mode == FileCompressionMode::lossless) return;
    if (compression.mode != FileCompressionMode::lossy ||
        (format != image::ImageFileFormat::avif && format != image::ImageFileFormat::jxl))
        throw std::invalid_argument("Lossy compression supports AVIF/JXL only.");
    (void)codec::lossy_profile_token(compression.profile);
}
// Canonical ASCII preference representation, independent of format and locale.
inline const char* file_compression_token(FileCompression value)
{
    validate_file_compression(image::ImageFileFormat::avif, value);
    if (value.mode == FileCompressionMode::lossless)
    {
        if (value.profile != codec::LossyProfile::balanced)
            throw std::invalid_argument("Lossless preference must use the canonical unused profile.");
        return "lossless";
    }
    switch (value.profile)
    {
    case codec::LossyProfile::high: return "lossy-high";
    case codec::LossyProfile::balanced: return "lossy-balanced";
    case codec::LossyProfile::small_file: return "lossy-small";
    }
    throw std::invalid_argument("Unknown compression profile.");
}
inline FileCompression parse_file_compression(std::string_view token)
{
    if (token == "lossless") return {};
    if (token.starts_with("lossy-"))
        return {FileCompressionMode::lossy, codec::parse_lossy_profile(token.substr(6))};
    throw std::invalid_argument("Unknown compression preference token.");
}
inline void cycle_file_compression(FileCompression& value)
{
    (void)file_compression_token(value);
    if (value.mode == FileCompressionMode::lossless)
        value = {FileCompressionMode::lossy, codec::LossyProfile::high};
    else switch (value.profile)
    {
    case codec::LossyProfile::high: value.profile = codec::LossyProfile::balanced; break;
    case codec::LossyProfile::balanced: value.profile = codec::LossyProfile::small_file; break;
    case codec::LossyProfile::small_file: value = {}; break;
    }
}
}

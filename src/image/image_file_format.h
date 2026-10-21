#pragma once

#include <stdexcept>
#include <string_view>

namespace clipss2::image
{
enum class ImageFileFormat { unknown, png, jpeg, webp, avif, jxl };

[[nodiscard]] inline const char* image_file_format_name(ImageFileFormat format)
{
    switch (format)
    {
    case ImageFileFormat::png: return "PNG";
    case ImageFileFormat::jpeg: return "JPEG";
    case ImageFileFormat::webp: return "WebP";
    case ImageFileFormat::avif: return "AVIF";
    case ImageFileFormat::jxl: return "JPEG XL";
    default: throw std::invalid_argument("Invalid image file format.");
    }
}
[[nodiscard]] inline const char* image_file_format_token(ImageFileFormat format)
{
    switch (format)
    {
    case ImageFileFormat::png: return "png";
    case ImageFileFormat::jpeg: return "jpeg";
    case ImageFileFormat::webp: return "webp";
    case ImageFileFormat::avif: return "avif";
    case ImageFileFormat::jxl: return "jxl";
    default: throw std::invalid_argument("Invalid image file format.");
    }
}
[[nodiscard]] inline const char* image_file_extension(ImageFileFormat format)
{
    switch (format)
    {
    case ImageFileFormat::png: return ".png";
    case ImageFileFormat::jpeg: return ".jpg";
    case ImageFileFormat::webp: return ".webp";
    case ImageFileFormat::avif: return ".avif";
    case ImageFileFormat::jxl: return ".jxl";
    default: throw std::invalid_argument("Invalid image file format.");
    }
}
[[nodiscard]] inline ImageFileFormat parse_image_file_format(std::string_view token)
{
    if (token == "png") return ImageFileFormat::png;
    if (token == "jpeg") return ImageFileFormat::jpeg;
    if (token == "webp") return ImageFileFormat::webp;
    if (token == "avif") return ImageFileFormat::avif;
    if (token == "jxl") return ImageFileFormat::jxl;
    throw std::invalid_argument("Worker file format must be png, jpeg, webp, avif, or jxl.");
}
}

#pragma once

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <span>

#include "image/cpu_image.h"

namespace clipss2::codec::png
{
struct PngValidationResult
{
    std::uint32_t width{};
    std::uint32_t height{};
    std::uintmax_t file_bytes{};
};

[[nodiscard]] PngValidationResult validate_screenshot_png(
    const std::filesystem::path& path,
    const image::CpuImage& source_bgra8);

[[nodiscard]] PngValidationResult validate_screenshot_png(
    std::span<const std::byte> bytes,
    const image::CpuImage& source_bgra8);
}

#pragma once

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

#include "image/cpu_image.h"

namespace clipss2::codec::png
{
struct PngEncodeResult
{
    std::filesystem::path path;
    std::uintmax_t file_bytes{};
};

[[nodiscard]] PngEncodeResult encode_srgb_png(
    const image::CpuImage& image,
    const std::filesystem::path& output_path);

[[nodiscard]] std::vector<std::byte> encode_srgb_png_bytes(
    const image::CpuImage& image);

[[nodiscard]] PngEncodeResult write_png_bytes(
    std::span<const std::byte> bytes,
    const std::filesystem::path& output_path);

[[nodiscard]] image::CpuImage decode_rgba8_png(
    const std::filesystem::path& input_path);

[[nodiscard]] image::CpuImage decode_rgba8_png(
    std::span<const std::byte> input);
}

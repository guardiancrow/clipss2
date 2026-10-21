#pragma once
#include "codec/jpeg/jpeg_codec.h"
#include <filesystem>
#include <string>
#include <span>
#include <vector>

namespace bench
{
using Rgb = clipss2::codec::jpeg::OpaqueRgb8Image;
struct Source { std::string id, category; Rgb rgb; bool local{}; };
struct Metrics
{
    double mae{}, rmse{}, psnr{}, edge{}, flat{}, colored{};
    int maximum{};
    std::size_t edge_count{}, flat_count{}, colored_count{};
};
struct Structure
{
    unsigned width{}, height{};
    bool progressive{};
    std::string sampling, markers, application_markers;
};
std::vector<Source> corpus();
std::string sha256(std::span<const std::byte> bytes);
Metrics measure(const Rgb& reference, const Rgb& decoded);
Structure inspect(std::span<const std::byte> jpeg);
void write_bytes(const std::filesystem::path& path, std::span<const std::byte> bytes);
std::vector<std::byte> read_bytes(const std::filesystem::path& path);
void write_ppm(const std::filesystem::path& path, const Rgb& rgb);
Rgb read_ppm(const std::filesystem::path& path);
std::string number(double value);
}

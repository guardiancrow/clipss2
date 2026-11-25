// Copyright (c) 2026 guardiancrow. MIT.
#include "codec/avif/avif_codec.h"
#include "codec/avif/avif_lossy_validator.h"

#include <aom/aom_codec.h>
#include <avif/avif.h>
#include <libyuv.h>
#include <libyuv/version.h>
#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace clipss2;
namespace avif = clipss2::codec::avif;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace
{
static_assert(LIBYUV_VERSION == SEC_MAINT_LIBYUV_VERSION);

void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

void check_runtime()
{
    wchar_t image_path[MAX_PATH]{};
    const auto length = GetModuleFileNameW(nullptr, image_path, MAX_PATH);
    require(length > 0 && length < MAX_PATH, "Cannot locate the probe");
    const auto folder = fs::path(image_path).parent_path();
    for (const auto* name : {L"avif.dll", L"aom.dll", L"libyuv.dll", L"jpeg62.dll"})
    {
        const auto module = GetModuleHandleW(name);
        require(module != nullptr, "Expected DLL not loaded");
        const auto size = GetModuleFileNameW(module, image_path, MAX_PATH);
        require(size > 0 && size < MAX_PATH && fs::equivalent(folder / name, fs::path(image_path)),
            "DLL did not load from this probe's isolated directory");
    }
    require(std::string_view(avifVersion()) == "1.4.2", "Unexpected AVIF runtime");
    require(aom_codec_version_major() == 3 && aom_codec_version_minor() == 15 &&
        aom_codec_version_patch() == SEC_MAINT_AOM_PATCH, "Unexpected AOM runtime");
}

image::CpuImage fixture(std::string_view name)
{
    const std::uint32_t width = name == "large" ? 1921U : name == "odd" ? 31U : 257U;
    const std::uint32_t height = name == "large" ? 1081U : name == "odd" ? 17U : 145U;
    image::CpuImage result{width, height, image::PixelFormat::bgra8_unorm,
        std::vector<std::byte>(static_cast<std::size_t>(width) * height * 4U)};
    std::uint32_t rng = 0x7762A7B1U;
    for (std::uint32_t y = 0; y < height; ++y)
    for (std::uint32_t x = 0; x < width; ++x)
    {
        const auto p = (static_cast<std::size_t>(y) * width + x) * 4U;
        rng = rng * 1664525U + 1013904223U;
        const auto alpha = name == "alpha" ? (x * 29U + y * 17U) & 255U : 255U;
        for (unsigned c = 0; c < 3; ++c)
        {
            auto value = (x / (c + 2U) + (y + c * 71U) / 3U +
                ((x / 11U) ^ (y / 7U))) & 255U;
            if (name == "flat") value = c * 73U + 22U;
            if (name == "text") value = (x / 4U % 3U == 0 && y / 9U % 3U != 0) ? 30U : 240U;
            if (name == "noise") value = (rng >> (8U * c)) & 255U;
            result.pixels[p + c] = static_cast<std::byte>(alpha == 0U ? 0U : value);
        }
        result.pixels[p + 3] = static_cast<std::byte>(alpha);
    }
    return result;
}

std::vector<std::byte> encode(const image::CpuImage& image, bool lossy)
{
    if (!lossy) return avif::encode_lossless_avif_bytes(image);
    avif::AvifLossyConfig applied;
    auto output = avif::encode_lossy_avif_bytes(image, codec::LossyProfile::balanced, &applied);
    require(applied.quality == 65 && applied.quality_alpha == 100 && applied.speed == 6 &&
        applied.threads == 8 && applied.depth == 8 && applied.matrix == AVIF_MATRIX_COEFFICIENTS_BT709,
        "AVIF encoding policy changed");
    return output;
}

void validate(const std::vector<std::byte>& bytes, const image::CpuImage& source, bool lossy)
{
    if (lossy) (void)avif::validate_screenshot_avif_lossy(bytes, source);
    else (void)avif::validate_screenshot_avif(bytes, source);
}

void colour_and_scale()
{
    constexpr int width = 7, height = 5, stride = 11, rgb_stride = width * 4 + 8;
    constexpr auto canary = std::uint8_t{0xd7};
    std::vector<std::uint8_t> bgra(rgb_stride * height, canary);
    for (auto y = 0; y < height; ++y)
    for (auto x = 0; x < width; ++x)
    {
        bgra[y * rgb_stride + 4 * x] = static_cast<std::uint8_t>(20 + x * 30);
        bgra[y * rgb_stride + 4 * x + 1] = static_cast<std::uint8_t>(50 + y * 30);
        bgra[y * rgb_stride + 4 * x + 2] = static_cast<std::uint8_t>(225 - x * 25);
        bgra[y * rgb_stride + 4 * x + 3] = 255;
    }
    std::vector<std::uint8_t> y_plane(stride * height, canary), u_plane(y_plane), v_plane(y_plane);
    std::vector<std::uint8_t> roundtrip(rgb_stride * height, canary);
    require(libyuv::ARGBToJ444(bgra.data(), rgb_stride, y_plane.data(), stride, u_plane.data(),
        stride, v_plane.data(), stride, width, height) == 0, "BGRA to full-range YUV444 failed");
    require(libyuv::J444ToARGB(y_plane.data(), stride, u_plane.data(), stride, v_plane.data(),
        stride, roundtrip.data(), rgb_stride, width, height) == 0, "Full-range YUV444 to BGRA failed");
    int max_diff = 0;
    for (auto y = 0; y < height; ++y)
    {
        for (auto x = 0; x < width * 4; ++x)
            max_diff = std::max(max_diff, std::abs(roundtrip[y * rgb_stride + x] - bgra[y * rgb_stride + x]));
        for (auto x = width; x < stride; ++x)
            require(y_plane[y * stride + x] == canary && u_plane[y * stride + x] == canary &&
                v_plane[y * stride + x] == canary, "YUV row padding changed");
        for (auto x = width * 4; x < rgb_stride; ++x)
            require(roundtrip[y * rgb_stride + x] == canary, "RGB row padding changed");
    }
    std::vector<std::uint8_t> scaled(5 * 3, canary);
    require(libyuv::ScalePlane(y_plane.data(), stride, width, height, scaled.data(),
        5, 3, 3, libyuv::kFilterBilinear) == 0, "ScalePlane rejected the valid fixture");
    for (auto y = 0; y < 3; ++y)
        require(scaled[y * 5 + 3] == canary && scaled[y * 5 + 4] == canary, "ScalePlane row padding changed");
    if constexpr (LIBYUV_VERSION == 1943)
    {
        const auto snapshot = scaled;
        require(libyuv::ScalePlane(y_plane.data(), stride, 0, height, scaled.data(),
            5, 3, 3, libyuv::kFilterBilinear) != 0 && scaled == snapshot, "Invalid zero width was not rejected");
    }
    std::cout << "YUV444 full range, odd dimensions and padded rows; max RGB roundtrip delta "
        << max_diff << "; scaled samples";
    for (const auto value : scaled) std::cout << ' ' << unsigned{value};
    std::cout << '\n';
}

void measure(const image::CpuImage& source, bool lossy)
{
    const auto start = Clock::now();
    const auto packed = encode(source, lossy);
    const auto middle = Clock::now();
    const auto decoded = avif::decode_bgra8_avif(packed);
    const auto end = Clock::now();
    require(decoded.pixels.size() == source.pixels.size(), "Unexpected AVIF decoded size");
    double sum = 0;
    double squares = 0;
    int maximum = 0;
    for (std::size_t i = 0; i < decoded.pixels.size(); ++i)
    {
        const auto difference = std::abs(std::to_integer<int>(decoded.pixels[i]) -
            std::to_integer<int>(source.pixels[i]));
        if (i % 4U == 3U)
            require(difference == 0, "AVIF alpha changed");
        else
        {
            sum += difference;
            squares += difference * difference;
            maximum = std::max(maximum, difference);
        }
    }
    require(lossy || maximum == 0, "Lossless pixels changed");
    PROCESS_MEMORY_COUNTERS memory{};
    require(GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)) != 0, "Memory measurement failed");
    using Ms = std::chrono::duration<double, std::milli>;
    const double rgb_samples = static_cast<double>(source.width) * source.height * 3.0;
    std::cout << Ms(middle - start).count() << ' ' << Ms(end - middle).count()
        << ' ' << packed.size() << ' ' << memory.PeakWorkingSetSize << ' '
        << sum / rgb_samples << ' ' << maximum << ' ' << squares / rgb_samples << std::endl;
}
}

int main(int argc, char* argv[])
{
    try
    {
        check_runtime();
        if (argc == 2 && std::string_view(argv[1]) == "convert")
        {
            colour_and_scale();
            return 0;
        }
        require(argc >= 4, "Expected probe mode, fixture and quality mode");
        const std::string_view mode(argv[1]);
        const auto source = fixture(argv[2]);
        const bool lossy = std::string_view(argv[3]) == "lossy";
        if (mode == "bench")
        {
            std::string command;
            while (std::getline(std::cin, command)) measure(source, lossy);
            return 0;
        }
        require(argc == 5, "Expected AVIF fixture path");
        const fs::path path = fs::u8path(argv[4]);
        if (mode == "encode")
        {
            const auto bytes = encode(source, lossy);
            validate(bytes, source, lossy);
            std::ofstream file(path, std::ios::binary);
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            require(static_cast<bool>(file), "Cannot write AVIF fixture");
        }
        else
        {
            require(mode == "decode", "Unknown probe operation");
            const auto length = fs::file_size(path);
            require(length > 0 && length <= avif::maximum_avif_bytes, "Invalid AVIF fixture size");
            std::vector<std::byte> bytes(static_cast<std::size_t>(length));
            std::ifstream file(path, std::ios::binary);
            file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(length));
            require(static_cast<bool>(file), "Cannot read AVIF fixture");
            validate(bytes, source, lossy);
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

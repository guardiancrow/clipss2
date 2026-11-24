// Copyright (c) 2026 guardiancrow. MIT.
#include "codec/png/png_codec.h"
#include "codec/png/png_validator.h"

#include <png.h>
#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
using clipss2::image::CpuImage;
using clipss2::image::PixelFormat;
using namespace clipss2::codec::png;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::filesystem::path module_directory(HMODULE module)
{
    wchar_t path[32768];
    const auto length = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
    require(length > 0 && length < std::size(path), "Cannot identify a loaded module.");
    return std::filesystem::path(path).parent_path();
}

void verify_loaded_png()
{
    require(PNG_LIBPNG_VER == SEC_MAINT_PNG_VERSION &&
                png_access_version_number() == SEC_MAINT_PNG_VERSION,
            "Unexpected libpng header or DLL version.");
    const auto executable = module_directory(nullptr);
    for (const auto name : {L"libpng16.dll", L"z.dll"})
    {
        const auto module = GetModuleHandleW(name);
        require(module && std::filesystem::equivalent(module_directory(module), executable),
                "PNG dependency loaded outside the isolated test directory.");
    }
}

CpuImage fixture(std::wstring_view name)
{
    const bool large = name == L"large";
    const bool transparent = name == L"transparent";
    const bool detail = name == L"detail";
    const bool flat = name == L"flat";
    require(large || transparent || detail || flat || name == L"odd", "Unknown synthetic fixture.");
    const std::uint32_t w = large ? 1921U : (detail ? 641U : 257U);
    const std::uint32_t h = large ? 1081U : (detail ? 479U : 199U);
    CpuImage result{w, h, PixelFormat::bgra8_unorm, std::vector<std::byte>(4ULL * w * h)};
    std::uint32_t random = 0x1e35a7bdU;
    for (std::uint32_t y = 0; y < h; ++y)
    {
        for (std::uint32_t x = 0; x < w; ++x)
        {
            const auto offset = 4ULL * (static_cast<std::size_t>(y) * w + x);
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            const auto value = detail ? random : 29U * x + 11U * y;
            result.pixels[offset] = static_cast<std::byte>((flat ? 37U : value) & 255U);
            result.pixels[offset + 1] = static_cast<std::byte>((flat ? 141U : value >> 3) & 255U);
            result.pixels[offset + 2] = static_cast<std::byte>((flat ? 205U : value >> 7) & 255U);
            result.pixels[offset + 3] = static_cast<std::byte>(
                transparent ? (((x + y) & 3U) * 85U) : 255U);
        }
    }
    return result;
}
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        require(argc == 4, "Usage: probe write|read fixture file.");
        verify_loaded_png();
        const std::wstring_view operation = argv[1];
        const auto image = fixture(argv[2]);
        const std::filesystem::path path = argv[3];
        if (operation == L"read")
        {
            (void)validate_screenshot_png(path, image);
        }
        else if (operation == L"write")
        {
            auto bytes = encode_srgb_png_bytes(image);
            (void)validate_screenshot_png(bytes, image);
            (void)write_png_bytes(bytes, path);
            bytes.resize(bytes.size() - 1);
            bool rejected = false;
            try
            {
                (void)validate_screenshot_png(bytes, image);
            }
            catch (const std::runtime_error&)
            {
                rejected = true;
            }
            require(rejected, "Truncated PNG passed the product validator.");
        }
        else
        {
            throw std::runtime_error("Unknown probe operation.");
        }
        std::cout << "libpng " << png_access_version_number() << " verified synthetic PNG\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

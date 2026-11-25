// Copyright (c) 2026 guardiancrow. MIT.
#include "audit/jpeg_output_audit.h"
#include "codec/jpeg/jpeg_validator.h"
#include "codec/png/png_codec.h"
#include "codec/png/png_validator.h"

#include <cstdio>
#include <jpeglib.h>
#include <turbojpeg.h>
#include <windows.h>
#include <zlib.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace
{
using namespace clipss2;
static_assert(JPEG_LIB_VERSION == 62);
static_assert(LIBJPEG_TURBO_VERSION_NUMBER == SEC_MAINT_TURBO_VERSION);

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::filesystem::path module_path(HMODULE module)
{
    wchar_t path[32768]{};
    const auto size = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
    require(size && size < std::size(path), "Cannot identify module.");
    return path;
}

HMODULE local_module(const wchar_t* name)
{
    const auto handle = GetModuleHandleW(name);
    require(handle && std::filesystem::equivalent(module_path(handle).parent_path(),
                                                  module_path(nullptr).parent_path()),
            "Dependency was loaded outside the isolated probe directory.");
    return handle;
}

void check_turbo_version(HMODULE module)
{
    const auto path = module_path(module);
    const auto size = GetFileVersionInfoSizeW(path.c_str(), nullptr);
    require(size != 0, "Version resource unavailable.");
    std::vector<std::byte> bytes(size);
    require(GetFileVersionInfoW(path.c_str(), 0, size, bytes.data()) != 0, "Cannot read version resource.");
    void* data = nullptr;
    UINT length = 0;
    require(VerQueryValueW(bytes.data(), L"\\", &data, &length) != 0 &&
            length >= sizeof(VS_FIXEDFILEINFO), "Missing product version.");
    const auto& info = *static_cast<const VS_FIXEDFILEINFO*>(data);
    const auto version = HIWORD(info.dwProductVersionMS) * 1000000U +
        LOWORD(info.dwProductVersionMS) * 1000U + HIWORD(info.dwProductVersionLS);
    require(version == SEC_MAINT_TURBO_VERSION, "Loaded JPEG DLL version differs.");
}

void verify_dependencies()
{
    check_turbo_version(local_module(L"turbojpeg.dll"));
    check_turbo_version(local_module(L"jpeg62.dll"));
    local_module(L"libpng16.dll");
    const auto zlib = local_module(L"libz.dll");
    const auto zversion = reinterpret_cast<decltype(&zlibVersion)>(GetProcAddress(zlib, "zlibVersion"));
    require(zversion && std::string_view(zversion()) == "1.4" &&
            std::string_view(ZLIB_VERSION) == "1.4" && !GetModuleHandleW(L"z.dll"),
            "Unexpected shared zlib.");
    if constexpr (SEC_MAINT_TURBO_VERSION == 3002001)
    {
        const auto spng = local_module(L"spng.dll");
        using Version = const char* (*)();
        const auto sversion = reinterpret_cast<Version>(GetProcAddress(spng, "spng_version_string"));
        require(sversion && std::string_view(sversion()) == "0.7.4", "Unexpected libspng version.");
    }
    jpeg_error_mgr error{};
    jpeg_decompress_struct decoder{};
    decoder.err = jpeg_std_error(&error);
    jpeg_create_decompress(&decoder);
    jpeg_destroy_decompress(&decoder);
}

void write(const std::filesystem::path& path, std::span<const std::byte> bytes)
{
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    require(static_cast<bool>(stream), "Cannot write synthetic image.");
}

std::vector<std::byte> read(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    const auto size = stream.tellg();
    require(size > 0 && static_cast<std::uintmax_t>(size) <= codec::jpeg::maximum_jpeg_bytes,
            "Unexpected JPEG fixture size.");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    require(static_cast<bool>(stream.read(reinterpret_cast<char*>(bytes.data()), size)), "Cannot read JPEG.");
    return bytes;
}

image::CpuImage png_fixture()
{
    constexpr unsigned int width = 257, height = 31;
    image::CpuImage image{width, height, image::PixelFormat::bgra8_unorm,
        std::vector<std::byte>(4 * width * height)};
    for (unsigned int y = 0; y < height; ++y)
        for (unsigned int x = 0; x < width; ++x)
        {
            const auto index = 4 * (y * width + x);
            image.pixels[index] = static_cast<std::byte>((x + 3 * y) & 255U);
            image.pixels[index + 1] = static_cast<std::byte>((x * 5 + y) & 255U);
            image.pixels[index + 2] = static_cast<std::byte>((x + 7 * y) & 255U);
            image.pixels[index + 3] = std::byte{255};
        }
    return image;
}

void load_png()
{
    const auto image = png_fixture();
    const std::unique_ptr<void, decltype(&tj3Destroy)> handle(tj3Init(TJINIT_COMPRESS), tj3Destroy);
    require(handle != nullptr, "Cannot initialize loader.");
    for (const auto file : {"loader-truncated.png", "loader.png"})
    {
        int width{}, height{}, format = TJPF_RGB;
        constexpr int align = 16;
        const std::unique_ptr<unsigned char, decltype(&tj3Free)> data(
            tj3LoadImage8(handle.get(), file, &width, align, &height, &format), tj3Free);
        if (std::string_view(file) == "loader-truncated.png")
        {
            require(!data, "Loader accepted incomplete PNG.");
            continue;
        }
        require(data && width == static_cast<int>(image.width) &&
            height == static_cast<int>(image.height) && format == TJPF_RGB, "Loader returned unexpected image shape.");
        const auto stride = (width * 3 + align - 1) & ~(align - 1);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const auto src = 4 * (y * width + x);
                const auto dst = y * stride + 3 * x;
                require(data.get()[dst] == std::to_integer<unsigned char>(image.pixels[src + 2]) &&
                        data.get()[dst + 1] == std::to_integer<unsigned char>(image.pixels[src + 1]) &&
                        data.get()[dst + 2] == std::to_integer<unsigned char>(image.pixels[src]),
                        "TurboJPEG PNG loader pixel or stride mismatch.");
            }
        std::cout << codec::jpeg::jpeg_backend_version() << '\t' << width << 'x' << height
                  << "\tRGB\t" << stride << "\tvalid after truncated input\n";
    }
}
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        require(argc >= 2, "Probe mode missing.");
        verify_dependencies();
        const std::wstring_view mode = argv[1];
        if (mode == L"fixture" && argc == 2)
        {
            const auto image = png_fixture();
            auto bytes = codec::png::encode_srgb_png_bytes(image);
            (void)codec::png::validate_screenshot_png(bytes, image);
            write("loader.png", bytes);
            bytes.resize(bytes.size() / 2);
            write("loader-truncated.png", bytes);
        }
        else if (mode == L"load" && argc == 2)
        {
            load_png();
        }
        else if (argc == 3)
        {
            const auto image = codec::jpeg::flatten_wgc_premultiplied_bgra8_for_jpeg(audit::make_jpeg_sdr_fixture());
            if (mode == L"encode")
            {
                const auto bytes = codec::jpeg::encode_srgb_jpeg_bytes(image);
                (void)codec::jpeg::validate_screenshot_jpeg(bytes, image.width, image.height);
                write(argv[2], bytes);
            }
            else if (mode == L"decode")
            {
                const auto decoded = codec::jpeg::decode_srgb_jpeg_bytes(read(argv[2]));
                const auto metrics = audit::measure_jpeg_fixture(image, decoded);
                require(audit::jpeg_fixture_quality_passes(metrics), "JPEG quality thresholds failed.");
                std::cout << codec::jpeg::jpeg_backend_version() << '\t' << metrics.mean_absolute_error
                          << '\t' << metrics.maximum_error << '\t' << metrics.patch_maximum_error << '\n';
            }
            else
                throw std::runtime_error("Unknown probe mode.");
        }
        else
            throw std::runtime_error("Unexpected probe arguments.");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

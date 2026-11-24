// Copyright (c) 2026 guardiancrow. MIT.
#include "codec/jxl/jxl_codec.h"
#include "codec/jxl/jxl_lossy_validator.h"
#include "codec/jxl/jxl_validator.h"

#include <jxl/decode.h>
#include <jxl/encode.h>
#include <jxl/thread_parallel_runner.h>
#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using clipss2::image::CpuImage;
using clipss2::image::PixelFormat;
using namespace clipss2::codec::jxl;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::filesystem::path module_directory(HMODULE module)
{
    wchar_t path[32768];
    const auto length = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
    require(length > 0 && length < std::size(path), "Cannot identify a loaded module");
    return std::filesystem::path(path).parent_path();
}

void verify_loaded_codec()
{
    require(JxlEncoderVersion() == SEC_MAINT_JXL_VERSION &&
            JxlDecoderVersion() == SEC_MAINT_JXL_VERSION, "Unexpected libjxl version");
    const auto executable_directory = module_directory(nullptr);
    for (const auto name : {L"jxl.dll", L"jxl_threads.dll", L"jxl_cms.dll", L"hwy.dll",
                            L"brotlicommon.dll", L"brotlienc.dll", L"brotlidec.dll", L"lcms2-2.dll"}) {
        const auto module = GetModuleHandleW(name);
        require(module && std::filesystem::equivalent(module_directory(module), executable_directory),
                "Unexpected module outside the isolated codec directory");
    }
}

CpuImage fixture(std::string_view name)
{
    const bool large = name == "large";
    const bool texture = name == "texture";
    require(large || texture || name == "odd", "Unknown synthetic fixture");
    const std::uint32_t w = large ? 1921U : (texture ? 641U : 257U);
    const std::uint32_t h = large ? 1081U : (texture ? 479U : 199U);
    CpuImage image{w, h, PixelFormat::bgra8_unorm, std::vector<std::byte>(4ULL * w * h)};
    std::uint32_t random = 0xa13f0603U;
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const auto i = 4ULL * (static_cast<std::size_t>(y) * w + x);
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            const auto pattern = texture ? (random & 255U) : (((x / 16 + y / 24) % 8) * 36U);
            const auto alpha = y < h / 2 ? 255U : ((x / 13) % 4) * 85U;
            image.pixels[i] = std::byte{static_cast<unsigned char>(alpha ? pattern : 0)};
            image.pixels[i + 1] = std::byte{static_cast<unsigned char>(alpha ? (pattern + 2 * x) % 256 : 0)};
            image.pixels[i + 2] = std::byte{static_cast<unsigned char>(alpha ? (pattern + y) % 256 : 0)};
            image.pixels[i + 3] = std::byte{static_cast<unsigned char>(alpha)};
        }
    }
    return image;
}

void verify(const std::vector<std::byte>& bytes, const CpuImage& image)
{
    const auto inspection = validate_screenshot_jxl(bytes, image);
    require(inspection.width == image.width && inspection.height == image.height &&
            inspection.depth == 8 && inspection.image_count == 1 && inspection.has_alpha,
            "JPEG XL output contract changed");
}

void verify_lossy(const CpuImage& image)
{
    JxlLossyConfig applied;
    const auto bytes = encode_lossy_jxl_bytes(image, clipss2::codec::LossyProfile::balanced, &applied);
    require(applied.effort == product_jxl_effort && applied.alpha_distance == 0,
            "JPEG XL effort or alpha policy changed");
    const auto inspection = validate_screenshot_jxl_lossy(bytes, image);
    require(inspection.has_alpha && inspection.image_count == 1, "Lossy alpha contract changed");
}

std::vector<std::byte> read_jxl(const char* path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto length = input.tellg();
    require(length > 0 && static_cast<std::size_t>(length) <= maximum_jxl_bytes, "Invalid comparison file");
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    require(static_cast<bool>(input.read(reinterpret_cast<char*>(bytes.data()), length)),
            "Cannot read comparison file");
    return bytes;
}
} // namespace

int main(int argc, char** argv)
{
    try {
        require(argc == 3 || argc == 4, "Usage: probe test|bench|encode|decode fixture [file]");
        verify_loaded_codec();
        const std::string_view mode = argv[1];
        const auto image = fixture(argv[2]);
        if (mode == "decode" && argc == 4) {
            verify(read_jxl(argv[3]), image);
        } else if (mode == "encode" && argc == 4) {
            const auto bytes = encode_lossless_jxl_bytes(image);
            verify(bytes, image);
            std::ofstream output(argv[3], std::ios::binary);
            require(static_cast<bool>(output.write(reinterpret_cast<const char*>(bytes.data()),
                                                   static_cast<std::streamsize>(bytes.size()))),
                    "Cannot write comparison file");
        } else if (mode == "test" && argc == 3) {
            auto bytes = encode_lossless_jxl_bytes(image);
            verify(bytes, image);
            verify_lossy(image);
            bytes.resize(2);
            bool rejected = false;
            try {
                verify(bytes, image);
            } catch (const std::exception&) {
                rejected = true;
            }
            require(rejected, "Truncated JPEG XL passed validation");
        } else if (mode == "bench" && argc == 3) {
            using clock = std::chrono::steady_clock;
            using milliseconds = std::chrono::duration<double, std::milli>;
            const auto start = clock::now();
            const auto bytes = encode_lossless_jxl_bytes(image);
            const auto encoded = clock::now();
            const auto decoded = decode_bgra8_jxl(bytes);
            const auto finished = clock::now();
            require(decoded.pixels == image.pixels, "Timed round trip changed pixels");
            PROCESS_MEMORY_COUNTERS counters{};
            require(GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0,
                    "Cannot measure process peak working set");
            std::cout << JxlEncoderVersion() << ','
                      << std::clamp<std::size_t>(JxlThreadParallelRunnerDefaultNumWorkerThreads(), 1, 8) << ','
                      << milliseconds(encoded - start).count() << ','
                      << milliseconds(finished - encoded).count() << ',' << bytes.size() << ','
                      << counters.PeakWorkingSetSize << '\n';
        } else {
            throw std::runtime_error("Invalid probe arguments");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

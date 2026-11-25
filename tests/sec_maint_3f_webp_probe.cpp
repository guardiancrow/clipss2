// Copyright (c) 2026 guardiancrow. MIT.
#include "codec/webp/webp_codec.h"
#include "codec/webp/webp_validator.h"

#include <webp/decode.h>
#include <webp/encode.h>
#include <windows.h>
#include <psapi.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace webp = clipss2::codec::webp;
namespace image = clipss2::image;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace
{
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

void check_runtime()
{
    wchar_t path[MAX_PATH]{};
    const auto length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    require(length > 0 && length < MAX_PATH, "Cannot locate WebP probe");
    const auto directory = fs::path(path).parent_path();
    for (const auto* name : {L"libwebp.dll", L"libsharpyuv.dll"})
    {
        const auto loaded = GetModuleHandleW(name);
        const auto size = loaded ? GetModuleFileNameW(loaded, path, MAX_PATH) : 0;
        require(size > 0 && size < MAX_PATH && fs::equivalent(directory / name, fs::path(path)),
            "WebP DLL resolved outside the isolated probe");
    }
    require(WebPGetEncoderVersion() == 0x010600 && WebPGetDecoderVersion() == 0x010600,
        "Unexpected WebP runtime version");
    const auto policy = webp::product_webp_policy();
    require(policy.lossless == 1 && policy.exact == 1 && policy.method == 4 &&
        policy.thread_level == 1 && policy.image_hint == WEBP_HINT_GRAPH && policy.quality == 75,
        "WebP product settings changed");
}

image::CpuImage fixture(std::string_view name)
{
    const std::uint32_t width = name == "4k" ? 3840U : name == "full-hd" ? 1920U :
        name == "wide" ? 16383U : name == "tiny" ? 1U : name == "odd" ? 31U : 257U;
    const std::uint32_t height = name == "4k" ? 2160U : name == "full-hd" ? 1080U :
        name == "wide" || name == "tiny" ? 1U : name == "odd" ? 17U : 145U;
    image::CpuImage source{width, height, image::PixelFormat::bgra8_unorm,
        std::vector<std::byte>(static_cast<std::size_t>(width) * height * 4U)};
    std::uint32_t rng = 0x5c6ec621U;
    for (std::uint32_t y = 0; y < height; ++y)
    for (std::uint32_t x = 0; x < width; ++x)
    {
        const auto offset = (static_cast<std::size_t>(y) * width + x) * 4U;
        rng = rng * 1664525U + 1013904223U;
        const auto alpha = name == "alpha" || name == "odd" ? (x * 37U + y * 13U) & 255U : 255U;
        for (unsigned c = 0; c < 3; ++c)
        {
            auto value = (x / (c + 3U) + (y + c * 71U) / 5U +
                ((x / 9U) ^ (y / 17U))) & 255U;
            if (name == "flat") value = c * 73U + 22U;
            if (name == "text") value = (x / 4U % 3U == 0 && y / 9U % 3U != 0) ? 30U : 240U;
            if (name == "detail") value = ((x ^ y ^ c) & 1U) * 255U;
            if (name == "noise") value = (rng >> (8U * c)) & 255U;
            if (name == "4k" || name == "full-hd") value = ((x / 40U) * 7U + y / 36U + c * 101U) & 255U;
            source.pixels[offset + c] = static_cast<std::byte>(alpha == 0U ? 0U : value);
        }
        source.pixels[offset + 3] = static_cast<std::byte>(alpha);
    }
    return source;
}

void measure(const image::CpuImage& source)
{
    const auto start = Clock::now();
    const auto packed = webp::encode_lossless_webp_bytes(source);
    const auto middle = Clock::now();
    const auto decoded = webp::decode_bgra8_webp(packed);
    const auto end = Clock::now();
    require(decoded.width == source.width && decoded.height == source.height && decoded.pixels == source.pixels,
        "WebP lossless pixels changed");
    PROCESS_MEMORY_COUNTERS memory{};
    require(GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)) != 0, "Memory measurement failed");
    using Ms = std::chrono::duration<double, std::milli>;
    std::cout << Ms(middle - start).count() << ' ' << Ms(end - middle).count()
        << ' ' << packed.size() << ' ' << memory.PeakWorkingSetSize << " 0 0 0" << std::endl;
}

void validate(const std::vector<std::byte>& bytes, const image::CpuImage& source)
{
    const auto shape = webp::validate_screenshot_webp(bytes, source);
    require(shape.image_payload_count == 1, "Expected one WebP image payload");
}
}

int main(int argc, char* argv[])
{
    try
    {
        check_runtime();
        require(argc >= 3, "Expected WebP probe mode and fixture");
        const std::string_view mode(argv[1]);
        const auto source = fixture(argv[2]);
        if (mode == "bench")
        {
            std::string command;
            while (std::getline(std::cin, command)) measure(source);
            return 0;
        }
        require(argc == 4, "Expected WebP fixture path");
        const fs::path path = fs::u8path(argv[3]);
        if (mode == "encode")
        {
            const auto bytes = webp::encode_lossless_webp_bytes(source);
            validate(bytes, source);
            if (std::string_view(argv[2]) == "tiny")
            {
                auto truncated = bytes;
                truncated.pop_back();
                bool rejected = false;
                try { (void)webp::inspect_webp_structure(truncated); }
                catch (const std::exception&) { rejected = true; }
                require(rejected, "Truncated WebP was accepted");
                image::CpuImage beyond{16384, 1, image::PixelFormat::bgra8_unorm, {}};
                rejected = false;
                try { (void)webp::encode_lossless_webp_bytes(beyond); }
                catch (const std::invalid_argument&) { rejected = true; }
                require(rejected, "Unsupported WebP width was accepted");
            }
            std::ofstream file(path, std::ios::binary);
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            require(static_cast<bool>(file), "Cannot write WebP fixture");
        }
        else
        {
            require(mode == "decode", "Unknown WebP probe mode");
            const auto length = fs::file_size(path);
            require(length > 0 && length <= webp::maximum_webp_bytes, "Invalid WebP fixture size");
            std::vector<std::byte> bytes(static_cast<std::size_t>(length));
            std::ifstream file(path, std::ios::binary);
            file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(length));
            require(static_cast<bool>(file), "Cannot read WebP fixture");
            validate(bytes, source);
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

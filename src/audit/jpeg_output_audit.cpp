#include "audit/jpeg_output_audit.h"
#include "codec/jpeg/jpeg_validator.h"
#include "codec/png/png_codec.h"
#include "codec/png/png_validator.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace clipss2::audit
{
namespace
{
constexpr std::uint32_t fixture_width = 320;
constexpr std::uint32_t fixture_height = 192;

image::CpuImage opaque_bgra(const codec::jpeg::OpaqueRgb8Image& rgb)
{
    image::CpuImage result{rgb.width, rgb.height, image::PixelFormat::bgra8_unorm,
                          std::vector<std::byte>(rgb.pixels.size() / 3 * 4)};
    for (std::size_t p = 0; p < rgb.pixels.size() / 3; ++p)
    {
        result.pixels[p * 4] = rgb.pixels[p * 3 + 2];
        result.pixels[p * 4 + 1] = rgb.pixels[p * 3 + 1];
        result.pixels[p * 4 + 2] = rgb.pixels[p * 3];
        result.pixels[p * 4 + 3] = std::byte{255};
    }
    return result;
}

void write_artifact(std::span<const std::byte> bytes, const std::filesystem::path& path)
{
    if (bytes.empty() || bytes.size() > static_cast<std::size_t>(
            std::numeric_limits<std::streamsize>::max()))
        throw std::runtime_error("JPEG audit artifact size is invalid.");
    std::ofstream stream{path, std::ios::binary};
    if (!stream) throw std::runtime_error("JPEG audit artifact open failed.");
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    stream.close();
    if (!stream) throw std::runtime_error("JPEG audit artifact write/close failed.");
}
}

image::CpuImage make_jpeg_sdr_fixture()
{
    image::CpuImage result{fixture_width, fixture_height, image::PixelFormat::bgra8_unorm,
        std::vector<std::byte>(static_cast<std::size_t>(fixture_width) * fixture_height * 4)};
    constexpr std::array<image::Bgra8Pixel, 10> patches{{
        {0, 0, 0, 255}, {255, 255, 255, 255}, {0, 0, 255, 255},
        {0, 255, 0, 255}, {255, 0, 0, 255}, {128, 128, 128, 255},
        {80, 150, 200, 0}, {0, 0, 128, 128}, {64, 0, 0, 64}, {0, 192, 0, 192}}};
    constexpr std::array<unsigned int, 7> glyph{14, 17, 17, 31, 17, 17, 17};
    for (std::uint32_t y = 0; y < fixture_height; ++y)
    {
        for (std::uint32_t x = 0; x < fixture_width; ++x)
        {
            auto pixel = patches[x / 32];
            if (y >= 64 && y < 128)
            {
                const auto gray = static_cast<std::uint8_t>(x * 255U / (fixture_width - 1));
                pixel = {gray, gray, gray, 255};
            }
            if (y >= 128)
            {
                pixel = {static_cast<std::uint8_t>((y - 128) * 255U / 63U),
                         static_cast<std::uint8_t>(x * 255U / (fixture_width - 1)),
                         static_cast<std::uint8_t>(255U - x * 255U / (fixture_width - 1)), 255};
                // Thin colored glyph-like UI edges and 1/2-pixel vertical lines.
                const auto gx = (x % 32) / 2;
                const auto gy = ((y - 128) % 32) / 2;
                if ((gx < 5 && gy < 7 && (glyph[gy] & (1U << (4 - gx))) != 0) ||
                    x % 32 == 20 || x % 32 == 24 || x % 32 == 25)
                    pixel = {0, 0, 255, 255};
            }
            const auto offset = (static_cast<std::size_t>(y) * fixture_width + x) * 4;
            result.pixels[offset] = static_cast<std::byte>(pixel.blue);
            result.pixels[offset + 1] = static_cast<std::byte>(pixel.green);
            result.pixels[offset + 2] = static_cast<std::byte>(pixel.red);
            result.pixels[offset + 3] = static_cast<std::byte>(pixel.alpha);
        }
    }
    return result;
}

JpegFixtureMetrics measure_jpeg_fixture(
    const codec::jpeg::OpaqueRgb8Image& reference,
    const codec::jpeg::OpaqueRgb8Image& decoded)
{
    if (reference.width != fixture_width || reference.height != fixture_height ||
        decoded.width != reference.width || decoded.height != reference.height ||
        reference.pixels.size() != codec::jpeg::checked_rgb_image_bytes(reference.width, reference.height) ||
        decoded.pixels.size() != reference.pixels.size())
        throw std::runtime_error("JPEG quality fixture RGB structure mismatch.");
    std::uint64_t sum = 0;
    JpegFixtureMetrics metrics{};
    for (std::size_t i = 0; i < reference.pixels.size(); ++i)
    {
        const auto error = static_cast<unsigned int>(std::abs(
            std::to_integer<int>(reference.pixels[i]) - std::to_integer<int>(decoded.pixels[i])));
        sum += error;
        metrics.maximum_error = std::max(metrics.maximum_error, error);
        const auto pixel = i / 3;
        const auto x = pixel % fixture_width;
        const auto y = pixel / fixture_width;
        if (y >= 16 && y < 48 && x % 32 >= 8 && x % 32 < 24)
            metrics.patch_maximum_error = std::max(metrics.patch_maximum_error, error);
    }
    metrics.mean_absolute_error = static_cast<double>(sum) /
                                  static_cast<double>(reference.pixels.size());
    return metrics;
}

bool jpeg_fixture_quality_passes(const JpegFixtureMetrics& metrics) noexcept
{
    // Conservative correctness checks at Q90/4:4:4, not an encoder ranking:
    // allow ringing near 1-pixel edges, but constrain aggregate distortion
    // and flat patch centers enough to catch channel swaps or dark halos.
    return metrics.mean_absolute_error <= 6.0 && metrics.maximum_error <= 80 &&
           metrics.patch_maximum_error <= 4;
}

int run_jpeg_output_audit(const std::filesystem::path& artifact_root)
{
    const auto source = make_jpeg_sdr_fixture();
    const codec::jpeg::JpegOptions options{};
    const auto reference = codec::jpeg::flatten_wgc_premultiplied_bgra8_for_jpeg(
        source, options.alpha_flatten);
    const auto jpeg = codec::jpeg::encode_srgb_jpeg_bytes(reference, options);
    const auto validation = codec::jpeg::validate_screenshot_jpeg(jpeg, reference.width, reference.height);
    const auto decoded = codec::jpeg::decode_srgb_jpeg_bytes(jpeg);
    const auto metrics = measure_jpeg_fixture(reference, decoded);
    if (!jpeg_fixture_quality_passes(metrics))
        throw std::runtime_error("JPEG controlled quality fixture failed.");
    const auto png_source = opaque_bgra(reference);
    const auto png = codec::png::encode_srgb_png_bytes(png_source);
    (void)codec::png::validate_screenshot_png(png, png_source);

    std::filesystem::create_directories(artifact_root);
    std::filesystem::path directory;
    for (unsigned int index = 1; index <= 100000; ++index)
    {
        const auto candidate = artifact_root / ("audit-" + std::to_string(index));
        if (std::filesystem::create_directory(candidate)) { directory = candidate; break; }
    }
    if (directory.empty()) throw std::runtime_error("JPEG audit could not reserve artifact directory.");
    const auto pending = directory / "controlled.pending";
    const auto output = directory / "controlled.jpg";
    const auto png_output = directory / "reference.png";
    try
    {
        write_artifact(jpeg, pending);
        write_artifact(png, png_output);
        std::filesystem::rename(pending, output);
    }
    catch (...)
    {
        // Only newly reserved files, never existing user's artifacts.
        std::error_code error;
        for (const auto& path : {pending, png_output})
        {
            std::filesystem::remove(path, error);
            if (error) std::cerr << "JPEG audit cleanup failed: " << error.message() << '\n';
        }
        std::filesystem::remove(directory, error);
        if (error) std::cerr << "JPEG audit directory cleanup failed: " << error.message() << '\n';
        throw;
    }
    std::cout << "JPEG backend: libjpeg-turbo " << codec::jpeg::jpeg_backend_version() << " (provisional TurboJPEG API)\n"
              << "JPEG quality: " << options.quality << "\nJPEG subsampling: 4:4:4\n"
              << "JPEG coding: baseline sequential, progressive/optimize/arithmetic off\n"
              << "JPEG input: SDR/sRGB opaque RGB8, premultiplied BGRA white matte\n"
              << "JPEG dimensions: " << validation.width << " x " << validation.height << '\n'
              << "JPEG bytes: " << jpeg.size() << "\nPNG opaque-reference bytes: " << png.size() << '\n'
              << "JPEG markers: " << validation.marker_summary << '\n'
              << "JPEG metadata/privacy validation: succeeded\nJPEG decode validation: succeeded\n"
              << "JPEG fixture MAE/max/patch: " << metrics.mean_absolute_error << "/"
              << metrics.maximum_error << "/" << metrics.patch_maximum_error << '\n'
              << "JPEG artifact: " << std::filesystem::absolute(output).string() << '\n'
              << "PNG reference: " << std::filesystem::absolute(png_output).string() << '\n';
    return 0;
}
}

#include "capture/capture_image_output.h"
#include "codec/png/png_codec.h"
#include "codec/png/png_validator.h"
#include "codec/jpeg/jpeg_codec.h"
#include "codec/jpeg/jpeg_validator.h"
#include "codec/jpeg/jpeg_managed_backend.h"
#include "codec/webp/webp_codec.h"
#include "codec/webp/webp_validator.h"
#include "codec/avif/avif_codec.h"
#include "codec/avif/avif_validator.h"
#include "codec/jxl/jxl_codec.h"
#include "codec/jxl/jxl_validator.h"
#include "codec/avif/avif_lossy_validator.h"
#include "codec/jxl/jxl_lossy_validator.h"
#include "image/alpha_diagnostics.h"

#include <chrono>
#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace clipss2::capture
{
PreparedCaptureImageOutput prepare_capture_image_output(
    const image::CpuImage& source, CaptureDestination destination,
    image::ImageFileFormat format, codec::jpeg::JpegProductSelection jpeg_selection, FileCompression compression)
{
    (void)image::image_file_format_name(format); // Even Clipboard fails closed.
    validate_file_compression(format, compression);
    const auto plan = capture_output_plan(destination);
    PreparedCaptureImageOutput result;
    if (plan.clipboard || (plan.file &&
        (format == image::ImageFileFormat::png || format == image::ImageFileFormat::webp ||
            format == image::ImageFileFormat::avif || format == image::ImageFileFormat::jxl)))
    {
        result.lossless_source.emplace(image::unpremultiply_bgra8_for_lossless(source));
    }
    if (plan.clipboard || (plan.file && format == image::ImageFileFormat::png))
    {
        result.png_bytes = codec::png::encode_srgb_png_bytes(*result.lossless_source);
        (void)codec::png::validate_screenshot_png(result.png_bytes, *result.lossless_source);
    }
    if (plan.file && format == image::ImageFileFormat::webp)
    {
        const auto start = std::chrono::steady_clock::now();
        result.webp_bytes = codec::webp::encode_lossless_webp_bytes(*result.lossless_source);
        (void)codec::webp::validate_screenshot_webp(result.webp_bytes, *result.lossless_source);
        result.webp_encode_milliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
    }
    if (plan.file && format == image::ImageFileFormat::avif)
    {
        const auto start = std::chrono::steady_clock::now();
        result.avif_bytes = compression.mode == FileCompressionMode::lossy ?
            codec::avif::encode_lossy_avif_bytes(*result.lossless_source, compression.profile) :
            codec::avif::encode_lossless_avif_bytes(*result.lossless_source);
        const auto encoded = std::chrono::steady_clock::now();
        validate_capture_file_payload(format, compression, result.avif_bytes, *result.lossless_source);
        if (compression.mode == FileCompressionMode::lossy)
        {
            result.lossy_encode_milliseconds = std::chrono::duration<double, std::milli>(encoded - start).count();
            result.lossy_validate_milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - encoded).count();
        }
        result.avif_encode_milliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
    }
    if (plan.file && format == image::ImageFileFormat::jxl)
    {
        const auto start = std::chrono::steady_clock::now();
        result.jxl_bytes = compression.mode == FileCompressionMode::lossy ?
            codec::jxl::encode_lossy_jxl_bytes(*result.lossless_source, compression.profile) :
            codec::jxl::encode_lossless_jxl_bytes(*result.lossless_source);
        const auto encoded = std::chrono::steady_clock::now();
        validate_capture_file_payload(format, compression, result.jxl_bytes, *result.lossless_source);
        if (compression.mode == FileCompressionMode::lossy)
        {
            result.lossy_encode_milliseconds = std::chrono::duration<double, std::milli>(encoded - start).count();
            result.lossy_validate_milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - encoded).count();
        }
        result.jxl_encode_milliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
    }
    if (plan.file && format == image::ImageFileFormat::jpeg)
    {
        const auto start = std::chrono::steady_clock::now();
        const auto resolved = codec::jpeg::resolve_jpeg_product_selection(jpeg_selection);
        const auto& request = resolved.request;
        const auto rgb = codec::jpeg::flatten_wgc_premultiplied_bgra8_for_jpeg(source, request.matte);
        auto encoded = codec::jpeg::encode_managed_srgb_jpeg({rgb.width, rgb.height,
            static_cast<std::size_t>(rgb.width) * 3, rgb.pixels}, request, resolved.selection.backend);
        encoded.effective.diagnostic = resolved.diagnostic + " " + encoded.effective.diagnostic;
        result.jpeg_bytes = std::move(encoded.bytes);
        result.jpeg_effective = std::move(encoded.effective);
        result.jpeg_encode_milliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        // Managed layer includes shared structure/privacy/technical/decode checks.
    }
    return result;
}

void validate_capture_file_payload(image::ImageFileFormat format, FileCompression compression,
    std::span<const std::byte> bytes, const image::CpuImage& source,
    const codec::jpeg::JpegEffectiveSettings* jpeg_settings)
{
    validate_file_compression(format, compression);
    switch (format)
    {
    case image::ImageFileFormat::png: (void)codec::png::validate_screenshot_png(bytes, source); break;
    case image::ImageFileFormat::jpeg:
        (void)codec::jpeg::validate_managed_screenshot_jpeg(bytes,
            jpeg_settings ? jpeg_settings->backend : codec::jpeg::JpegBackendId::turbo, source.width, source.height); break;
    case image::ImageFileFormat::webp: (void)codec::webp::validate_screenshot_webp(bytes, source); break;
    case image::ImageFileFormat::avif:
        if (compression.mode == FileCompressionMode::lossy) (void)codec::avif::validate_screenshot_avif_lossy(bytes, source);
        else (void)codec::avif::validate_screenshot_avif(bytes, source);
        break;
    case image::ImageFileFormat::jxl:
        if (compression.mode == FileCompressionMode::lossy) (void)codec::jxl::validate_screenshot_jxl_lossy(bytes, source);
        else (void)codec::jxl::validate_screenshot_jxl(bytes, source);
        break;
    default: throw std::invalid_argument("Unknown capture file format.");
    }
}

void write_capture_file_image(
    image::ImageFileFormat format, std::span<const std::byte> bytes,
    const std::filesystem::path& pending, const image::CpuImage& validation_source,
    const codec::jpeg::JpegEffectiveSettings* jpeg_settings)
{
    switch (format)
    {
    case image::ImageFileFormat::png:
        (void)codec::png::write_png_bytes(bytes, pending);
        (void)codec::png::validate_screenshot_png(pending, validation_source);
        return;
    case image::ImageFileFormat::jpeg:
    {
        const auto selected = jpeg_settings ? jpeg_settings->backend : codec::jpeg::JpegBackendId::turbo;
        (void)codec::jpeg::validate_managed_screenshot_jpeg(bytes, selected, validation_source.width, validation_source.height);
        if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
            throw std::overflow_error("JPEG file size exceeds stream range.");
        std::ofstream output{pending, std::ios::binary};
        if (!output) throw std::runtime_error("JPEG pending file open failed.");
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (!output) throw std::runtime_error("JPEG pending file write/close failed.");
        if (std::filesystem::file_size(pending) != bytes.size())
            throw std::runtime_error("JPEG pending file size mismatch.");
        std::vector<std::byte> persisted(bytes.size()); // Already bounded by validator.
        std::ifstream input{pending, std::ios::binary};
        input.read(reinterpret_cast<char*>(persisted.data()), static_cast<std::streamsize>(persisted.size()));
        if (!input || !std::equal(persisted.begin(), persisted.end(), bytes.begin(), bytes.end()))
            throw std::runtime_error("JPEG pending file read-back mismatch.");
        (void)codec::jpeg::validate_managed_screenshot_jpeg(persisted, selected, validation_source.width, validation_source.height);
        return;
    }
    case image::ImageFileFormat::webp:
    {
        (void)codec::webp::validate_screenshot_webp(bytes, validation_source);
        if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
            throw std::overflow_error("WebP file size exceeds stream range.");
        std::ofstream output{pending, std::ios::binary};
        if (!output) throw std::runtime_error("WebP pending file open failed.");
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (!output || std::filesystem::file_size(pending) != bytes.size())
            throw std::runtime_error("WebP pending file write/close failed.");
        std::vector<std::byte> persisted(bytes.size());
        std::ifstream input{pending, std::ios::binary};
        input.read(reinterpret_cast<char*>(persisted.data()), static_cast<std::streamsize>(persisted.size()));
        if (!input || persisted != std::vector<std::byte>{bytes.begin(), bytes.end()})
            throw std::runtime_error("WebP pending file read-back mismatch.");
        (void)codec::webp::validate_screenshot_webp(persisted, validation_source);
        return;
    }
    case image::ImageFileFormat::avif:
    {
        (void)codec::avif::validate_screenshot_avif(bytes, validation_source);
        if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
            throw std::overflow_error("AVIF file size exceeds stream range.");
        std::ofstream output{pending, std::ios::binary};
        if (!output) throw std::runtime_error("AVIF pending file open failed.");
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (!output || std::filesystem::file_size(pending) != bytes.size())
            throw std::runtime_error("AVIF pending file write/close failed.");
        std::vector<std::byte> persisted(bytes.size());
        std::ifstream input{pending, std::ios::binary};
        input.read(reinterpret_cast<char*>(persisted.data()), static_cast<std::streamsize>(persisted.size()));
        if (!input || persisted != std::vector<std::byte>{bytes.begin(), bytes.end()})
            throw std::runtime_error("AVIF pending file read-back mismatch.");
        (void)codec::avif::validate_screenshot_avif(persisted, validation_source);
        return;
    }
    case image::ImageFileFormat::jxl:
    {
        (void)codec::jxl::validate_screenshot_jxl(bytes, validation_source);
        std::ofstream output{pending, std::ios::binary};
        if (!output) throw std::runtime_error("JPEG XL pending file open failed.");
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (!output || std::filesystem::file_size(pending) != bytes.size())
            throw std::runtime_error("JPEG XL pending file write/close failed.");
        std::vector<std::byte> persisted(bytes.size()); // validator bounds output to 256 MiB
        std::ifstream input{pending, std::ios::binary};
        input.read(reinterpret_cast<char*>(persisted.data()), static_cast<std::streamsize>(persisted.size()));
        if (!input || !std::equal(persisted.begin(), persisted.end(), bytes.begin(), bytes.end()))
            throw std::runtime_error("JPEG XL pending file read-back mismatch.");
        (void)codec::jxl::validate_screenshot_jxl(persisted, validation_source);
        return;
    }
    default: throw std::invalid_argument("Invalid file format; pending file untouched.");
    }
}
}

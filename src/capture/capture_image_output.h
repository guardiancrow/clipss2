#pragma once

#include "capture/capture_destination.h"
#include "image/cpu_image.h"
#include "image/image_file_format.h"
#include "codec/jpeg/jpeg_product_selection.h"
#include "capture/file_compression.h"

#include <filesystem>
#include <optional>
#include <span>

namespace clipss2::capture
{
struct PreparedCaptureImageOutput
{
    // One canonical straight-alpha copy shared by File validation and Clipboard PNG.
    // Legacy member name is retained; lossy validators borrow this same owned source.
    std::optional<image::CpuImage> lossless_source;
    std::vector<std::byte> png_bytes;
    std::vector<std::byte> jpeg_bytes;
    std::vector<std::byte> webp_bytes;
    std::vector<std::byte> avif_bytes;
    std::vector<std::byte> jxl_bytes;
    double jpeg_encode_milliseconds{};
    double webp_encode_milliseconds{};
    double avif_encode_milliseconds{};
    double jxl_encode_milliseconds{};
    std::optional<codec::jpeg::JpegEffectiveSettings> jpeg_effective;
    double lossy_encode_milliseconds{}, lossy_validate_milliseconds{};
};

// Caller has passed the product SDR gate. Input is audited WGC premultiplied
// BGRA8; conversion never changes the captured image or DIB representation.
[[nodiscard]] PreparedCaptureImageOutput prepare_capture_image_output(
    const image::CpuImage& source, CaptureDestination destination,
    image::ImageFileFormat format,
    codec::jpeg::JpegProductSelection jpeg_selection = codec::jpeg::JpegCurrentDefault{},
    FileCompression compression = {});

// Shared immediate/persisted gate; caller retains the trusted canonical source throughout the call.
void validate_capture_file_payload(image::ImageFileFormat format, FileCompression compression,
    std::span<const std::byte> bytes, const image::CpuImage& canonical_source,
    const codec::jpeg::JpegEffectiveSettings* jpeg_settings = nullptr);

// PNG/WebP/AVIF/JXL validation_source is the straight lossless copy; JPEG uses dimensions.
// Writes/validates the caller's reserved pending path, never publishes it.
void write_capture_file_image(
    image::ImageFileFormat format, std::span<const std::byte> bytes,
    const std::filesystem::path& pending,
    const image::CpuImage& validation_source,
    const codec::jpeg::JpegEffectiveSettings* jpeg_settings = nullptr);
}

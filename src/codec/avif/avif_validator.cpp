#include "codec/avif/avif_validator.h"

#include "codec/avif/avif_codec.h"

#include <avif/avif.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace clipss2::codec::avif
{
namespace
{
using Decoder = std::unique_ptr<avifDecoder, decltype(&avifDecoderDestroy)>;

[[noreturn]] void reject(const std::string& reason)
{
    throw std::runtime_error("AVIF structure/privacy validation failed: " + reason);
}

void configure_decoder(avifDecoder& decoder)
{
    decoder.maxThreads = 1;
    decoder.allowProgressive = AVIF_FALSE;
    decoder.allowIncremental = AVIF_FALSE;
    decoder.imageCountLimit = 1;
    decoder.strictFlags = AVIF_STRICT_ENABLED;
}

void require_result(const char* stage, const avifResult result)
{
    if (result != AVIF_RESULT_OK) reject(std::string{stage} + ": " + avifResultToString(result));
}
}

AvifInspection inspect_avif_structure(const std::span<const std::byte> bytes)
{
    if (bytes.empty() || bytes.size() > maximum_avif_bytes) reject("encoded size outside safety limit");
    Decoder decoder{avifDecoderCreate(), &avifDecoderDestroy};
    if (!decoder) reject("decoder allocation failed");
    configure_decoder(*decoder);
    require_result("memory input", avifDecoderSetIOMemory(decoder.get(),
        reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
    require_result("parse", avifDecoderParse(decoder.get()));
    if (!decoder->image) reject("parse produced no image");
    if (decoder->imageCount != 1 || decoder->imageSequenceTrackPresent ||
        decoder->progressiveState == AVIF_PROGRESSIVE_STATE_ACTIVE)
        reject("single still image required");
    const auto& image = *decoder->image;
    if (image.depth != 8 || image.yuvFormat != AVIF_PIXEL_FORMAT_YUV444 || image.yuvRange != AVIF_RANGE_FULL)
        reject("expected 8-bit YUV444 full-range image");
    if (image.colorPrimaries != AVIF_COLOR_PRIMARIES_BT709 ||
        image.transferCharacteristics != AVIF_TRANSFER_CHARACTERISTICS_SRGB ||
        image.matrixCoefficients != AVIF_MATRIX_COEFFICIENTS_IDENTITY)
        reject("expected CICP 1/13/0");
    if (image.icc.size != 0 || image.exif.size != 0 || image.xmp.size != 0 ||
        image.transformFlags != AVIF_TRANSFORM_NONE || image.numProperties != 0 || image.gainMap != nullptr)
        reject("metadata, transforms, custom properties, or gain map are forbidden");
    require_result("full decode", avifDecoderNextImage(decoder.get()));
    return {image.width, image.height, image.depth, bytes.size(),
        static_cast<std::uint32_t>(decoder->imageCount), decoder->alphaPresent == AVIF_TRUE};
}

AvifInspection validate_screenshot_avif(
    const std::span<const std::byte> bytes, const image::CpuImage& source)
{
    const auto result = inspect_avif_structure(bytes);
    if (source.format != image::PixelFormat::bgra8_unorm || result.width != source.width || result.height != source.height)
        reject("dimensions or source pixel format do not match");
    bool source_has_alpha{};
    for (std::size_t offset = 3; offset < source.pixels.size(); offset += 4)
        if (source.pixels[offset] != std::byte{0xff}) { source_has_alpha = true; break; }
    if (result.has_alpha != source_has_alpha) reject("alpha presence does not match source");
    const auto decoded = decode_bgra8_avif(bytes);
    if (decoded.width != source.width || decoded.height != source.height ||
        decoded.format != source.format || decoded.pixels != source.pixels)
        reject("lossless pixel round-trip mismatch");
    return result;
}
}

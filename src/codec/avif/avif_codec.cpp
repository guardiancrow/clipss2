#include "codec/avif/avif_codec.h"
#include "codec/lossy_validation.h"

#include <avif/avif.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace clipss2::codec::avif
{
namespace
{
constexpr std::size_t bytes_per_pixel = 4;

std::size_t checked_image_bytes(const std::uint32_t width, const std::uint32_t height)
{
    if (width == 0 || height == 0)
        throw std::invalid_argument("AVIF dimensions must be non-zero.");
    const auto w = static_cast<std::size_t>(width);
    const auto h = static_cast<std::size_t>(height);
    if (w > std::numeric_limits<std::size_t>::max() / h ||
        w * h > std::numeric_limits<std::size_t>::max() / bytes_per_pixel)
        throw std::overflow_error("AVIF image dimensions overflow the address space.");
    return w * h * bytes_per_pixel;
}

[[noreturn]] void fail(const char* stage, const avifResult result)
{
    throw std::runtime_error(std::string{"AVIF "} + stage + " failed: " + avifResultToString(result));
}

using Image = std::unique_ptr<avifImage, decltype(&avifImageDestroy)>;
using Encoder = std::unique_ptr<avifEncoder, decltype(&avifEncoderDestroy)>;
using Decoder = std::unique_ptr<avifDecoder, decltype(&avifDecoderDestroy)>;

void configure_lossless_sdr_image(avifImage& image)
{
    image.yuvRange = AVIF_RANGE_FULL;
    image.colorPrimaries = AVIF_COLOR_PRIMARIES_BT709;
    image.transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SRGB;
    image.matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;
    image.alphaPremultiplied = AVIF_FALSE;
}

bool needs_alpha_plane(const image::CpuImage& source)
{
    for (std::size_t offset = 3; offset < source.pixels.size(); offset += bytes_per_pixel)
        if (source.pixels[offset] != std::byte{0xff}) return true;
    return false;
}

void configure_decoder(avifDecoder& decoder)
{
    decoder.maxThreads = 1;
    decoder.allowProgressive = AVIF_FALSE;
    decoder.allowIncremental = AVIF_FALSE;
    decoder.imageCountLimit = 1;
    decoder.strictFlags = AVIF_STRICT_ENABLED;
}

image::CpuImage decode_from_image(const avifImage& source)
{
    const auto size = checked_image_bytes(source.width, source.height);
    image::CpuImage result{source.width, source.height, image::PixelFormat::bgra8_unorm,
        std::vector<std::byte>(size)};
    avifRGBImage rgb{};
    avifRGBImageSetDefaults(&rgb, &source);
    rgb.format = AVIF_RGB_FORMAT_BGRA;
    rgb.depth = 8;
    rgb.alphaPremultiplied = AVIF_FALSE;
    rgb.pixels = reinterpret_cast<std::uint8_t*>(result.pixels.data());
    rgb.rowBytes = static_cast<std::uint32_t>(static_cast<std::size_t>(source.width) * bytes_per_pixel);
    const auto converted = avifImageYUVToRGB(&source, &rgb);
    if (converted != AVIF_RESULT_OK) fail("YUV-to-BGRA conversion", converted);
    return result;
}
}

namespace
{
std::vector<std::byte> encode_avif(const image::CpuImage& source,
    std::optional<LossyProfile> lossy, AvifLossyConfig* applied)
{
    if (lossy) (void)codec::detail::check_lossy_source(source);
    if (source.format != image::PixelFormat::bgra8_unorm)
        throw std::invalid_argument("AVIF encoder requires straight BGRA8 UNORM input.");
    if (source.pixels.size() != checked_image_bytes(source.width, source.height))
        throw std::invalid_argument("AVIF source buffer does not match its dimensions.");

    Image image{avifImageCreate(source.width, source.height, 8, AVIF_PIXEL_FORMAT_YUV444), &avifImageDestroy};
    if (!image) throw std::runtime_error("AVIF image allocation failed.");
    configure_lossless_sdr_image(*image);
    if (lossy) image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT709;
    const auto planes = static_cast<avifPlanesFlags>(AVIF_PLANES_YUV |
        (needs_alpha_plane(source) ? AVIF_PLANES_A : 0));
    const auto allocated = avifImageAllocatePlanes(image.get(), planes);
    if (allocated != AVIF_RESULT_OK) fail("plane allocation", allocated);

    avifRGBImage rgb{};
    avifRGBImageSetDefaults(&rgb, image.get());
    rgb.format = AVIF_RGB_FORMAT_BGRA;
    rgb.depth = 8;
    rgb.alphaPremultiplied = AVIF_FALSE;
    rgb.pixels = const_cast<std::uint8_t*>(reinterpret_cast<const std::uint8_t*>(source.pixels.data()));
    rgb.rowBytes = static_cast<std::uint32_t>(static_cast<std::size_t>(source.width) * bytes_per_pixel);
    const auto converted = avifImageRGBToYUV(image.get(), &rgb);
    if (converted != AVIF_RESULT_OK) fail("BGRA-to-YUV conversion", converted);

    Encoder encoder{avifEncoderCreate(), &avifEncoderDestroy};
    if (!encoder) throw std::runtime_error("AVIF encoder allocation failed.");
    const auto policy = product_avif_policy();
    encoder->maxThreads = lossy ? 8 : 1;
    encoder->speed = policy.speed;
    encoder->quality = AVIF_QUALITY_LOSSLESS;
    if (lossy)
    {
        switch (*lossy)
        {
        case LossyProfile::high: encoder->quality = 80; break;
        case LossyProfile::balanced: encoder->quality = 65; break;
        case LossyProfile::small_file: encoder->quality = 50; break;
        default: throw std::invalid_argument("Invalid AVIF lossy profile.");
        }
        encoder->codecChoice = AVIF_CODEC_CHOICE_AOM;
    }
    encoder->qualityAlpha = AVIF_QUALITY_LOSSLESS;
    encoder->extraLayerCount = 0;
    encoder->autoTiling = AVIF_FALSE;
    if (applied) *applied = {encoder->quality, encoder->qualityAlpha, encoder->speed, encoder->maxThreads,
        static_cast<int>(image->depth), static_cast<int>(image->matrixCoefficients)};
    avifRWData encoded = AVIF_DATA_EMPTY;
    const auto written = avifEncoderWrite(encoder.get(), image.get(), &encoded);
    std::unique_ptr<avifRWData, void(*)(avifRWData*)> output{&encoded, &avifRWDataFree};
    if (written != AVIF_RESULT_OK) fail(lossy ? "lossy encode" : "lossless encode", written);
    if (encoded.size == 0 || encoded.size > maximum_avif_bytes)
        throw std::runtime_error("AVIF encoder produced an invalid output size.");
    std::vector<std::byte> bytes(encoded.size);
    std::copy_n(reinterpret_cast<const std::byte*>(encoded.data), encoded.size, bytes.begin());
    return bytes;
}
}

std::vector<std::byte> encode_lossless_avif_bytes(const image::CpuImage& source)
{ return encode_avif(source, {}, nullptr); }
std::vector<std::byte> encode_lossy_avif_bytes(const image::CpuImage& source, LossyProfile profile, AvifLossyConfig* applied)
{ return encode_avif(source, profile, applied); }

image::CpuImage decode_bgra8_avif(const std::span<const std::byte> bytes)
{
    if (bytes.empty() || bytes.size() > maximum_avif_bytes)
        throw std::invalid_argument("AVIF memory input is outside the safety limit.");
    Decoder decoder{avifDecoderCreate(), &avifDecoderDestroy};
    if (!decoder) throw std::runtime_error("AVIF decoder allocation failed.");
    configure_decoder(*decoder);
    auto result = avifDecoderSetIOMemory(decoder.get(), reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    if (result != AVIF_RESULT_OK) fail("memory input", result);
    result = avifDecoderParse(decoder.get());
    if (result != AVIF_RESULT_OK) fail("parse", result);
    result = avifDecoderNextImage(decoder.get());
    if (result != AVIF_RESULT_OK) fail("decode", result);
    if (!decoder->image) throw std::runtime_error("AVIF decoder produced no image.");
    return decode_from_image(*decoder->image);
}
}

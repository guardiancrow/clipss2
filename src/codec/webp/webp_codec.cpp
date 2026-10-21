#include "codec/webp/webp_codec.h"

#include <webp/decode.h>
#include <webp/encode.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace clipss2::codec::webp
{
namespace
{
constexpr std::size_t bytes_per_pixel = 4;

std::size_t checked_image_bytes(const std::uint32_t width, const std::uint32_t height)
{
    if (width == 0 || height == 0 || width > WEBP_MAX_DIMENSION || height > WEBP_MAX_DIMENSION)
        throw std::invalid_argument("WebP dimensions are outside the supported range.");
    const auto w = static_cast<std::size_t>(width);
    const auto h = static_cast<std::size_t>(height);
    if (w > std::numeric_limits<std::size_t>::max() / h ||
        w * h > std::numeric_limits<std::size_t>::max() / bytes_per_pixel)
        throw std::overflow_error("WebP image dimensions overflow the address space.");
    return w * h * bytes_per_pixel;
}

class Picture final
{
public:
    Picture()
    {
        if (!WebPPictureInit(&picture_))
            throw std::runtime_error("WebPPictureInit rejected the linked libwebp ABI.");
    }
    ~Picture() { WebPPictureFree(&picture_); }
    Picture(const Picture&) = delete;
    Picture& operator=(const Picture&) = delete;
    [[nodiscard]] WebPPicture* get() noexcept { return &picture_; }
private:
    WebPPicture picture_{};
};

struct VectorWriter
{
    std::vector<std::byte> bytes;
    bool failed{};
};

int write_vector(const std::uint8_t* data, const std::size_t size, const WebPPicture* picture) noexcept
{
    auto* output = static_cast<VectorWriter*>(picture->custom_ptr);
    if (!output || output->failed || size > maximum_webp_bytes - std::min(output->bytes.size(), maximum_webp_bytes))
    {
        if (output) output->failed = true;
        return 0;
    }
    try
    {
        const auto old_size = output->bytes.size();
        output->bytes.resize(old_size + size);
        std::copy_n(reinterpret_cast<const std::byte*>(data), size, output->bytes.begin() + old_size);
        return 1;
    }
    catch (...)
    {
        output->failed = true;
        return 0;
    }
}

const char* encoding_error(const WebPEncodingError error) noexcept
{
    switch (error)
    {
    case VP8_ENC_ERROR_OUT_OF_MEMORY: return "out of memory";
    case VP8_ENC_ERROR_BITSTREAM_OUT_OF_MEMORY: return "bitstream limit";
    case VP8_ENC_ERROR_NULL_PARAMETER: return "null parameter";
    case VP8_ENC_ERROR_INVALID_CONFIGURATION: return "invalid configuration";
    case VP8_ENC_ERROR_BAD_DIMENSION: return "bad dimensions";
    case VP8_ENC_ERROR_PARTITION0_OVERFLOW: return "partition overflow";
    case VP8_ENC_ERROR_PARTITION_OVERFLOW: return "partition overflow";
    case VP8_ENC_ERROR_BAD_WRITE: return "writer failure";
    case VP8_ENC_ERROR_FILE_TOO_BIG: return "file too large";
    case VP8_ENC_ERROR_USER_ABORT: return "aborted";
    default: return "unknown error";
    }
}
}

std::vector<std::byte> encode_lossless_webp_bytes(const image::CpuImage& source)
{
    if (source.format != image::PixelFormat::bgra8_unorm)
        throw std::invalid_argument("WebP encoder requires straight BGRA8 UNORM input.");
    const auto expected = checked_image_bytes(source.width, source.height);
    if (source.pixels.size() != expected)
        throw std::invalid_argument("WebP source buffer does not match its dimensions.");

    WebPConfig config{};
    if (!WebPConfigInit(&config))
        throw std::runtime_error("WebPConfigInit rejected the linked libwebp ABI.");
    const auto policy = product_webp_policy();
    config.lossless = policy.lossless;
    config.exact = policy.exact;
    config.method = policy.method;
    config.thread_level = policy.thread_level;
    config.image_hint = static_cast<WebPImageHint>(policy.image_hint);
    config.quality = policy.quality;
    if (!WebPValidateConfig(&config))
        throw std::runtime_error("Product WebP configuration is invalid.");

    Picture picture;
    picture.get()->width = static_cast<int>(source.width);
    picture.get()->height = static_cast<int>(source.height);
    picture.get()->use_argb = 1;
    if (!WebPPictureImportBGRA(picture.get(),
            reinterpret_cast<const std::uint8_t*>(source.pixels.data()),
            static_cast<int>(static_cast<std::size_t>(source.width) * bytes_per_pixel)))
        throw std::runtime_error("WebPPictureImportBGRA failed.");

    VectorWriter output;
    picture.get()->writer = write_vector;
    picture.get()->custom_ptr = &output;
    if (!WebPEncode(&config, picture.get()))
        throw std::runtime_error(std::string{"WebP lossless encoding failed: "} +
            encoding_error(picture.get()->error_code));
    if (output.failed || output.bytes.empty())
        throw std::runtime_error("WebP lossless writer produced no complete payload.");
    return output.bytes;
}

image::CpuImage decode_bgra8_webp(const std::span<const std::byte> bytes)
{
    if (bytes.empty()) throw std::invalid_argument("WebP memory input is empty.");
    int width{};
    int height{};
    if (!WebPGetInfo(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), &width, &height) ||
        width <= 0 || height <= 0)
        throw std::runtime_error("WebP decode header validation failed.");
    const auto size = checked_image_bytes(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
    using Buffer = std::unique_ptr<std::uint8_t, decltype(&WebPFree)>;
    Buffer decoded{WebPDecodeBGRA(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(),
        &width, &height), &WebPFree};
    if (!decoded) throw std::runtime_error("WebP full decode failed.");
    image::CpuImage result{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
        image::PixelFormat::bgra8_unorm, std::vector<std::byte>(size)};
    std::copy_n(reinterpret_cast<const std::byte*>(decoded.get()), size, result.pixels.begin());
    return result;
}
}

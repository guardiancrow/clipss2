#include "codec/jpeg/jpeg_codec.h"
#include "codec/jpeg/jpeg_validator.h"
#include "codec/jpeg/jpeg_codec_internal.h"

#include <turbojpeg.h>
#include <limits>
#include <stdexcept>
#include <string>

namespace clipss2::codec::jpeg
{
namespace
{
class TurboJpegHandle final
{
public:
    explicit TurboJpegHandle(int operation) : handle_{tj3Init(operation)}
    {
        if (!handle_) throw std::runtime_error("JPEG backend initialization failed.");
    }
    ~TurboJpegHandle() { tj3Destroy(handle_); }
    TurboJpegHandle(const TurboJpegHandle&) = delete;
    TurboJpegHandle& operator=(const TurboJpegHandle&) = delete;
    tjhandle get() const noexcept { return handle_; }
    void set(int parameter, int value, const char* stage)
    {
        if (tj3Set(handle_, parameter, value) != 0) fail(stage);
    }
    [[noreturn]] void fail(const char* stage) const
    {
        // Memory-only operations: library messages have no supplied filename.
        throw std::runtime_error(std::string{stage} + ": " + tj3GetErrorStr(handle_));
    }
private:
    tjhandle handle_{};
};

int checked_pitch(std::uint32_t width)
{
    if (width > static_cast<std::uint32_t>(std::numeric_limits<int>::max() / 3))
        throw std::overflow_error("JPEG RGB row pitch overflowed.");
    return static_cast<int>(width * 3);
}
}

std::vector<std::byte> detail::encode_turbo_jpeg_bytes(CanonicalJpegRgb8View source, int quality)
{
    validate_canonical_jpeg_rgb8(source);
    if (quality < 1 || quality > 100)
        throw std::invalid_argument("JPEG quality must be in [1,100].");
    if (source.stride > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::overflow_error("JPEG RGB row pitch overflowed.");
    const auto pitch = static_cast<int>(source.stride);
    const auto width = static_cast<int>(source.width);
    const auto height = static_cast<int>(source.height);
    TurboJpegHandle compressor{TJINIT_COMPRESS};
    constexpr const char* stage = "JPEG encode failed";
    compressor.set(TJPARAM_STOPONWARNING, 1, stage);
    compressor.set(TJPARAM_QUALITY, quality, stage);
    compressor.set(TJPARAM_SUBSAMP, TJSAMP_444, stage);
    compressor.set(TJPARAM_COLORSPACE, TJCS_YCbCr, stage);
    compressor.set(TJPARAM_PROGRESSIVE, 0, stage);
    compressor.set(TJPARAM_OPTIMIZE, 0, stage);
    compressor.set(TJPARAM_ARITHMETIC, 0, stage);
    compressor.set(TJPARAM_LOSSLESS, 0, stage);
    compressor.set(TJPARAM_BOTTOMUP, 0, stage);
    compressor.set(TJPARAM_FASTDCT, 0, stage);
    compressor.set(TJPARAM_NOREALLOC, 1, stage);
    compressor.set(TJPARAM_DENSITYUNITS, 0, stage);
    compressor.set(TJPARAM_XDENSITY, 1, stage);
    compressor.set(TJPARAM_YDENSITY, 1, stage);
    compressor.set(TJPARAM_RESTARTBLOCKS, 0, stage);
    compressor.set(TJPARAM_RESTARTROWS, 0, stage);
    const auto capacity = tj3JPEGBufSize(width, height, TJSAMP_444);
    if (capacity == 0 || capacity > maximum_jpeg_bytes)
        throw std::runtime_error("JPEG encode failed: output capacity exceeds safety limit.");
    std::vector<std::byte> bytes(capacity);
    auto* destination = reinterpret_cast<unsigned char*>(bytes.data());
    std::size_t written = capacity;
    if (tj3Compress8(compressor.get(),
            reinterpret_cast<const unsigned char*>(source.pixels.data()),
            width, pitch, height, TJPF_RGB, &destination, &written) != 0)
        compressor.fail(stage);
    if (destination != reinterpret_cast<unsigned char*>(bytes.data()) ||
        written == 0 || written > capacity)
        throw std::runtime_error("JPEG encode failed: invalid output buffer/size.");
    bytes.resize(written);
    return bytes;
}

std::vector<std::byte> encode_srgb_jpeg_bytes(const OpaqueRgb8Image& source, const JpegOptions& options)
{
    if (source.pixels.size() != checked_rgb_image_bytes(source.width, source.height))
        throw std::invalid_argument("JPEG RGB input buffer does not match its dimensions.");
    if (options.subsampling != ChromaSubsampling::yuv444 || options.alpha_flatten != AlphaFlattenPolicy::opaque_srgb_white)
        throw std::invalid_argument("Unsupported JPEG baseline policy.");
    auto bytes = detail::encode_turbo_jpeg_bytes({source.width, source.height,
        static_cast<std::size_t>(checked_pitch(source.width)), source.pixels}, options.quality);
    (void)validate_screenshot_jpeg(bytes, source.width, source.height);
    return bytes; // Compatibility API for existing controlled audit/options callers.
}

OpaqueRgb8Image decode_srgb_jpeg_bytes(std::span<const std::byte> bytes)
{
    const auto inspection = inspect_jpeg_structure(bytes);
    validate_jpeg_common_privacy(inspection);
    validate_jpeg_backend_technical(inspection, JpegBackendId::turbo);
    return detail::decode_validated_jpeg_bytes(inspection);
}
OpaqueRgb8Image detail::decode_validated_jpeg_bytes(const JpegInspection& inspection)
{
    const auto bytes = inspection.bytes();
    TurboJpegHandle decompressor{TJINIT_DECOMPRESS};
    constexpr const char* stage = "JPEG decode validation failed";
    decompressor.set(TJPARAM_STOPONWARNING, 1, stage);
    decompressor.set(TJPARAM_BOTTOMUP, 0, stage);
    decompressor.set(TJPARAM_FASTDCT, 0, stage);
    if (tj3DecompressHeader(decompressor.get(),
            reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size()) != 0)
        decompressor.fail(stage);
    if (tj3Get(decompressor.get(), TJPARAM_JPEGWIDTH) != static_cast<int>(inspection.width()) ||
        tj3Get(decompressor.get(), TJPARAM_JPEGHEIGHT) != static_cast<int>(inspection.height()) ||
        tj3Get(decompressor.get(), TJPARAM_PRECISION) != 8 ||
        tj3Get(decompressor.get(), TJPARAM_COLORSPACE) != TJCS_YCbCr ||
        tj3Get(decompressor.get(), TJPARAM_SUBSAMP) != TJSAMP_444 ||
        tj3Get(decompressor.get(), TJPARAM_PROGRESSIVE) != 0)
        throw std::runtime_error("JPEG decode validation failed: unexpected header properties.");
    OpaqueRgb8Image result{inspection.width(), inspection.height(),
        std::vector<std::byte>(checked_rgb_image_bytes(inspection.width(), inspection.height()))};
    if (tj3Decompress8(decompressor.get(),
            reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(),
            reinterpret_cast<unsigned char*>(result.pixels.data()),
            checked_pitch(result.width), TJPF_RGB) != 0)
        decompressor.fail(stage);
    return result;
}

const char* jpeg_backend_version() noexcept
{
    return CLIPSS2_JPEG_BACKEND_VERSION;
}
}

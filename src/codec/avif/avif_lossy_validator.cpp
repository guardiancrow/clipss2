#include "codec/avif/avif_lossy_validator.h"
#include "codec/avif/avif_codec.h"
#include "codec/lossy_validation.h"
#include <avif/avif.h>
#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace clipss2::codec::avif
{
namespace
{
[[noreturn]] void reject(const char* reason)
{ throw std::runtime_error(std::string{"AVIF lossy validation failed: "} + reason); }
void expect(bool condition, const char* reason) { if (!condition) reject(reason); }

// Fixed-depth, fixed-count grammar. Every box and every payload byte is consumed.
// No recursive unknown-box skipping, large-size boxes, implicit EOF sizes, or allocations.
class Reader
{
public:
    Reader(std::span<const std::byte> bytes, std::size_t absolute, unsigned& boxes)
        : bytes_(bytes), absolute_(absolute), boxes_(boxes) {}
    std::uint8_t u8()
    {
        expect(position_ < bytes_.size(), "truncated payload");
        return std::to_integer<std::uint8_t>(bytes_[position_++]);
    }
    std::uint16_t u16() { const auto high = u8(); return static_cast<std::uint16_t>((high << 8) | u8()); }
    std::uint32_t u32() { const auto high = u16(); return (static_cast<std::uint32_t>(high) << 16) | u16(); }
    void value(std::uint32_t actual, std::uint32_t expected)
    { expect(actual == expected, "unexpected field value"); }
    void text(std::string_view expected)
    { for (const auto c : expected) value(u8(), static_cast<unsigned char>(c)); }
    void full(std::uint8_t version = 0) { value(u8(), version); value(u8(), 0); value(u16(), 0); }
    void end() const { expect(position_ == bytes_.size(), "unconsumed/extra payload"); }
    std::size_t start() const { return absolute_; }
    std::size_t size() const { return bytes_.size(); }
    Reader box(std::string_view type)
    {
        expect(++boxes_ <= 32, "box count limit");
        const auto size = u32(); text(type);
        expect(size >= 8 && size - 8 <= bytes_.size() - position_, "invalid/extended/overflowing box size");
        Reader child{bytes_.subspan(position_, size - 8), absolute_ + position_, boxes_};
        position_ += size - 8;
        return child;
    }
private:
    std::span<const std::byte> bytes_;
    std::size_t absolute_{}, position_{};
    unsigned& boxes_;
};
struct Extent { std::size_t offset{}, length{}; };

void av1_config(Reader property, bool alpha)
{
    property.value(property.u8(), 0x81); // marker + version 1
    const auto profile_level = property.u8();
    expect((profile_level >> 5) == (alpha ? 0 : 1) && (profile_level & 31) <= 23, "AV1 profile/level");
    property.value(property.u8(), alpha ? 0x1c : 0); // 8-bit, tier0, monochrome420 or color444
    property.value(property.u8(), 0); // no presentation delay/config OBUs in this output subset
    property.end();
}

void approved_bmff(std::span<const std::byte> bytes, const image::CpuImage& source, bool alpha)
{
    unsigned boxes{};
    Reader file{bytes, 0, boxes};
    auto ftyp = file.box("ftyp");
    ftyp.text("avif"); ftyp.value(ftyp.u32(), 0); ftyp.text("avifmif1miafMA1A"); ftyp.end();
    auto meta = file.box("meta"); meta.full();
    auto hdlr = meta.box("hdlr"); hdlr.full(); hdlr.value(hdlr.u32(), 0); hdlr.text("pict");
    for (unsigned i = 0; i < 3; ++i) hdlr.value(hdlr.u32(), 0);
    hdlr.value(hdlr.u8(), 0); hdlr.end();
    auto pitm = meta.box("pitm"); pitm.full(); pitm.value(pitm.u16(), 1); pitm.end();
    const unsigned count = alpha ? 2U : 1U;
    std::array<Extent, 2> extents{};
    auto iloc = meta.box("iloc"); iloc.full(); iloc.value(iloc.u8(), 0x44); iloc.value(iloc.u8(), 0);
    iloc.value(iloc.u16(), count);
    for (unsigned i = 0; i < count; ++i)
    {
        iloc.value(iloc.u16(), i + 1); iloc.value(iloc.u16(), 0); iloc.value(iloc.u16(), 1);
        extents[i].offset = iloc.u32(); extents[i].length = iloc.u32();
    }
    iloc.end();
    auto iinf = meta.box("iinf"); iinf.full(); iinf.value(iinf.u16(), count);
    for (unsigned i = 0; i < count; ++i)
    {
        auto infe = iinf.box("infe"); infe.full(2); infe.value(infe.u16(), i + 1); infe.value(infe.u16(), 0);
        infe.text("av01"); infe.text(i == 0 ? "Color" : "Alpha"); infe.value(infe.u8(), 0); infe.end();
    }
    iinf.end();
    if (alpha)
    {
        auto iref = meta.box("iref"); iref.full(); auto auxl = iref.box("auxl");
        auxl.value(auxl.u16(), 2); auxl.value(auxl.u16(), 1); auxl.value(auxl.u16(), 1); auxl.end(); iref.end();
    }
    auto iprp = meta.box("iprp"); auto ipco = iprp.box("ipco");
    auto ispe = ipco.box("ispe"); ispe.full(); ispe.value(ispe.u32(), source.width); ispe.value(ispe.u32(), source.height); ispe.end();
    auto pixi = ipco.box("pixi"); pixi.full(); pixi.value(pixi.u8(), 3);
    for (unsigned i = 0; i < 3; ++i) pixi.value(pixi.u8(), 8);
    pixi.end(); av1_config(ipco.box("av1C"), false);
    auto colr = ipco.box("colr"); colr.text("nclx"); colr.value(colr.u16(), 1); colr.value(colr.u16(), 13);
    colr.value(colr.u16(), 1); colr.value(colr.u8(), 0x80); colr.end();
    if (alpha)
    {
        auto apixi = ipco.box("pixi"); apixi.full(); apixi.value(apixi.u8(), 1); apixi.value(apixi.u8(), 8); apixi.end();
        av1_config(ipco.box("av1C"), true);
        auto auxc = ipco.box("auxC"); auxc.full(); auxc.text("urn:mpeg:mpegB:cicp:systems:auxiliary:alpha");
        auxc.value(auxc.u8(), 0); auxc.end();
    }
    ipco.end();
    auto ipma = iprp.box("ipma"); ipma.full(); ipma.value(ipma.u32(), count);
    for (unsigned i = 0; i < count; ++i)
    {
        ipma.value(ipma.u16(), i + 1); ipma.value(ipma.u8(), 4);
        for (const auto association : (i == 0 ? std::array<unsigned, 4>{1, 2, 0x83, 4} :
            std::array<unsigned, 4>{1, 5, 0x86, 7})) ipma.value(ipma.u8(), association);
    }
    ipma.end(); iprp.end(); meta.end();
    const auto mdat = file.box("mdat"); file.end();
    if (count == 2 && extents[0].offset > extents[1].offset) std::swap(extents[0], extents[1]);
    auto cursor = mdat.start(); const auto end = cursor + mdat.size();
    for (unsigned i = 0; i < count; ++i)
    {
        expect(extents[i].offset == cursor && extents[i].length > 0 && extents[i].length <= end - cursor,
            "extent outside mdat, overlapping, gapped, or overflowing");
        cursor += extents[i].length;
    }
    expect(cursor == end, "unreferenced mdat payload");
}

void ok(avifResult result)
{ if (result != AVIF_RESULT_OK) reject(avifResultToString(result)); }
}

AvifInspection validate_screenshot_avif_lossy(std::span<const std::byte> bytes, const image::CpuImage& source)
{
    const auto alpha = codec::detail::check_lossy_source(source);
    expect(!bytes.empty() && bytes.size() <= maximum_avif_bytes, "encoded size limit");
    approved_bmff(bytes, source, alpha); // Privacy structure must pass before a codec can ignore unknown boxes.
    const std::unique_ptr<avifDecoder, decltype(&avifDecoderDestroy)> decoder{avifDecoderCreate(), &avifDecoderDestroy};
    expect(!!decoder, "decoder allocation");
    decoder->maxThreads = 8; decoder->allowProgressive = AVIF_FALSE; decoder->allowIncremental = AVIF_FALSE;
    decoder->imageCountLimit = 1; decoder->strictFlags = AVIF_STRICT_ENABLED;
    decoder->imageSizeLimit = static_cast<std::uint32_t>(codec::detail::maximum_lossy_decoded_bytes / 4);
    decoder->imageDimensionLimit = codec::detail::maximum_lossy_dimension;
    ok(avifDecoderSetIOMemory(decoder.get(), reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
    ok(avifDecoderParse(decoder.get()));
    expect(decoder->image && decoder->imageCount == 1 && !decoder->imageSequenceTrackPresent &&
        decoder->progressiveState != AVIF_PROGRESSIVE_STATE_ACTIVE, "single still required");
    const auto semantic_policy = [&] {
        const auto& decoded = *decoder->image;
        expect(decoded.width == source.width && decoded.height == source.height && decoded.depth == 8 &&
            decoded.yuvFormat == AVIF_PIXEL_FORMAT_YUV444 && decoded.yuvRange == AVIF_RANGE_FULL &&
            decoded.colorPrimaries == AVIF_COLOR_PRIMARIES_BT709 && decoded.transferCharacteristics == AVIF_TRANSFER_CHARACTERISTICS_SRGB &&
            decoded.matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_BT709 && !decoded.alphaPremultiplied &&
            decoded.icc.size == 0 && decoded.exif.size == 0 && decoded.xmp.size == 0 && decoded.transformFlags == AVIF_TRANSFORM_NONE &&
            decoded.numProperties == 0 && !decoded.gainMap && (decoder->alphaPresent == AVIF_TRUE) == alpha,
            "SDR/color/alpha/privacy semantic policy");
    };
    semantic_policy();
    ok(avifDecoderNextImage(decoder.get()));
    semantic_policy(); // Do not trust container fields alone when AV1 decoding supplies image semantics.
    const auto& decoded = *decoder->image;
    image::CpuImage pixels{source.width, source.height, source.format, std::vector<std::byte>(source.pixels.size())};
    avifRGBImage rgb{}; avifRGBImageSetDefaults(&rgb, &decoded);
    rgb.format = AVIF_RGB_FORMAT_BGRA; rgb.depth = 8; rgb.alphaPremultiplied = AVIF_FALSE;
    rgb.pixels = reinterpret_cast<std::uint8_t*>(pixels.pixels.data()); rgb.rowBytes = source.width * 4;
    ok(avifImageYUVToRGB(&decoded, &rgb));
    codec::detail::check_lossy_alpha(pixels, source);
    return {decoded.width, decoded.height, decoded.depth, bytes.size(), 1, alpha};
}
}

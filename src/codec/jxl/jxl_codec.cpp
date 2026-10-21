#include "codec/jxl/jxl_codec.h"
#include "codec/jxl/jxl_internal.h"
#include "codec/lossy_validation.h"

#include <jxl/decode.h>
#include <jxl/encode.h>
#include <jxl/thread_parallel_runner.h>
#include <algorithm>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace clipss2::codec::jxl
{
namespace
{
using Encoder = std::unique_ptr<JxlEncoder, decltype(&JxlEncoderDestroy)>;
using Decoder = std::unique_ptr<JxlDecoder, decltype(&JxlDecoderDestroy)>;
using Runner = std::unique_ptr<void, decltype(&JxlThreadParallelRunnerDestroy)>;
constexpr JxlPixelFormat rgba8{4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};

[[noreturn]] void reject(const char* reason)
{ throw std::runtime_error(std::string{"JPEG XL validation/API failed: "} + reason); }
void enc_ok(JxlEncoderStatus status, const char* stage)
{ if (status != JXL_ENC_SUCCESS) reject(stage); }
void dec_ok(JxlDecoderStatus status, const char* stage)
{ if (status != JXL_DEC_SUCCESS) reject(stage); }

std::size_t image_bytes(std::uint32_t width, std::uint32_t height)
{
    if (!width || !height || static_cast<std::size_t>(width) > maximum_jxl_bytes / 4 / height)
        throw std::invalid_argument("JPEG XL dimensions exceed the decoded image safety limit.");
    return static_cast<std::size_t>(width) * height * 4;
}
Runner make_runner()
{
    // Use libjxl's CPU-derived default, capped to avoid worker oversubscription.
    const auto threads = std::clamp<std::size_t>(JxlThreadParallelRunnerDefaultNumWorkerThreads(), 1, 8);
    Runner runner{JxlThreadParallelRunnerCreate(nullptr, threads), &JxlThreadParallelRunnerDestroy};
    if (!runner) reject("thread runner allocation");
    return runner;
}
bool srgb(const JxlColorEncoding& color)
{
    return color.color_space == JXL_COLOR_SPACE_RGB && color.white_point == JXL_WHITE_POINT_D65 &&
        color.primaries == JXL_PRIMARIES_SRGB && color.transfer_function == JXL_TRANSFER_FUNCTION_SRGB;
}
void swap_red_blue(std::vector<std::byte>& pixels)
{
    for (std::size_t i = 0; i < pixels.size(); i += 4) std::swap(pixels[i], pixels[i + 2]);
}
}

namespace
{
std::vector<std::byte> encode_jxl(const image::CpuImage& source,
    std::optional<LossyProfile> lossy, JxlLossyConfig* applied)
{
    float distance{};
    if (lossy)
    {
        (void)codec::detail::check_lossy_source(source);
        switch (*lossy)
        {
        case LossyProfile::high: distance = .5F; break;
        case LossyProfile::balanced: distance = 1.F; break;
        case LossyProfile::small_file: distance = 2.F; break;
        default: throw std::invalid_argument("Invalid JXL lossy profile.");
        }
    }
    if (source.format != image::PixelFormat::bgra8_unorm ||
        source.pixels.size() != image_bytes(source.width, source.height))
        throw std::invalid_argument("JPEG XL requires a dimension-matched canonical BGRA8 buffer.");
    for (std::size_t i = 0; i < source.pixels.size(); i += 4)
        if (source.pixels[i + 3] == std::byte{} &&
            (source.pixels[i] != std::byte{} || source.pixels[i + 1] != std::byte{} || source.pixels[i + 2] != std::byte{}))
            throw std::invalid_argument("JPEG XL canonical source requires RGB=0 when A=0.");
    auto pixels = source.pixels;
    swap_red_blue(pixels);
    auto runner = make_runner(); // Must outlive encoder, which borrows it.
    Encoder encoder{JxlEncoderCreate(nullptr), &JxlEncoderDestroy};
    if (!encoder) reject("encoder allocation");
    enc_ok(JxlEncoderSetParallelRunner(encoder.get(), JxlThreadParallelRunner, runner.get()), "encoder runner");
    JxlBasicInfo info{};
    JxlEncoderInitBasicInfo(&info);
    info.xsize = source.width; info.ysize = source.height;
    info.bits_per_sample = 8; info.exponent_bits_per_sample = 0;
    info.num_color_channels = 3; info.num_extra_channels = 1;
    info.alpha_bits = 8; info.alpha_exponent_bits = 0; info.alpha_premultiplied = JXL_FALSE;
    info.uses_original_profile = lossy ? JXL_FALSE : JXL_TRUE;
    info.have_animation = JXL_FALSE; info.have_preview = JXL_FALSE;
    info.orientation = JXL_ORIENT_IDENTITY;
    enc_ok(JxlEncoderSetBasicInfo(encoder.get(), &info), "basic info");
    JxlColorEncoding color{};
    JxlColorEncodingSetToSRGB(&color, JXL_FALSE);
    enc_ok(JxlEncoderSetColorEncoding(encoder.get(), &color), "sRGB encoding");
    auto* settings = JxlEncoderFrameSettingsCreate(encoder.get(), nullptr); // encoder-owned
    if (!settings) reject("frame settings allocation");
    enc_ok(JxlEncoderSetFrameLossless(settings, lossy ? JXL_FALSE : JXL_TRUE), "compression mode");
    enc_ok(JxlEncoderFrameSettingsSetOption(settings, JXL_ENC_FRAME_SETTING_EFFORT, product_jxl_effort), "effort");
    if (lossy)
    {
        enc_ok(JxlEncoderSetFrameDistance(settings, distance), "color distance");
        enc_ok(JxlEncoderSetExtraChannelDistance(settings, 0, 0), "lossless alpha distance");
        if (applied) *applied = {distance, 0, product_jxl_effort, info.uses_original_profile == JXL_TRUE};
    }
    // Lossless also defaults KEEP_INVISIBLE=1. No container/boxes/JPEG APIs.
    enc_ok(JxlEncoderAddImageFrame(settings, &rgba8, pixels.data(), pixels.size()), "RGBA frame");
    JxlEncoderCloseInput(encoder.get());
    std::vector<std::byte> output(65536);
    std::size_t used{};
    for (;;)
    {
        auto* next = reinterpret_cast<std::uint8_t*>(output.data() + used);
        std::size_t available = output.size() - used;
        const auto status = JxlEncoderProcessOutput(encoder.get(), &next, &available);
        used = output.size() - available;
        if (status == JXL_ENC_SUCCESS) break;
        if (status != JXL_ENC_NEED_MORE_OUTPUT) reject("encode output");
        if (output.size() == maximum_jxl_bytes) reject("encoded output safety limit");
        output.resize(std::min(output.size() * 2, maximum_jxl_bytes));
    }
    output.resize(used);
    if (JxlSignatureCheck(reinterpret_cast<const std::uint8_t*>(output.data()), output.size()) != JXL_SIG_CODESTREAM)
        reject("encoder did not produce a naked codestream");
    return output;
}
}

std::vector<std::byte> encode_lossless_jxl_bytes(const image::CpuImage& source)
{ return encode_jxl(source, {}, nullptr); }
std::vector<std::byte> encode_lossy_jxl_bytes(const image::CpuImage& source, LossyProfile profile, JxlLossyConfig* applied)
{ return encode_jxl(source, profile, applied); }

detail::DecodedJxl detail::decode_and_inspect(std::span<const std::byte> bytes,
    DecodePolicy policy, const image::CpuImage* expected_source)
{
    if (bytes.empty() || bytes.size() > maximum_jxl_bytes ||
        JxlSignatureCheck(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()) != JXL_SIG_CODESTREAM)
        reject("naked JPEG XL codestream required");
    DecodedJxl result; // Output storage must outlive the decoder's borrowed buffer.
    auto runner = make_runner();
    Decoder decoder{JxlDecoderCreate(nullptr), &JxlDecoderDestroy};
    if (!decoder) reject("decoder allocation");
    dec_ok(JxlDecoderSetParallelRunner(decoder.get(), JxlThreadParallelRunner, runner.get()), "decoder runner");
    dec_ok(JxlDecoderSetKeepOrientation(decoder.get(), JXL_TRUE), "keep orientation");
    dec_ok(JxlDecoderSetUnpremultiplyAlpha(decoder.get(), JXL_FALSE), "alpha handling");
    dec_ok(JxlDecoderSetCoalescing(decoder.get(), JXL_FALSE), "layer visibility");
    dec_ok(JxlDecoderSubscribeEvents(decoder.get(), JXL_DEC_BASIC_INFO | JXL_DEC_COLOR_ENCODING |
        JXL_DEC_FRAME | JXL_DEC_FULL_IMAGE), "events");
    dec_ok(JxlDecoderSetInput(decoder.get(), reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()), "input");
    JxlDecoderCloseInput(decoder.get());
    bool basic{}, color{}, full{}, buffer{};
    for (unsigned event = 0; event < 32; ++event)
    {
        const auto status = JxlDecoderProcessInput(decoder.get());
        switch (status)
        {
        case JXL_DEC_BASIC_INFO:
        {
            if (basic) reject("duplicate basic info");
            JxlBasicInfo info{};
            dec_ok(JxlDecoderGetBasicInfo(decoder.get(), &info), "read basic info");
            if (info.have_container || info.have_animation || info.have_preview ||
                info.orientation != JXL_ORIENT_IDENTITY ||
                (info.uses_original_profile != (policy == DecodePolicy::lossless ? JXL_TRUE : JXL_FALSE)) ||
                info.bits_per_sample != 8 || info.exponent_bits_per_sample != 0 || info.num_color_channels != 3 ||
                info.num_extra_channels != 1 || info.alpha_bits != 8 || info.alpha_exponent_bits != 0 || info.alpha_premultiplied ||
                (info.intrinsic_xsize && info.intrinsic_xsize != info.xsize) ||
                (info.intrinsic_ysize && info.intrinsic_ysize != info.ysize)) reject("unsupported image/header policy");
            if (expected_source && (info.xsize != expected_source->width || info.ysize != expected_source->height))
                reject("decoded dimensions do not match trusted source");
            JxlExtraChannelInfo alpha{};
            dec_ok(JxlDecoderGetExtraChannelInfo(decoder.get(), 0, &alpha), "read alpha info");
            if (alpha.type != JXL_CHANNEL_ALPHA || alpha.bits_per_sample != 8 || alpha.exponent_bits_per_sample ||
                alpha.alpha_premultiplied || alpha.dim_shift || alpha.name_length) reject("unsupported alpha semantics");
            result.inspection = {info.xsize, info.ysize, 8, 0, bytes.size(), true};
            result.pixels = {info.xsize, info.ysize, image::PixelFormat::bgra8_unorm,
                std::vector<std::byte>(image_bytes(info.xsize, info.ysize))};
            basic = true;
            break;
        }
        case JXL_DEC_COLOR_ENCODING:
        {
            if (!basic || color) reject("unexpected color encoding event");
            JxlColorEncoding original{}, data{};
            dec_ok(JxlDecoderGetColorAsEncodedProfile(decoder.get(), JXL_COLOR_PROFILE_TARGET_ORIGINAL, &original), "encoded color");
            dec_ok(JxlDecoderGetColorAsEncodedProfile(decoder.get(), JXL_COLOR_PROFILE_TARGET_DATA, &data), "decoded color");
            if (!srgb(original) || !srgb(data)) reject("sRGB structured color description required");
            color = true;
            break;
        }
        case JXL_DEC_FRAME:
        {
            if (!basic || !color || ++result.inspection.image_count != 1) reject("single still frame required");
            JxlFrameHeader frame{};
            dec_ok(JxlDecoderGetFrameHeader(decoder.get(), &frame), "frame header");
            if (!frame.is_last || frame.duration || frame.timecode || frame.name_length || frame.layer_info.have_crop ||
                frame.layer_info.xsize != result.pixels.width || frame.layer_info.ysize != result.pixels.height ||
                frame.layer_info.blend_info.blendmode != JXL_BLEND_REPLACE) reject("unsupported frame/layer metadata");
            if (policy == DecodePolicy::lossy && (frame.layer_info.crop_x0 || frame.layer_info.crop_y0 ||
                frame.layer_info.save_as_reference || frame.layer_info.blend_info.source ||
                frame.layer_info.blend_info.alpha || frame.layer_info.blend_info.clamp))
                reject("unexpected lossy frame/reference semantics");
            break;
        }
        case JXL_DEC_NEED_IMAGE_OUT_BUFFER:
        {
            if (!basic || result.inspection.image_count != 1 || buffer) reject("unexpected output request");
            std::size_t size{};
            dec_ok(JxlDecoderImageOutBufferSize(decoder.get(), &rgba8, &size), "output size");
            if (size != result.pixels.pixels.size()) reject("unexpected decoded byte size");
            dec_ok(JxlDecoderSetImageOutBuffer(decoder.get(), &rgba8, result.pixels.pixels.data(), size), "output buffer");
            buffer = true;
            break;
        }
        case JXL_DEC_FULL_IMAGE:
            if (!buffer || full) reject("unexpected decoded frame");
            full = true; break;
        case JXL_DEC_SUCCESS:
            if (!basic || !color || !full || result.inspection.image_count != 1 || JxlDecoderReleaseInput(decoder.get()) != 0)
                reject("incomplete image or trailing bytes");
            swap_red_blue(result.pixels.pixels);
            return result;
        default: reject("corrupt/truncated or unexpected decoder event");
        }
    }
    reject("decoder event limit");
}

image::CpuImage decode_bgra8_jxl(std::span<const std::byte> bytes)
{ return detail::decode_and_inspect(bytes).pixels; }
}

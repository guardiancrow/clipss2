#include "codec/jxl/jxl_codec.h"
#include "codec/jxl/jxl_validator.h"
#include "codec/png/png_codec.h"
#include "codec/webp/webp_codec.h"
#include "image/alpha_diagnostics.h"
#include <jxl/encode.h>
#include <jxl/decode.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

using namespace clipss2;
namespace
{
void require(bool condition, const char* reason) { if (!condition) throw std::runtime_error(reason); }
template<class F> void rejects(F&& f, const char* reason)
{ try { f(); } catch (const std::exception&) { return; } throw std::runtime_error(reason); }
image::CpuImage fixture(std::uint32_t w, std::uint32_t h, bool opaque = false)
{
    image::CpuImage result{w, h, image::PixelFormat::bgra8_unorm, std::vector<std::byte>(static_cast<std::size_t>(w) * h * 4)};
    for (std::uint32_t y = 0; y < h; ++y)
    for (std::uint32_t x = 0; x < w; ++x)
    {
        const auto i = (static_cast<std::size_t>(y) * w + x) * 4;
        const auto a = opaque ? 255U : (x + y) % 11 == 0 ? 0U : (x * 17U + y * 31U) & 255U;
        result.pixels[i] = static_cast<std::byte>(a ? (x * 3 + 197) & 255 : 0);
        result.pixels[i + 1] = static_cast<std::byte>(a ? (y * 5 + 73) & 255 : 0);
        result.pixels[i + 2] = static_cast<std::byte>(a ? (x + y * 7 + 19) & 255 : 0);
        result.pixels[i + 3] = static_cast<std::byte>(a);
    }
    return result;
}

// Adversarial streams use public APIs only; these settings are not product policy.
enum class Variant { container, animation, orientation, linear, depth, premultiplied, frame_name };
std::vector<std::byte> adversarial(Variant variant)
{
    const std::unique_ptr<JxlEncoder, decltype(&JxlEncoderDestroy)> encoder{JxlEncoderCreate(nullptr), &JxlEncoderDestroy};
    require(!!encoder, "adversarial allocation");
    const auto ok = [](JxlEncoderStatus s) { require(s == JXL_ENC_SUCCESS, "adversarial setup failed"); };
    JxlBasicInfo info{}; JxlEncoderInitBasicInfo(&info);
    info.xsize = 3; info.ysize = 2; info.bits_per_sample = variant == Variant::depth ? 10 : 8;
    info.num_color_channels = 3; info.num_extra_channels = 1; info.alpha_bits = 8;
    info.uses_original_profile = JXL_TRUE;
    info.have_animation = variant == Variant::animation;
    info.orientation = variant == Variant::orientation ? JXL_ORIENT_ROTATE_180 : JXL_ORIENT_IDENTITY;
    info.alpha_premultiplied = variant == Variant::premultiplied;
    ok(JxlEncoderSetBasicInfo(encoder.get(), &info));
    JxlColorEncoding color{};
    if (variant == Variant::linear) JxlColorEncodingSetToLinearSRGB(&color, JXL_FALSE);
    else JxlColorEncodingSetToSRGB(&color, JXL_FALSE);
    ok(JxlEncoderSetColorEncoding(encoder.get(), &color));
    if (variant == Variant::container) ok(JxlEncoderUseContainer(encoder.get(), JXL_TRUE));
    auto* settings = JxlEncoderFrameSettingsCreate(encoder.get(), nullptr);
    require(settings != nullptr, "adversarial settings");
    ok(JxlEncoderSetFrameLossless(settings, JXL_TRUE));
    if (variant == Variant::frame_name) ok(JxlEncoderSetFrameName(settings, "private application title"));
    if (variant == Variant::animation)
    { JxlFrameHeader frame{}; JxlEncoderInitFrameHeader(&frame); frame.duration = 1; ok(JxlEncoderSetFrameHeader(settings, &frame)); }
    constexpr JxlPixelFormat rgba{4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
    const auto pixels = fixture(3, 2, true).pixels;
    ok(JxlEncoderAddImageFrame(settings, &rgba, pixels.data(), pixels.size()));
    if (variant == Variant::animation) ok(JxlEncoderAddImageFrame(settings, &rgba, pixels.data(), pixels.size()));
    JxlEncoderCloseInput(encoder.get());
    std::vector<std::byte> result(65536);
    auto* next = reinterpret_cast<std::uint8_t*>(result.data()); auto available = result.size();
    ok(JxlEncoderProcessOutput(encoder.get(), &next, &available));
    result.resize(result.size() - available); return result;
}
template<class F> void measure(const char* name, F&& f)
{
    const auto start = std::chrono::steady_clock::now();
    const auto bytes = f();
    std::cout << name << "=" << bytes.size() << " bytes/"
        << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() << " ms\n";
}
}

int main(int argc, char** argv)
{
    try
    {
        require(codec::jxl::product_jxl_effort == 7, "effort policy changed");
        for (const auto& source : {fixture(1, 1, true), fixture(31, 13, true), fixture(17, 9), fixture(257, 31)})
        {
            const auto bytes = codec::jxl::encode_lossless_jxl_bytes(source);
            require(bytes.size() >= 2 && bytes[0] == std::byte{0xff} && bytes[1] == std::byte{0x0a}, "codestream signature");
            require(JxlSignatureCheck(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()) == JXL_SIG_CODESTREAM,
                "container was emitted");
            const auto info = codec::jxl::validate_screenshot_jxl(bytes, source);
            require(info.width == source.width && info.height == source.height && info.depth == 8 &&
                info.has_alpha && info.image_count == 1, "inspection mismatch");
            const auto decoded = codec::jxl::decode_bgra8_jxl(bytes);
            std::uint64_t sum{}, max{};
            for (std::size_t i = 0; i < source.pixels.size(); ++i)
            {
                const auto a = std::to_integer<int>(source.pixels[i]), b = std::to_integer<int>(decoded.pixels[i]);
                const auto error = static_cast<std::uint64_t>(a > b ? a - b : b - a);
                sum += error; max = std::max(max, error);
            }
            require(sum == 0 && max == 0, "RGBA MAE/max not zero");
            std::cout << source.width << 'x' << source.height << " RGBA MAE=0 max=0\n";
            auto truncated = bytes; truncated.pop_back();
            rejects([&] { (void)codec::jxl::inspect_jxl_structure(truncated); }, "truncated accepted");
            auto trailing = bytes; trailing.push_back(std::byte{0x01});
            rejects([&] { (void)codec::jxl::inspect_jxl_structure(trailing); }, "trailing bytes accepted");
            auto corrupt = bytes; corrupt[0] = std::byte{};
            rejects([&] { (void)codec::jxl::inspect_jxl_structure(corrupt); }, "corrupt signature accepted");
            auto mismatch = source; mismatch.pixels[0] ^= std::byte{1};
            rejects([&] { (void)codec::jxl::validate_screenshot_jxl(bytes, mismatch); }, "pixel mismatch accepted");
            auto dimensions = source; ++dimensions.width;
            rejects([&] { (void)codec::jxl::validate_screenshot_jxl(bytes, dimensions); }, "dimension mismatch accepted");
        }
        auto premultiplied = fixture(17, 9);
        premultiplied.pixels[0] = std::byte{0xaa}; // garbage at A=0 must be canonicalized
        const auto canonical = image::unpremultiply_bgra8_for_lossless(premultiplied);
        require(canonical.pixels[0] == std::byte{} && canonical.pixels[1] == std::byte{} && canonical.pixels[2] == std::byte{},
            "zero-alpha canonicalization failed");
        (void)codec::jxl::validate_screenshot_jxl(codec::jxl::encode_lossless_jxl_bytes(canonical), canonical);
        rejects([&] { (void)codec::jxl::encode_lossless_jxl_bytes(premultiplied); }, "noncanonical invisible RGB accepted");
        rejects([&] { (void)codec::jxl::encode_lossless_jxl_bytes({}); }, "zero dimensions accepted");
        const std::vector<std::byte> corrupt_body{std::byte{0xff}, std::byte{0x0a}, std::byte{0xff}, std::byte{0xff},
            std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
        rejects([&] { (void)codec::jxl::inspect_jxl_structure(corrupt_body); }, "valid signature with corrupt body accepted");
        rejects([&] { (void)codec::jxl::encode_lossless_jxl_bytes({UINT32_MAX, UINT32_MAX, image::PixelFormat::bgra8_unorm, {}}); },
            "unsafe dimensions accepted");
        auto malformed = fixture(2, 3); malformed.pixels.pop_back();
        rejects([&] { (void)codec::jxl::encode_lossless_jxl_bytes(malformed); }, "buffer mismatch accepted");
        malformed = fixture(2, 3); malformed.format = image::PixelFormat::rgba16_float;
        rejects([&] { (void)codec::jxl::encode_lossless_jxl_bytes(malformed); }, "FP16 accepted");
        for (const auto variant : {Variant::container, Variant::animation, Variant::orientation, Variant::linear,
            Variant::depth, Variant::premultiplied, Variant::frame_name})
        {
            const auto bytes = adversarial(variant);
            rejects([&] { (void)codec::jxl::inspect_jxl_structure(bytes); }, "unsupported structure accepted");
        }
        if (argc == 2 && std::string_view{argv[1]} == "--benchmark")
        {
            const auto source = fixture(768, 512); // one fixture, one measurement per codec
            std::cout << "Representative 768x512 canonical RGBA (encode only):\n";
            measure("PNG", [&] { return codec::png::encode_srgb_png_bytes(source); });
            measure("WebP Lossless", [&] { return codec::webp::encode_lossless_webp_bytes(source); });
            measure("JPEG XL Lossless", [&] { return codec::jxl::encode_lossless_jxl_bytes(source); });
        }
        std::cout << "JPEG XL exact/alpha/channel-order/naked/strict rejection tests: passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

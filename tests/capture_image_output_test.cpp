#include "capture/capture_image_output.h"
#include "image/alpha_diagnostics.h"
#include "codec/jpeg/jpeg_codec.h"
#include "codec/jpeg/jpeg_validator.h"
#include "codec/png/png_codec.h"
#include "codec/png/png_validator.h"
#include "codec/webp/webp_codec.h"
#include "codec/webp/webp_validator.h"
#include "codec/avif/avif_codec.h"
#include "codec/avif/avif_validator.h"
#include "codec/jxl/jxl_codec.h"
#include "codec/jxl/jxl_validator.h"
#include "codec/avif/avif_lossy_validator.h"
#include "codec/jxl/jxl_lossy_validator.h"
#include <cstdlib>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace
{
class ScratchOutput final
{
public:
    ScratchOutput() : directory{std::filesystem::temp_directory_path() /
        ("clipss2-output-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()))}
    {
        if (!std::filesystem::create_directory(directory))
            throw std::runtime_error("Could not reserve output test directory.");
    }
    ~ScratchOutput()
    {
        std::error_code ignored;
        std::filesystem::remove(directory / "jpeg.pending", ignored);
        std::filesystem::remove(directory / "png.pending", ignored);
        std::filesystem::remove(directory / "webp.pending", ignored);
        std::filesystem::remove(directory / "avif.pending", ignored);
        std::filesystem::remove(directory / "jxl.pending", ignored);
        std::filesystem::remove(directory, ignored); // Only this test's empty directory.
    }
    ScratchOutput(const ScratchOutput&) = delete;
    ScratchOutput& operator=(const ScratchOutput&) = delete;
    const std::filesystem::path directory;
};
}

int main()
{
    using namespace clipss2;
    try
    {
        image::CpuImage red{32, 16, image::PixelFormat::bgra8_unorm,
            std::vector<std::byte>(32 * 16 * 4)};
        for (std::size_t i = 0; i < red.pixels.size(); i += 4)
        { red.pixels[i + 2] = std::byte{128}; red.pixels[i + 3] = std::byte{128}; }
        const auto original = red.pixels;
        for (const auto profile : {codec::LossyProfile::high, codec::LossyProfile::balanced, codec::LossyProfile::small_file})
        {
            const auto canonical = image::unpremultiply_bgra8_for_lossless(red);
            codec::avif::AvifLossyConfig av_config;
            const auto av_bytes = codec::avif::encode_lossy_avif_bytes(canonical, profile, &av_config);
            const auto expected_q = profile == codec::LossyProfile::high ? 80 : profile == codec::LossyProfile::balanced ? 65 : 50;
            if (av_config.quality != expected_q || av_config.quality_alpha != 100 || av_config.speed != 6 ||
                av_config.threads != 8 || av_config.depth != 8 || av_config.matrix != 1)
                throw std::runtime_error("AVIF applied lossy policy mismatch.");
            (void)codec::avif::validate_screenshot_avif_lossy(av_bytes, canonical);
            codec::jxl::JxlLossyConfig j_config;
            const auto j_bytes = codec::jxl::encode_lossy_jxl_bytes(canonical, profile, &j_config);
            const auto expected_d = profile == codec::LossyProfile::high ? .5F : profile == codec::LossyProfile::balanced ? 1.F : 2.F;
            if (j_config.distance != expected_d || j_config.alpha_distance != 0 || j_config.effort != 7 || j_config.uses_original_profile)
                throw std::runtime_error("JXL applied lossy policy mismatch.");
            (void)codec::jxl::validate_screenshot_jxl_lossy(j_bytes, canonical);
            for (const auto format : {image::ImageFileFormat::avif, image::ImageFileFormat::jxl})
            for (const auto destination : {capture::CaptureDestination::file, capture::CaptureDestination::both, capture::CaptureDestination::clipboard})
            {
                const capture::FileCompression compression{capture::FileCompressionMode::lossy, profile};
                const auto out = capture::prepare_capture_image_output(red, destination, format, {}, compression);
                const auto& bytes = format == image::ImageFileFormat::avif ? out.avif_bytes : out.jxl_bytes;
                if (destination == capture::CaptureDestination::clipboard)
                {
                    if (!bytes.empty() || out.lossy_encode_milliseconds != 0 || out.lossy_validate_milliseconds != 0 || out.png_bytes.empty())
                        throw std::runtime_error("Clipboard-only invoked lossy encoder.");
                    continue;
                }
                capture::validate_capture_file_payload(format, compression, bytes, *out.lossless_source);
                auto bad = bytes; bad.pop_back(); bool clipboard_called{};
                const auto failed = capture::execute_capture_output_plan(capture::CaptureDestination::both,
                    [&] { try { capture::validate_capture_file_payload(format, compression, bad, *out.lossless_source); }
                        catch (const std::exception&) { return false; } return true; },
                    [&] { clipboard_called = true; return true; });
                if (failed.file_succeeded || failed.clipboard_attempted || clipboard_called)
                    throw std::runtime_error("Lossy file validation failure attempted Clipboard.");
                auto wrong_alpha = *out.lossless_source; wrong_alpha.pixels.back() ^= std::byte{1};
                bool rejected{};
                try { capture::validate_capture_file_payload(format, compression, bytes, wrong_alpha); }
                catch (const std::exception&) { rejected = true; }
                if (!rejected || red.pixels != original) throw std::runtime_error("Lossy source/alpha ownership mismatch.");
            }
        }
        const codec::jpeg::JpegProductSelection unsupported = codec::jpeg::JpegProfileSelection{
            codec::jpeg::JpegProfile::balanced, codec::jpeg::JpegBackendId::turbo};
        for (const auto destination : {capture::CaptureDestination::file, capture::CaptureDestination::both,
                                      capture::CaptureDestination::clipboard})
        {
            const auto normal = capture::prepare_capture_image_output(red, destination, image::ImageFileFormat::png);
            const auto unrelated = capture::prepare_capture_image_output(red, destination, image::ImageFileFormat::png, unsupported);
            if (normal.png_bytes != unrelated.png_bytes || unrelated.jpeg_effective || !unrelated.jpeg_bytes.empty())
                throw std::runtime_error("JPEG selection affected PNG.");
        }
        const auto clipboard_only = capture::prepare_capture_image_output(red, capture::CaptureDestination::clipboard,
            image::ImageFileFormat::jpeg, unsupported);
        if (clipboard_only.jpeg_effective || !clipboard_only.jpeg_bytes.empty())
            throw std::runtime_error("Clipboard-only attempted JPEG selection.");
        for (const auto format : {image::ImageFileFormat::png, image::ImageFileFormat::jpeg,
                                  image::ImageFileFormat::webp, image::ImageFileFormat::avif, image::ImageFileFormat::jxl})
        for (const auto destination : {capture::CaptureDestination::file,
                 capture::CaptureDestination::clipboard, capture::CaptureDestination::both})
        {
            const auto prepared = capture::prepare_capture_image_output(red, destination, format);
            const auto plan = capture::capture_output_plan(destination);
            if (prepared.jpeg_effective.has_value() != (plan.file && format == image::ImageFileFormat::jpeg))
                throw std::runtime_error("Managed JPEG ran on a PNG/Clipboard-only path.");
            if (prepared.jpeg_effective && (prepared.jpeg_effective->backend != codec::jpeg::JpegBackendId::turbo ||
                std::get<codec::jpeg::TurboJpegParameters>(prepared.jpeg_effective->parameters).quality != 90 ||
                prepared.jpeg_effective->progressive || prepared.jpeg_effective->optimized_entropy))
                throw std::runtime_error("Product JPEG baseline settings changed.");
            const bool needs_lossless = plan.clipboard || (plan.file &&
                (format == image::ImageFileFormat::png || format == image::ImageFileFormat::webp ||
                    format == image::ImageFileFormat::avif || format == image::ImageFileFormat::jxl));
            const bool needs_png = plan.clipboard || (plan.file && format == image::ImageFileFormat::png);
            if (prepared.lossless_source.has_value() != needs_lossless ||
                prepared.png_bytes.empty() == needs_png ||
                prepared.jpeg_bytes.empty() == (plan.file && format == image::ImageFileFormat::jpeg) ||
                prepared.webp_bytes.empty() == (plan.file && format == image::ImageFileFormat::webp) ||
                prepared.avif_bytes.empty() == (plan.file && format == image::ImageFileFormat::avif) ||
                prepared.jxl_bytes.empty() == (plan.file && format == image::ImageFileFormat::jxl))
                throw std::runtime_error("Output preparation selected wrong encodings.");
            if (needs_png)
            {
                (void)codec::png::validate_screenshot_png(prepared.png_bytes, *prepared.lossless_source);
                const auto png = codec::png::decode_rgba8_png(prepared.png_bytes);
                if (png.pixels[0] != std::byte{255} || png.pixels[1] != std::byte{0} ||
                    png.pixels[3] != std::byte{128}) throw std::runtime_error("Clipboard PNG alpha was flattened.");
            }
            if (!prepared.webp_bytes.empty())
            {
                (void)codec::webp::validate_screenshot_webp(prepared.webp_bytes, *prepared.lossless_source);
                const auto decoded = codec::webp::decode_bgra8_webp(prepared.webp_bytes);
                if (decoded.pixels[0] != std::byte{0} || decoded.pixels[1] != std::byte{0} ||
                    decoded.pixels[2] != std::byte{255} || decoded.pixels[3] != std::byte{128})
                    throw std::runtime_error("WebP used matte/premultiplied/channel-swapped pixels.");
            }
            if (!prepared.avif_bytes.empty())
            {
                (void)codec::avif::validate_screenshot_avif(prepared.avif_bytes, *prepared.lossless_source);
                const auto decoded = codec::avif::decode_bgra8_avif(prepared.avif_bytes);
                if (decoded.pixels[0] != std::byte{0} || decoded.pixels[1] != std::byte{0} ||
                    decoded.pixels[2] != std::byte{255} || decoded.pixels[3] != std::byte{128})
                    throw std::runtime_error("AVIF used matte/premultiplied/channel-swapped pixels.");
            }
            if (!prepared.jxl_bytes.empty())
            {
                (void)codec::jxl::validate_screenshot_jxl(prepared.jxl_bytes, *prepared.lossless_source);
                const auto decoded = codec::jxl::decode_bgra8_jxl(prepared.jxl_bytes);
                if (decoded.pixels[0] != std::byte{0} || decoded.pixels[1] != std::byte{0} ||
                    decoded.pixels[2] != std::byte{255} || decoded.pixels[3] != std::byte{128})
                    throw std::runtime_error("JPEG XL used matte/premultiplied/channel-swapped pixels.");
            }
            if (!prepared.jpeg_bytes.empty())
            {
                const auto decoded = codec::jpeg::decode_srgb_jpeg_bytes(prepared.jpeg_bytes);
                if (std::abs(std::to_integer<int>(decoded.pixels[0]) - 255) > 3 ||
                    std::abs(std::to_integer<int>(decoded.pixels[1]) - 127) > 3)
                    throw std::runtime_error("JPEG premultiplied white matte mismatch.");
            }
            if (red.pixels != original) throw std::runtime_error("Captured/DIB image was modified.");
        }
        bool rejected = false;
        try { (void)capture::prepare_capture_image_output(red, capture::CaptureDestination::clipboard,
            image::ImageFileFormat::unknown); } catch (const std::invalid_argument&) { rejected = true; }
        if (!rejected) throw std::runtime_error("Unknown file format accepted.");
        const ScratchOutput scratch;
        const auto prepared = capture::prepare_capture_image_output(red,
            capture::CaptureDestination::both, image::ImageFileFormat::jpeg);
        bool clipboard_called = false;
        const auto failed = capture::execute_capture_output_plan(capture::CaptureDestination::both,
            [&] {
                try { capture::write_capture_file_image(image::ImageFileFormat::jpeg,
                    prepared.jpeg_bytes, scratch.directory / "missing" / "capture.pending", red); }
                catch (const std::runtime_error&) { return false; }
                return true;
            }, [&] { clipboard_called = true; return true; });
        if (failed.file_succeeded || failed.clipboard_attempted || clipboard_called)
            throw std::runtime_error("JPEG file failure attempted Clipboard.");
        const auto pending = scratch.directory / "jpeg.pending";
        const auto jpegli_prepared = capture::prepare_capture_image_output(red,
            capture::CaptureDestination::both, image::ImageFileFormat::jpeg,
            codec::jpeg::JpegProfileSelection{codec::jpeg::JpegProfile::balanced, codec::jpeg::JpegBackendId::jpegli});
        if (!jpegli_prepared.jpeg_effective || jpegli_prepared.jpeg_effective->backend != codec::jpeg::JpegBackendId::jpegli)
            throw std::runtime_error("Prepared output lost jpegli identity.");
        capture::write_capture_file_image(image::ImageFileFormat::jpeg, jpegli_prepared.jpeg_bytes,
            pending, red, &*jpegli_prepared.jpeg_effective);
        bool wrong_profile_rejected = false;
        try { capture::write_capture_file_image(image::ImageFileFormat::jpeg,
            jpegli_prepared.jpeg_bytes, pending, red); } catch (const std::runtime_error&) { wrong_profile_rejected = true; }
        if (!wrong_profile_rejected) throw std::runtime_error("Pending gate inferred/lost backend identity.");
        const auto partial = capture::execute_capture_output_plan(capture::CaptureDestination::both,
            [&] { capture::write_capture_file_image(image::ImageFileFormat::jpeg,
                prepared.jpeg_bytes, pending, red); return true; }, [] { return false; });
        if (partial.outcome != capture::CaptureOutputOutcome::partial_success ||
            !std::filesystem::exists(pending))
            throw std::runtime_error("JPEG partial success did not retain file.");
        auto invalid = prepared.jpeg_bytes;
        invalid.pop_back();
        rejected = false;
        try { capture::write_capture_file_image(image::ImageFileFormat::jpeg, invalid, pending, red); }
        catch (const std::runtime_error&) { rejected = true; }
        if (!rejected || std::filesystem::file_size(pending) != prepared.jpeg_bytes.size())
            throw std::runtime_error("Invalid JPEG modified pending file before validation.");
        capture::write_capture_file_image(image::ImageFileFormat::png, prepared.png_bytes,
            scratch.directory / "png.pending", *prepared.lossless_source);
        const auto webp = capture::prepare_capture_image_output(red,
            capture::CaptureDestination::both, image::ImageFileFormat::webp);
        capture::write_capture_file_image(image::ImageFileFormat::webp, webp.webp_bytes,
            scratch.directory / "webp.pending", *webp.lossless_source);
        auto invalid_webp = webp.webp_bytes; invalid_webp.pop_back();
        rejected = false;
        try { capture::write_capture_file_image(image::ImageFileFormat::webp, invalid_webp,
            scratch.directory / "webp-invalid.pending", *webp.lossless_source); }
        catch (const std::runtime_error&) { rejected = true; }
        if (!rejected || std::filesystem::exists(scratch.directory / "webp-invalid.pending"))
            throw std::runtime_error("Invalid WebP touched pending output before validation.");
        bool webp_clipboard_called{};
        const auto webp_failure = capture::execute_capture_output_plan(capture::CaptureDestination::both,
            [] { return false; }, [&] { webp_clipboard_called = true; return true; });
        if (webp_failure.clipboard_attempted || webp_clipboard_called)
            throw std::runtime_error("WebP file failure attempted Clipboard.");
        const auto avif = capture::prepare_capture_image_output(red,
            capture::CaptureDestination::both, image::ImageFileFormat::avif);
        capture::write_capture_file_image(image::ImageFileFormat::avif, avif.avif_bytes,
            scratch.directory / "avif.pending", *avif.lossless_source);
        auto invalid_avif = avif.avif_bytes; invalid_avif.pop_back();
        rejected = false;
        try { capture::write_capture_file_image(image::ImageFileFormat::avif, invalid_avif,
            scratch.directory / "avif-invalid.pending", *avif.lossless_source); }
        catch (const std::runtime_error&) { rejected = true; }
        if (!rejected || std::filesystem::exists(scratch.directory / "avif-invalid.pending"))
            throw std::runtime_error("Invalid AVIF touched pending output before validation.");
        bool avif_clipboard_called{};
        const auto avif_failure = capture::execute_capture_output_plan(capture::CaptureDestination::both,
            [] { return false; }, [&] { avif_clipboard_called = true; return true; });
        if (avif_failure.clipboard_attempted || avif_clipboard_called)
            throw std::runtime_error("AVIF file failure attempted Clipboard.");
        const auto jxl = capture::prepare_capture_image_output(red,
            capture::CaptureDestination::both, image::ImageFileFormat::jxl);
        capture::write_capture_file_image(image::ImageFileFormat::jxl, jxl.jxl_bytes,
            scratch.directory / "jxl.pending", *jxl.lossless_source);
        auto invalid_jxl = jxl.jxl_bytes; invalid_jxl.pop_back();
        rejected = false;
        try { capture::write_capture_file_image(image::ImageFileFormat::jxl, invalid_jxl,
            scratch.directory / "jxl-invalid.pending", *jxl.lossless_source); }
        catch (const std::exception&) { rejected = true; }
        if (!rejected || std::filesystem::exists(scratch.directory / "jxl-invalid.pending"))
            throw std::runtime_error("Invalid JPEG XL touched pending output before validation.");
        bool jxl_clipboard_called{};
        const auto jxl_failure = capture::execute_capture_output_plan(capture::CaptureDestination::both,
            [] { return false; }, [&] { jxl_clipboard_called = true; return true; });
        if (jxl_failure.clipboard_attempted || jxl_clipboard_called)
            throw std::runtime_error("JPEG XL file failure attempted Clipboard.");
        std::cout << "PNG/JPEG/WebP/AVIF/JPEG XL destination preparation and alpha separation: passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

#include "audit/jpeg_output_audit.h"
#include "codec/jpeg/jpeg_validator.h"
#include "codec/jpeg/jpeg_managed_backend.h"
#include "codec/jpeg/jpeg_codec_internal.h"
#include "codec/jpeg/jpeg_product_selection.h"

#include <iostream>
#include <stdexcept>
#include <array>
#include <chrono>

namespace
{
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
template<class Function> void rejects(Function function)
{
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "Invalid JPEG encode input was accepted.");
}
struct DispatchProbe { unsigned turbo{}, other{}; bool fail{}, invalid{}; };
std::vector<std::byte> probe_dispatch(clipss2::codec::jpeg::CanonicalJpegRgb8View source,
    const clipss2::codec::jpeg::JpegEffectiveSettings& settings, void* context)
{
    using namespace clipss2::codec::jpeg;
    auto& probe = *static_cast<DispatchProbe*>(context);
    if (settings.backend == JpegBackendId::turbo) ++probe.turbo; else ++probe.other;
    if (probe.fail) throw std::runtime_error("injected encode failure");
    if (probe.invalid) return {std::byte{0}};
    return detail::encode_turbo_jpeg_bytes(source, std::get<TurboJpegParameters>(settings.parameters).quality);
}
}

int main()
{
    using namespace clipss2;
    try
    {
        const auto source = audit::make_jpeg_sdr_fixture();
        const auto rgb = codec::jpeg::flatten_wgc_premultiplied_bgra8_for_jpeg(source);
        const codec::jpeg::JpegOptions baseline{};
        require(baseline.quality == 90 && baseline.subsampling == codec::jpeg::ChromaSubsampling::yuv444 &&
            baseline.alpha_flatten == codec::jpeg::AlphaFlattenPolicy::opaque_srgb_white,
            "Unexpected provisional baseline options.");
        const auto jpeg = codec::jpeg::encode_srgb_jpeg_bytes(rgb, baseline);
        const codec::jpeg::CanonicalJpegRgb8View view{rgb.width, rgb.height,
            static_cast<std::size_t>(rgb.width) * 3, rgb.pixels};
        const auto request = codec::jpeg::current_product_jpeg_request();
        require(request.profile == codec::jpeg::JpegProfile::current_default,
            "Current product default was relabeled as future Balanced.");
        const auto managed = codec::jpeg::encode_managed_srgb_jpeg(view);
        require(managed.bytes == jpeg && managed.backend == codec::jpeg::JpegBackendId::turbo &&
            std::get<codec::jpeg::TurboJpegParameters>(managed.effective.parameters).quality == 90 &&
            !managed.effective.progressive && !managed.effective.optimized_entropy &&
            managed.effective.subsampling == codec::jpeg::JpegSubsamplingIntent::yuv444,
            "Managed encoding changed baseline bytes/settings.");
        require(codec::jpeg::encode_managed_srgb_jpeg(view, request,
            codec::jpeg::JpegBackendId::turbo).bytes == jpeg, "Manual Turbo differs from Automatic.");
        const auto jpegli = codec::jpeg::encode_managed_srgb_jpeg(view, request, codec::jpeg::JpegBackendId::jpegli);
        require(jpegli.backend == codec::jpeg::JpegBackendId::jpegli &&
            std::get<codec::jpeg::JpegliParameters>(jpegli.effective.parameters).distance == 1.0F,
            "Managed manual jpegli returned wrong backend/settings.");
        require(codec::jpeg::encode_managed_srgb_jpeg(view,
            codec::jpeg::normalize_jpeg_profile(codec::jpeg::JpegProfile::balanced)).backend ==
            codec::jpeg::JpegBackendId::jpegli, "Managed Automatic Balanced not jpegli.");
        for (const auto backend : {codec::jpeg::JpegBackendId::mozjpeg, codec::jpeg::JpegBackendId::wic})
            rejects([&] { (void)codec::jpeg::encode_managed_srgb_jpeg(view, request, backend); });
        std::array<codec::jpeg::JpegBackendCapabilities, 2> catalog{};
        catalog[0] = codec::jpeg::current_product_jpeg_backends().front();
        catalog[1] = catalog[0]; catalog[1].id = codec::jpeg::JpegBackendId::wic;
        const auto fast = codec::jpeg::normalize_jpeg_profile(codec::jpeg::JpegProfile::fast);
        for (const bool invalid : {false, true})
        {
            DispatchProbe probe{}; probe.fail = !invalid; probe.invalid = invalid;
            rejects([&] { (void)codec::jpeg::detail::encode_managed_jpeg_with_dispatch(view,
                fast, codec::jpeg::JpegBackendId::automatic, catalog, probe_dispatch, &probe); });
            require(probe.turbo == 1 && probe.other == 0, "Encode/validation failure dispatched a fallback.");
        }
        catalog[0].supports444 = true; catalog[1].id = codec::jpeg::JpegBackendId::jpegli;
        for (const bool invalid : {false, true})
        {
            DispatchProbe probe{}; probe.fail = !invalid; probe.invalid = invalid;
            const auto resolved = codec::jpeg::resolve_jpeg_product_selection(codec::jpeg::JpegProfileSelection{
                codec::jpeg::JpegProfile::balanced, codec::jpeg::JpegBackendId::automatic});
            rejects([&] { (void)codec::jpeg::detail::encode_managed_jpeg_with_dispatch(view,
                resolved.request,
                resolved.selection.backend, catalog, probe_dispatch, &probe); });
            require(probe.other == 1 && probe.turbo == 0, "jpegli failure retried available Turbo.");
        }
        catalog[1].id = codec::jpeg::JpegBackendId::wic; catalog[0].supports444 = false;
        require(codec::jpeg::select_jpeg_backend(codec::jpeg::JpegBackendId::automatic, fast, catalog).backend ==
            codec::jpeg::JpegBackendId::wic, "Capability fallback did not occur before dispatch.");
        rejects([&] { (void)codec::jpeg::select_jpeg_backend(codec::jpeg::JpegBackendId::turbo, fast, catalog); });
        const auto legacy_started = std::chrono::steady_clock::now();
        for (int i = 0; i < 10; ++i) (void)codec::jpeg::encode_srgb_jpeg_bytes(rgb);
        const auto legacy_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - legacy_started).count();
        const auto started = std::chrono::steady_clock::now();
        for (int i = 0; i < 10; ++i) (void)codec::jpeg::encode_managed_srgb_jpeg(view);
        std::cout << "Legacy/managed JPEG latency sanity (320x192, 10 iterations total ms): " << legacy_ms << '/'
            << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() << '\n';
        const auto valid = codec::jpeg::validate_screenshot_jpeg(jpeg, rgb.width, rgb.height);
        require(jpeg[0] == std::byte{255} && jpeg[1] == std::byte{0xd8} &&
            valid.marker_summary.find("SOF0") != std::string::npos,
            "Encoded bytes are not baseline JPEG.");
        const auto decoded = codec::jpeg::decode_srgb_jpeg_bytes(jpeg);
        const auto metrics = audit::measure_jpeg_fixture(rgb, decoded);
        require(audit::jpeg_fixture_quality_passes(metrics), "Lossy fixture quality threshold exceeded.");
        auto changed = baseline; changed.quality = 35;
        require(codec::jpeg::encode_srgb_jpeg_bytes(rgb, changed) != jpeg,
                "Explicit quality option was ignored.");
        // Tiny/non-MCU-aligned dimensions test row pitch and padded DCT blocks.
        for (const auto size : {1U, 17U})
        {
            const codec::jpeg::OpaqueRgb8Image small{size, size + 2,
                std::vector<std::byte>(codec::jpeg::checked_rgb_image_bytes(size, size + 2), std::byte{128})};
            const auto bytes = codec::jpeg::encode_srgb_jpeg_bytes(small);
            (void)codec::jpeg::validate_screenshot_jpeg(bytes, small.width, small.height);
        }
        const codec::jpeg::OpaqueRgb8Image packed{2, 2, std::vector<std::byte>(12, std::byte{128})};
        const std::vector<std::byte> padded(14, std::byte{128});
        require(codec::jpeg::encode_managed_srgb_jpeg({2, 2, 8, padded}).bytes ==
            codec::jpeg::encode_srgb_jpeg_bytes(packed), "Canonical padded stride changed pixel interpretation.");
        for (const auto quality : {0, 101, -1})
        {
            changed = baseline; changed.quality = quality;
            rejects([&] { (void)codec::jpeg::encode_srgb_jpeg_bytes(rgb, changed); });
        }
        changed = baseline; changed.subsampling = static_cast<codec::jpeg::ChromaSubsampling>(99);
        rejects([&] { (void)codec::jpeg::encode_srgb_jpeg_bytes(rgb, changed); });
        changed = baseline; changed.alpha_flatten = static_cast<codec::jpeg::AlphaFlattenPolicy>(99);
        rejects([&] { (void)codec::jpeg::encode_srgb_jpeg_bytes(rgb, changed); });
        auto invalid = rgb; invalid.pixels.pop_back();
        rejects([&] { (void)codec::jpeg::encode_srgb_jpeg_bytes(invalid); });
        invalid = rgb; invalid.pixels.push_back(std::byte{0});
        rejects([&] { (void)codec::jpeg::encode_srgb_jpeg_bytes(invalid); });
        invalid = rgb; invalid.width = 0;
        rejects([&] { (void)codec::jpeg::encode_srgb_jpeg_bytes(invalid); });
        std::cout << "JPEG encode/decode/options/quality: passed; bytes=" << jpeg.size()
                  << ", MAE/max/patch=" << metrics.mean_absolute_error << '/'
                  << metrics.maximum_error << '/' << metrics.patch_maximum_error << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "JPEG encoder test failed: " << error.what() << '\n';
        return 1;
    }
}

#include "codec/jpeg/jpeg_backend_policy.h"
#include "codec/jpeg/jpeg_product_selection.h"
#include <chrono>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message)
{ if (!condition) throw std::runtime_error(message); }
template<class Function> void rejects(Function function, std::string_view expected = {})
{
    try { function(); }
    catch (const std::exception& error)
    {
        require(expected.empty() || std::string_view{error.what()}.find(expected) != std::string_view::npos,
            "Unexpected policy rejection diagnostic.");
        return;
    }
    throw std::runtime_error("Invalid JPEG policy/input was accepted.");
}
}
int main()
{
    using namespace clipss2::codec::jpeg;
    try
    {
        const auto product = current_product_jpeg_backends();
        require(product.size() == 4, "Unexpected backend catalog.");
        for (const auto& entry : product)
        {
            require(entry.id != JpegBackendId::automatic, "Automatic became an adapter.");
            require(entry.available == (entry.id == JpegBackendId::turbo || entry.id == JpegBackendId::jpegli), "Wrong product availability.");
            require(!entry.diagnostic.empty(), "Missing availability context.");
        }
        for (const auto profile : {JpegProfile::fast, JpegProfile::balanced, JpegProfile::smallest})
        {
            const auto request = normalize_jpeg_profile(profile);
            require(request.quality == JpegQualityIntent::high && request.subsampling == JpegSubsamplingIntent::yuv444 &&
                request.progressive == JpegProgressivePreference::sequential_only &&
                request.matte == AlphaFlattenPolicy::opaque_srgb_white, "Profile silently changed image policy.");
            const auto selected = select_jpeg_backend(JpegBackendId::automatic, request);
            require(selected.backend == (profile == JpegProfile::fast ? JpegBackendId::turbo : JpegBackendId::jpegli), "Automatic admitted policy wrong.");
            require(selected.attempts.back().issue == JpegSelectionIssue::none, "Missing successful selection diagnostic.");
            const auto translated = translate_jpeg_request(request, product[0]);
            require(std::get<TurboJpegParameters>(translated.parameters).quality == 90 &&
                !translated.progressive && !translated.optimized_entropy, "Current effective baseline changed.");
            require(select_jpeg_backend(JpegBackendId::jpegli, request).backend == JpegBackendId::jpegli, "Manual jpegli not selected.");
            for (const auto id : {JpegBackendId::mozjpeg, JpegBackendId::wic})
                rejects([&] { (void)select_jpeg_backend(id, request); }, "unavailable");
        }

        // Hypothetical admitted-package fixture ONLY. Does not modify product availability.
        std::array<JpegBackendCapabilities, 4> admitted{};
        for (std::size_t i = 0; i < admitted.size(); ++i)
        {
            admitted[i] = product[i]; admitted[i].available = true;
            admitted[i].supports444 = admitted[i].supports422 = admitted[i].supports420 = true;
            admitted[i].supports_progressive = admitted[i].supports_optimized_entropy = true;
            admitted[i].supports_quality_control = admitted[i].supports_privacy_compatible_output = true;
        }
        const auto balanced = normalize_jpeg_profile(JpegProfile::balanced);
        const auto smallest = normalize_jpeg_profile(JpegProfile::smallest);
        require(select_jpeg_backend(JpegBackendId::automatic, normalize_jpeg_profile(JpegProfile::fast), admitted).backend ==
            JpegBackendId::turbo, "Fast order wrong.");
        require(select_jpeg_backend(JpegBackendId::automatic, balanced, admitted).backend == JpegBackendId::jpegli,
            "Balanced order wrong.");
        require(select_jpeg_backend(JpegBackendId::automatic, smallest, admitted).backend == JpegBackendId::mozjpeg,
            "Smallest order wrong.");
        for (const auto profile : {JpegProfile::fast, JpegProfile::balanced, JpegProfile::smallest})
        {
            auto fixture = admitted;
            const auto order = automatic_jpeg_fallback_order(profile);
            const auto request = normalize_jpeg_profile(profile);
            for (std::size_t i = 0; i < order.size(); ++i)
            {
                const auto selection = select_jpeg_backend(JpegBackendId::automatic, request, fixture);
                require(selection.backend == order[i] && selection.attempts.size() == i + 1, "Fallback order/context wrong.");
                for (std::size_t j = 0; j < i; ++j)
                    require(selection.attempts[j].backend == order[j] &&
                        selection.attempts[j].issue == JpegSelectionIssue::unavailable, "Fallback trace wrong.");
                for (auto& entry : fixture) if (entry.id == order[i]) entry.available = false;
            }
            rejects([&] { (void)select_jpeg_backend(JpegBackendId::automatic, request, fixture); }, "no available");
        }
        auto fixture = admitted;
        fixture[1].supports_privacy_compatible_output = false;
        require(select_jpeg_backend(JpegBackendId::automatic, balanced, fixture).backend == JpegBackendId::turbo,
            "Automatic selected privacy-ineligible backend.");
        rejects([&] { (void)select_jpeg_backend(JpegBackendId::jpegli, balanced, fixture); }, "privacy-ineligible");
        fixture = admitted; fixture[1].supports444 = false;
        rejects([&] { (void)select_jpeg_backend(JpegBackendId::jpegli, balanced, fixture); }, "subsampling");
        fixture = admitted; fixture[1].supports_quality_control = false;
        rejects([&] { (void)select_jpeg_backend(JpegBackendId::jpegli, balanced, fixture); }, "quality control");

        auto request = balanced; request.progressive = JpegProgressivePreference::require_progressive;
        rejects([&] { (void)select_jpeg_backend(JpegBackendId::automatic, request); }, "no available");
        rejects([&] { (void)select_jpeg_backend(JpegBackendId::turbo, request); }, "progressive");
        request.progressive = JpegProgressivePreference::prefer_progressive;
        require(!translate_jpeg_request(request, product[0]).progressive, "Preferred mode did not degrade explicitly.");
        request.subsampling = JpegSubsamplingIntent::yuv422;
        rejects([&] { (void)select_jpeg_backend(JpegBackendId::turbo, request); }, "subsampling");
        request.subsampling = JpegSubsamplingIntent::yuv420;
        rejects([&] { (void)select_jpeg_backend(JpegBackendId::turbo, request); }, "subsampling");

        constexpr std::array qualities{90, 85, 75};
        constexpr std::array distances{1.0F, 1.5F, 3.0F};
        constexpr std::array floats{0.90F, 0.85F, 0.75F};
        for (std::size_t i = 0; i < qualities.size(); ++i)
        {
            request = smallest; request.quality = static_cast<JpegQualityIntent>(i);
            request.progressive = JpegProgressivePreference::prefer_progressive;
            const auto t = translate_jpeg_request(request, admitted[0]);
            const auto j = translate_jpeg_request(request, admitted[1]);
            const auto m = translate_jpeg_request(request, admitted[2]);
            const auto w = translate_jpeg_request(request, admitted[3]);
            require(std::get<TurboJpegParameters>(t.parameters).quality == qualities[i] &&
                std::get<JpegliParameters>(j.parameters).distance == distances[i] &&
                std::get<MozJpegParameters>(m.parameters).quality == qualities[i] &&
                std::get<MozJpegParameters>(m.parameters).native_size_optimization &&
                std::get<WicJpegParameters>(w.parameters).image_quality == floats[i], "Typed translation mismatch.");
            require(t.progressive && t.optimized_entropy, "Supported intent not translated.");
        }
        request = balanced; request.profile = static_cast<JpegProfile>(99);
        rejects([&] { validate_normalized_jpeg_request(request); });
        request = balanced; request.quality = static_cast<JpegQualityIntent>(99);
        rejects([&] { (void)translate_jpeg_request(request, admitted[0]); });
        request = balanced; request.optimization = JpegOptimizationIntent::speed;
        rejects([&] { validate_normalized_jpeg_request(request); });
        request = balanced; request.matte = static_cast<AlphaFlattenPolicy>(99);
        rejects([&] { validate_normalized_jpeg_request(request); });
        request = balanced; request.subsampling = static_cast<JpegSubsamplingIntent>(99);
        rejects([&] { validate_normalized_jpeg_request(request); });
        request = balanced; request.progressive = static_cast<JpegProgressivePreference>(99);
        rejects([&] { validate_normalized_jpeg_request(request); });
        rejects([&] { (void)select_jpeg_backend(static_cast<JpegBackendId>(99), balanced); });
        fixture = admitted; fixture[1].id = fixture[0].id;
        rejects([&] { (void)select_jpeg_backend(JpegBackendId::automatic, balanced, fixture); }, "Duplicate");
        fixture = admitted; fixture[1].id = JpegBackendId::automatic;
        rejects([&] { (void)select_jpeg_backend(JpegBackendId::automatic, balanced, fixture); }, "not a JPEG adapter");
        fixture = admitted; fixture[0].requires_isolated_process = true;
        rejects([&] { (void)select_jpeg_backend(JpegBackendId::automatic, balanced, fixture); }, "Contradictory");
        rejects([&] { (void)select_jpeg_backend(JpegBackendId::automatic, balanced, {}); }, "no available");

        std::array<std::byte, 12> pixels{};
        validate_canonical_jpeg_rgb8({2, 2, 6, pixels});
        std::array<std::byte, 14> padded{};
        validate_canonical_jpeg_rgb8({2, 2, 8, padded});
        rejects([&] { validate_canonical_jpeg_rgb8({2, 2, 5, pixels}); });
        rejects([&] { validate_canonical_jpeg_rgb8({2, 2, 8, pixels}); });
        rejects([&] { validate_canonical_jpeg_rgb8({0, 2, 6, pixels}); });
        rejects([&] { validate_canonical_jpeg_rgb8({2, 2, std::numeric_limits<std::size_t>::max(), pixels}); });
        rejects([&] { validate_canonical_jpeg_rgb8({2, 2, maximum_rgb_bytes, pixels}); });
        const auto compatible = resolve_jpeg_product_selection(JpegCurrentDefault{});
        require(compatible.selection.backend == JpegBackendId::turbo &&
            compatible.request.profile == JpegProfile::current_default &&
            std::get<TurboJpegParameters>(compatible.effective.parameters).quality == 90 &&
            compatible.effective.subsampling == JpegSubsamplingIntent::yuv444 &&
            !compatible.effective.progressive && !compatible.effective.optimized_entropy,
            "Product CurrentDefault changed or aliased a user profile.");
        for (const auto profile : {JpegProfile::fast, JpegProfile::balanced, JpegProfile::smallest})
        {
            const auto resolved = resolve_jpeg_product_selection(JpegProfileSelection{profile, JpegBackendId::automatic});
            require(resolved.selection.backend == (profile == JpegProfile::fast ? JpegBackendId::turbo : JpegBackendId::jpegli),
                "Product Automatic policy wrong.");
        }
        require(resolve_jpeg_product_selection(JpegProfileSelection{JpegProfile::fast, JpegBackendId::turbo}).selection.backend ==
            JpegBackendId::turbo, "Product manual Turbo wrong.");
        require(resolve_jpeg_product_selection(JpegProfileSelection{JpegProfile::balanced, JpegBackendId::jpegli}).selection.backend ==
            JpegBackendId::jpegli, "Product manual jpegli wrong.");
        for (const auto backend : {JpegBackendId::mozjpeg, JpegBackendId::wic})
            rejects([&] { (void)resolve_jpeg_product_selection(JpegProfileSelection{JpegProfile::balanced, backend}); }, "unavailable");
        rejects([&] { (void)resolve_jpeg_product_selection(JpegProfileSelection{JpegProfile::balanced, JpegBackendId::turbo}); }, "unsupported");
        rejects([&] { (void)resolve_jpeg_product_selection(JpegProfileSelection{JpegProfile::fast, JpegBackendId::jpegli}); }, "unsupported");
        rejects([&] { (void)resolve_jpeg_product_selection(JpegProfileSelection{JpegProfile::current_default, JpegBackendId::turbo}); });
        rejects([&] { (void)resolve_jpeg_product_selection(JpegProfileSelection{static_cast<JpegProfile>(99), JpegBackendId::automatic}); });
        rejects([&] { (void)resolve_jpeg_product_selection(JpegProfileSelection{JpegProfile::fast, static_cast<JpegBackendId>(99)}); });
        // Hypothetical catalog must explicitly approve fallback translations;
        // this does not admit those settings/encoders to the real product.
        fixture = admitted;
        for (auto& entry : fixture) entry.approved_product_profiles = 15U;
        fixture[1].available = fixture[2].available = false;
        for (const auto profile : {JpegProfile::balanced, JpegProfile::smallest})
            require(resolve_jpeg_product_selection(JpegProfileSelection{profile, JpegBackendId::automatic}, fixture).selection.backend ==
                JpegBackendId::turbo, "Product fallback simulation wrong.");
        fixture[0].available = false;
        rejects([&] { (void)resolve_jpeg_product_selection(JpegCurrentDefault{}, fixture); }, "selection failed");
        require(resolve_jpeg_product_selection(JpegProfileSelection{JpegProfile::smallest, JpegBackendId::automatic}, fixture).selection.backend ==
            JpegBackendId::wic, "WIC final fallback simulation wrong.");
        fixture = admitted;
        for (auto& entry : fixture) entry.approved_product_profiles = 15U;
        fixture[1].approved_product_profiles = 0;
        const auto skipped = resolve_jpeg_product_selection(JpegProfileSelection{JpegProfile::balanced, JpegBackendId::automatic}, fixture);
        require(skipped.selection.backend == JpegBackendId::turbo &&
            skipped.selection.attempts.front().issue == JpegSelectionIssue::unsupported_product_profile, "Missing profile translation was not skipped.");
        rejects([&] { (void)resolve_jpeg_product_selection(JpegProfileSelection{JpegProfile::balanced, JpegBackendId::jpegli}, fixture); }, "unsupported");
        const auto begin = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < 10000; ++i) (void)resolve_jpeg_product_selection(JpegProfileSelection{JpegProfile::balanced, JpegBackendId::automatic});
        std::cout << "Product selection 10000 resolutions ms: " << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-begin).count() << '\n';
        std::cout << "JPEG contract/catalog/Automatic/fallback/manual/capabilities/intents/typed/product translation: passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

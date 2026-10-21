#include "codec/jpeg/jpeg_backend_policy.h"
#include "codec/jpeg/jpeg_product_selection.h"

#include <array>
#include <limits>
#include <stdexcept>

namespace clipss2::codec::jpeg
{
namespace
{
constexpr std::array product_catalog{
    JpegBackendCapabilities{JpegBackendId::turbo, true, true, false, false, false, false,
        true, true, true, false, "Current Q90/444/sequential adapter; upstream has additional modes.", 1U | 8U},
    JpegBackendCapabilities{JpegBackendId::jpegli, true, true, false, false, false, false,
        true, true, true, false, "Pinned MSVC private prefixed RGB8/444/sequential adapter; distance intent mapping.", 2U | 4U},
    JpegBackendCapabilities{JpegBackendId::mozjpeg, false, false, false, false, false, false,
        false, false, false, true, "Benchmark only; libjpeg symbols/ABI require managed process isolation."},
    JpegBackendCapabilities{JpegBackendId::wic, false, false, false, false, false, false,
        false, false, true, false, "OS COM can coexist, but no product adapter/technical allowlist yet."}
};
constexpr std::array fast_order{JpegBackendId::turbo, JpegBackendId::wic};
constexpr std::array current_default_order{JpegBackendId::turbo};
constexpr std::array balanced_order{JpegBackendId::jpegli, JpegBackendId::turbo, JpegBackendId::wic};
constexpr std::array smallest_order{JpegBackendId::mozjpeg, JpegBackendId::jpegli, JpegBackendId::turbo, JpegBackendId::wic};

void validate_catalog(std::span<const JpegBackendCapabilities> catalog)
{
    std::array<bool, 4> seen{};
    for (const auto& entry : catalog)
    {
        (void)jpeg_backend_name(entry.id);
        if (entry.id == JpegBackendId::automatic) throw std::invalid_argument("Automatic is not a JPEG adapter.");
        const auto index = static_cast<std::size_t>(entry.id) - 1;
        if (seen[index]) throw std::invalid_argument("Duplicate JPEG backend capability entry.");
        seen[index] = true;
        if (entry.can_coexist_in_process && entry.requires_isolated_process)
            throw std::invalid_argument("Contradictory JPEG deployment capabilities.");
    }
}
JpegSelectionIssue eligibility(const NormalizedJpegRequest& request, const JpegBackendCapabilities* backend)
{
    if (!backend || !backend->available) return JpegSelectionIssue::unavailable;
    if (!backend->supports_privacy_compatible_output) return JpegSelectionIssue::privacy_ineligible;
    if (!backend->supports_quality_control) return JpegSelectionIssue::unsupported_quality_control;
    const bool sampling = request.subsampling == JpegSubsamplingIntent::yuv444 ? backend->supports444 :
        request.subsampling == JpegSubsamplingIntent::yuv422 ? backend->supports422 : backend->supports420;
    if (!sampling) return JpegSelectionIssue::unsupported_subsampling;
    if (request.progressive == JpegProgressivePreference::require_progressive && !backend->supports_progressive)
        return JpegSelectionIssue::unsupported_progressive;
    return JpegSelectionIssue::none;
}
const char* issue_name(JpegSelectionIssue issue)
{
    switch (issue)
    {
    case JpegSelectionIssue::unavailable: return "unavailable";
    case JpegSelectionIssue::privacy_ineligible: return "privacy-ineligible";
    case JpegSelectionIssue::unsupported_subsampling: return "unsupported subsampling";
    case JpegSelectionIssue::unsupported_progressive: return "unsupported required progressive";
    case JpegSelectionIssue::unsupported_quality_control: return "unsupported quality control";
    case JpegSelectionIssue::none: return "eligible";
    case JpegSelectionIssue::unsupported_product_profile: return "profile/backend combination unsupported";
    }
    throw std::invalid_argument("Invalid JPEG selection issue.");
}
}

const char* jpeg_backend_name(JpegBackendId backend)
{
    switch (backend)
    {
    case JpegBackendId::automatic: return "Automatic";
    case JpegBackendId::turbo: return "libjpeg-turbo";
    case JpegBackendId::jpegli: return "jpegli";
    case JpegBackendId::mozjpeg: return "MozJPEG";
    case JpegBackendId::wic: return "Windows WIC";
    }
    throw std::invalid_argument("Invalid JPEG backend id.");
}
std::span<const JpegBackendCapabilities> current_product_jpeg_backends() noexcept { return product_catalog; }
const char* jpeg_profile_token(JpegProfile profile)
{
    switch (profile)
    {
    case JpegProfile::fast: return "fast";
    case JpegProfile::balanced: return "balanced";
    case JpegProfile::smallest: return "smallest";
    case JpegProfile::current_default: return "current-default";
    }
    throw std::invalid_argument("Unknown JPEG profile.");
}
void validate_jpeg_product_selection(const JpegProductSelection& selection)
{
    if (selection.valueless_by_exception()) throw std::invalid_argument("Empty JPEG selection.");
    if (const auto* explicit_selection = std::get_if<JpegProfileSelection>(&selection))
    {
        (void)jpeg_profile_token(explicit_selection->profile);
        (void)jpeg_backend_name(explicit_selection->backend);
        if (explicit_selection->profile == JpegProfile::current_default)
            throw std::invalid_argument("CurrentDefault is not a user profile; no manual override allowed.");
    }
}
ResolvedJpegProductSelection resolve_jpeg_product_selection(const JpegProductSelection& input,
    std::span<const JpegBackendCapabilities> catalog)
{
    validate_jpeg_product_selection(input);
    validate_catalog(catalog);
    const auto* explicit_selection = std::get_if<JpegProfileSelection>(&input);
    const auto profile = explicit_selection ? explicit_selection->profile : JpegProfile::current_default;
    const auto preference = explicit_selection ? explicit_selection->backend : JpegBackendId::automatic;
    const auto request = normalize_jpeg_profile(profile);
    JpegBackendSelection selected;
    std::string diagnostic = "JPEG selection mode: " + std::string{explicit_selection ? "profile" : "current-default"} +
        "; resolved profile: " + jpeg_profile_token(profile) + "; backend preference: " + jpeg_backend_name(preference) + ".";
    const JpegBackendCapabilities* chosen{};
    const auto consider = [&](JpegBackendId id) {
        const JpegBackendCapabilities* capability{};
        for (const auto& entry : catalog) if (entry.id == id) capability = &entry;
        auto issue = eligibility(request, capability);
        if (issue == JpegSelectionIssue::none && !(capability->approved_product_profiles & jpeg_product_profile_bit(profile)))
            issue = JpegSelectionIssue::unsupported_product_profile;
        selected.attempts.push_back({id, issue});
        if (issue == JpegSelectionIssue::none) { selected.backend = id; chosen = capability; return true; }
        diagnostic += " " + std::string{jpeg_backend_name(id)} + " skipped: " + issue_name(issue) + ".";
        return false;
    };
    if (preference != JpegBackendId::automatic)
    {
        if (!consider(preference)) throw std::invalid_argument("JPEG selection failed: manual " +
            std::string{jpeg_backend_name(preference)} + ": " + issue_name(selected.attempts.back().issue));
    }
    else
    {
        for (const auto id : automatic_jpeg_fallback_order(profile)) if (consider(id)) break;
        if (!chosen) throw std::invalid_argument("JPEG selection failed: no available approved profile translation. " + diagnostic);
    }
    auto effective = translate_jpeg_request(request, *chosen);
    diagnostic += " Selected concrete backend: " + std::string{jpeg_backend_name(selected.backend)} + ".";
    return {request, std::move(selected), std::move(effective), std::move(diagnostic)};
}
std::span<const JpegBackendId> automatic_jpeg_fallback_order(JpegProfile profile)
{
    switch (profile)
    {
    case JpegProfile::fast: return fast_order;
    case JpegProfile::balanced: return balanced_order;
    case JpegProfile::smallest: return smallest_order;
    case JpegProfile::current_default: return current_default_order;
    }
    throw std::invalid_argument("Invalid JPEG profile.");
}
NormalizedJpegRequest normalize_jpeg_profile(JpegProfile profile)
{
    (void)automatic_jpeg_fallback_order(profile);
    NormalizedJpegRequest result;
    result.profile = profile;
    result.optimization = profile == JpegProfile::fast || profile == JpegProfile::current_default ? JpegOptimizationIntent::speed :
        profile == JpegProfile::balanced ? JpegOptimizationIntent::balanced : JpegOptimizationIntent::size;
    return result; // Profiles change effort, not silently quality, sampling or matte.
}
NormalizedJpegRequest current_product_jpeg_request()
{ return normalize_jpeg_profile(JpegProfile::current_default); }
void validate_normalized_jpeg_request(const NormalizedJpegRequest& request)
{
    const auto normalized = normalize_jpeg_profile(request.profile);
    if (request.optimization != normalized.optimization)
        throw std::invalid_argument("JPEG profile/optimization intent mismatch.");
    switch (request.quality)
    {
    case JpegQualityIntent::high: case JpegQualityIntent::balanced: case JpegQualityIntent::compact: break;
    default: throw std::invalid_argument("Invalid JPEG quality intent.");
    }
    switch (request.subsampling)
    {
    case JpegSubsamplingIntent::yuv444: case JpegSubsamplingIntent::yuv422: case JpegSubsamplingIntent::yuv420: break;
    default: throw std::invalid_argument("Invalid JPEG subsampling intent.");
    }
    switch (request.progressive)
    {
    case JpegProgressivePreference::sequential_only: case JpegProgressivePreference::prefer_progressive:
    case JpegProgressivePreference::require_progressive: break;
    default: throw std::invalid_argument("Invalid JPEG progressive preference.");
    }
    if (request.matte != AlphaFlattenPolicy::opaque_srgb_white)
        throw std::invalid_argument("Unsupported upstream JPEG matte policy.");
}
void validate_canonical_jpeg_rgb8(CanonicalJpegRgb8View source)
{
    (void)checked_rgb_image_bytes(source.width, source.height);
    const auto row = static_cast<std::size_t>(source.width) * 3;
    if (source.stride < row || source.stride > maximum_rgb_bytes)
        throw std::invalid_argument("Invalid canonical JPEG RGB stride.");
    const auto preceding = static_cast<std::size_t>(source.height) - 1;
    if (preceding > (std::numeric_limits<std::size_t>::max() - row) / source.stride)
        throw std::overflow_error("Canonical JPEG RGB extent overflow.");
    const auto extent = preceding * source.stride + row;
    if (extent > maximum_rgb_bytes || source.pixels.size() != extent)
        throw std::invalid_argument("Canonical JPEG RGB extent exceeds limits or differs from buffer.");
}
JpegBackendSelection select_jpeg_backend(JpegBackendId override_backend,
    const NormalizedJpegRequest& request, std::span<const JpegBackendCapabilities> catalog)
{
    (void)jpeg_backend_name(override_backend);
    validate_normalized_jpeg_request(request); validate_catalog(catalog);
    JpegBackendSelection result;
    const auto consider = [&](JpegBackendId id)
    {
        const JpegBackendCapabilities* backend{};
        for (const auto& candidate : catalog) if (candidate.id == id) backend = &candidate;
        const auto issue = eligibility(request, backend);
        result.attempts.push_back({id, issue});
        if (issue == JpegSelectionIssue::none) { result.backend = id; return true; }
        return false;
    };
    if (override_backend != JpegBackendId::automatic)
    {
        if (!consider(override_backend)) throw std::invalid_argument(std::string{"Manual JPEG backend "} +
            jpeg_backend_name(override_backend) + ": " + issue_name(result.attempts.back().issue));
        return result; // No manual substitution, even to Turbo.
    }
    for (const auto id : automatic_jpeg_fallback_order(request.profile)) if (consider(id)) return result;
    throw std::invalid_argument("Automatic JPEG selection: no available privacy-compatible backend meets the request.");
}
JpegEffectiveSettings translate_jpeg_request(const NormalizedJpegRequest& request, const JpegBackendCapabilities& backend)
{
    (void)select_jpeg_backend(backend.id, request, std::span{&backend, 1});
    // Provisional representative mappings, NOT equal-quality calibration.
    // Kept here exclusively, outside capture orchestration/publication.
    constexpr std::array classic_quality{90, 85, 75};
    constexpr std::array jpegli_distance{1.0F, 1.5F, 3.0F};
    constexpr std::array wic_quality{0.90F, 0.85F, 0.75F};
    const auto index = static_cast<std::size_t>(request.quality);
    JpegEffectiveSettings result{backend.id, request.subsampling,
        request.progressive != JpegProgressivePreference::sequential_only && backend.supports_progressive,
        request.optimization != JpegOptimizationIntent::speed && backend.supports_optimized_entropy,
        TurboJpegParameters{classic_quality[index]}, "Provisional intent mapping; not matched measured quality."};
    switch (backend.id)
    {
    case JpegBackendId::turbo: result.parameters = TurboJpegParameters{classic_quality[index]}; break;
    case JpegBackendId::jpegli: result.parameters = JpegliParameters{jpegli_distance[index]}; break;
    case JpegBackendId::mozjpeg: result.parameters = MozJpegParameters{classic_quality[index],
        request.optimization == JpegOptimizationIntent::size && backend.supports_optimized_entropy}; break;
    case JpegBackendId::wic: result.parameters = WicJpegParameters{wic_quality[index]}; break;
    default: throw std::invalid_argument("Automatic has no JPEG parameter translation.");
    }
    if (request.progressive == JpegProgressivePreference::prefer_progressive && !result.progressive)
        result.diagnostic += " Preferred progressive unavailable; effective sequential.";
    if (request.optimization != JpegOptimizationIntent::speed && !result.optimized_entropy)
        result.diagnostic += " Optimization preference unavailable; effective baseline entropy.";
    return result;
}
}

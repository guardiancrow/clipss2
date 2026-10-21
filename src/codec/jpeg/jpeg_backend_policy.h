#pragma once

#include "codec/jpeg/jpeg_codec.h"
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace clipss2::codec::jpeg
{
// Automatic is a policy choice, never an encoder implementation/catalog entry.
enum class JpegBackendId { automatic, turbo, jpegli, mozjpeg, wic };
enum class JpegProfile { fast, balanced, smallest, current_default }; // Internal legacy intent, not UI profile.
enum class JpegQualityIntent { high, balanced, compact };
enum class JpegSubsamplingIntent { yuv444, yuv422, yuv420 };
enum class JpegProgressivePreference { sequential_only, prefer_progressive, require_progressive };
enum class JpegOptimizationIntent { speed, balanced, size };

struct NormalizedJpegRequest
{
    JpegProfile profile{JpegProfile::balanced};
    JpegQualityIntent quality{JpegQualityIntent::high};
    JpegSubsamplingIntent subsampling{JpegSubsamplingIntent::yuv444};
    JpegProgressivePreference progressive{JpegProgressivePreference::sequential_only};
    JpegOptimizationIntent optimization{JpegOptimizationIntent::balanced};
    AlphaFlattenPolicy matte{AlphaFlattenPolicy::opaque_srgb_white}; // Already applied upstream.
};

// Non-owning, synchronous call only. Caller keeps bytes alive throughout encode.
// Opaque top-down R,G,B, 8-bit, sRGB-assumed; padding is not image content.
// Extent is (height-1)*stride + width*3, with no required final-row padding.
struct CanonicalJpegRgb8View
{
    std::uint32_t width{}, height{};
    std::size_t stride{};
    std::span<const std::byte> pixels;
};
void validate_canonical_jpeg_rgb8(CanonicalJpegRgb8View source);

// These describe an integrated adapter, NOT an upstream library's potential.
// Available means packaged, implemented and admitted to the product registry.
struct JpegBackendCapabilities
{
    JpegBackendId id{JpegBackendId::turbo};
    bool available{};
    bool supports444{}, supports422{}, supports420{};
    bool supports_progressive{}, supports_optimized_entropy{}, supports_quality_control{};
    bool supports_privacy_compatible_output{};
    bool can_coexist_in_process{}, requires_isolated_process{};
    std::string_view diagnostic;
    unsigned int approved_product_profiles{}; // Explicit admission, not native potential.
};

enum class JpegSelectionIssue { none, unavailable, privacy_ineligible, unsupported_subsampling,
    unsupported_progressive, unsupported_quality_control, unsupported_product_profile };
struct JpegSelectionAttempt { JpegBackendId backend; JpegSelectionIssue issue; };
struct JpegBackendSelection
{
    JpegBackendId backend{JpegBackendId::turbo};
    std::vector<JpegSelectionAttempt> attempts; // Ordered, including selected backend with issue=none.
};

// Strongly typed raw controls: numerical similarity does NOT mean equal quality.
struct TurboJpegParameters { int quality; };
struct JpegliParameters { float distance; };
struct MozJpegParameters { int quality; bool native_size_optimization; };
struct WicJpegParameters { float image_quality; };
using JpegRawParameters = std::variant<TurboJpegParameters, JpegliParameters, MozJpegParameters, WicJpegParameters>;
struct JpegEffectiveSettings
{
    JpegBackendId backend;
    JpegSubsamplingIntent subsampling;
    bool progressive{}, optimized_entropy{};
    JpegRawParameters parameters;
    std::string diagnostic; // Fixed policy context only; no content, title or path.
};

// Future adapter boundary: canonical view + effective settings -> result.
// Unpublished bytes must still pass common privacy/structure/decode validation.
// No filesystem/clipboard/WGC/state ownership, no asynchronous retention of view.
struct JpegBackendResult
{
    std::vector<std::byte> bytes;
    JpegBackendId backend;
    JpegEffectiveSettings effective;
    std::string diagnostic;
};

[[nodiscard]] const char* jpeg_backend_name(JpegBackendId backend);
[[nodiscard]] std::span<const JpegBackendCapabilities> current_product_jpeg_backends() noexcept;
[[nodiscard]] std::span<const JpegBackendId> automatic_jpeg_fallback_order(JpegProfile profile);
[[nodiscard]] NormalizedJpegRequest normalize_jpeg_profile(JpegProfile profile);
[[nodiscard]] NormalizedJpegRequest current_product_jpeg_request();
void validate_normalized_jpeg_request(const NormalizedJpegRequest& request);
// Caller-supplied capability snapshots allow pure tests/future admitted packages;
// the default snapshot NEVER advertises benchmark-only candidates as available.
[[nodiscard]] JpegBackendSelection select_jpeg_backend(
    JpegBackendId override_backend, const NormalizedJpegRequest& request,
    std::span<const JpegBackendCapabilities> catalog = current_product_jpeg_backends());
[[nodiscard]] JpegEffectiveSettings translate_jpeg_request(
    const NormalizedJpegRequest& request, const JpegBackendCapabilities& backend);
}

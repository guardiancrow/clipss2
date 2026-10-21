#pragma once
#include "codec/jpeg/jpeg_backend_policy.h"
#include <stdexcept>

namespace clipss2::codec::jpeg
{
// Compatibility state is not an alias for any user profile and has no override.
struct JpegCurrentDefault { bool operator==(const JpegCurrentDefault&) const = default; };
struct JpegProfileSelection
{
    JpegProfile profile{JpegProfile::fast}; // Only fast/balanced/smallest allowed.
    JpegBackendId backend{JpegBackendId::automatic};
    bool operator==(const JpegProfileSelection&) const = default;
};
using JpegProductSelection = std::variant<JpegCurrentDefault, JpegProfileSelection>;
constexpr unsigned int jpeg_product_profile_bit(JpegProfile profile)
{
    switch (profile)
    {
    case JpegProfile::fast: return 1U;
    case JpegProfile::balanced: return 2U;
    case JpegProfile::smallest: return 4U;
    case JpegProfile::current_default: return 8U;
    }
    throw std::invalid_argument("Unknown JPEG product profile.");
}
struct ResolvedJpegProductSelection
{
    NormalizedJpegRequest request;
    JpegBackendSelection selection;
    JpegEffectiveSettings effective;
    std::string diagnostic;
};
void validate_jpeg_product_selection(const JpegProductSelection& selection);
[[nodiscard]] const char* jpeg_profile_token(JpegProfile profile);
[[nodiscard]] ResolvedJpegProductSelection resolve_jpeg_product_selection(
    const JpegProductSelection& selection,
    std::span<const JpegBackendCapabilities> catalog = current_product_jpeg_backends());
}

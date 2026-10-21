#pragma once
#include <stdexcept>
#include <string_view>

namespace clipss2::codec
{
// AVIF/JXL profiles only. Numeric policy is codec-specific, not universal quality.
enum class LossyProfile { high, balanced, small_file };
inline const char* lossy_profile_token(LossyProfile profile)
{
    switch (profile)
    {
    case LossyProfile::high: return "high";
    case LossyProfile::balanced: return "balanced";
    case LossyProfile::small_file: return "small";
    }
    throw std::invalid_argument("Invalid private lossy profile.");
}
inline LossyProfile parse_lossy_profile(std::string_view token)
{
    if (token == "high") return LossyProfile::high;
    if (token == "balanced") return LossyProfile::balanced;
    if (token == "small") return LossyProfile::small_file;
    throw std::invalid_argument("Unknown private lossy profile.");
}
}

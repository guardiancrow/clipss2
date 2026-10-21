#pragma once

#include <cstdint>
#include <string_view>

namespace clipss2::platform::windows
{
// Evidence from two independent APIs, never from an inferred/default SDR value alone.
struct FallbackColorEvidence
{
    unsigned matching_paths{};
    unsigned matching_outputs{};
    bool identities_match{};
    bool edid_ids_valid{};
    bool stable{};
    std::uint32_t advanced_flags{};
    unsigned color_encoding{};
    unsigned display_bits{};
    unsigned dxgi_bits{};
    unsigned dxgi_color_space{};
};

inline std::string_view fallback_color_kind(const FallbackColorEvidence& e) noexcept
{
    if (e.matching_paths != 1 || e.matching_outputs != 1 || !e.identities_match ||
        !e.edid_ids_valid || !e.stable || (e.advanced_flags & ~15u) != 0)
        return "Unknown";
    if (e.advanced_flags & 4u) return "WideColorGamut";
    // G2084/P2020 is 12. A contradictory enabled/unsupported report is unknown.
    if ((e.advanced_flags & 2u) && (!(e.advanced_flags & 1u) || (e.advanced_flags & 8u))) return "Unknown";
    if (e.dxgi_color_space == 12u && (e.advanced_flags & 2u)) return "HighDynamicRange";
    // Enabled advanced color with an SDR DXGI descriptor could be WCG/ACM.
    if ((e.advanced_flags & 2u) || e.dxgi_color_space != 0u) return "Unknown";
    if (e.color_encoding != 0u || e.display_bits < 8 || e.display_bits > 16 ||
        e.display_bits != e.dxgi_bits) return "Unknown";
    return "StandardDynamicRange";
}
}

#pragma once
#include "codec/avif/avif_validator.h"

namespace clipss2::codec::avif
{
// Managed libavif/AOM SDR still-output subset, not a general AVIF validator.
// Full decode + exact source alpha; does not attest encoder quality/speed.
[[nodiscard]] AvifInspection validate_screenshot_avif_lossy(
    std::span<const std::byte> bytes, const image::CpuImage& canonical_straight_bgra8);
}

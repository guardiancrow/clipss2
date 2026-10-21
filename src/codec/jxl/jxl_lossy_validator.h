#pragma once
#include "codec/jxl/jxl_validator.h"

namespace clipss2::codec::jxl
{
// Naked normal-mode SDR still + full decode + exact trusted-source alpha.
// Does not attest encoder distance/effort or perceptual quality.
[[nodiscard]] JxlInspection validate_screenshot_jxl_lossy(
    std::span<const std::byte> bytes, const image::CpuImage& canonical_straight_bgra8);
}

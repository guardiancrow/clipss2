#pragma once

namespace clipss2::image
{
// Reference EOTF for normalized SDR sRGB samples; rejects non-finite/out-of-range input.
[[nodiscard]] double srgb_encoded_to_linear(double encoded);
// Two binary16 ULPs: one for storage rounding and one for capture arithmetic.
[[nodiscard]] double srgb_fp16_tolerance(double linear);
}

#include "image/srgb_transfer.h"
#include <cmath>
#include <stdexcept>

namespace clipss2::image
{
double srgb_encoded_to_linear(double encoded)
{
    if (!std::isfinite(encoded) || encoded < 0 || encoded > 1)
        throw std::invalid_argument("sRGB reference input must be finite and in [0,1].");
    return encoded <= 0.04045 ? encoded / 12.92
                             : std::pow((encoded + 0.055) / 1.055, 2.4);
}

double srgb_fp16_tolerance(double linear)
{
    if (!std::isfinite(linear) || linear < 0 || linear > 1)
        throw std::invalid_argument("Tolerance reference must be finite and in [0,1].");
    // Subnormal spacing is 2^-24. Normal spacing is 2^(floor(log2(L))-10).
    if (linear < 0x1p-14)
        return 0x1p-23;
    return 2 * std::ldexp(1.0, std::ilogb(linear) - 10);
}
}

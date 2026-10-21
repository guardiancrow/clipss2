#include "image/srgb_transfer.h"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

int main()
{
    // Independently tabulated reference values, not calculated with the tested function.
    constexpr std::array inputs{0., 1./255, 10./255, 11./255, 12./255,
        25./255, 128./255, 1., 0.04045, 0.04046};
    constexpr std::array expected{0., 0.0003035269835488375,
        0.003035269835488375, 0.003346535763899161,
        0.003676507324047436, 0.00972121732023785,
        0.21586050011389926, 1., 0.0031308049535603713,
        0.003131594552688991};
    for (std::size_t i = 0; i < inputs.size(); ++i)
        if (std::abs(clipss2::image::srgb_encoded_to_linear(inputs[i]) - expected[i]) > 1e-14)
        {
            std::cerr << "EOTF reference mismatch at " << i << '\n';
            return 1;
        }
    using clipss2::image::srgb_fp16_tolerance;
    if (srgb_fp16_tolerance(0) != 0x1p-23 ||
        srgb_fp16_tolerance(0x1p-14) != 0x1p-23 ||
        srgb_fp16_tolerance(0.5) != 0x1p-10 ||
        srgb_fp16_tolerance(1) != 0x1p-9)
        return 1;
    for (double bad : {-1., 2., std::numeric_limits<double>::quiet_NaN()})
    {
        try { static_cast<void>(clipss2::image::srgb_encoded_to_linear(bad)); return 1; }
        catch (const std::invalid_argument&) {}
    }
    std::cout << "sRGB EOTF/reference/boundary/tolerance tests: passed\n";
}

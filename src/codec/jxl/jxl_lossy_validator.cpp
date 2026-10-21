#include "codec/jxl/jxl_lossy_validator.h"
#include "codec/jxl/jxl_internal.h"
#include "codec/lossy_validation.h"

namespace clipss2::codec::jxl
{
JxlInspection validate_screenshot_jxl_lossy(std::span<const std::byte> bytes, const image::CpuImage& source)
{
    (void)codec::detail::check_lossy_source(source);
    const auto decoded = detail::decode_and_inspect(bytes, detail::DecodePolicy::lossy, &source);
    codec::detail::check_lossy_alpha(decoded.pixels, source);
    return decoded.inspection;
}
}

#pragma once
#include "codec/jxl/jxl_validator.h"

namespace clipss2::codec::jxl::detail
{
struct DecodedJxl { JxlInspection inspection; image::CpuImage pixels; };
enum class DecodePolicy { lossless, lossy };
[[nodiscard]] DecodedJxl decode_and_inspect(std::span<const std::byte> bytes,
    DecodePolicy policy = DecodePolicy::lossless, const image::CpuImage* expected_source = nullptr);
}

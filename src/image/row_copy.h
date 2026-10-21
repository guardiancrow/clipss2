#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace clipss2::image
{
[[nodiscard]] std::vector<std::byte> copy_rows_contiguous(
    std::span<const std::byte> source,
    std::size_t row_pitch,
    std::size_t logical_row_bytes,
    std::size_t height);
}

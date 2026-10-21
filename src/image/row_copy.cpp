#include "image/row_copy.h"

#include <cstring>
#include <limits>
#include <stdexcept>

namespace clipss2::image
{
namespace
{
std::size_t checked_multiply(std::size_t left, std::size_t right)
{
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left)
    {
        throw std::overflow_error("Row-copy size calculation overflowed.");
    }
    return left * right;
}
}

std::vector<std::byte> copy_rows_contiguous(
    std::span<const std::byte> source,
    std::size_t row_pitch,
    std::size_t logical_row_bytes,
    std::size_t height)
{
    if (height == 0 || logical_row_bytes == 0)
    {
        throw std::invalid_argument("Row-copy dimensions must be non-zero.");
    }
    if (row_pitch < logical_row_bytes)
    {
        throw std::invalid_argument("Mapped row pitch is smaller than the logical row size.");
    }

    const std::size_t source_bytes = checked_multiply(row_pitch, height);
    const std::size_t destination_bytes = checked_multiply(logical_row_bytes, height);
    if (source.size() < source_bytes)
    {
        throw std::invalid_argument("Source span is smaller than its pitched dimensions.");
    }

    std::vector<std::byte> destination(destination_bytes);
    for (std::size_t y = 0; y < height; ++y)
    {
        std::memcpy(
            destination.data() + y * logical_row_bytes,
            source.data() + y * row_pitch,
            logical_row_bytes);
    }

    if (destination.size() != destination_bytes)
    {
        throw std::runtime_error("Contiguous row-copy produced an unexpected buffer size.");
    }
    return destination;
}
}

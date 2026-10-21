#include "image/row_copy.h"

#include <array>
#include <cstddef>
#include <iostream>
#include <span>

int main()
{
    {
        constexpr std::size_t logical_row_bytes = 8;
        constexpr std::size_t row_pitch = 12;
        constexpr std::size_t height = 2;
        constexpr std::byte sentinel{0xee};

        std::array<std::byte, row_pitch * height> source{};
        source.fill(sentinel);
        for (std::size_t index = 0; index < logical_row_bytes; ++index)
        {
            source[index] = static_cast<std::byte>(index + 1);
            source[row_pitch + index] = static_cast<std::byte>(index + 9);
        }

        const auto result = clipss2::image::copy_rows_contiguous(
            std::span<const std::byte>{source}, row_pitch, logical_row_bytes, height);

        if (result.size() != logical_row_bytes * height)
        {
            std::cerr << "Unexpected BGRA8 destination size.\n";
            return 1;
        }

        for (std::size_t index = 0; index < result.size(); ++index)
        {
            if (result[index] != static_cast<std::byte>(index + 1) ||
                result[index] == sentinel)
            {
                std::cerr << "BGRA8 row data or padding handling is incorrect.\n";
                return 1;
            }
        }
    }

    {
        constexpr std::size_t logical_row_bytes = 16;
        constexpr std::size_t row_pitch = 24;
        constexpr std::size_t height = 2;
        constexpr std::byte sentinel{0xee};

        std::array<std::byte, row_pitch * height> source{};
        source.fill(sentinel);
        for (std::size_t index = 0; index < logical_row_bytes; ++index)
        {
            source[index] = static_cast<std::byte>(index + 1);
            source[row_pitch + index] = static_cast<std::byte>(index + 17);
        }

        const auto result = clipss2::image::copy_rows_contiguous(
            std::span<const std::byte>{source}, row_pitch, logical_row_bytes, height);
        if (result.size() != 32)
        {
            std::cerr << "Unexpected RGBA16F destination size.\n";
            return 1;
        }
        for (std::size_t index = 0; index < result.size(); ++index)
        {
            if (result[index] != static_cast<std::byte>(index + 1) ||
                result[index] == sentinel)
            {
                std::cerr << "RGBA16F row data or padding handling is incorrect.\n";
                return 1;
            }
        }
    }

    std::cout << "BGRA8/RGBA16F row-copy padding tests: passed\n";
    return 0;
}

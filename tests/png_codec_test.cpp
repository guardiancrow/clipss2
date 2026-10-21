#include "codec/png/png_codec.h"
#include "codec/png/png_validator.h"

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <vector>

int main()
{
    try
    {
        const clipss2::image::CpuImage source{
            2,
            2,
            clipss2::image::PixelFormat::bgra8_unorm,
            std::vector<std::byte>{
                std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
                std::byte{10}, std::byte{20}, std::byte{30}, std::byte{40},
                std::byte{50}, std::byte{60}, std::byte{70}, std::byte{80},
                std::byte{100}, std::byte{110}, std::byte{120}, std::byte{255}}};

        const auto output =
            std::filesystem::current_path() / "artifacts" / "png_codec_test.png";
        const auto encoded = clipss2::codec::png::encode_srgb_png(source, output);
        const auto validated =
            clipss2::codec::png::validate_screenshot_png(encoded.path, source);
        if (validated.width != source.width || validated.height != source.height ||
            validated.file_bytes == 0)
        {
            std::cerr << "PNG validation returned unexpected properties.\n";
            return 1;
        }

        const auto memory_png = clipss2::codec::png::encode_srgb_png_bytes(source);
        const auto memory_validated =
            clipss2::codec::png::validate_screenshot_png(memory_png, source);
        if (memory_validated.width != source.width ||
            memory_validated.height != source.height ||
            memory_validated.file_bytes != memory_png.size())
        {
            std::cerr << "PNG memory validation returned unexpected properties.\n";
            return 1;
        }
        const auto memory_output =
            std::filesystem::current_path() / "artifacts" / "png_codec_memory_test.png";
        const auto written =
            clipss2::codec::png::write_png_bytes(memory_png, memory_output);
        const auto written_validated =
            clipss2::codec::png::validate_screenshot_png(written.path, source);
        if (written.file_bytes != memory_png.size() ||
            written_validated.file_bytes != memory_png.size())
            return 1;

        std::cout << "PNG file/memory chunk/decode/round-trip test: passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "PNG codec test failed: " << error.what() << '\n';
        return 1;
    }
}

#include "codec/jpeg/jpeg_codec_internal.h"
#include "codec/png/png_codec.h"
#include "support.h" // Existing 14C-A deterministic corpus/metrics, test only.
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <windows.h>
#include <psapi.h>

namespace
{
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class Function> void rejects(Function function)
{
    try { function(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid jpegli stream accepted.");
}
}
int main(int argc, char** argv)
{
    using namespace clipss2::codec::jpeg;
    try
    {
        if (argc == 3)
        {
            const auto bytes = bench::read_bytes(argv[2]);
            rejects([&] {
                const auto inspection = inspect_jpeg_structure(bytes);
                validate_jpeg_common_privacy(inspection);
                validate_jpeg_backend_technical(inspection, JpegBackendId::jpegli);
            });
            std::cout << "Actual unadmitted mode rejected: " << argv[2] << '\n';
            return 0;
        }
        const auto sources = bench::corpus();
        for (const auto& source : sources)
        {
            const auto& rgb = source.rgb;
            const CanonicalJpegRgb8View view{rgb.width, rgb.height, static_cast<std::size_t>(rgb.width) * 3, rgb.pixels};
            auto jpeg = detail::encode_jpegli_bytes(view, 1.0F);
            const auto inspection = inspect_jpeg_structure(jpeg);
            if (source.id == "s1") std::cout << "jpegli markers: " << inspection.summary() << '\n';
            require(!inspection.jfif(), "Unexpected jpegli metadata.");
            validate_jpeg_common_privacy(inspection);
            validate_jpeg_backend_technical(inspection, JpegBackendId::jpegli);
            rejects([&] { validate_jpeg_backend_technical(inspection, JpegBackendId::turbo); });
            (void)validate_managed_screenshot_jpeg(jpeg, JpegBackendId::jpegli, rgb.width, rgb.height);
            const auto decoded = detail::decode_validated_jpeg_bytes(inspection);
            const auto metrics = bench::measure(rgb, decoded);
            require(metrics.psnr >= 35, "jpegli admission PSNR safety floor failed.");
            const auto turbo = encode_srgb_jpeg_bytes(rgb);
            const auto turbo_metrics = bench::measure(rgb, decode_srgb_jpeg_bytes(turbo));
            std::cout << source.id << ' ' << rgb.width << 'x' << rgb.height << " Turbo/jpegli bytes="
                << turbo.size() << '/' << jpeg.size() << " PSNR=" << turbo_metrics.psnr << '/' << metrics.psnr << '\n';
            if (argc == 2 && rgb.width == 768)
            {
                const std::filesystem::path output{argv[1]};
                std::filesystem::create_directories(output);
                bench::write_ppm(output / (source.id + "-source.ppm"), rgb);
                clipss2::image::CpuImage opaque{rgb.width, rgb.height, clipss2::image::PixelFormat::bgra8_unorm,
                    std::vector<std::byte>(rgb.pixels.size() / 3 * 4)};
                for (std::size_t i = 0; i < rgb.pixels.size() / 3; ++i)
                {
                    opaque.pixels[i * 4] = rgb.pixels[i * 3 + 2];
                    opaque.pixels[i * 4 + 1] = rgb.pixels[i * 3 + 1];
                    opaque.pixels[i * 4 + 2] = rgb.pixels[i * 3];
                    opaque.pixels[i * 4 + 3] = std::byte{255};
                }
                bench::write_bytes(output / (source.id + "-source.png"), clipss2::codec::png::encode_srgb_png_bytes(opaque));
                bench::write_bytes(output / (source.id + "-turbo.jpg"), turbo);
                bench::write_bytes(output / (source.id + "-jpegli.jpg"), jpeg);
            }
            if (argc == 2 && (source.id == "s1" || source.id == "s7"))
            {
                for (const bool jpegli : {false, true})
                {
                    std::vector<double> times;
                    for (int i = 0; i < 6; ++i)
                    {
                        const auto started = std::chrono::steady_clock::now();
                        const auto bytes = jpegli ? detail::encode_jpegli_bytes(view, 1.0F) : detail::encode_turbo_jpeg_bytes(view, 90);
                        require(!bytes.empty(), "Empty timed encode.");
                        if (i) times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
                    }
                    std::ranges::sort(times);
                    std::cout << source.id << (jpegli ? " jpegli" : " Turbo") << " core encode median/max ms="
                        << times[times.size() / 2] << '/' << times.back() << '\n';
                }
            }
            if (source.id != "s1") continue;
            for (const auto size : {1U, 17U})
            {
                const OpaqueRgb8Image tiny{size, size + 2, std::vector<std::byte>(checked_rgb_image_bytes(size, size + 2), std::byte{128})};
                const auto tiny_bytes = detail::encode_jpegli_bytes({tiny.width, tiny.height,
                    static_cast<std::size_t>(tiny.width) * 3, tiny.pixels}, 1.0F);
                (void)validate_managed_screenshot_jpeg(tiny_bytes, JpegBackendId::jpegli, tiny.width, tiny.height);
            }
            const OpaqueRgb8Image packed{2, 2, std::vector<std::byte>(12, std::byte{128})};
            const std::vector<std::byte> padded(14, std::byte{128});
            require(detail::encode_jpegli_bytes({2, 2, 8, padded}, 1.0F) ==
                detail::encode_jpegli_bytes({2, 2, 6, packed.pixels}, 1.0F), "jpegli padded stride changed pixels.");
            for (int i = 0; i < 2; ++i)
            {
                rejects([&] { (void)detail::encode_jpegli_bytes(view, 1.0F, 1); });
                rejects([&] { (void)detail::encode_jpegli_bytes(view, 1.0F, maximum_jpeg_bytes, 1); });
            }
            for (const unsigned marker : {0xe0U, 0xe1U, 0xe2U, 0xeeU, 0xefU, 0xfeU})
            {
                auto private_bytes = jpeg;
                private_bytes.insert(private_bytes.begin() + 2,
                    {std::byte{255}, static_cast<std::byte>(marker), std::byte{0}, std::byte{3}, std::byte{'X'}});
                rejects([&] { (void)validate_managed_screenshot_jpeg(private_bytes, JpegBackendId::jpegli, rgb.width, rgb.height); });
            }
            for (const auto payload : {std::string{"Exif\0\0", 6},
                std::string{"http://ns.adobe.com/xap/1.0/"}})
            {
                auto private_bytes = jpeg;
                const auto length = payload.size() + 2;
                std::vector<std::byte> segment{std::byte{255}, std::byte{0xe1}, std::byte{0}, static_cast<std::byte>(length)};
                for (const auto c : payload) segment.push_back(static_cast<std::byte>(c));
                private_bytes.insert(private_bytes.begin() + 2, segment.begin(), segment.end());
                rejects([&] { (void)validate_managed_screenshot_jpeg(private_bytes, JpegBackendId::jpegli, rgb.width, rgb.height); });
            }
            auto truncated = jpeg; truncated.pop_back();
            rejects([&] { (void)validate_managed_screenshot_jpeg(truncated, JpegBackendId::jpegli, rgb.width, rgb.height); });
            rejects([&] { (void)validate_managed_screenshot_jpeg(jpeg, JpegBackendId::jpegli, rgb.width + 1, rgb.height); });
            for (const auto& marker : inspection.markers()) if (marker.code == 0xc0)
            {
                auto progressive = jpeg; progressive[marker.payload_offset - 3] = std::byte{0xc2};
                rejects([&] { (void)validate_managed_screenshot_jpeg(progressive, JpegBackendId::jpegli, rgb.width, rgb.height); });
                auto sampling = jpeg; sampling[marker.payload_offset + 7] = std::byte{0x22};
                rejects([&] { (void)validate_managed_screenshot_jpeg(sampling, JpegBackendId::jpegli, rgb.width, rgb.height); });
            }
        }
        PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
        if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
            throw std::runtime_error("Memory sanity counter unavailable.");
        std::cout << "Shared corpus/encoders/decode process peak WS/peak commit proxy/current private bytes="
            << memory.PeakWorkingSetSize << '/' << memory.PeakPagefileUsage << '/' << memory.PrivateUsage << '\n';
        std::cout << "jpegli candidate RGB8/common privacy/technical/common decode/quality sanity: passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

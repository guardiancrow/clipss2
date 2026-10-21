#include "platform/windows/frame_geometry.h"
#include "platform/windows/display_color_note.h"
#include "platform/windows/d3d_device_internal.h"
#include "platform/windows/texture_readback.h"
#include "platform/windows/clipboard_image.h"
#include "capture/capture_image_output.h"
#include "image/row_copy.h"
#include <array>
#include <algorithm>
#include <cstring>
#include <iostream>

using namespace clipss2;
using namespace clipss2::platform::windows;
namespace
{
int checks{};
void require(bool ok, const char* why) { ++checks; if (!ok) throw std::runtime_error(why); }
template<class F> void reject(F f, const char* reason)
{
    bool rejected{};
    try { f(); } catch (const CaptureStageError& e) {
        rejected = std::string_view{e.what()}.find("stage=capture.geometry") != std::string_view::npos &&
            std::string_view{e.what()}.find(reason) != std::string_view::npos;
    }
    require(rejected, reason);
}
template<class F> void reject_exact(F action, std::string_view expected)
{
    bool matched{};
    try { action(); }
    catch (const CaptureStageError& error) {
        matched = !error.result() && std::string_view{error.what()} == expected;
    }
    require(matched, "geometry diagnostic bytes changed");
}
image::CpuImage gpu_case(const D3dDevice& device, int cw, int ch, UINT sw, UINT sh)
{
    // No screen content: deterministic owned texture; surplus has a distinct sentinel.
    std::vector<std::byte> source(std::size_t{sw} * sh * 4, std::byte{0xFE});
    std::vector<std::byte> expected(static_cast<std::size_t>(cw) * ch * 4);
    for (int y = 0; y < ch; ++y) for (int x = 0; x < cw; ++x) {
        const std::array pixel{std::byte(x % 251), std::byte(y % 251), std::byte((x + y) % 251), std::byte{255}};
        std::memcpy(source.data() + (std::size_t{sw} * y + x) * 4, pixel.data(), 4);
        std::memcpy(expected.data() + (static_cast<std::size_t>(cw) * y + x) * 4, pixel.data(), 4);
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = sw; desc.Height = sh; desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.Usage = D3D11_USAGE_DEFAULT;
    const D3D11_SUBRESOURCE_DATA data{source.data(), sw * 4, 0};
    winrt::com_ptr<ID3D11Texture2D> texture;
    winrt::check_hresult(detail::D3dDeviceAccess::native_device(device)->CreateTexture2D(&desc, &data, texture.put()));
    auto output = readback_texture(device, texture, window_frame_crop(cw, ch, sw, sh));
    require(output.image.width == static_cast<UINT>(cw) && output.image.height == static_cast<UINT>(ch), "GPU output dimensions");
    require(output.image.pixels == expected, "GPU row placement/sentinel exclusion");
    require(output.logical_row_bytes == static_cast<std::size_t>(cw) * 4 && output.mapped_row_pitch >= output.logical_row_bytes, "GPU pitch");
    return std::move(output.image);
}
}
int main()
{
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        require(detail::window_color_fallback_note(E_FAIL).empty(), "shipping generated unused color note");
        reject_exact([] { (void)window_frame_crop(0, 669, 1048, 676); },
            "stage=capture.geometry; operation=Window ContentSize / surface bounds; reason=invalid-content-size; content=0x669; texture=1048x676");
        reject_exact([] { (void)window_frame_crop(16385, 1, 16385, 1); },
            "stage=capture.geometry; operation=Window ContentSize / surface bounds; reason=unsupported-texture-size; content=16385x1; texture=16385x1");
        reject_exact([] { (void)window_frame_crop(1034, 669, 1033, 668); },
            "stage=capture.geometry; operation=Window ContentSize / surface bounds; reason=content-exceeds-surface; framepool-resize-required; content=1034x669; texture=1033x668");
        reject_exact([] { require_stable_frame_pool(1048, 676, 1049, 677, 1048, 676, 1048, 676); },
            "stage=capture.geometry; operation=Window frame pool stability; reason=framepool-resize-required; item-size-changed; retry capture; initial=1048x676; current=1049x677; pool=1048x676; texture=1048x676");
        reject_exact([] { require_stable_frame_pool(1048, 676, 1048, 676, 1048, 676, 1049, 677); },
            "stage=capture.geometry; operation=Window frame pool stability; reason=unexpected-surface-size; capture/output not permitted; initial=1048x676; current=1048x676; pool=1048x676; texture=1049x677");
        reject_exact([] { require_readback_buffer(15, 16); },
            "stage=capture.geometry; operation=CPU buffer bounds; reason=buffer-size-mismatch; actual-bytes=15; required-bytes=16");
        reject_exact([] { (void)readback_layout(2, 2, 4, 7); },
            "stage=capture.geometry; operation=Readback row pitch / buffer arithmetic; reason=invalid-row-pitch");
        reject_exact([] { (void)readback_layout(SIZE_MAX, 2, 8, SIZE_MAX); },
            "stage=capture.geometry; operation=Readback row pitch / buffer arithmetic; reason=size-overflow; capture/output not permitted");
        require(!window_frame_crop(1034, 669, 1034, 669), "equality must use full copy");
        require(window_frame_crop(1034, 669, 1048, 676) == PixelRectangle{0, 0, 1034, 669}, "actual Windows 10 dimensions");
        for (const auto dims : {std::array{0, 1, 1, 1}, std::array{-1, 1, 1, 1}, std::array{1, 0, 1, 1},
            std::array{1, -1, 1, 1}, std::array{1, 1, 0, 1}, std::array{1, 1, 1, 0}})
            reject([&] { (void)window_frame_crop(dims[0], dims[1], dims[2], dims[3]); }, "invalid-content-size");
        reject([] { (void)window_frame_crop(INT32_MAX, 1, UINT32_MAX, 1); }, "unsupported-texture-size");
        reject([] { (void)window_frame_crop(16385, 1, 16385, 1); }, "unsupported-texture-size");
        reject([] { (void)window_frame_crop(100, 60, 99, 60); }, "content-exceeds-surface");
        reject([] { (void)window_frame_crop(100, 60, 100, 59); }, "content-exceeds-surface");
        require_stable_frame_pool(1048, 676, 1048, 676, 1048, 676, 1048, 676);
        reject([] { require_stable_frame_pool(100, 60, 101, 60, 100, 60, 100, 60); }, "framepool-resize-required");
        reject([] { require_stable_frame_pool(100, 60, 100, 61, 100, 60, 100, 60); }, "framepool-resize-required");
        reject([] { require_stable_frame_pool(100, 60, 100, 60, 100, 60, 101, 60); }, "unexpected-surface-size");
        reject([] { (void)readback_layout(2, 2, 4, 7); }, "invalid-row-pitch");
        reject([] { (void)readback_layout(0, 2, 4, 8); }, "invalid-readback-layout");
        reject([] { (void)readback_layout(2, 2, 3, 8); }, "invalid-readback-layout");
        reject([] { (void)readback_layout(SIZE_MAX, 2, 8, SIZE_MAX); }, "size-overflow");
        reject([] { (void)readback_layout(1, SIZE_MAX, 4, SIZE_MAX); }, "size-overflow");
        reject([] { require_readback_buffer(15, 16); }, "buffer-size-mismatch");
        reject([] { require_readback_buffer(17, 16); }, "buffer-size-mismatch");
        const auto layout = readback_layout(2, 2, 4, 16);
        require(layout.row_bytes == 8 && layout.tight_bytes == 16 && layout.mapped_bytes == 32, "pitched arithmetic");
        std::array<std::byte, 32> rows{};
        std::fill(rows.begin(), rows.end(), std::byte{0xFE});
        std::fill_n(rows.begin(), 8, std::byte{1}); std::fill_n(rows.begin() + 16, 8, std::byte{2});
        const auto tight = image::copy_rows_contiguous(rows, 16, 8, 2);
        require(tight == std::vector<std::byte>({std::byte{1},std::byte{1},std::byte{1},std::byte{1},std::byte{1},std::byte{1},std::byte{1},std::byte{1},
            std::byte{2},std::byte{2},std::byte{2},std::byte{2},std::byte{2},std::byte{2},std::byte{2},std::byte{2}}), "row padding leaked");
        bool short_rejected{};
        try { (void)image::copy_rows_contiguous(std::span{rows}.first(31), 16, 8, 2); }
        catch (const std::invalid_argument&) { short_rejected = true; }
        require(short_rejected, "short mapped buffer accepted");
        const auto device = D3dDevice::create();
        (void)gpu_case(device, 1034, 669, 1048, 676);
        (void)gpu_case(device, 1034, 669, 1048, 669);
        (void)gpu_case(device, 1034, 669, 1034, 676);
        (void)gpu_case(device, 1034, 669, 1034, 669);
        (void)gpu_case(device, 1, 1, 2, 2);
        const auto cropped = gpu_case(device, 21, 13, 35, 20);
        for (const auto format : {image::ImageFileFormat::png, image::ImageFileFormat::jpeg, image::ImageFileFormat::webp,
            image::ImageFileFormat::avif, image::ImageFileFormat::jxl})
        for (const auto destination : {capture::CaptureDestination::file, capture::CaptureDestination::clipboard, capture::CaptureDestination::both}) {
            const auto out = capture::prepare_capture_image_output(cropped, destination, format);
            if (out.lossless_source) require(out.lossless_source->width == 21 && out.lossless_source->height == 13 &&
                out.lossless_source->pixels.size() == 21 * 13 * 4, "codec source dimensions");
            const auto& bytes = format == image::ImageFileFormat::png ? out.png_bytes : format == image::ImageFileFormat::jpeg ? out.jpeg_bytes :
                format == image::ImageFileFormat::webp ? out.webp_bytes : format == image::ImageFileFormat::avif ? out.avif_bytes : out.jxl_bytes;
            if (destination != capture::CaptureDestination::clipboard) {
                const auto& reference = out.lossless_source ? *out.lossless_source : cropped;
                capture::validate_capture_file_payload(format, {}, bytes, reference, out.jpeg_effective ? &*out.jpeg_effective : nullptr);
                require(!bytes.empty(), "file payload missing");
            }
            if (destination != capture::CaptureDestination::file) {
                const auto clipboard = prepare_image_for_clipboard(cropped, *out.lossless_source, out.png_bytes);
                BITMAPV5HEADER header{}; std::memcpy(&header, clipboard.dibv5.bytes.data(), sizeof(header));
                require(header.bV5Width == 21 && header.bV5Height == -13 && clipboard.dibv5.pixel_bytes == 21 * 13 * 4,
                    "Clipboard prepared dimensions");
            }
        }
        // Repeated readback exercises successful Map/Unmap and resource release.
        for (int i = 0; i < 8; ++i) (void)gpu_case(device, 7, 5, 21, 12);
        std::cout << "PASS frame geometry " << checks << " checks; synthetic textures, no screen/Clipboard publication\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

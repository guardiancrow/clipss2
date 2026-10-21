#include "audit/artifact_directory.h"
#include "platform/windows/alpha_capture_audit.h"

#include <windows.h>
#include <dwmapi.h>
#include <DispatcherQueue.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "image/alpha_diagnostics.h"
#include "image/fp16_diagnostics.h"
#include "codec/png/png_codec.h"
#include "codec/png/png_validator.h"
#include "platform/windows/d3d_device.h"
#include "platform/windows/display_color.h"
#include "platform/windows/one_frame_capture.h"

namespace clipss2::platform::windows
{
namespace
{
constexpr int patch_size = 128;
constexpr int columns = 3;
constexpr int rows = 2;
constexpr int fixture_width = columns * patch_size;
constexpr int fixture_height = rows * patch_size;
constexpr std::uint8_t classification_tolerance = 2;

struct Patch
{
    std::string_view name;
    image::Bgra8Pixel logical_straight;
};

constexpr std::array<Patch, 6> patches{{
    {"opaque red", {0, 0, 255, 255}},
    {"half red", {0, 0, 255, 128}},
    {"half green", {0, 255, 0, 128}},
    {"quarter blue", {255, 0, 0, 64}},
    {"three-quarter white", {255, 255, 255, 192}},
    {"transparent black", {0, 0, 0, 0}},
}};

class Dispatcher final
{
public:
    Dispatcher()
    {
        DispatcherQueueOptions options{
            sizeof(DispatcherQueueOptions), DQTYPE_THREAD_CURRENT, DQTAT_COM_NONE};
        winrt::check_hresult(CreateDispatcherQueueController(
            options,
            reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(
                winrt::put_abi(controller_))));
    }
    ~Dispatcher()
    {
        try
        {
            const auto shutdown = controller_.ShutdownQueueAsync();
            const auto deadline = GetTickCount64() + 5000;
            while (shutdown.Status() == winrt::Windows::Foundation::AsyncStatus::Started &&
                   GetTickCount64() < deadline)
            {
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                MsgWaitForMultipleObjectsEx(
                    0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            }
            if (shutdown.Status() == winrt::Windows::Foundation::AsyncStatus::Completed)
                shutdown.GetResults();
        }
        catch (...)
        {
            std::cerr << "Alpha audit DispatcherQueue shutdown failed.\n";
        }
    }
    Dispatcher(const Dispatcher&) = delete;
    Dispatcher& operator=(const Dispatcher&) = delete;
private:
    winrt::Windows::System::DispatcherQueueController controller_{nullptr};
};

class RegisteredClass final
{
public:
    RegisteredClass(const wchar_t* name, WNDPROC procedure) : name_(name)
    {
        WNDCLASSW window_class{};
        window_class.lpfnWndProc = procedure;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpszClassName = name_;
        atom_ = RegisterClassW(&window_class);
        if (!atom_)
            winrt::throw_last_error();
    }
    ~RegisteredClass() { UnregisterClassW(name_, GetModuleHandleW(nullptr)); }
    RegisteredClass(const RegisteredClass&) = delete;
    RegisteredClass& operator=(const RegisteredClass&) = delete;
private:
    const wchar_t* name_{};
    ATOM atom_{};
};

class Window final
{
public:
    explicit Window(HWND handle) : handle_(handle)
    {
        if (!handle_)
            winrt::throw_last_error();
    }
    ~Window() { DestroyWindow(handle_); }
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    [[nodiscard]] HWND get() const noexcept { return handle_; }
private:
    HWND handle_{};
};

class MemoryDc final
{
public:
    MemoryDc() : dc_(CreateCompatibleDC(nullptr))
    {
        if (!dc_)
            winrt::throw_last_error();
    }
    ~MemoryDc() { DeleteDC(dc_); }
    MemoryDc(const MemoryDc&) = delete;
    MemoryDc& operator=(const MemoryDc&) = delete;
    [[nodiscard]] HDC get() const noexcept { return dc_; }
private:
    HDC dc_{};
};

class Bitmap final
{
public:
    Bitmap(const int width, const int height)
    {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        bitmap_ = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits_, nullptr, 0);
        if (!bitmap_)
            winrt::throw_last_error();
    }
    ~Bitmap() { DeleteObject(bitmap_); }
    Bitmap(const Bitmap&) = delete;
    Bitmap& operator=(const Bitmap&) = delete;
    [[nodiscard]] HBITMAP get() const noexcept { return bitmap_; }
    [[nodiscard]] std::byte* bits() const noexcept
    {
        return static_cast<std::byte*>(bits_);
    }
private:
    HBITMAP bitmap_{};
    void* bits_{};
};

class SelectedBitmap final
{
public:
    SelectedBitmap(HDC dc, HBITMAP bitmap) : dc_(dc), previous_(SelectObject(dc, bitmap))
    {
        if (!previous_ || previous_ == HGDI_ERROR)
            throw std::runtime_error("Could not select alpha fixture bitmap.");
    }
    ~SelectedBitmap() { SelectObject(dc_, previous_); }
    SelectedBitmap(const SelectedBitmap&) = delete;
    SelectedBitmap& operator=(const SelectedBitmap&) = delete;
private:
    HDC dc_{};
    HGDIOBJ previous_{};
};

LRESULT CALLBACK underlay_procedure(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(
            window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint{};
        const auto dc = BeginPaint(window, &paint);
        const auto color = reinterpret_cast<const COLORREF*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (dc && color)
        {
            SetDCBrushColor(dc, *color);
            FillRect(dc, &paint.rcPaint, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        }
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

LRESULT CALLBACK layered_procedure(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
    return DefWindowProcW(window, message, wparam, lparam);
}

POINT fixture_position()
{
    const auto monitor = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(monitor, &info))
        winrt::throw_last_error();
    const auto available_width = info.rcWork.right - info.rcWork.left;
    const auto available_height = info.rcWork.bottom - info.rcWork.top;
    if (available_width < fixture_width + 64 || available_height < fixture_height + 64)
        throw std::runtime_error("Primary monitor is too small for the alpha fixture.");
    return {
        info.rcWork.left + (available_width - fixture_width) / 2,
        info.rcWork.top + (available_height - fixture_height) / 2};
}

WindowCaptureTarget target_for(HWND window, const wchar_t* title)
{
    return {reinterpret_cast<std::uintptr_t>(window), title};
}

void settle_windows(HWND underlay, HWND layered)
{
    ShowWindow(underlay, SW_SHOWNOACTIVATE);
    ShowWindow(layered, SW_SHOWNOACTIVATE);
    UpdateWindow(underlay);
    SetWindowPos(layered, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (!GdiFlush())
        throw std::runtime_error("GdiFlush failed for alpha fixture.");
    winrt::check_hresult(DwmFlush());
}

image::Bgra8Pixel mean_patch(
    const image::CpuImage& captured, const std::size_t patch_index,
    std::uint8_t& maximum_spread)
{
    const auto center_x = static_cast<std::uint32_t>(
        (2 * (patch_index % columns) + 1) * captured.width / (2 * columns));
    const auto center_y = static_cast<std::uint32_t>(
        (2 * (patch_index / columns) + 1) * captured.height / (2 * rows));
    std::array<unsigned int, 4> sum{};
    std::array<std::uint8_t, 4> minimum{255, 255, 255, 255};
    std::array<std::uint8_t, 4> maximum{};
    for (std::uint32_t y = center_y - 2; y <= center_y + 2; ++y)
    for (std::uint32_t x = center_x - 2; x <= center_x + 2; ++x)
    {
        const auto pixel = image::read_bgra8_pixel(captured, x, y);
        const std::array values{pixel.blue, pixel.green, pixel.red, pixel.alpha};
        for (std::size_t channel = 0; channel < values.size(); ++channel)
        {
            sum[channel] += values[channel];
            minimum[channel] = std::min(minimum[channel], values[channel]);
            maximum[channel] = std::max(maximum[channel], values[channel]);
        }
    }
    for (std::size_t channel = 0; channel < minimum.size(); ++channel)
        maximum_spread = std::max<std::uint8_t>(
            maximum_spread, static_cast<std::uint8_t>(maximum[channel] - minimum[channel]));
    return {
        static_cast<std::uint8_t>((sum[0] + 12) / 25),
        static_cast<std::uint8_t>((sum[1] + 12) / 25),
        static_cast<std::uint8_t>((sum[2] + 12) / 25),
        static_cast<std::uint8_t>((sum[3] + 12) / 25)};
}

void print_bgra(const image::Bgra8Pixel pixel)
{
    std::cout << "B=" << static_cast<unsigned int>(pixel.blue)
              << " G=" << static_cast<unsigned int>(pixel.green)
              << " R=" << static_cast<unsigned int>(pixel.red)
              << " A=" << static_cast<unsigned int>(pixel.alpha);
}

void verify_geometry(const OneFrameCaptureResult& result)
{
    if (result.content_width != static_cast<std::int32_t>(result.texture_width) ||
        result.content_height != static_cast<std::int32_t>(result.texture_height) ||
        result.cpu_image.width != result.texture_width ||
        result.cpu_image.height != result.texture_height ||
        result.cpu_image.width != fixture_width || result.cpu_image.height != fixture_height)
        throw std::runtime_error("Alpha fixture capture geometry mismatch.");
}
}

int run_alpha_capture_audit(const D3dDevice& device)
{
    Dispatcher dispatcher;
    RegisteredClass underlay_class{
        L"clipss2.Phase11CF2.Underlay", underlay_procedure};
    RegisteredClass layered_class{
        L"clipss2.Phase11CF2.Layered", layered_procedure};
    auto position = fixture_position();
    COLORREF underlay_color = RGB(0, 0, 0);
    Window underlay{CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"clipss2.Phase11CF2.Underlay", L"clipss2 alpha audit underlay",
        WS_POPUP, position.x - 32, position.y - 32,
        fixture_width + 64, fixture_height + 64,
        nullptr, nullptr, GetModuleHandleW(nullptr), &underlay_color)};
    Window layered{CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"clipss2.Phase11CF2.Layered", L"clipss2 alpha capture audit",
        WS_POPUP, position.x, position.y, fixture_width, fixture_height,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr)};

    MemoryDc memory_dc;
    Bitmap bitmap{fixture_width, fixture_height};
    SelectedBitmap selected{memory_dc.get(), bitmap.get()};
    for (int y = 0; y < fixture_height; ++y)
    for (int x = 0; x < fixture_width; ++x)
    {
        const auto patch_index = static_cast<std::size_t>(
            (y / patch_size) * columns + x / patch_size);
        const auto pixel = image::premultiply_bgra8(patches[patch_index].logical_straight);
        const auto offset = static_cast<std::size_t>(y * fixture_width + x) * 4;
        bitmap.bits()[offset] = static_cast<std::byte>(pixel.blue);
        bitmap.bits()[offset + 1] = static_cast<std::byte>(pixel.green);
        bitmap.bits()[offset + 2] = static_cast<std::byte>(pixel.red);
        bitmap.bits()[offset + 3] = static_cast<std::byte>(pixel.alpha);
    }
    SIZE size{fixture_width, fixture_height};
    POINT source{};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    if (!UpdateLayeredWindow(
            layered.get(), nullptr, &position, &size, memory_dc.get(), &source,
            0, &blend, ULW_ALPHA))
        winrt::throw_last_error();

    settle_windows(underlay.get(), layered.get());
    const auto target = target_for(layered.get(), L"clipss2 alpha capture audit");
    MONITORINFOEXW monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    if (!GetMonitorInfoW(
            MonitorFromWindow(layered.get(), MONITOR_DEFAULTTONEAREST), &monitor_info))
        winrt::throw_last_error();
    const auto pre_color = display_color_info_for_window(target);
    std::cout << "Alpha fixture: 3 x 2 patches, 128 x 128 physical pixels each\n"
              << "Fixture HWND: 0x" << std::hex
              << reinterpret_cast<std::uintptr_t>(layered.get()) << std::dec
              << "\nFixture monitor: ";
    std::wcout << monitor_info.szDevice;
    std::cout << " (DPI " << GetDpiForWindow(layered.get()) << ")\n"
              << "Fixture monitor Advanced Color kind (pre): "
              << pre_color.advanced_color_kind << '\n';

    const CaptureOptions options{false, CaptureBorderPolicy::system_required};
    const auto black = capture_one_frame(
        device, target, CapturePixelFormat::bgra8_unorm, options);
    verify_geometry(black);

    underlay_color = RGB(255, 0, 255);
    if (!InvalidateRect(underlay.get(), nullptr, FALSE))
        winrt::throw_last_error();
    UpdateWindow(underlay.get());
    settle_windows(underlay.get(), layered.get());
    const auto magenta = capture_one_frame(
        device, target, CapturePixelFormat::bgra8_unorm, options);
    verify_geometry(magenta);
    const auto fp16 = capture_one_frame(
        device, target, CapturePixelFormat::rgba16_float, options);
    verify_geometry(fp16);
    const auto post_color = display_color_info_for_window(target);
    std::cout << "Fixture monitor Advanced Color kind (post): "
              << post_color.advanced_color_kind << '\n';

    std::array<image::AlphaProbeSample, patches.size()> samples{};
    unsigned int maximum_underlay_delta = 0;
    std::uint8_t maximum_uniform_spread = 0;
    std::cout << "Classification tolerance: +/-"
              << static_cast<unsigned int>(classification_tolerance)
              << " per BGRA8 channel\n";
    for (std::size_t index = 0; index < patches.size(); ++index)
    {
        std::uint8_t black_spread = 0;
        std::uint8_t magenta_spread = 0;
        const auto black_pixel = mean_patch(black.cpu_image, index, black_spread);
        const auto magenta_pixel = mean_patch(
            magenta.cpu_image, index, magenta_spread);
        maximum_uniform_spread = std::max(
            maximum_uniform_spread, std::max(black_spread, magenta_spread));
        samples[index] = {
            patches[index].logical_straight,
            image::premultiply_bgra8(patches[index].logical_straight),
            black_pixel};
        const std::array first{
            black_pixel.blue, black_pixel.green, black_pixel.red, black_pixel.alpha};
        const std::array second{
            magenta_pixel.blue, magenta_pixel.green,
            magenta_pixel.red, magenta_pixel.alpha};
        for (std::size_t channel = 0; channel < first.size(); ++channel)
            maximum_underlay_delta = std::max(
                maximum_underlay_delta,
                static_cast<unsigned int>(std::abs(
                    static_cast<int>(first[channel]) - static_cast<int>(second[channel]))));

        const auto center_x = static_cast<std::uint32_t>(
            (2 * (index % columns) + 1) * fp16.cpu_image.width / (2 * columns));
        const auto center_y = static_cast<std::uint32_t>(
            (2 * (index / columns) + 1) * fp16.cpu_image.height / (2 * rows));
        const auto fp = image::read_rgba16_float_pixel(
            fp16.cpu_image, center_x, center_y);
        std::cout << "Patch " << index << " (" << patches[index].name << "):\n"
                  << "  logical straight: ";
        print_bgra(patches[index].logical_straight);
        std::cout << "\n  fixture premultiplied input: ";
        print_bgra(samples[index].fixture_premultiplied);
        std::cout << "\n  observed WGC BGRA8 (25-sample mean): ";
        print_bgra(black_pixel);
        std::cout << "\n  observed WGC FP16 center RGBA: R=" << fp.red
                  << " G=" << fp.green << " B=" << fp.blue
                  << " A=" << fp.alpha << '\n';
    }

    const auto classification = image::classify_alpha_samples(
        samples, classification_tolerance);
    const auto layered_stats = image::analyze_bgra8_alpha(black.cpu_image);
    const auto png_image = image::unpremultiply_bgra8_for_png(black.cpu_image);
    std::array<image::AlphaProbeSample, patches.size()> png_samples{};
    for (std::size_t index = 0; index < patches.size(); ++index)
    {
        std::uint8_t ignored_spread = 0;
        png_samples[index] = {
            patches[index].logical_straight,
            image::premultiply_bgra8(patches[index].logical_straight),
            mean_patch(png_image, index, ignored_spread)};
    }
    const auto png_classification = image::classify_alpha_samples(
        png_samples, classification_tolerance);
    const auto png_bytes = codec::png::encode_srgb_png_bytes(png_image);
    (void)codec::png::validate_screenshot_png(png_bytes, png_image);
    const auto artifact_path = clipss2::audit::artifact_directory() /
                               "alpha-capture-audit.png";
    (void)codec::png::write_png_bytes(png_bytes, artifact_path);
    (void)codec::png::validate_screenshot_png(artifact_path, png_image);
    std::cout << "Patch uniformity maximum channel spread: "
              << static_cast<unsigned int>(maximum_uniform_spread)
              << "\nBlack/magenta underlay maximum captured-channel delta: "
              << maximum_underlay_delta
              << "\nLayered capture alpha stats: min="
              << static_cast<unsigned int>(layered_stats.minimum)
              << " max=" << static_cast<unsigned int>(layered_stats.maximum)
              << " nonopaque=" << layered_stats.non_opaque_count
              << " zero=" << layered_stats.zero_count
              << " zero-alpha/nonzero-RGB="
              << layered_stats.zero_alpha_nonzero_rgb_count
              << " RGB-exceeds-alpha="
              << layered_stats.premultiplied_rgb_exceeds_alpha_count
              << "\nObserved BGRA8 alpha association: "
              << image::alpha_association_name(classification.association)
              << "\nMaximum RGB error vs straight: "
              << static_cast<unsigned int>(classification.maximum_straight_error)
              << "\nMaximum RGB error vs premultiplied: "
              << static_cast<unsigned int>(classification.maximum_premultiplied_error)
              << "\nMaximum alpha error: "
              << static_cast<unsigned int>(classification.maximum_alpha_error)
              << "\nPNG boundary output alpha association: "
              << image::alpha_association_name(png_classification.association)
              << "\nPNG encode/decode full-pixel validation: passed"
              << "\nVisual audit artifact: " << artifact_path.string() << '\n';

    COLORREF opaque_color = RGB(37, 91, 173);
    Window opaque{CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"clipss2.Phase11CF2.Underlay", L"clipss2 opaque alpha audit",
        WS_POPUP, position.x, position.y, fixture_width, fixture_height,
        nullptr, nullptr, GetModuleHandleW(nullptr), &opaque_color)};
    ShowWindow(opaque.get(), SW_SHOWNOACTIVATE);
    UpdateWindow(opaque.get());
    SetWindowPos(opaque.get(), HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (!GdiFlush())
        throw std::runtime_error("GdiFlush failed for opaque alpha audit.");
    winrt::check_hresult(DwmFlush());
    const auto opaque_capture = capture_one_frame(
        device, target_for(opaque.get(), L"clipss2 opaque alpha audit"),
        CapturePixelFormat::bgra8_unorm, options);
    verify_geometry(opaque_capture);
    const auto opaque_stats = image::analyze_bgra8_alpha(opaque_capture.cpu_image);
    std::cout << "Opaque controlled-window alpha stats: min="
              << static_cast<unsigned int>(opaque_stats.minimum)
              << " max=" << static_cast<unsigned int>(opaque_stats.maximum)
              << " nonopaque=" << opaque_stats.non_opaque_count
              << " zero=" << opaque_stats.zero_count
              << " zero-alpha/nonzero-RGB="
              << opaque_stats.zero_alpha_nonzero_rgb_count
              << " RGB-exceeds-alpha="
              << opaque_stats.premultiplied_rgb_exceeds_alpha_count << '\n';

    const bool sdr_stable =
        pre_color.advanced_color_kind == "StandardDynamicRange" &&
        post_color.advanced_color_kind == "StandardDynamicRange";
    const bool evidence_valid = sdr_stable && maximum_uniform_spread <= 2 &&
        maximum_underlay_delta <= 2 && classification.maximum_alpha_error <= 2 &&
        png_classification.association == image::AlphaAssociation::straight_unassociated;
    if (!evidence_valid || classification.association == image::AlphaAssociation::unknown)
    {
        std::cout << "ALPHA PATH UNRESOLVED -- no conversion applied\n";
        return 1;
    }
    if (classification.association == image::AlphaAssociation::premultiplied)
        std::cout << "ALPHA PATH FIXED -- captured BGRA8 is premultiplied; "
                     "PNG boundary uses explicit unpremultiplication\n";
    else if (classification.association == image::AlphaAssociation::straight_unassociated)
        std::cout << "ALPHA PATH VALID -- no conversion needed\n";
    else
        std::cout << "ALPHA PATH RESTRICTED -- capture forced alpha opaque\n";
    return classification.association == image::AlphaAssociation::forced_opaque ? 1 : 0;
}
}

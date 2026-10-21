#include "platform/windows/color_self_test.h"
#include "platform/windows/display_color.h"
#include "platform/windows/display_monitor.h"
#include "platform/windows/one_frame_capture.h"
#include "image/srgb_transfer.h"
#include "image/fp16_diagnostics.h"

#include <windows.h>
#include <dwmapi.h>
#include <DispatcherQueue.h>
#include <winrt/base.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.Foundation.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace clipss2::platform::windows
{
namespace
{
constexpr int patch_size = 80;
constexpr int columns = 4;
constexpr int width = columns * patch_size;
constexpr int height = 4 * patch_size;
using Rgb = std::array<int, 3>;
constexpr std::array<Rgb, 16> colors{{
    {0,0,0}, {1,1,1}, {10,10,10}, {11,11,11},
    {12,12,12}, {25,25,25}, {64,64,64}, {128,128,128},
    {192,192,192}, {255,255,255}, {255,0,0}, {0,255,0},
    {0,0,255}, {10,11,12}, {25,64,128}, {192,128,64}}};

class TestDispatcher final
{
public:
    TestDispatcher()
    {
        DispatcherQueueOptions options{sizeof(DispatcherQueueOptions), DQTYPE_THREAD_CURRENT, DQTAT_COM_NONE};
        winrt::check_hresult(CreateDispatcherQueueController(options,
            reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(controller_))));
    }
    ~TestDispatcher()
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
                MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            }
            if (shutdown.Status() == winrt::Windows::Foundation::AsyncStatus::Completed)
                shutdown.GetResults();
            else
                std::cerr << "Self-test DispatcherQueue shutdown did not complete successfully.\n";
        }
        catch (...) { std::cerr << "Self-test DispatcherQueue shutdown failed.\n"; }
    }
    TestDispatcher(const TestDispatcher&) = delete;
    TestDispatcher& operator=(const TestDispatcher&) = delete;
private:
    winrt::Windows::System::DispatcherQueueController controller_{nullptr};
};

class TestWindow final
{
public:
    explicit TestWindow(const DisplayMonitorInfo* monitor = nullptr)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = procedure;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = class_name;
        if (!RegisterClassW(&wc))
            winrt::throw_last_error();
        int window_width = width;
        int window_height = height;
        int x = 100;
        int y = 100;
        if (monitor)
        {
            window_width = MulDiv(width, static_cast<int>(monitor->dpi_x), 96);
            window_height = MulDiv(height, static_cast<int>(monitor->dpi_y), 96);
            const auto& work = monitor->work_rectangle;
            if (window_width > work.right - work.left ||
                window_height > work.bottom - work.top)
            {
                UnregisterClassW(class_name, wc.hInstance);
                throw std::runtime_error("Color test window does not fit in monitor work area.");
            }
            x = work.left + ((work.right - work.left) - window_width) / 2;
            y = work.top + ((work.bottom - work.top) - window_height) / 2;
        }
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW, class_name,
            L"clipss2 Phase 6 static color self-test", WS_POPUP,
            x, y, window_width, window_height, nullptr, nullptr, wc.hInstance, &paint_failed_);
        if (!window_)
        {
            const auto error = GetLastError();
            UnregisterClassW(class_name, wc.hInstance);
            throw winrt::hresult_error(HRESULT_FROM_WIN32(error));
        }
    }
    ~TestWindow()
    {
        DestroyWindow(window_);
        UnregisterClassW(class_name, GetModuleHandleW(nullptr));
    }
    TestWindow(const TestWindow&) = delete;
    TestWindow& operator=(const TestWindow&) = delete;

    WindowCaptureTarget target() const
    {
        return {reinterpret_cast<std::uintptr_t>(window_), L"Phase 6 static color self-test"};
    }
    unsigned int dpi() const { return GetDpiForWindow(window_); }
    DisplayRectangle client_rectangle() const
    {
        RECT rectangle{};
        if (!GetClientRect(window_, &rectangle)) winrt::throw_last_error();
        return {rectangle.left, rectangle.top, rectangle.right, rectangle.bottom};
    }
    void present()
    {
        ShowWindow(window_, SW_SHOWNOACTIVATE);
        if (!InvalidateRect(window_, nullptr, FALSE)) winrt::throw_last_error();
        UpdateWindow(window_);
        MSG message{};
        while (PeekMessageW(&message, window_, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (paint_failed_) throw std::runtime_error("GDI patch drawing failed.");
        if (!GdiFlush()) throw std::runtime_error("GdiFlush failed.");
        winrt::check_hresult(DwmFlush());
    }
private:
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept
    {
        if (message == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        if (message == WM_PAINT)
        {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            bool failed = dc == nullptr;
            if (dc)
            {
                RECT client{};
                if (!GetClientRect(window, &client)) failed = true;
                for (std::size_t i = 0; i < colors.size(); ++i)
                {
                    const int column = static_cast<int>(i % columns);
                    const int row = static_cast<int>(i / columns);
                    RECT rect{
                        client.right * column / columns,
                        client.bottom * row / columns,
                        client.right * (column + 1) / columns,
                        client.bottom * (row + 1) / columns};
                    const auto& rgb = colors[i];
                    if (SetDCBrushColor(dc, RGB(rgb[0], rgb[1], rgb[2])) == CLR_INVALID ||
                        !FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH))))
                        failed = true;
                }
            }
            EndPaint(window, &paint);
            auto* status = reinterpret_cast<bool*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (status) *status = *status || failed;
            return 0;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }
    static constexpr const wchar_t* class_name = L"clipss2.Phase6.ColorTest";
    bool paint_failed_{};
    HWND window_{};
};

template<typename T>
void print_rgb(const char* label, const std::array<T, 3>& value)
{
    std::cout << label << value[0] << ", " << value[1] << ", " << value[2] << '\n';
}
}

namespace
{
int run_color_test(const D3dDevice& device, const DisplayMonitorInfo* monitor)
{
    TestDispatcher dispatcher;
    TestWindow window{monitor};
    window.present();
    const auto target = window.target();
    std::cout << "Color self-test target: created (static opaque 4x4 patches)\n";
    if (monitor)
    {
        std::cout << "Target monitor: " << monitor->index << " (";
        std::wcout << monitor->device_name << L")\n";
        const auto client = window.client_rectangle();
        std::cout << "Monitor DPI: " << window.dpi()
                  << "\nTest window logical size: " << width << " x " << height
                  << "\nTest window client size: " << client.right - client.left
                  << " x " << client.bottom - client.top << '\n';
    }
    std::optional<DisplayColorInfo> display;
    try
    {
        display = monitor ? display_color_info_for_monitor(monitor->monitor_value)
                          : display_color_info_for_window(target);
        std::cout << "Advanced Color kind: " << display->advanced_color_kind
                  << "\nDisplayInformation source: " << display->acquisition_method << '\n';
        if (!display->diagnostic_note.empty()) std::cout << display->diagnostic_note << '\n';
    }
    catch (const winrt::hresult_error& error)
    {
        std::cout << "Advanced Color unavailable: HRESULT 0x" << std::hex
                  << static_cast<unsigned long>(error.code()) << std::dec << '\n';
    }
    // Controlled diagnostics must not let pointer placement alter patch pixels.
    const CaptureOptions options{
        false, CaptureBorderPolicy::system_required};
    const auto bgra = capture_one_frame(
        device, target, CapturePixelFormat::bgra8_unorm, options);
    const auto fp16 = capture_one_frame(
        device, target, CapturePixelFormat::rgba16_float, options);
    std::cout << "BGRA8 capture: succeeded; " << bgra.texture_format
              << "; " << bgra.texture_width << " x " << bgra.texture_height
              << "\nBGRA8 CPU readback: succeeded"
              << "\nFP16 capture: succeeded; " << fp16.texture_format
              << "; " << fp16.texture_width << " x " << fp16.texture_height << '\n';
    const auto fp16_statistics = image::analyze_rgba16_float(fp16.cpu_image);
    constexpr const char* channel_names[]{"R", "G", "B", "A"};
    std::size_t fp_nan = 0;
    std::size_t fp_inf = 0;
    for (std::size_t channel = 0; channel < fp16_statistics.channels.size(); ++channel)
    {
        const auto& statistics = fp16_statistics.channels[channel];
        std::cout << "FP16 " << channel_names[channel] << " range: "
                  << statistics.min_finite << " .. " << statistics.max_finite << '\n';
        fp_nan += statistics.nan_count;
        fp_inf += statistics.positive_infinity_count + statistics.negative_infinity_count;
    }
    std::cout << "FP16 negative RGB count: "
              << fp16_statistics.finite_rgb_negative_count
              << "\nFP16 RGB >1.0 count: "
              << fp16_statistics.finite_rgb_above_one_count
              << "\nFP16 NaN count: " << fp_nan
              << "\nFP16 Inf count: " << fp_inf << '\n';
    const bool sdr_confirmed =
        display && display->advanced_color_kind == "StandardDynamicRange";
    // WS_POPUP has no non-client frame. Scaling, if applied by Windows, is handled
    // using relative patch positions; all sampled pixels are deep inside patches.
    if (bgra.content_width != fp16.content_width || bgra.content_height != fp16.content_height ||
        bgra.texture_width != fp16.texture_width || bgra.texture_height != fp16.texture_height ||
        bgra.cpu_image.width != fp16.cpu_image.width || bgra.cpu_image.height != fp16.cpu_image.height ||
        bgra.content_width != static_cast<int>(bgra.texture_width) ||
        bgra.content_height != static_cast<int>(bgra.texture_height) ||
        bgra.cpu_image.width < 40 || bgra.cpu_image.height < 40)
        throw std::runtime_error("Self-test capture geometry mismatch.");
    std::cout << "Matched content/texture/CPU dimensions: " << bgra.texture_width
              << " x " << bgra.texture_height << '\n';
    bool passed = true;
    double sum_error = 0, max_error = 0;
    std::size_t worst_patch = 0, worst_channel = 0, nonfinite = 0, rgb_mismatches = 0;
    int min_alpha8 = 255, max_alpha8 = 0;
    float min_alpha16 = 1, max_alpha16 = 0;
    std::cout << std::setprecision(10);
    for (std::size_t i = 0; i < colors.size(); ++i)
    {
        std::array<double, 3> expected{}, captured{}, errors{};
        Rgb captured8{};
        const auto cx = static_cast<std::uint32_t>((2 * (i % columns) + 1) * bgra.cpu_image.width / 8);
        const auto cy = static_cast<std::uint32_t>((2 * (i / columns) + 1) * bgra.cpu_image.height / 8);
        for (std::uint32_t y = cy - 2; y <= cy + 2; ++y)
        for (std::uint32_t x = cx - 2; x <= cx + 2; ++x)
        {
            const auto b = image::read_bgra8_pixel(bgra.cpu_image, x, y);
            const auto f = image::read_rgba16_float_pixel(fp16.cpu_image, x, y);
            const Rgb bytes{b.red, b.green, b.blue};
            const std::array<float, 3> linear{f.red, f.green, f.blue};
            captured8 = bytes;
            min_alpha8 = std::min(min_alpha8, int(b.alpha));
            max_alpha8 = std::max(max_alpha8, int(b.alpha));
            if (!std::isfinite(f.alpha)) { ++nonfinite; passed = false; }
            else
            {
                min_alpha16 = std::min(min_alpha16, f.alpha);
                max_alpha16 = std::max(max_alpha16, f.alpha);
                // Opaque interior sanity, not an alpha transfer assertion.
                if (b.alpha < 254 || f.alpha < 0.99f || f.alpha > 1.01f) passed = false;
            }
            for (std::size_t c = 0; c < 3; ++c)
            {
                if (bytes[c] != colors[i][c]) { ++rgb_mismatches; passed = false; }
                const double reference = image::srgb_encoded_to_linear(bytes[c] / 255.0);
                expected[c] += reference / 25;
                captured[c] += linear[c] / 25.0;
                if (!std::isfinite(linear[c])) { ++nonfinite; passed = false; continue; }
                const double error = std::abs(reference - linear[c]);
                errors[c] = std::max(errors[c], error);
                sum_error += error;
                if (error > max_error) { max_error = error; worst_patch = i; worst_channel = c; }
                if (error > image::srgb_fp16_tolerance(reference)) passed = false;
            }
        }
        std::cout << "Patch " << i << " (25 samples):\n";
        print_rgb("  Requested RGB8: ", colors[i]);
        print_rgb("  Captured RGB8 (last): ", captured8);
        print_rgb("  Expected linear RGB (mean): ", expected);
        print_rgb("  Captured FP16 RGB (mean): ", captured);
        print_rgb("  Maximum absolute error RGB: ", errors);
    }
    std::cout << "Requested RGB8 mismatches: " << rgb_mismatches
              << "\nMaximum absolute RGB error: " << max_error
              << "\nMean absolute RGB error: " << sum_error / (colors.size() * 25 * 3)
              << "\nWorst patch/channel: " << worst_patch << '/' << "RGB"[worst_channel]
              << "\nNaN/Inf samples: " << nonfinite
              << "\nAlpha BGRA8 range: " << min_alpha8 << " .. " << max_alpha8
              << "\nAlpha FP16 range: " << min_alpha16 << " .. " << max_alpha16
              << "\nSDR BGRA8 -> FP16 relationship: ";
    if (!sdr_confirmed)
    {
        std::cout << "SKIP (SDR not confirmed; observations retained)\n";
        return 0;
    }
    std::cout << (passed ? "passed" : "FAILED") << '\n';
    return passed ? 0 : 1;
}

}

int run_color_self_test(const D3dDevice& device)
{
    return run_color_test(device, nullptr);
}

int run_color_self_test_all_monitors(const D3dDevice& device)
{
    const auto monitors = enumerate_display_monitors();
    int result = 0;
    for (const auto& monitor : monitors)
    {
        std::cout << "\n========== Monitor " << monitor.index << " ==========\n";
        try
        {
            if (run_color_test(device, &monitor) != 0) result = 1;
        }
        catch (const std::exception& error)
        {
            std::cerr << "Monitor " << monitor.index
                      << " color self-test failed: " << error.what() << '\n';
            result = 1;
        }
    }
    return result;
}
}

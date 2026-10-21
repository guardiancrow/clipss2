#include "platform/windows/resource_lifetime_audit.h"

#include <windows.h>
#include <dwmapi.h>
#include <winrt/base.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "codec/png/png_codec.h"
#include "codec/png/png_validator.h"
#include "image/alpha_diagnostics.h"
#include "platform/windows/d3d_device.h"
#include "platform/windows/error_diagnostics.h"
#include "platform/windows/one_frame_capture.h"
#include "platform/windows/resource_lifetime_model.h"
#include "platform/windows/resource_counters.h"

namespace clipss2::platform::windows
{
namespace
{
constexpr int fixture_width = 320;
constexpr int fixture_height = 240;
constexpr int warmup_iterations = 5;
constexpr int measured_iterations = 50;

LRESULT CALLBACK fixture_procedure(
    HWND window, UINT message, WPARAM word, LPARAM parameter) noexcept
{
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window, &paint);
        SetDCBrushColor(dc, RGB(35, 90, 170));
        FillRect(dc, &paint.rcPaint, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, word, parameter);
}

class Fixture final
{
public:
    Fixture()
    {
        WNDCLASSW type{};
        type.lpfnWndProc = fixture_procedure;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = class_name;
        atom_ = RegisterClassW(&type);
        if (!atom_) throw std::runtime_error(format_win32_error(
            "Register resource-audit window class failed", GetLastError()));
        window_ = CreateWindowExW(
            0, class_name, L"clipss2 resource lifetime audit fixture",
            WS_OVERLAPPEDWINDOW, 80, 80, fixture_width, fixture_height,
            nullptr, nullptr, type.hInstance, nullptr);
        if (!window_) throw std::runtime_error(format_win32_error(
            "Create resource-audit window failed", GetLastError()));
        ShowWindow(window_, SW_SHOWNOACTIVATE);
        UpdateWindow(window_);
        winrt::check_hresult(DwmFlush());
    }
    ~Fixture()
    {
        if (window_) DestroyWindow(window_);
        if (atom_) UnregisterClassW(class_name, GetModuleHandleW(nullptr));
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    [[nodiscard]] HWND get() const noexcept { return window_; }
private:
    static constexpr const wchar_t* class_name = L"clipss2.ResourceLifetimeAudit";
    ATOM atom_{};
    HWND window_{};
};

}

int run_resource_lifetime_audit(const D3dDevice& device)
{
    const Fixture fixture;
    const WindowCaptureTarget target{
        reinterpret_cast<std::uintptr_t>(fixture.get()),
        L"clipss2 resource lifetime audit fixture"};
    std::chrono::microseconds frame_wait_total{}, gpu_wait_total{}, png_total{};
    std::chrono::microseconds iteration_total{};
    std::chrono::microseconds frame_wait_max{}, gpu_wait_max{}, png_max{};
    std::chrono::microseconds iteration_max{};
    std::uint64_t retry_total{};

    auto iteration = [&](const bool record) {
        const auto iteration_started = std::chrono::steady_clock::now();
        const auto capture = capture_one_frame(
            device, target, CapturePixelFormat::bgra8_unorm,
            CaptureOptions{false, CaptureBorderPolicy::system_required, false});
        const auto converted = image::unpremultiply_bgra8_for_png(capture.cpu_image);
        const auto png_started = std::chrono::steady_clock::now();
        const auto bytes = codec::png::encode_srgb_png_bytes(converted);
        const auto validation = codec::png::validate_screenshot_png(bytes, converted);
        const auto png_duration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - png_started);
        const auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - iteration_started);
        if (validation.width != converted.width || validation.height != converted.height)
            throw std::runtime_error("Resource audit PNG dimensions did not round-trip.");
        if (record)
        {
            frame_wait_total += capture.frame_wait_duration;
            gpu_wait_total += capture.gpu_readback_duration;
            png_total += png_duration;
            iteration_total += total_duration;
            frame_wait_max = std::max(frame_wait_max, capture.frame_wait_duration);
            gpu_wait_max = std::max(gpu_wait_max, capture.gpu_readback_duration);
            png_max = std::max(png_max, png_duration);
            iteration_max = std::max(iteration_max, total_duration);
            retry_total += capture.gpu_busy_retry_count;
        }
    };

    std::cout << "Resource lifetime audit: warmup=" << warmup_iterations
              << ", measured=" << measured_iterations << '\n';
    for (int index = 0; index < warmup_iterations; ++index) iteration(false);
    // WGC closes its capture engine asynchronously. Sample only after a short
    // quiescent interval so queued teardown is not misclassified as a leak.
    std::this_thread::sleep_for(std::chrono::milliseconds{500});
    const auto baseline_handle_types = count_handle_types(collect_process_handles());
    const auto baseline = collect_resource_snapshot();
    std::vector<ResourceSnapshot> samples;
    samples.reserve(measured_iterations);
    for (int index = 0; index < measured_iterations; ++index)
    {
        iteration(true);
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
        samples.push_back(collect_resource_snapshot());
    }
    const auto evaluation = evaluate_resource_samples(baseline, samples);
    const auto final_handle_types = count_handle_types(collect_process_handles());
    print_resource_snapshot("Resource baseline", baseline);
    print_resource_snapshot("Resource final", samples.back());
    std::cout << "Resource deltas: handles=" << evaluation.handle_delta
              << ", USER=" << evaluation.user_delta
              << ", GDI=" << evaluation.gdi_delta << '\n'
              << "Monotonic growth: handles=" << std::boolalpha
              << evaluation.handles_monotonically_growing
              << ", USER=" << evaluation.user_monotonically_growing
              << ", GDI=" << evaluation.gdi_monotonically_growing << '\n'
              << "Memory growth (>16 MiB, informational): "
              << evaluation.memory_growth_observed << '\n'
              << "Memory maxima/deltas: working-set="
              << evaluation.maximum_working_set_bytes << '/'
              << evaluation.working_set_delta << ", private="
              << evaluation.maximum_private_bytes << '/'
              << evaluation.private_delta << '\n'
              << "Memory monotonic trend: working-set="
              << evaluation.working_set_monotonically_growing
              << ", private=" << evaluation.private_monotonically_growing << '\n';
    const auto average = [](const std::chrono::microseconds total) {
        return total.count() / measured_iterations;
    };
    std::cout << "Timing us average/max: frame-wait=" << average(frame_wait_total)
              << '/' << frame_wait_max.count() << ", gpu-readback="
              << average(gpu_wait_total) << '/' << gpu_wait_max.count()
              << ", png-encode+validate=" << average(png_total) << '/'
              << png_max.count() << ", total=" << average(iteration_total) << '/'
              << iteration_max.count() << ", gpu-busy-retries=" << retry_total << '\n'
              << "Resource count audit: "
              << (evaluation.resource_counts_passed ? "passed" : "FAILED") << '\n';
    print_handle_type_deltas(baseline_handle_types, final_handle_types);
    return evaluation.resource_counts_passed ? 0 : 1;
}
}

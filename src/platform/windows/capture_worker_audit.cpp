#include "platform/windows/capture_worker_audit.h"

#include "platform/windows/capture_worker_client.h"
#include "platform/windows/capture_worker_protocol.h"
#include "platform/windows/error_diagnostics.h"
#include "platform/windows/resource_counters.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace clipss2::platform::windows
{
namespace
{
using namespace std::chrono_literals;

LRESULT CALLBACK fixture_procedure(
    HWND window, UINT message, WPARAM word, LPARAM parameter) noexcept
{
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window, &paint);
        SetDCBrushColor(dc, RGB(44, 103, 181));
        FillRect(dc, &paint.rcPaint, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, word, parameter);
}

class FixtureWindows final
{
public:
    FixtureWindows()
    {
        WNDCLASSW type{};
        type.lpfnWndProc = fixture_procedure;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = class_name;
        atom_ = RegisterClassW(&type);
        if (!atom_)
            throw std::runtime_error(format_win32_error(
                "Register worker fixture class failed", GetLastError()));
    }

    ~FixtureWindows()
    {
        for (const HWND window : windows_)
            if (IsWindow(window)) (void)DestroyWindow(window);
        if (atom_) (void)UnregisterClassW(class_name, GetModuleHandleW(nullptr));
    }
    FixtureWindows(const FixtureWindows&) = delete;
    FixtureWindows& operator=(const FixtureWindows&) = delete;

    HWND create(const int offset = 0)
    {
        const HWND window = CreateWindowExW(
            0, class_name, L"clipss2 H3 worker target", WS_OVERLAPPEDWINDOW,
            100 + offset, 100 + offset, 420, 300, nullptr, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        if (!window)
            throw std::runtime_error(format_win32_error(
                "Create worker fixture failed", GetLastError()));
        windows_.push_back(window);
        ShowWindow(window, SW_SHOWNOACTIVATE);
        UpdateWindow(window);
        return window;
    }

    void destroy(const HWND window)
    {
        const auto found = std::find(windows_.begin(), windows_.end(), window);
        if (found == windows_.end() || !IsWindow(window))
            throw std::runtime_error("Worker fixture HWND is not live.");
        if (!DestroyWindow(window))
            throw std::runtime_error(format_win32_error(
                "Destroy worker fixture failed", GetLastError()));
        windows_.erase(found);
    }

    CaptureWorkerRequest request(
        const HWND window,
        const capture::CaptureDestination destination,
        const bool cursor,
        const bool quiet) const
    {
        DWORD process_id{};
        if (!GetWindowThreadProcessId(window, &process_id) || process_id == 0)
            throw std::runtime_error("Worker fixture PID is unavailable.");
        return {
            reinterpret_cast<std::uintptr_t>(window), process_id, class_name,
            cursor, CaptureBorderPolicy::system_required, destination, image::ImageFileFormat::png, quiet};
    }

private:
    static constexpr const wchar_t* class_name = L"clipss2.CaptureWorkerAudit";
    ATOM atom_{};
    std::vector<HWND> windows_;
};

std::int64_t signed_delta(const std::uint32_t after, const std::uint32_t before)
{
    return static_cast<std::int64_t>(after) - static_cast<std::int64_t>(before);
}

std::int64_t alpc_delta(
    const std::vector<ProcessHandleRecord>& before,
    const std::vector<ProcessHandleRecord>& after)
{
    const auto left = count_handle_types(before);
    const auto right = count_handle_types(after);
    const std::wstring type = L"ALPC Port";
    const auto a = left.contains(type) ? left.at(type) : 0;
    const auto b = right.contains(type) ? right.at(type) : 0;
    return static_cast<std::int64_t>(b) - static_cast<std::int64_t>(a);
}

void print_parent_delta(
    const ResourceSnapshot& before,
    const ResourceSnapshot& after,
    const std::vector<ProcessHandleRecord>& handles_before,
    const std::vector<ProcessHandleRecord>& handles_after)
{
    std::cout << "Parent resource delta: handles="
              << signed_delta(after.handles, before.handles)
              << ", ALPC=" << alpc_delta(handles_before, handles_after)
              << ", USER=" << signed_delta(after.user_objects, before.user_objects)
              << ", GDI=" << signed_delta(after.gdi_objects, before.gdi_objects)
              << ", working-set="
              << static_cast<std::int64_t>(after.working_set_bytes) -
                     static_cast<std::int64_t>(before.working_set_bytes)
              << ", private="
              << static_cast<std::int64_t>(after.private_bytes) -
                     static_cast<std::int64_t>(before.private_bytes) << '\n';
    print_handle_type_deltas(
        count_handle_types(handles_before), count_handle_types(handles_after));
}

void print_latency(std::vector<std::chrono::microseconds> values)
{
    std::sort(values.begin(), values.end());
    const auto percentile = [&](const double value) {
        const auto rank = static_cast<std::size_t>(
            std::ceil(value * static_cast<double>(values.size())));
        return values[std::max<std::size_t>(1, rank) - 1];
    };
    std::cout << "Parent-observed worker latency ms: median="
              << values[values.size() / 2].count() / 1000.0
              << ", p95=" << percentile(0.95).count() / 1000.0
              << ", max=" << values.back().count() / 1000.0 << '\n';
}

void require_success(const CaptureWorkerClientResult& run, const char* operation)
{
    if (run.status != CaptureWorkerClientStatus::success || run.forced_termination)
        throw std::runtime_error(
            std::string{operation} + " failed: " +
            capture_worker_client_status_name(run.status));
}

void warm_workers(FixtureWindows& fixtures, const HWND window)
{
    for (int index = 0; index < 3; ++index)
        require_success(launch_capture_worker(fixtures.request(
            window, capture::CaptureDestination::clipboard, false, true)),
            "Worker warm-up");
    std::this_thread::sleep_for(500ms);
}

int one_shot_probe(FixtureWindows& fixtures)
{
    const HWND window = fixtures.create();
    const auto run = launch_capture_worker(fixtures.request(
        window, capture::CaptureDestination::clipboard, false, false));
    require_success(run, "One-shot worker");
    std::cout << "Worker normal termination: true\n"
              << "Normal-path TerminateProcess: false\n"
              << "One-shot latency ms: " << run.duration.count() / 1000.0 << '\n';
    return 0;
}

int output_probe(FixtureWindows& fixtures)
{
    const HWND window = fixtures.create();
    require_success(launch_capture_worker(fixtures.request(
        window, capture::CaptureDestination::file, false, false)), "File worker");
    require_success(launch_capture_worker(fixtures.request(
        window, capture::CaptureDestination::clipboard, true, false)),
        "Clipboard worker");
    const UINT png = RegisterClipboardFormatW(L"PNG");
    if (!IsClipboardFormatAvailable(CF_DIBV5) ||
        png == 0 || !IsClipboardFormatAvailable(png))
        throw std::runtime_error("Clipboard did not survive worker process exit.");
    require_success(launch_capture_worker(fixtures.request(
        window, capture::CaptureDestination::both, false, false)), "Both worker");
    std::cout << "Worker outputs: File=success, Clipboard=success, Both=success\n"
              << "Clipboard after worker exit: CF_DIBV5 + PNG available\n"
              << "Cursor policy: Off and On succeeded\n"
              << "Border policy: system-required\n";
    return 0;
}

int stale_probe(FixtureWindows& fixtures)
{
    const HWND window = fixtures.create();
    warm_workers(fixtures, window);
    const auto request = fixtures.request(
        window, capture::CaptureDestination::clipboard, false, false);
    const DWORD clipboard_before = GetClipboardSequenceNumber();
    const auto handles_before = collect_resource_snapshot();
    auto pid_mismatch = request;
    pid_mismatch.expected_process_id =
        request.expected_process_id == std::numeric_limits<std::uint32_t>::max()
            ? request.expected_process_id - 1 : request.expected_process_id + 1;
    auto class_mismatch = request;
    class_mismatch.expected_class += L".mismatch";
    const auto pid_run = launch_capture_worker(pid_mismatch);
    const auto class_run = launch_capture_worker(class_mismatch);
    fixtures.destroy(window);
    const auto destroyed_run = launch_capture_worker(request);
    std::this_thread::sleep_for(100ms);
    const auto handles_after = collect_resource_snapshot();
    if (pid_run.status != CaptureWorkerClientStatus::stale_target ||
        class_run.status != CaptureWorkerClientStatus::stale_target ||
        destroyed_run.status != CaptureWorkerClientStatus::stale_target ||
        pid_run.forced_termination || class_run.forced_termination ||
        destroyed_run.forced_termination ||
        GetClipboardSequenceNumber() != clipboard_before)
        throw std::runtime_error("Stale-target worker did not fail closed.");
    std::cout << "Identity mismatch results: PID=rejected, class=rejected\n"
              << "Destroyed target result: explicit stale rejection\n"
              << "Clipboard side effect: none\n"
              << "Parent handle delta after failure: "
              << signed_delta(handles_after.handles, handles_before.handles) << '\n';
    return 0;
}

int containment_probe(FixtureWindows& fixtures)
{
    const HWND window = fixtures.create();
    warm_workers(fixtures, window);
    const auto handles_before = collect_process_handles();
    const auto before = collect_resource_snapshot();
    std::vector<std::chrono::microseconds> timings;
    timings.reserve(100);
    for (int index = 0; index < 100; ++index)
    {
        const auto run = launch_capture_worker(fixtures.request(
            window, capture::CaptureDestination::clipboard, false, true));
        require_success(run, "Containment worker");
        timings.push_back(run.duration);
    }
    std::this_thread::sleep_for(1s);
    const auto after = collect_resource_snapshot();
    const auto handles_after = collect_process_handles();
    std::cout << "Per-capture workers completed: 100\n";
    print_parent_delta(before, after, handles_before, handles_after);
    print_latency(std::move(timings));
    return alpc_delta(handles_before, handles_after) == 0 ? 0 : 1;
}

int churn_probe(FixtureWindows& fixtures)
{
    const HWND warm = fixtures.create();
    warm_workers(fixtures, warm);
    const auto handles_before = collect_process_handles();
    const auto before = collect_resource_snapshot();
    std::vector<std::chrono::microseconds> timings;
    for (int index = 0; index < 20; ++index)
    {
        const HWND window = fixtures.create(index % 5 * 25);
        const auto run = launch_capture_worker(fixtures.request(
            window, capture::CaptureDestination::clipboard, index % 2 != 0, true));
        require_success(run, "Churn worker");
        timings.push_back(run.duration);
        fixtures.destroy(window);
    }
    std::this_thread::sleep_for(1s);
    const auto after = collect_resource_snapshot();
    const auto handles_after = collect_process_handles();
    std::cout << "Worker distinct-target churn completed: 20\n";
    print_parent_delta(before, after, handles_before, handles_after);
    print_latency(std::move(timings));
    return alpc_delta(handles_before, handles_after) == 0 ? 0 : 1;
}
}

int run_capture_worker_audit(const std::string_view probe)
{
    std::cout << "Capture worker architecture audit: diagnostic-only\n"
              << "Transport: validated command line; no temporary IPC files\n"
              << "Probe: " << probe << '\n';
    FixtureWindows fixtures;
    if (probe == "one-shot") return one_shot_probe(fixtures);
    if (probe == "outputs") return output_probe(fixtures);
    if (probe == "stale") return stale_probe(fixtures);
    if (probe == "containment") return containment_probe(fixtures);
    if (probe == "churn") return churn_probe(fixtures);
    if (probe == "risk-model")
    {
        std::cout << "Strategy A workload: 100 captures, 80 same target, "
                     "20 replacements -> 21 item creations / expected ALPC +21\n";
        return 0;
    }
    throw std::invalid_argument(
        "Worker audit probe must be one-shot, outputs, stale, containment, churn, "
        "or risk-model.");
}
}

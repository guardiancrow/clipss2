#include "platform/windows/wgc_handle_isolation_audit.h"

#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "platform/windows/d3d_device.h"
#include "platform/windows/d3d_device_internal.h"
#include "platform/windows/error_diagnostics.h"
#include "platform/windows/handle_isolation_model.h"
#include "platform/windows/one_frame_capture.h"
#include "platform/windows/resource_counters.h"
#include "platform/windows/texture_readback.h"

namespace clipss2::platform::windows
{
namespace
{
using namespace std::chrono_literals;
using winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame;
using winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool;
using winrt::Windows::Graphics::Capture::GraphicsCaptureItem;
using winrt::Windows::Graphics::Capture::GraphicsCaptureSession;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;
using winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;

constexpr int control_iterations = 100;
constexpr int d3d_iterations = 50;
constexpr int boundary_iterations = 10;
constexpr int product_iterations = 10;
#ifdef NDEBUG
constexpr int long_iterations = 100;
#else
constexpr int long_iterations = 20;
#endif

std::int64_t handle_delta(const ResourceSnapshot& before, const ResourceSnapshot& after)
{
    return static_cast<std::int64_t>(after.handles) - before.handles;
}

struct StageSample
{
    std::string label;
    ResourceSnapshot resources;
};

void print_stage_table(const std::vector<StageSample>& samples)
{
    std::cout << "\nStaged lifecycle (live-object counts; teardown persistence is measured separately):\n"
                 "Stage                              Handles   Prior   USER   GDI\n";
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
        const auto prior = index == 0 ? 0 : handle_delta(
            samples[index - 1].resources, samples[index].resources);
        std::cout << std::left << std::setw(35) << samples[index].label << std::right
                  << std::setw(8) << samples[index].resources.handles
                  << std::setw(8) << prior
                  << std::setw(7) << samples[index].resources.user_objects
                  << std::setw(6) << samples[index].resources.gdi_objects << '\n';
    }
}

LRESULT CALLBACK fixture_procedure(
    HWND window, UINT message, WPARAM word, LPARAM parameter) noexcept
{
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window, &paint);
        SetDCBrushColor(dc, RGB(30, 100, 175));
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
            "Register isolation-audit window class failed", GetLastError()));
        window_ = CreateWindowExW(
            0, class_name, L"clipss2 WGC handle isolation fixture",
            WS_OVERLAPPEDWINDOW, 100, 100, 320, 240, nullptr, nullptr,
            type.hInstance, nullptr);
        if (!window_) throw std::runtime_error(format_win32_error(
            "Create isolation-audit window failed", GetLastError()));
        ShowWindow(window_, SW_SHOWNOACTIVATE);
        UpdateWindow(window_);
        winrt::check_hresult(DwmFlush());
    }
    ~Fixture()
    {
        if (window_) (void)DestroyWindow(window_);
        if (atom_) (void)UnregisterClassW(class_name, GetModuleHandleW(nullptr));
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    [[nodiscard]] HWND get() const noexcept { return window_; }
private:
    static constexpr const wchar_t* class_name = L"clipss2.WgcHandleIsolation";
    ATOM atom_{};
    HWND window_{};
};

winrt::com_ptr<ID3D11Device> create_native_device()
{
    constexpr UINT base_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    UINT flags = base_flags;
#ifndef NDEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    winrt::com_ptr<ID3D11Device> device;
    HRESULT result = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
        D3D11_SDK_VERSION, device.put(), nullptr, nullptr);
#ifndef NDEBUG
    if (result == DXGI_ERROR_SDK_COMPONENT_MISSING)
        result = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, base_flags, nullptr, 0,
            D3D11_SDK_VERSION, device.put(), nullptr, nullptr);
#endif
    if (FAILED(result)) throw_hresult_failure("D3D11CreateDevice failed", result);
    return device;
}

IDirect3DDevice project_device(const winrt::com_ptr<IDXGIDevice>& dxgi)
{
    winrt::com_ptr<IInspectable> inspectable;
    const HRESULT result = CreateDirect3D11DeviceFromDXGIDevice(
        dxgi.get(), inspectable.put());
    if (FAILED(result))
        throw_hresult_failure("CreateDirect3D11DeviceFromDXGIDevice failed", result);
    return inspectable.as<IDirect3DDevice>();
}

GraphicsCaptureItem create_item(HWND window)
{
    const auto interop = winrt::get_activation_factory<
        GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    GraphicsCaptureItem item{nullptr};
    const HRESULT result = interop->CreateForWindow(
        window, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item));
    if (FAILED(result)) throw_hresult_failure("CreateForWindow failed", result);
    return item;
}

Direct3D11CaptureFramePool create_pool(
    const IDirect3DDevice& device, const GraphicsCaptureItem& item)
{
    return Direct3D11CaptureFramePool::CreateFreeThreaded(
        device, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, item.Size());
}

void configure_session(const GraphicsCaptureSession& session)
{
    constexpr std::wstring_view runtime_class =
        L"Windows.Graphics.Capture.GraphicsCaptureSession";
    if (!winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(
            runtime_class, L"IsCursorCaptureEnabled"))
        throw std::runtime_error("IsCursorCaptureEnabled is unavailable.");
    session.IsCursorCaptureEnabled(false);
    if (session.IsCursorCaptureEnabled())
        throw std::runtime_error("Cursor capture policy was not applied.");
    // The system-required border policy intentionally leaves IsBorderRequired alone.
}

struct FrameState
{
    std::mutex mutex;
    std::condition_variable condition;
    Direct3D11CaptureFrame frame{nullptr};
    std::exception_ptr error;
    bool completed{};
};

winrt::event_token subscribe(
    const Direct3D11CaptureFramePool& pool,
    const std::shared_ptr<FrameState>& state)
{
    return pool.FrameArrived(
        [state](const Direct3D11CaptureFramePool& sender,
                const winrt::Windows::Foundation::IInspectable&) noexcept
        {
            try
            {
                auto frame = sender.TryGetNextFrame();
                if (!frame) return;
                bool discard{};
                {
                    const std::lock_guard lock{state->mutex};
                    if (state->completed) discard = true;
                    else
                    {
                        state->frame = std::move(frame);
                        state->completed = true;
                    }
                }
                if (discard)
                {
                    try { frame.Close(); } catch (...) {}
                    return;
                }
                state->condition.notify_one();
            }
            catch (...)
            {
                bool notify{};
                {
                    const std::lock_guard lock{state->mutex};
                    if (!state->completed)
                    {
                        state->error = std::current_exception();
                        state->completed = true;
                        notify = true;
                    }
                }
                if (notify) state->condition.notify_one();
            }
        });
}

Direct3D11CaptureFrame wait_for_frame(
    const std::shared_ptr<FrameState>& state, const std::chrono::seconds timeout = 5s)
{
    std::unique_lock lock{state->mutex};
    if (!state->condition.wait_for(lock, timeout, [&] { return state->completed; }))
    {
        state->completed = true;
        throw std::runtime_error("Isolation audit timed out waiting for a frame.");
    }
    if (state->error) std::rethrow_exception(state->error);
    return std::move(state->frame);
}

void safe_close(Direct3D11CaptureFrame& frame)
{
    if (frame) { frame.Close(); frame = nullptr; }
}
void safe_close(GraphicsCaptureSession& session)
{
    if (session) { session.Close(); session = nullptr; }
}
void safe_close(Direct3D11CaptureFramePool& pool)
{
    if (pool) { pool.Close(); pool = nullptr; }
}

class CaptureGuard final
{
public:
    Direct3D11CaptureFramePool pool{nullptr};
    GraphicsCaptureSession session{nullptr};
    winrt::event_token token{};
    bool subscribed{};

    ~CaptureGuard()
    {
        try { unsubscribe(); } catch (...) {}
        try { close_session(); } catch (...) {}
        try { close_pool(); } catch (...) {}
    }
    CaptureGuard() = default;
    CaptureGuard(const CaptureGuard&) = delete;
    CaptureGuard& operator=(const CaptureGuard&) = delete;

    void unsubscribe()
    {
        if (subscribed && pool)
        {
            pool.FrameArrived(token);
            subscribed = false;
        }
    }
    void close_session() { safe_close(session); }
    void close_pool() { safe_close(pool); }
};

class FrameGuard final
{
public:
    explicit FrameGuard(Direct3D11CaptureFrame value) : frame(std::move(value)) {}
    ~FrameGuard() { try { safe_close(frame); } catch (...) {} }
    FrameGuard(const FrameGuard&) = delete;
    FrameGuard& operator=(const FrameGuard&) = delete;
    Direct3D11CaptureFrame frame{nullptr};
};

enum class ProbeBoundary { item, pool, subscription, session, full };

void run_boundary_probe(
    const D3dDevice& reusable_device, HWND window, const ProbeBoundary boundary)
{
    auto projected = detail::D3dDeviceAccess::winrt_device(reusable_device);
    auto item = create_item(window);
    if (boundary == ProbeBoundary::item) { item = nullptr; return; }
    CaptureGuard resources;
    resources.pool = create_pool(projected, item);
    if (boundary == ProbeBoundary::pool)
    {
        resources.close_pool(); item = nullptr; return;
    }
    auto state = std::make_shared<FrameState>();
    resources.token = subscribe(resources.pool, state);
    resources.subscribed = true;
    if (boundary == ProbeBoundary::subscription)
    {
        resources.unsubscribe(); resources.close_pool(); item = nullptr; return;
    }
    resources.session = resources.pool.CreateCaptureSession(item);
    configure_session(resources.session);
    if (boundary == ProbeBoundary::session)
    {
        resources.close_session(); resources.unsubscribe(); resources.close_pool();
        item = nullptr; return;
    }
    resources.session.StartCapture();
    FrameGuard frame{wait_for_frame(state)};
    const auto surface = frame.frame.Surface();
    const auto access = surface.as<
        ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    winrt::com_ptr<ID3D11Texture2D> texture;
    const HRESULT result = access->GetInterface(
        winrt::guid_of<ID3D11Texture2D>(), texture.put_void());
    if (FAILED(result)) throw_hresult_failure("Surface texture extraction failed", result);
    safe_close(frame.frame);
    resources.unsubscribe();
    resources.close_session();
    resources.close_pool();
    texture = nullptr;
    item = nullptr;
    projected = nullptr;
}

struct RepeatedResult
{
    ResourceSnapshot baseline;
    ResourceSnapshot immediate;
    ResourceSnapshot after_100ms;
    ResourceSnapshot after_1s;
    ResourceSnapshot after_5s;
    std::vector<std::int64_t> cumulative;
};

template<class Operation>
RepeatedResult run_repeated(const int iterations, Operation operation, const bool deep)
{
    RepeatedResult result{};
    result.baseline = collect_resource_snapshot();
    result.cumulative.reserve(iterations);
    for (int index = 0; index < iterations; ++index)
    {
        operation();
        result.cumulative.push_back(handle_delta(
            result.baseline, collect_resource_snapshot()));
        if (!deep && index + 1 < iterations) std::this_thread::sleep_for(100ms);
    }
    result.immediate = collect_resource_snapshot();
    std::this_thread::sleep_for(100ms);
    result.after_100ms = collect_resource_snapshot();
    std::this_thread::sleep_for(900ms);
    result.after_1s = collect_resource_snapshot();
    std::this_thread::sleep_for(4s);
    result.after_5s = collect_resource_snapshot();
    return result;
}

QuiescenceDeltas deltas(const RepeatedResult& result)
{
    return {
        handle_delta(result.baseline, result.immediate),
        handle_delta(result.baseline, result.after_100ms),
        handle_delta(result.baseline, result.after_1s),
        handle_delta(result.baseline, result.after_5s)};
}

void print_repeated(const char* label, const RepeatedResult& result)
{
    const auto value = deltas(result);
    std::cout << std::left << std::setw(30) << label << std::right
              << " immediate=" << value.immediate
              << ", 100ms=" << value.after_100ms
              << ", 1s=" << value.after_1s
              << ", 5s=" << value.after_5s << '\n';
}

bool resource_counts_equal(const ResourceSnapshot& left, const ResourceSnapshot& right)
{
    return left.handles == right.handles &&
           left.user_objects == right.user_objects &&
           left.gdi_objects == right.gdi_objects;
}

void print_new_handles(
    const std::vector<ProcessHandleRecord>& before,
    const std::vector<ProcessHandleRecord>& after)
{
    std::set<std::uintptr_t> existing;
    for (const auto& record : before) existing.insert(record.value);
    const auto before_types = count_handle_types(before);
    const auto after_types = count_handle_types(after);
    std::set<std::wstring> increased_types;
    for (const auto& [type, count] : after_types)
    {
        const auto found = before_types.find(type);
        const auto prior = found == before_types.end() ? 0 : found->second;
        if (count > prior) increased_types.insert(type);
    }
    std::wcout << L"Persistent new handles after quiescence:\n";
    std::size_t printed{};
    for (const auto& record : after)
    {
        const auto type = record.type.empty() ? L"<unknown>" : record.type;
        if (existing.contains(record.value) || !increased_types.contains(type)) continue;
        std::wcout << L"  value=0x" << std::hex << record.value << std::dec
                   << L", type=" << type
                   << L", access=0x" << std::hex << record.granted_access << std::dec
                   << L", name=" << (record.object_name.empty() ? L"<unavailable>" : record.object_name)
                   << L'\n';
        if (++printed == 12) break;
    }
    if (!printed) std::wcout << L"  none\n";
}

std::vector<StageSample> run_staged_lifecycle(const D3dDevice& readback_device, HWND window)
{
    std::vector<StageSample> stages;
    const auto sample = [&](const char* label) {
        stages.push_back({label, collect_resource_snapshot()});
    };
    sample("S0 baseline (shared device alive)");
    auto native = detail::D3dDeviceAccess::native_device(readback_device);
    sample("S1 shared D3D native reference");
    auto dxgi = native.as<IDXGIDevice>(); sample("S2 IDXGIDevice");
    auto projected = detail::D3dDeviceAccess::winrt_device(readback_device);
    sample("S3 shared WinRT device reference");
    auto item = create_item(window); sample("S4 CreateForWindow");
    CaptureGuard resources;
    resources.pool = create_pool(projected, item); sample("S5 CreateFreeThreaded");
    auto state = std::make_shared<FrameState>();
    resources.token = subscribe(resources.pool, state);
    resources.subscribed = true;
    sample("S6 FrameArrived subscribe");
    resources.session = resources.pool.CreateCaptureSession(item);
    sample("S7 CreateCaptureSession");
    configure_session(resources.session); sample("S8 session properties");
    resources.session.StartCapture(); sample("S9 StartCapture (immediate)");
    FrameGuard frame{wait_for_frame(state)}; sample("S10 first frame received");
    auto surface = frame.frame.Surface();
    auto access = surface.as<
        ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    winrt::com_ptr<ID3D11Texture2D> texture;
    const HRESULT interface_result = access->GetInterface(
        winrt::guid_of<ID3D11Texture2D>(), texture.put_void());
    if (FAILED(interface_result))
        throw_hresult_failure("Surface texture extraction failed", interface_result);
    sample("S11 native texture extraction");
    auto readback = readback_texture(readback_device, texture);
    (void)readback;
    sample("S12 staging/readback");
    safe_close(frame.frame); sample("S13 frame Close");
    resources.unsubscribe(); sample("S14 event unsubscribe");
    resources.close_session(); sample("S15 session Close");
    resources.close_pool(); sample("S16 frame pool Close");
    texture = nullptr;
    access = nullptr;
    surface = nullptr;
    item = nullptr;
    projected = nullptr;
    dxgi = nullptr;
    native = nullptr;
    state.reset();
    sample("S17 explicit reference release");
    std::this_thread::sleep_for(100ms); sample("S18a quiescence 100ms");
    std::this_thread::sleep_for(900ms); sample("S18b quiescence 1s");
    std::this_thread::sleep_for(4s); sample("S18c quiescence 5s");
    return stages;
}
}

namespace
{
bool run_measurement_control()
{
    const auto counters_before = collect_resource_snapshot();
    for (int index = 0; index < control_iterations; ++index)
        (void)collect_resource_snapshot();
    const auto counters_after = collect_resource_snapshot();
    std::cout << "Measurement control (100 counter samples): handles="
              << handle_delta(counters_before, counters_after)
              << ", USER=" << static_cast<std::int64_t>(counters_after.user_objects) - counters_before.user_objects
              << ", GDI=" << static_cast<std::int64_t>(counters_after.gdi_objects) - counters_before.gdi_objects << '\n';

    const auto pss_before_snapshot = collect_resource_snapshot();
    for (int index = 0; index < control_iterations; ++index)
        (void)collect_process_handles();
    const auto pss_after_snapshot = collect_resource_snapshot();
    std::cout << "Measurement control (100 PSS capture/walk/free): handles="
              << handle_delta(pss_before_snapshot, pss_after_snapshot)
              << ", USER=" << static_cast<std::int64_t>(pss_after_snapshot.user_objects) - pss_before_snapshot.user_objects
              << ", GDI=" << static_cast<std::int64_t>(pss_after_snapshot.gdi_objects) - pss_before_snapshot.gdi_objects << '\n';
    if (!resource_counts_equal(counters_before, counters_after) ||
        !resource_counts_equal(pss_before_snapshot, pss_after_snapshot))
    {
        std::cout << "Classification: MEASUREMENT ARTIFACT IDENTIFIED\n";
        return false;
    }
    std::cout << "Measurement infrastructure self-leak: none\n";
    return true;
}

template<class Operation>
void warm_up(Operation operation)
{
    for (int index = 0; index < 5; ++index) operation();
    std::this_thread::sleep_for(500ms);
}

template<class Operation>
RepeatedResult run_isolated_probe(
    const char* label, const int iterations, Operation operation, const bool deep)
{
    warm_up(operation);
    const auto handles_before = collect_process_handles();
    const auto result = run_repeated(iterations, operation, deep);
    const auto handles_after = collect_process_handles();
    print_repeated(label, result);
    print_handle_type_deltas(
        count_handle_types(handles_before), count_handle_types(handles_after));
    print_new_handles(handles_before, handles_after);
    return result;
}
}

int run_wgc_handle_isolation_audit(const std::string_view probe)
{
    std::cout << "WGC handle isolation audit: diagnostic-only; one COM apartment per process\n"
              << "Probe: " << probe << '\n';
    if (!run_measurement_control()) return 1;
    if (probe == "control") return 0;

    const auto d3d_operation = [] {
        auto native = create_native_device();
        auto dxgi = native.as<IDXGIDevice>();
        auto projected = project_device(dxgi);
        projected = nullptr;
        dxgi = nullptr;
        native = nullptr;
    };
    if (probe == "d3d")
    {
        const auto result = run_isolated_probe(
            "D3D + DXGI + projection x50", d3d_iterations,
            d3d_operation, false);
        const auto value = deltas(result);
        const auto classification = classify_handle_growth(
            0, value, result.cumulative);
        std::cout << "Classification: " << classification_name(classification) << '\n';
        return 0;
    }

    // One device is intentionally reused for every WGC isolation experiment.
    const auto reusable_device = D3dDevice::create();
    const Fixture fixture;
    if (probe == "staged")
    {
        const auto before = collect_process_handles();
        const auto stages = run_staged_lifecycle(reusable_device, fixture.get());
        const auto after = collect_process_handles();
        print_stage_table(stages);
        print_handle_type_deltas(count_handle_types(before), count_handle_types(after));
        print_new_handles(before, after);
        return 0;
    }

    const auto operation_for = [&](const ProbeBoundary boundary) {
        return [&reusable_device, &fixture, boundary] {
            run_boundary_probe(reusable_device, fixture.get(), boundary);
        };
    };
    if (probe == "factory")
    {
        (void)run_isolated_probe("Activation factory only", boundary_iterations, [] {
            auto factory = winrt::get_activation_factory<
                GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
            factory = nullptr;
        }, false);
        return 0;
    }
    if (probe == "item")
    {
        const auto result = run_isolated_probe("CreateForWindow only", boundary_iterations,
            operation_for(ProbeBoundary::item), false);
        const auto classification = classify_handle_growth(
            0, deltas(result), result.cumulative);
        std::cout << "Classification: " << classification_name(classification) << '\n'
                  << "First persistent API boundary: IGraphicsCaptureItemInterop::CreateForWindow\n"
                  << "Final classification: ROOT CAUSE ISOLATED - WGC/WinRT STAGE IDENTIFIED\n"
                  << "Readiness: NOT READY - BLOCKERS FOUND\n";
        return 0;
    }
    if (probe == "pool")
    {
        auto projected = detail::D3dDeviceAccess::winrt_device(reusable_device);
        auto item = create_item(fixture.get());
        (void)run_isolated_probe("FramePool create/close", boundary_iterations, [&] {
            CaptureGuard resources;
            resources.pool = create_pool(projected, item);
            resources.close_pool();
        }, false);
        return 0;
    }
    if (probe == "subscription")
    {
        auto projected = detail::D3dDeviceAccess::winrt_device(reusable_device);
        auto item = create_item(fixture.get());
        CaptureGuard resources;
        resources.pool = create_pool(projected, item);
        (void)run_isolated_probe("Subscribe/unsubscribe no start", boundary_iterations, [&] {
            const auto state = std::make_shared<FrameState>();
            resources.token = subscribe(resources.pool, state);
            resources.subscribed = true;
            resources.unsubscribe();
        }, false);
        resources.close_pool();
        return 0;
    }
    if (probe == "session")
    {
        auto projected = detail::D3dDeviceAccess::winrt_device(reusable_device);
        auto item = create_item(fixture.get());
        (void)run_isolated_probe("Session/properties no start", boundary_iterations, [&] {
            // A frame pool does not contractually support repeatedly creating
            // sessions. Create/close one pool+session pair per iteration and
            // compare this result with the pool-only probe.
            CaptureGuard resources;
            resources.pool = create_pool(projected, item);
            resources.session = resources.pool.CreateCaptureSession(item);
            configure_session(resources.session);
            resources.close_session();
            resources.close_pool();
        }, false);
        return 0;
    }
    if (probe == "start-short" || probe == "start-deep" || probe == "start-long")
    {
        const bool deep = probe == "start-deep";
        const int iterations = probe == "start-long" ? long_iterations :
            (deep ? 5 : boundary_iterations);
        const auto result = run_isolated_probe("Start + first frame", iterations,
            operation_for(ProbeBoundary::full), deep);
        const auto classification = classify_handle_growth(0, deltas(result), result.cumulative);
        std::cout << "Iterations: " << iterations << "\nClassification: "
                  << classification_name(classification) << '\n';
        if (!result.cumulative.empty())
            std::cout << "Midpoint/final cumulative delta: "
                      << result.cumulative[result.cumulative.size() / 2]
                      << '/' << result.cumulative.back() << '\n';
        return classification == HandleCleanupClassification::persistent_growth ? 1 : 0;
    }

    const WindowCaptureTarget target{
        reinterpret_cast<std::uintptr_t>(fixture.get()),
        L"clipss2 WGC handle isolation fixture"};
    if (probe == "product")
    {
        (void)run_isolated_probe("Product capture_one_frame x10", product_iterations, [&] {
            (void)capture_one_frame(
                reusable_device, target, CapturePixelFormat::bgra8_unorm,
                CaptureOptions{false, CaptureBorderPolicy::system_required, false});
        }, false);
        return 0;
    }
    throw std::invalid_argument(
        "Isolation probe must be control, d3d, staged, factory, item, pool, subscription, "
        "session, start-short, start-deep, start-long, or product.");
}
}

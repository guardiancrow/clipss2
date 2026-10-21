#include "platform/windows/wgc_item_reuse_audit.h"

#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
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
#include <memory>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "platform/windows/capture_reuse_model.h"
#include "platform/windows/d3d_device.h"
#include "platform/windows/d3d_device_internal.h"
#include "platform/windows/display_color.h"
#include "platform/windows/error_diagnostics.h"
#include "platform/windows/one_frame_capture.h"
#include "platform/windows/resource_counters.h"
#include "platform/windows/texture_readback.h"

namespace clipss2::platform::windows
{
namespace
{
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
using winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame;
using winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool;
using winrt::Windows::Graphics::Capture::GraphicsCaptureItem;
using winrt::Windows::Graphics::Capture::GraphicsCaptureSession;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;
using winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;

#ifdef NDEBUG
constexpr int repeated_capture_count = 100;
constexpr int churn_count = 20;
#else
constexpr int repeated_capture_count = 20;
constexpr int churn_count = 10;
#endif
constexpr int repeated_item_count = 20;
constexpr int no_start_count = 50;

std::int64_t delta(const std::uint32_t after, const std::uint32_t before)
{
    return static_cast<std::int64_t>(after) - before;
}

LRESULT CALLBACK fixture_procedure(
    HWND window, UINT message, WPARAM word, LPARAM parameter) noexcept
{
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window, &paint);
        SetDCBrushColor(dc, RGB(39, 96, 171));
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
        if (!atom_) throw std::runtime_error(format_win32_error(
            "Register H2 fixture class failed", GetLastError()));
    }

    ~FixtureWindows()
    {
        for (const HWND window : windows_)
            if (IsWindow(window)) (void)DestroyWindow(window);
        if (atom_) (void)UnregisterClassW(class_name, GetModuleHandleW(nullptr));
    }

    FixtureWindows(const FixtureWindows&) = delete;
    FixtureWindows& operator=(const FixtureWindows&) = delete;

    [[nodiscard]] HWND create(const std::wstring_view title, const int offset = 0)
    {
        const HWND window = CreateWindowExW(
            0, class_name, std::wstring{title}.c_str(), WS_OVERLAPPEDWINDOW,
            100 + offset, 100 + offset, 360, 260, nullptr, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        if (!window) throw std::runtime_error(format_win32_error(
            "Create H2 fixture window failed", GetLastError()));
        windows_.push_back(window);
        ShowWindow(window, SW_SHOWNOACTIVATE);
        UpdateWindow(window);
        winrt::check_hresult(DwmFlush());
        return window;
    }

    void destroy(const HWND window)
    {
        const auto found = std::find(windows_.begin(), windows_.end(), window);
        if (found == windows_.end() || !IsWindow(window))
            throw std::runtime_error("H2 fixture HWND is not live.");
        const BOOL result = DestroyWindow(window);
        const DWORD error = result ? ERROR_SUCCESS : GetLastError();
        require_win32_success(result != FALSE, error, "Destroy H2 fixture failed");
        windows_.erase(found);
    }

private:
    static constexpr const wchar_t* class_name = L"clipss2.WgcItemReuseAudit";
    ATOM atom_{};
    std::vector<HWND> windows_;
};

GraphicsCaptureItem create_item(const HWND window)
{
    if (!window || !IsWindow(window))
        throw std::runtime_error("CreateForWindow target is not live.");
    const auto factory = winrt::get_activation_factory<
        GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    GraphicsCaptureItem item{nullptr};
    const HRESULT result = factory->CreateForWindow(
        window, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item));
    if (FAILED(result)) throw_hresult_failure("CreateForWindow failed", result);
    return item;
}

Direct3D11CaptureFramePool create_pool(
    const IDirect3DDevice& device, const GraphicsCaptureItem& item)
{
    const auto size = item.Size();
    if (size.Width <= 0 || size.Height <= 0)
        throw std::runtime_error("GraphicsCaptureItem has an empty size.");
    return Direct3D11CaptureFramePool::CreateFreeThreaded(
        device, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
}

void configure_session(const GraphicsCaptureSession& session, const bool cursor)
{
    constexpr std::wstring_view runtime_class =
        L"Windows.Graphics.Capture.GraphicsCaptureSession";
    if (!winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(
            runtime_class, L"IsCursorCaptureEnabled"))
        throw std::runtime_error("IsCursorCaptureEnabled is unavailable.");
    session.IsCursorCaptureEnabled(cursor);
    if (session.IsCursorCaptureEnabled() != cursor)
        throw std::runtime_error("WGC cursor policy was not applied.");
    // system-required border: IsBorderRequired is intentionally untouched.
}

struct FrameState
{
    std::mutex mutex;
    std::condition_variable condition;
    Direct3D11CaptureFrame frame{nullptr};
    std::exception_ptr error;
    bool complete{};
};

winrt::event_token subscribe_frame(
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
                    if (state->complete) discard = true;
                    else
                    {
                        state->frame = std::move(frame);
                        state->complete = true;
                    }
                }
                if (discard)
                {
                    try { frame.Close(); } catch (...) {}
                }
                else state->condition.notify_one();
            }
            catch (...)
            {
                bool notify{};
                {
                    const std::lock_guard lock{state->mutex};
                    if (!state->complete)
                    {
                        state->error = std::current_exception();
                        state->complete = true;
                        notify = true;
                    }
                }
                if (notify) state->condition.notify_one();
            }
        });
}

Direct3D11CaptureFrame wait_frame(const std::shared_ptr<FrameState>& state)
{
    std::unique_lock lock{state->mutex};
    if (!state->condition.wait_for(lock, 5s, [&] { return state->complete; }))
    {
        state->complete = true;
        throw std::runtime_error("H2 timed out waiting for a capture frame.");
    }
    if (state->error) std::rethrow_exception(state->error);
    return std::move(state->frame);
}

class SessionGuard final
{
public:
    explicit SessionGuard(Direct3D11CaptureFramePool pool)
        : pool_(std::move(pool)) {}
    ~SessionGuard()
    {
        try { unsubscribe(); } catch (...) {}
        try { close_session(); } catch (...) {}
    }
    SessionGuard(const SessionGuard&) = delete;
    SessionGuard& operator=(const SessionGuard&) = delete;

    GraphicsCaptureSession session{nullptr};
    winrt::event_token token{};
    bool subscribed{};

    void unsubscribe()
    {
        if (subscribed && pool_)
        {
            pool_.FrameArrived(token);
            subscribed = false;
        }
    }
    void close_session()
    {
        if (session) { session.Close(); session = nullptr; }
    }
private:
    Direct3D11CaptureFramePool pool_{nullptr};
};

class FrameGuard final
{
public:
    explicit FrameGuard(Direct3D11CaptureFrame frame)
        : frame_(std::move(frame)) {}
    ~FrameGuard()
    {
        try { close(); } catch (...) {}
    }
    FrameGuard(const FrameGuard&) = delete;
    FrameGuard& operator=(const FrameGuard&) = delete;

    [[nodiscard]] const Direct3D11CaptureFrame& get() const noexcept
    {
        return frame_;
    }

    void close()
    {
        if (frame_) { frame_.Close(); frame_ = nullptr; }
    }

private:
    Direct3D11CaptureFrame frame_{nullptr};
};

class FramePoolGuard final
{
public:
    explicit FramePoolGuard(Direct3D11CaptureFramePool pool)
        : pool_(std::move(pool)) {}
    ~FramePoolGuard()
    {
        try { close(); } catch (...) {}
    }
    FramePoolGuard(const FramePoolGuard&) = delete;
    FramePoolGuard& operator=(const FramePoolGuard&) = delete;

    [[nodiscard]] const Direct3D11CaptureFramePool& get() const noexcept
    {
        return pool_;
    }

    void close()
    {
        if (pool_) { pool_.Close(); pool_ = nullptr; }
    }

private:
    Direct3D11CaptureFramePool pool_{nullptr};
};

struct CaptureTiming
{
    std::chrono::microseconds session_setup{};
    std::chrono::microseconds first_frame{};
    std::chrono::microseconds total{};
    std::uint32_t width{};
    std::uint32_t height{};
};

CaptureTiming capture_with_pool(
    const D3dDevice& device,
    const GraphicsCaptureItem& item,
    const Direct3D11CaptureFramePool& pool,
    const bool cursor = false)
{
    const auto total_started = Clock::now();
    SessionGuard resources{pool};
    auto state = std::make_shared<FrameState>();
    resources.token = subscribe_frame(pool, state);
    resources.subscribed = true;
    resources.session = pool.CreateCaptureSession(item);
    configure_session(resources.session, cursor);
    const auto setup_done = Clock::now();
    resources.session.StartCapture();
    FrameGuard frame{wait_frame(state)};
    const auto frame_received = Clock::now();
    const auto content_size = frame.get().ContentSize();
    const auto surface = frame.get().Surface();
    const auto access = surface.as<
        ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    winrt::com_ptr<ID3D11Texture2D> texture;
    const HRESULT result = access->GetInterface(
        winrt::guid_of<ID3D11Texture2D>(), texture.put_void());
    if (FAILED(result)) throw_hresult_failure("H2 texture extraction failed", result);
    const auto readback = readback_texture(device, texture);
    (void)readback;
    frame.close();
    resources.unsubscribe();
    resources.close_session();
    state.reset();
    return {
        std::chrono::duration_cast<std::chrono::microseconds>(setup_done - total_started),
        std::chrono::duration_cast<std::chrono::microseconds>(frame_received - setup_done),
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - total_started),
        static_cast<std::uint32_t>(content_size.Width),
        static_cast<std::uint32_t>(content_size.Height)};
}

CaptureTiming capture_new_pool(
    const D3dDevice& device, const GraphicsCaptureItem& item, const bool cursor = false)
{
    FramePoolGuard pool{
        create_pool(detail::D3dDeviceAccess::winrt_device(device), item)};
    auto result = capture_with_pool(device, item, pool.get(), cursor);
    pool.close();
    return result;
}

void warm_item_creation(const HWND window)
{
    for (int index = 0; index < 5; ++index)
    {
        auto item = create_item(window);
        item = nullptr;
    }
    std::this_thread::sleep_for(500ms);
}

struct QuiescenceSamples
{
    ResourceSnapshot immediate;
    ResourceSnapshot after_100ms;
    ResourceSnapshot after_1s;
    ResourceSnapshot after_5s;
};

QuiescenceSamples sample_quiescence()
{
    QuiescenceSamples samples{};
    samples.immediate = collect_resource_snapshot();
    std::this_thread::sleep_for(100ms);
    samples.after_100ms = collect_resource_snapshot();
    std::this_thread::sleep_for(900ms);
    samples.after_1s = collect_resource_snapshot();
    std::this_thread::sleep_for(4s);
    samples.after_5s = collect_resource_snapshot();
    return samples;
}

void print_deltas(
    const char* label,
    const ResourceSnapshot& baseline,
    const QuiescenceSamples& samples)
{
    const auto print_one = [&](const ResourceSnapshot& sample) {
        std::cout << "handles=" << delta(sample.handles, baseline.handles)
                  << ", USER=" << delta(sample.user_objects, baseline.user_objects)
                  << ", GDI=" << delta(sample.gdi_objects, baseline.gdi_objects)
                  << ", working-set="
                  << static_cast<std::int64_t>(sample.working_set_bytes) -
                         static_cast<std::int64_t>(baseline.working_set_bytes)
                  << ", private="
                  << static_cast<std::int64_t>(sample.private_bytes) -
                         static_cast<std::int64_t>(baseline.private_bytes);
    };
    std::cout << label << " immediate: "; print_one(samples.immediate);
    std::cout << "\n  after 100ms: "; print_one(samples.after_100ms);
    std::cout << "\n  after 1s: "; print_one(samples.after_1s);
    std::cout << "\n  after 5s: "; print_one(samples.after_5s);
    std::cout << '\n';
}

void print_type_diff(
    const std::vector<ProcessHandleRecord>& before,
    const std::vector<ProcessHandleRecord>& after)
{
    print_handle_type_deltas(count_handle_types(before), count_handle_types(after));
}

std::int64_t type_delta(
    const std::vector<ProcessHandleRecord>& before,
    const std::vector<ProcessHandleRecord>& after,
    const std::wstring_view type)
{
    const auto left = count_handle_types(before);
    const auto right = count_handle_types(after);
    const auto a = left.contains(std::wstring{type}) ? left.at(std::wstring{type}) : 0;
    const auto b = right.contains(std::wstring{type}) ? right.at(std::wstring{type}) : 0;
    return static_cast<std::int64_t>(b) - static_cast<std::int64_t>(a);
}

void print_timing(const std::vector<CaptureTiming>& timings)
{
    if (timings.empty()) return;
    const auto average = [&](auto member) {
        std::int64_t total{};
        for (const auto& timing : timings) total += (timing.*member).count();
        return total / static_cast<std::int64_t>(timings.size());
    };
    const auto maximum = [&](auto member) {
        std::int64_t value{};
        for (const auto& timing : timings)
            value = std::max(value, (timing.*member).count());
        return value;
    };
    std::cout << "Timing us average/max: session-setup="
              << average(&CaptureTiming::session_setup) << '/'
              << maximum(&CaptureTiming::session_setup)
              << ", first-frame=" << average(&CaptureTiming::first_frame) << '/'
              << maximum(&CaptureTiming::first_frame)
              << ", total=" << average(&CaptureTiming::total) << '/'
              << maximum(&CaptureTiming::total) << '\n';
}

WindowCaptureTarget target_for(const HWND window, std::wstring title)
{
    return {reinterpret_cast<std::uintptr_t>(window), std::move(title)};
}

void require_sdr(const WindowCaptureTarget& target, const char* phase)
{
    const auto color = display_color_info_for_window(target);
    std::cout << "SDR gate " << phase << ": " << color.advanced_color_kind << '\n';
    if (color.advanced_color_kind != "StandardDynamicRange")
        throw std::runtime_error("H2 reuse audit requires an SDR fixture monitor.");
}

int retained_item_probe(const HWND window)
{
    warm_item_creation(window);
    const auto baseline_handles = collect_process_handles();
    const auto baseline = collect_resource_snapshot();
    const auto started = Clock::now();
    auto item = create_item(window);
    const auto creation_time = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - started);
    const auto retained = sample_quiescence();
    print_deltas("Retained item", baseline, retained);
    const auto idle_baseline = collect_resource_snapshot();
    for (int index = 0; index < 100; ++index) (void)collect_resource_snapshot();
    const auto idle_final = collect_resource_snapshot();
    std::cout << "Retained item idle sampling x100: handles="
              << delta(idle_final.handles, idle_baseline.handles) << '\n';
    item = nullptr;
    const auto released = sample_quiescence();
    print_deltas("After item release", baseline, released);
    const auto final_handles = collect_process_handles();
    print_type_diff(baseline_handles, final_handles);
    std::cout << "Item creation time: " << creation_time.count() << " us\n";
    return 0;
}

int repeated_items_probe(const HWND window)
{
    warm_item_creation(window);
    const auto handles_before = collect_process_handles();
    const auto baseline = collect_resource_snapshot();
    for (int index = 0; index < repeated_item_count; ++index)
    {
        auto item = create_item(window);
        item = nullptr;
    }
    const auto samples = sample_quiescence();
    const auto handles_after = collect_process_handles();
    print_deltas("Same HWND, new item x20", baseline, samples);
    print_type_diff(handles_before, handles_after);
    return 0;
}

int multiple_items_probe(const HWND window)
{
    warm_item_creation(window);
    const auto handles_before = collect_process_handles();
    const auto baseline = collect_resource_snapshot();
    std::vector<GraphicsCaptureItem> items;
    for (int index = 0; index < 3; ++index) items.push_back(create_item(window));
    const auto retained = sample_quiescence();
    print_deltas("Three retained items, same HWND", baseline, retained);
    items.clear();
    const auto released = sample_quiescence();
    print_deltas("Three items released", baseline, released);
    const auto handles_after = collect_process_handles();
    print_type_diff(handles_before, handles_after);
    return 0;
}

int no_start_probe(const D3dDevice& device, const HWND window)
{
    warm_item_creation(window);
    auto item = create_item(window);
    (void)capture_new_pool(device, item);
    std::this_thread::sleep_for(500ms);
    const auto handles_before = collect_process_handles();
    const auto baseline = collect_resource_snapshot();
    const auto projected = detail::D3dDeviceAccess::winrt_device(device);
    for (int index = 0; index < no_start_count; ++index)
    {
        FramePoolGuard pool{create_pool(projected, item)};
        try
        {
            auto session = pool.get().CreateCaptureSession(item);
            try
            {
                configure_session(session, false);
                session.Close();
                session = nullptr;
            }
            catch (...)
            {
                try { session.Close(); } catch (...) {}
                throw;
            }
            pool.close();
        }
        catch (...)
        {
            throw;
        }
    }
    const auto samples = sample_quiescence();
    const auto handles_after = collect_process_handles();
    print_deltas("Same item, no StartCapture x50", baseline, samples);
    print_type_diff(handles_before, handles_after);
    item = nullptr;
    return 0;
}

int same_item_capture_probe(const D3dDevice& device, const HWND window)
{
    warm_item_creation(window);
    const auto before_item_handles = collect_process_handles();
    const auto before_item = collect_resource_snapshot();
    const auto item_started = Clock::now();
    auto item = create_item(window);
    const auto item_time = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - item_started);
    std::this_thread::sleep_for(100ms);

    const auto target = target_for(window, L"clipss2 H2 same-item fixture");
    require_sdr(target, "before");
    for (int index = 0; index < 5; ++index) (void)capture_new_pool(device, item);
    std::this_thread::sleep_for(500ms);
    const auto handles_before_captures = collect_process_handles();
    const auto baseline = collect_resource_snapshot();
    std::vector<CaptureTiming> timings;
    timings.reserve(repeated_capture_count);
    for (int index = 0; index < repeated_capture_count; ++index)
        timings.push_back(capture_new_pool(device, item, false));
    const auto capture_samples = sample_quiescence();
    const auto handles_after_captures = collect_process_handles();
    require_sdr(target, "after");
    print_deltas("Same item repeated captures", baseline, capture_samples);
    print_type_diff(handles_before_captures, handles_after_captures);
    print_timing(timings);
    std::cout << "Capture count=" << repeated_capture_count
              << ", item creations=1, session creations=" << repeated_capture_count
              << ", cursor=off, border=system-required\n";

    const auto cursor_on = capture_new_pool(device, item, true);
    std::cout << "Cursor-on independent session: succeeded; "
              << cursor_on.width << 'x' << cursor_on.height << '\n';
    item = nullptr;
    const auto released = sample_quiescence();
    const auto final_handles = collect_process_handles();
    print_deltas("Item lifetime including release", before_item, released);
    print_type_diff(before_item_handles, final_handles);
    const auto additional_alpc = type_delta(
        handles_before_captures, handles_after_captures, L"ALPC Port");
    std::cout << "Item creation time: " << item_time.count() << " us\n"
              << "Additional ALPC during same-item captures: " << additional_alpc
              << "\nInitial item ALPC remains after release: "
              << type_delta(before_item_handles, final_handles, L"ALPC Port") << '\n';
    if (additional_alpc == 0)
        std::cout << "Strategy result: same-item reuse stops per-capture ALPC growth\n";
    return additional_alpc == 0 ? 0 : 1;
}

int different_items_probe(const D3dDevice& device, FixtureWindows& fixtures)
{
    const HWND warm = fixtures.create(L"clipss2 H2 warm target");
    warm_item_creation(warm);
    const auto handles_before = collect_process_handles();
    const auto baseline = collect_resource_snapshot();
    std::vector<HWND> windows;
    std::vector<GraphicsCaptureItem> items;
    for (int index = 0; index < 3; ++index)
    {
        windows.push_back(fixtures.create(
            L"clipss2 H2 distinct target " + std::to_wstring(index), 35 * index));
        items.push_back(create_item(windows.back()));
    }
    for (const auto& item : items) (void)capture_new_pool(device, item);
    const auto captured = sample_quiescence();
    print_deltas("Three distinct HWND/items captured", baseline, captured);
    items.clear();
    const auto released = sample_quiescence();
    print_deltas("Distinct items released", baseline, released);
    const auto handles_after = collect_process_handles();
    print_type_diff(handles_before, handles_after);
    return 0;
}

int churn_probe(const D3dDevice& device, FixtureWindows& fixtures)
{
    const HWND warm = fixtures.create(L"clipss2 H2 churn warm target");
    warm_item_creation(warm);
    (void)capture_new_pool(device, create_item(warm));
    std::this_thread::sleep_for(500ms);
    const auto handles_before = collect_process_handles();
    const auto baseline = collect_resource_snapshot();
    for (int index = 0; index < churn_count; ++index)
    {
        const HWND window = fixtures.create(
            L"clipss2 H2 churn " + std::to_wstring(index), index % 5 * 20);
        auto item = create_item(window);
        (void)capture_new_pool(device, item);
        fixtures.destroy(window);
        item = nullptr;
    }
    const auto samples = sample_quiescence();
    const auto handles_after = collect_process_handles();
    print_deltas("Distinct target churn", baseline, samples);
    print_type_diff(handles_before, handles_after);
    std::cout << "Churn target count: " << churn_count << '\n';
    return 0;
}

struct ClosedState
{
    std::mutex mutex;
    std::condition_variable condition;
    bool closed{};
};

class ItemClosedGuard final
{
public:
    ItemClosedGuard(GraphicsCaptureItem item, const winrt::event_token token)
        : item_(std::move(item)), token_(token) {}
    ~ItemClosedGuard()
    {
        try { unsubscribe(); } catch (...) {}
    }
    ItemClosedGuard(const ItemClosedGuard&) = delete;
    ItemClosedGuard& operator=(const ItemClosedGuard&) = delete;

    void unsubscribe()
    {
        if (item_)
        {
            item_.Closed(token_);
            item_ = nullptr;
        }
    }

private:
    GraphicsCaptureItem item_{nullptr};
    winrt::event_token token_{};
};

bool wait_for_closed_with_message_pump(const std::shared_ptr<ClosedState>& state)
{
    const auto deadline = Clock::now() + 5s;
    while (Clock::now() < deadline)
    {
        {
            std::unique_lock lock{state->mutex};
            if (state->condition.wait_for(lock, 20ms, [&] { return state->closed; }))
                return true;
        }
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    const std::lock_guard lock{state->mutex};
    return state->closed;
}

int closed_probe(const D3dDevice& device, FixtureWindows& fixtures)
{
    const HWND window = fixtures.create(L"clipss2 H2 Closed target");
    warm_item_creation(window);
    const auto handles_before = collect_process_handles();
    const auto baseline = collect_resource_snapshot();
    auto item = create_item(window);
    const auto closed_state = std::make_shared<ClosedState>();
    const auto closed_token = item.Closed(
        [closed_state](const GraphicsCaptureItem&,
                       const winrt::Windows::Foundation::IInspectable&) noexcept
        {
            {
                const std::lock_guard lock{closed_state->mutex};
                closed_state->closed = true;
            }
            closed_state->condition.notify_one();
        });
    ItemClosedGuard closed_subscription{item, closed_token};

    FramePoolGuard pool{
        create_pool(detail::D3dDeviceAccess::winrt_device(device), item)};
    SessionGuard active{pool.get()};
    auto frame_state = std::make_shared<FrameState>();
    active.token = subscribe_frame(pool.get(), frame_state);
    active.subscribed = true;
    active.session = pool.get().CreateCaptureSession(item);
    configure_session(active.session, false);
    active.session.StartCapture();
    FrameGuard frame{wait_frame(frame_state)};
    frame.close();
    active.unsubscribe();
    fixtures.destroy(window);
    const bool notified = wait_for_closed_with_message_pump(closed_state);
    active.close_session();
    pool.close();
    closed_subscription.unsubscribe();
    item = nullptr;
    frame_state.reset();
    const auto samples = sample_quiescence();
    std::cout << "GraphicsCaptureItem Closed notification: "
              << (notified ? "received" : "TIMED OUT") << '\n'
              << "Closed item reusable: false\n";
    print_deltas("Closed target/item release", baseline, samples);

    const HWND replacement = fixtures.create(L"clipss2 H2 replacement target", 40);
    auto replacement_item = create_item(replacement);
    replacement_item = nullptr;
    const auto replacement_samples = sample_quiescence();
    print_deltas("Replacement target item", baseline, replacement_samples);
    const auto handles_after = collect_process_handles();
    print_type_diff(handles_before, handles_after);
    return notified ? 0 : 1;
}

int pool_reuse_probe(const D3dDevice& device, const HWND window)
{
    warm_item_creation(window);
    auto item = create_item(window);
    FramePoolGuard pool{
        create_pool(detail::D3dDeviceAccess::winrt_device(device), item)};
    const auto baseline = collect_resource_snapshot();
    bool second_succeeded{};
    std::string failure;
    try
    {
        const auto first = capture_with_pool(device, item, pool.get());
        std::cout << "Live pool session A: succeeded; " << first.width << 'x'
                  << first.height << '\n';
        const auto second = capture_with_pool(device, item, pool.get());
        second_succeeded = true;
        std::cout << "Live pool session B: succeeded; " << second.width << 'x'
                  << second.height << '\n';
    }
    catch (...)
    {
        failure = describe_exception(std::current_exception());
    }
    pool.close();
    item = nullptr;
    const auto samples = sample_quiescence();
    print_deltas("Live frame-pool reuse", baseline, samples);
    if (!second_succeeded)
        std::cout << "Frame-pool reuse product strategy: excluded (observed failure: "
                  << failure << ")\n";
    else
        std::cout << "Frame-pool reuse: observed to work, but not treated as a documented guarantee\n";
    return 0;
}

int resize_recreate_probe(const D3dDevice& device, const HWND window)
{
    warm_item_creation(window);
    auto item = create_item(window);
    const auto first = capture_new_pool(device, item);
    FramePoolGuard pool{
        create_pool(detail::D3dDeviceAccess::winrt_device(device), item)};
    const BOOL positioned = SetWindowPos(
        window, nullptr, 0, 0, 520, 360,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    const DWORD error = positioned ? ERROR_SUCCESS : GetLastError();
    require_win32_success(positioned != FALSE, error, "Resize H2 fixture failed");
    winrt::check_hresult(DwmFlush());
    std::this_thread::sleep_for(200ms);
    const auto resized = item.Size();
    pool.get().Recreate(
        detail::D3dDeviceAccess::winrt_device(device),
        DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, resized);
    const auto second = capture_with_pool(device, item, pool.get());
    pool.close();
    std::cout << "Resize capture: " << first.width << 'x' << first.height
              << " -> " << second.width << 'x' << second.height << '\n'
              << "Recreate purpose: size update only; not an ALPC mitigation\n";
    return first.width != second.width || first.height != second.height ? 0 : 1;
}
}

int run_wgc_item_reuse_audit(const std::string_view probe)
{
    std::cout << "WGC item reuse audit: diagnostic-only\nProbe: " << probe << '\n';
    const auto device = D3dDevice::create();
    FixtureWindows fixtures;
    if (probe == "different-items") return different_items_probe(device, fixtures);
    if (probe == "churn") return churn_probe(device, fixtures);
    if (probe == "closed") return closed_probe(device, fixtures);

    const HWND window = fixtures.create(L"clipss2 H2 primary target");
    if (probe == "retained-item") return retained_item_probe(window);
    if (probe == "same-hwnd-items") return repeated_items_probe(window);
    if (probe == "multiple-items") return multiple_items_probe(window);
    if (probe == "same-item-no-start") return no_start_probe(device, window);
    if (probe == "same-item-capture") return same_item_capture_probe(device, window);
    if (probe == "pool-reuse") return pool_reuse_probe(device, window);
    if (probe == "resize-recreate") return resize_recreate_probe(device, window);
    throw std::invalid_argument(
        "H2 probe must be retained-item, same-item-no-start, same-item-capture, "
        "same-hwnd-items, multiple-items, different-items, closed, pool-reuse, "
        "resize-recreate, or churn.");
}
}

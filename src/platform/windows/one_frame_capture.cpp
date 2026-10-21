#include "one_frame_capture.h"

#include "d3d_device_internal.h"
#include "texture_readback.h"
#include "frame_geometry.h"
#include "monitor_capture_source.h"
#include "platform/windows/error_diagnostics.h"

#include <d3d11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <condition_variable>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

namespace clipss2::platform::windows
{
namespace
{
using winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame;
using winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool;
using winrt::Windows::Graphics::Capture::GraphicsCaptureItem;
using winrt::Windows::Graphics::Capture::GraphicsCaptureSession;
using winrt::Windows::Foundation::Metadata::ApiInformation;

constexpr int frame_pool_buffer_count = 2;

struct FrameState
{
    std::mutex mutex;
    std::condition_variable condition;
    Direct3D11CaptureFrame frame{nullptr};
    std::exception_ptr error;
    bool completed{false};
};

class CaptureResources final
{
public:
    Direct3D11CaptureFramePool frame_pool{nullptr};
    GraphicsCaptureSession session{nullptr};
    winrt::event_token frame_arrived_token{};
    bool handler_registered{false};

    ~CaptureResources()
    {
        close_noexcept();
    }

    CaptureResources() = default;
    CaptureResources(const CaptureResources&) = delete;
    CaptureResources& operator=(const CaptureResources&) = delete;

    void close()
    {
        std::exception_ptr first_error;
        close_all(first_error);
        if (first_error)
        {
            std::rethrow_exception(first_error);
        }
    }

private:
    void close_noexcept() noexcept
    {
        std::exception_ptr ignored_error;
        close_all(ignored_error);
    }

    void close_all(std::exception_ptr& first_error) noexcept
    {
        const auto remember_error = [&first_error]
        {
            if (!first_error)
            {
                first_error = std::current_exception();
            }
        };

        if (handler_registered && frame_pool)
        {
            try
            {
                frame_pool.FrameArrived(frame_arrived_token);
            }
            catch (...)
            {
                remember_error();
            }
            handler_registered = false;
        }

        if (session)
        {
            try
            {
                session.Close();
            }
            catch (...)
            {
                remember_error();
            }
            session = nullptr;
        }

        if (frame_pool)
        {
            try
            {
                frame_pool.Close();
            }
            catch (...)
            {
                remember_error();
            }
            frame_pool = nullptr;
        }
    }
};

class FrameCloser final
{
public:
    explicit FrameCloser(Direct3D11CaptureFrame frame)
        : frame_(std::move(frame))
    {
    }

    ~FrameCloser()
    {
        if (frame_)
        {
            try
            {
                frame_.Close();
            }
            catch (...)
            {
            }
        }
    }

    FrameCloser(const FrameCloser&) = delete;
    FrameCloser& operator=(const FrameCloser&) = delete;

    void close()
    {
        if (frame_)
        {
            frame_.Close();
            frame_ = nullptr;
        }
    }

    [[nodiscard]] const Direct3D11CaptureFrame& get() const noexcept
    {
        return frame_;
    }

private:
    Direct3D11CaptureFrame frame_{nullptr};
};

GraphicsCaptureItem create_capture_item(HWND window)
{
    if (window == nullptr || !IsWindow(window))
    {
        throw DiagnosticError("Capture target HWND is null or no longer valid.");
    }

    return capture_stage({"wgc.item", "GraphicsCaptureItem factory / CreateForWindow",
        "IGraphicsCaptureItemInterop"}, [&] {
        const auto interop = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        GraphicsCaptureItem item{nullptr};
        const HRESULT result = interop->CreateForWindow(window,
            winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item));
        if (FAILED(result)) throw_hresult_failure("GraphicsCaptureItem CreateForWindow failed", result);
        return item;
    });
}

std::string dxgi_format_name(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return "DXGI_FORMAT_B8G8R8A8_UNORM (87)";
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return "DXGI_FORMAT_R16G16B16A16_FLOAT (10)";
    default:
        return "DXGI_FORMAT(" + std::to_string(static_cast<std::uint32_t>(format)) + ')';
    }
}

winrt::Windows::Graphics::DirectX::DirectXPixelFormat winrt_pixel_format(
    CapturePixelFormat format)
{
    using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;
    switch (format)
    {
    case CapturePixelFormat::bgra8_unorm:
        return DirectXPixelFormat::B8G8R8A8UIntNormalized;
    case CapturePixelFormat::rgba16_float:
        return DirectXPixelFormat::R16G16B16A16Float;
    }
    throw std::invalid_argument("Unsupported capture pixel format.");
}

DXGI_FORMAT native_pixel_format(CapturePixelFormat format)
{
    switch (format)
    {
    case CapturePixelFormat::bgra8_unorm:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case CapturePixelFormat::rgba16_float:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    }
    throw std::invalid_argument("Unsupported capture pixel format.");
}

Direct3D11CaptureFrame wait_for_frame(
    CaptureResources& resources,
    const std::shared_ptr<FrameState>& state,
    std::chrono::milliseconds timeout)
{
    resources.frame_arrived_token = resources.frame_pool.FrameArrived(
        [state](const Direct3D11CaptureFramePool& sender, const winrt::Windows::Foundation::IInspectable&) noexcept
        {
            try
            {
                auto frame = sender.TryGetNextFrame();
                if (!frame)
                {
                    return;
                }

                bool discard = false;
                {
                    const std::lock_guard lock{state->mutex};
                    if (state->completed)
                    {
                        discard = true;
                    }
                    else
                    {
                        state->frame = std::move(frame);
                        state->completed = true;
                    }
                }
                if (discard)
                {
                    try { frame.Close(); }
                    catch (...) {}
                    return;
                }
                state->condition.notify_one();
            }
            catch (...)
            {
                bool notify = false;
                {
                    const std::lock_guard lock{state->mutex};
                    if (!state->completed)
                    {
                        state->error = std::current_exception();
                        state->completed = true;
                        notify = true;
                    }
                }
                if (notify)
                {
                    state->condition.notify_one();
                }
            }
        });
    resources.handler_registered = true;
    try
    {
        resources.session.StartCapture();
    }
    catch (const winrt::hresult_error& error)
    {
        throw DiagnosticError(
            format_hresult("GraphicsCaptureSession StartCapture failed", error.code()), error.code());
    }

    std::unique_lock lock{state->mutex};
    if (!state->condition.wait_for(lock, timeout, [&state] { return state->completed; }))
    {
        state->completed = true;
        throw DiagnosticError("Timed out waiting for the first capture frame.");
    }

    if (state->error)
    {
        std::rethrow_exception(state->error);
    }

    return std::move(state->frame);
}

void configure_session(
    const GraphicsCaptureSession& session,
    CaptureOptions options)
{
    constexpr std::wstring_view runtime_class =
        L"Windows.Graphics.Capture.GraphicsCaptureSession";
    constexpr std::wstring_view property = L"IsCursorCaptureEnabled";
    if (!ApiInformation::IsPropertyPresent(runtime_class, property))
        throw DiagnosticError(
            "The Windows runtime cannot guarantee the requested cursor capture policy "
            "because GraphicsCaptureSession.IsCursorCaptureEnabled is unavailable.");
    session.IsCursorCaptureEnabled(options.include_cursor);
    if (session.IsCursorCaptureEnabled() != options.include_cursor)
        throw DiagnosticError("Windows did not apply the requested cursor capture policy.");
    if (options.diagnostic_output)
        std::cout << "WGC session cursor capture: "
                  << (options.include_cursor ? "on" : "off") << '\n';
    switch (options.border_policy)
    {
    case CaptureBorderPolicy::system_required:
        // A normal executable manifest does not grant the package capability
        // required for borderless capture. Preserve the OS indicator policy.
        if (options.diagnostic_output)
            std::cout << "WGC session border policy: system-required "
                         "(IsBorderRequired unchanged)\n";
        break;
    }
}
}


static OneFrameCaptureResult capture_item_frame(
    const D3dDevice& device,
    const GraphicsCaptureItem& item,
    CapturePixelFormat pixel_format,
    CaptureOptions options,
    std::chrono::milliseconds timeout,
    const MonitorSource* monitor = nullptr)
{
    return capture_stage({"wgc.frame", "Frame acquisition / geometry / cleanup", "IDirect3DDxgiInterfaceAccess"}, [&] {
    if (timeout <= std::chrono::milliseconds::zero())
    {
        throw std::invalid_argument("Capture timeout must be positive.");
    }

    const auto content_size = item.Size();
    if (monitor && (content_size.Width != monitor->expected_bounds.width() || content_size.Height != monitor->expected_bounds.height()))
        throw DiagnosticError("Monitor WGC item geometry differs from physical bounds.");
    if (content_size.Width <= 0 || content_size.Height <= 0)
    {
        throw DiagnosticError("Capture target has an empty content size.");
    }

    auto pool_size = content_size;
#if CLIPSS2_AUDIT_FEATURES
    // Owned fixture only: real surface padding/clipping, never fake ContentSize.
    if (!monitor)
    {
        wchar_t padding[32]{};
        const auto n = GetEnvironmentVariableW(L"_CLIPSS2_AUDIT_FRAMEPOOL_PADDING", padding, 32);
        if (n && n < 32)
        {
            const std::wstring_view value{padding};
            if (value == L"14x7") { pool_size.Width += 14; pool_size.Height += 7; }
            else if (value == L"-14x-7") { pool_size.Width -= 14; pool_size.Height -= 7; }
        }
    }
#endif
    CaptureResources resources;
    resources.frame_pool = capture_stage({"wgc.framepool", "CreateFreeThreaded", "IDirect3D11CaptureFramePoolStatics2"}, [&] {
        return Direct3D11CaptureFramePool::CreateFreeThreaded(
            detail::D3dDeviceAccess::winrt_device(device), winrt_pixel_format(pixel_format),
            frame_pool_buffer_count, pool_size);
    });
#if CLIPSS2_AUDIT_FEATURES
    if (!monitor)
    {
        wchar_t mode[32]{};
        if (GetEnvironmentVariableW(L"_CLIPSS2_AUDIT_FRAMEPOOL_PADDING", mode, 32) == 6 &&
            std::wstring_view{mode} == L"resize")
        {
            std::cout << "Owned frame pool ready\n" << std::flush;
            Sleep(1000); // Parent resizes its own fixture after this marker.
        }
    }
#endif
    resources.session = capture_stage({"wgc.session", "CreateCaptureSession", "IGraphicsCaptureSession"}, [&] {
        return resources.frame_pool.CreateCaptureSession(item);
    });
    capture_stage({"wgc.session", "Configure cursor and system border policy", "IGraphicsCaptureSession2"}, [&] {
        configure_session(resources.session, options);
    });

    const auto state = std::make_shared<FrameState>();
    const auto frame_wait_started = std::chrono::steady_clock::now();
    FrameCloser frame{wait_for_frame(resources, state, timeout)};
    const auto frame_wait_duration =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - frame_wait_started);

    const auto frame_content_size = frame.get().ContentSize();
    const auto surface = frame.get().Surface();
    const auto access = surface.as<
        ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    winrt::com_ptr<ID3D11Texture2D> texture;
    const HRESULT interface_result = access->GetInterface(
        winrt::guid_of<ID3D11Texture2D>(), texture.put_void());
    if (FAILED(interface_result))
        throw_hresult_failure(
            "IDirect3DSurface GetInterface(ID3D11Texture2D) failed",
            interface_result);

    D3D11_TEXTURE2D_DESC description{};
    texture->GetDesc(&description);

    if (description.Format != native_pixel_format(pixel_format))
    {
        throw DiagnosticError(
            "Capture texture format does not match the requested pixel format.");
    }

    if (monitor)
    {
        (void)resolve_monitor_source(*monitor);
        if (frame_content_size.Width != content_size.Width || frame_content_size.Height != content_size.Height ||
            description.Width != static_cast<UINT>(content_size.Width) || description.Height != static_cast<UINT>(content_size.Height))
            throw DiagnosticError("Monitor WGC frame geometry changed; no output published.");
    }
    const auto crop = monitor ? monitor->crop : window_frame_crop(frame_content_size.Width,
        frame_content_size.Height, description.Width, description.Height);
    const auto validate_pool = [&] {
        if (!monitor)
        {
            const auto current = item.Size();
            require_stable_frame_pool(content_size.Width, content_size.Height, current.Width, current.Height,
                pool_size.Width, pool_size.Height, description.Width, description.Height);
        }
    };
    validate_pool();
    auto readback = capture_stage({"texture.readback", "Staging texture / Map / CPU rows", "ID3D11Resource"}, [&] {
        return readback_texture(device, texture, crop);
    });
    validate_pool();

    OneFrameCaptureResult result{
        frame_content_size.Width,
        frame_content_size.Height,
        description.Width,
        description.Height,
        dxgi_format_name(description.Format),
        std::move(readback.image),
        readback.mapped_row_pitch,
        readback.logical_row_bytes,
        frame_wait_duration,
        readback.wait_duration,
        readback.busy_retry_count};

    frame.close();
    resources.close();
    return result;
    });
}

OneFrameCaptureResult capture_one_frame(const D3dDevice& device, const WindowCaptureTarget& target,
    CapturePixelFormat format, CaptureOptions options, std::chrono::milliseconds timeout)
{ return capture_item_frame(device,create_capture_item(reinterpret_cast<HWND>(target.hwnd_value)),format,options,timeout); }

OneFrameCaptureResult capture_one_frame(const D3dDevice& device, const CaptureSource& source,
    CapturePixelFormat format, CaptureOptions options, std::chrono::milliseconds timeout)
{
    if (const auto* window=std::get_if<WindowSource>(&source))
        return capture_one_frame(device,WindowCaptureTarget{window->hwnd_value,{}},format,options,timeout);
    const auto& monitor=std::get<MonitorSource>(source);
    const auto handle=reinterpret_cast<HMONITOR>(resolve_monitor_source(monitor));
    const auto item = capture_stage({"wgc.item", "GraphicsCaptureItem factory / CreateForMonitor", "IGraphicsCaptureItemInterop"}, [&] {
        const auto interop=winrt::get_activation_factory<GraphicsCaptureItem,IGraphicsCaptureItemInterop>();
        GraphicsCaptureItem value{nullptr};
        const HRESULT hr=interop->CreateForMonitor(handle,winrt::guid_of<GraphicsCaptureItem>(),winrt::put_abi(value));
        if (FAILED(hr)) throw_hresult_failure("GraphicsCaptureItem CreateForMonitor failed",hr);
        return value;
    });
    return capture_item_frame(device,item,format,options,timeout,&monitor);
}
}

#include "d3d_device.h"
#include "d3d_device_internal.h"
#include "platform/windows/error_diagnostics.h"

#include <d3d11.h>
#include <dxgi.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <utility>

#include <winrt/base.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

namespace clipss2::platform::windows
{
namespace
{
winrt::com_ptr<ID3D11Device> create_native_device()
{
    constexpr UINT base_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    UINT flags = base_flags;

#ifndef NDEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    winrt::com_ptr<ID3D11Device> device;
    HRESULT result = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        device.put(),
        nullptr,
        nullptr);

#ifndef NDEBUG
    if (result == DXGI_ERROR_SDK_COMPONENT_MISSING)
    {
        result = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            base_flags,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            device.put(),
            nullptr,
            nullptr);
    }
#endif

    winrt::check_hresult(result);
    return device;
}
}

struct D3dDevice::Impl
{
    winrt::com_ptr<ID3D11Device> d3d11_device;
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice winrt_device{nullptr};
};

D3dDevice D3dDevice::create()
{
    auto impl = std::make_unique<Impl>();
    impl->d3d11_device = capture_stage({"d3d.device", "D3D11CreateDevice", "ID3D11Device"}, [] {
        return create_native_device();
    });

    const auto dxgi_device = capture_stage({"d3d.winrt.bridge", "QueryInterface", "IDXGIDevice"}, [&] {
        return impl->d3d11_device.as<IDXGIDevice>();
    });
    winrt::com_ptr<IInspectable> inspectable;
    capture_stage({"d3d.winrt.bridge", "CreateDirect3D11DeviceFromDXGIDevice", "IInspectable"}, [&] {
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi_device.get(), inspectable.put()));
    });
    impl->winrt_device = capture_stage({"d3d.winrt.bridge", "QueryInterface", "IDirect3DDevice"}, [&] {
        return inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
    });

    return D3dDevice{std::move(impl)};
}

D3dDevice::D3dDevice(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{
}

D3dDevice::~D3dDevice() = default;
D3dDevice::D3dDevice(D3dDevice&&) noexcept = default;
D3dDevice& D3dDevice::operator=(D3dDevice&&) noexcept = default;

bool D3dDevice::has_d3d11_device() const noexcept
{
    return impl_ && static_cast<bool>(impl_->d3d11_device);
}

bool D3dDevice::has_winrt_device() const noexcept
{
    return impl_ && static_cast<bool>(impl_->winrt_device);
}

winrt::com_ptr<ID3D11Device>
detail::D3dDeviceAccess::native_device(const D3dDevice& device)
{
    if (!device.impl_ || !device.impl_->d3d11_device)
    {
        throw winrt::hresult_error(E_POINTER, L"D3D device is not initialized.");
    }

    return device.impl_->d3d11_device;
}

winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice
detail::D3dDeviceAccess::winrt_device(const D3dDevice& device)
{
    if (!device.impl_ || !device.impl_->winrt_device)
    {
        throw winrt::hresult_error(E_POINTER, L"D3D device is not initialized.");
    }

    return device.impl_->winrt_device;
}
}

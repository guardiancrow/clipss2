#pragma once

#include <d3d11.h>

#include <winrt/base.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include "d3d_device.h"

namespace clipss2::platform::windows::detail
{
class D3dDeviceAccess final
{
public:
    static winrt::com_ptr<ID3D11Device> native_device(const D3dDevice& device);

    static winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice
    winrt_device(const D3dDevice& device);
};
}

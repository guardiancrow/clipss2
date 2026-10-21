#pragma once

#include <memory>

namespace clipss2::platform::windows
{
namespace detail
{
class D3dDeviceAccess;
}

class D3dDevice final
{
public:
    static D3dDevice create();

    ~D3dDevice();
    D3dDevice(D3dDevice&&) noexcept;
    D3dDevice& operator=(D3dDevice&&) noexcept;

    D3dDevice(const D3dDevice&) = delete;
    D3dDevice& operator=(const D3dDevice&) = delete;

    [[nodiscard]] bool has_d3d11_device() const noexcept;
    [[nodiscard]] bool has_winrt_device() const noexcept;

private:
    friend class detail::D3dDeviceAccess;

    struct Impl;

    explicit D3dDevice(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};
}

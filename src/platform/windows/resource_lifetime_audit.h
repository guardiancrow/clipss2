#pragma once

namespace clipss2::platform::windows
{
class D3dDevice;
[[nodiscard]] int run_resource_lifetime_audit(const D3dDevice& device);
}

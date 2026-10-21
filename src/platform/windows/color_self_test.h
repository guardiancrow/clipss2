#pragma once
namespace clipss2::platform::windows
{
class D3dDevice;
// Explicit interactive Phase 6 harness, not part of automatic CTest.
int run_color_self_test(const D3dDevice& device);
int run_color_self_test_all_monitors(const D3dDevice& device);
}

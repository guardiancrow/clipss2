#pragma once

#include <windows.h>

namespace clipss2::platform::windows
{
enum class ReadbackWaitAction
{
    succeeded,
    retry,
    timed_out,
    failed,
};

[[nodiscard]] ReadbackWaitAction decide_readback_map_result(
    HRESULT result, bool deadline_expired) noexcept;
[[nodiscard]] bool is_device_lost_hresult(HRESULT result) noexcept;
}

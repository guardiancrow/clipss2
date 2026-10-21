#include "platform/windows/readback_wait.h"

#include <dxgi.h>

namespace clipss2::platform::windows
{
ReadbackWaitAction decide_readback_map_result(
    const HRESULT result, const bool deadline_expired) noexcept
{
    if (SUCCEEDED(result))
        return ReadbackWaitAction::succeeded;
    if (result == DXGI_ERROR_WAS_STILL_DRAWING)
        return deadline_expired ? ReadbackWaitAction::timed_out
                                : ReadbackWaitAction::retry;
    return ReadbackWaitAction::failed;
}

bool is_device_lost_hresult(const HRESULT result) noexcept
{
    return result == DXGI_ERROR_DEVICE_REMOVED ||
           result == DXGI_ERROR_DEVICE_RESET ||
           result == DXGI_ERROR_DEVICE_HUNG ||
           result == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}
}

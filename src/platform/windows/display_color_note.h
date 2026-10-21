#pragma once

#include "audit/audit_policy.h"
#include <string>
#if CLIPSS2_AUDIT_FEATURES
#include <iomanip>
#include <sstream>
#endif

namespace clipss2::platform::windows::detail
{
// Only fixture self-tests display this explanation. Provider/state data stay separate.
inline std::string window_color_fallback_note(HRESULT window_result)
{
#if CLIPSS2_AUDIT_FEATURES
    std::ostringstream note;
    note << "GetForWindow failed with HRESULT 0x" << std::hex
         << static_cast<unsigned long>(window_result)
         << "; used the target HWND's nearest monitor.";
    return note.str();
#else
    (void)window_result;
    return {};
#endif
}
}

#pragma once

#include <windows.h>

// Explicit target policy, independent of Debug/Release and NDEBUG.
#ifndef CLIPSS2_AUDIT_FEATURES
#define CLIPSS2_AUDIT_FEATURES 0
#endif
#if CLIPSS2_AUDIT_FEATURES != 0 && CLIPSS2_AUDIT_FEATURES != 1
#error CLIPSS2_AUDIT_FEATURES must be 0 or 1
#endif

namespace clipss2::audit
{
inline DWORD environment_variable(
    const wchar_t* name, wchar_t* buffer, const DWORD capacity) noexcept
{
#if CLIPSS2_AUDIT_FEATURES
    return GetEnvironmentVariableW(name, buffer, capacity);
#else
    (void)name;
    (void)buffer;
    (void)capacity;
    return 0;
#endif
}
}

#pragma once

#include <Windows.h>
#include <mutex>

namespace clipss2::platform::windows
{
// A single settings transaction. Construct before acquiring so ownership can
// be unwound even when the second acquisition fails.
class SettingsIoLock final
{
public:
    SettingsIoLock();
    ~SettingsIoLock();
    SettingsIoLock(const SettingsIoLock&) = delete;
    SettingsIoLock& operator=(const SettingsIoLock&) = delete;

    void acquire();

private:
    HANDLE handle_;
    bool owns_named_mutex_{};
    std::unique_lock<std::timed_mutex> local_;
};
}

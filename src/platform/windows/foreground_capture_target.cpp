#include "platform/windows/foreground_capture_target.h"
#include <windows.h>
#include <stdexcept>
#include <utility>

namespace clipss2::platform::windows
{
WindowCaptureTarget foreground_window_capture_target()
{
    const HWND window = GetForegroundWindow();
    if (window == nullptr)
    {
        throw std::runtime_error("GetForegroundWindow returned a null HWND.");
    }
    if (!IsWindow(window))
    {
        throw std::runtime_error("GetForegroundWindow returned an invalid HWND.");
    }

    std::wstring title;
    const int title_length = GetWindowTextLengthW(window);
    if (title_length > 0)
    {
        title.resize(static_cast<std::size_t>(title_length) + 1);
        const int copied = GetWindowTextW(window, title.data(), static_cast<int>(title.size()));
        if (copied > 0)
        {
            title.resize(static_cast<std::size_t>(copied));
        }
        else
        {
            title.clear();
        }
    }

    if (title.empty())
    {
        title = L"<untitled or inaccessible>";
    }

    return WindowCaptureTarget{reinterpret_cast<std::uintptr_t>(window), std::move(title)};
}
}

#include "platform/windows/window_candidates.h"

#include <dwmapi.h>
#include <windows.h>

#include <array>
#include <exception>
#include <iomanip>
#include <sstream>
#include <utility>

#include <winrt/base.h>

namespace clipss2::platform::windows
{
namespace
{
std::string hexadecimal_hresult(HRESULT result)
{
    std::ostringstream stream;
    stream << std::hex << static_cast<std::uint32_t>(result);
    return stream.str();
}

std::wstring window_title(HWND window)
{
    SetLastError(ERROR_SUCCESS);
    const int length = GetWindowTextLengthW(window);
    const DWORD length_error = GetLastError();
    if (length == 0)
    {
        if (length_error != ERROR_SUCCESS)
            return L"<title unavailable: Win32 " + std::to_wstring(length_error) + L">";
        return L"<untitled>";
    }
    std::wstring result(static_cast<std::size_t>(length) + 1, L'\0');
    SetLastError(ERROR_SUCCESS);
    const int copied = GetWindowTextW(window, result.data(), static_cast<int>(result.size()));
    if (copied == 0)
    {
        const DWORD error = GetLastError();
        return error == ERROR_SUCCESS ? L"<untitled>"
            : L"<title unavailable: Win32 " + std::to_wstring(error) + L">";
    }
    result.resize(static_cast<std::size_t>(copied));
    return result;
}

std::wstring window_class(HWND window)
{
    std::array<wchar_t, 256> buffer{};
    SetLastError(ERROR_SUCCESS);
    const int copied = GetClassNameW(window, buffer.data(), static_cast<int>(buffer.size()));
    if (copied == 0)
    {
        const DWORD error = GetLastError();
        return L"<class unavailable: Win32 " + std::to_wstring(error) + L">";
    }
    return {buffer.data(), static_cast<std::size_t>(copied)};
}

struct EnumerationContext
{
    std::vector<WindowCandidate> candidates;
    std::exception_ptr error;
    std::uint32_t own_process_id{GetCurrentProcessId()};
};

BOOL CALLBACK enumerate_callback(HWND window, LPARAM parameter) noexcept
{
    auto& context = *reinterpret_cast<EnumerationContext*>(parameter);
    try
    {
        WindowCandidate candidate;
        candidate.hwnd_value = reinterpret_cast<std::uintptr_t>(window);
        candidate.enumeration_order = context.candidates.size();
        candidate.valid_window = IsWindow(window) != FALSE;
        candidate.visible = IsWindowVisible(window) != FALSE;
        candidate.minimized = IsIconic(window) != FALSE;
        candidate.has_owner = GetWindow(window, GW_OWNER) != nullptr;
        candidate.title = window_title(window);
        candidate.class_name = window_class(window);

        DWORD process_id{};
        SetLastError(ERROR_SUCCESS);
        if (GetWindowThreadProcessId(window, &process_id) == 0)
        {
            candidate.valid_window = false;
            candidate.diagnostic += "process ID unavailable Win32 " +
                std::to_string(GetLastError()) + "; ";
        }
        candidate.process_id = process_id;
        candidate.own_process = process_id == context.own_process_id;

        SetLastError(ERROR_SUCCESS);
        const LONG_PTR extended_style = GetWindowLongPtrW(window, GWL_EXSTYLE);
        const DWORD style_error = GetLastError();
        if (extended_style == 0 && style_error != ERROR_SUCCESS)
            candidate.diagnostic += "extended-style unavailable Win32 " +
                std::to_string(style_error) + "; ";
        else
        {
            candidate.tool_window = (extended_style & WS_EX_TOOLWINDOW) != 0;
            candidate.topmost = (extended_style & WS_EX_TOPMOST) != 0;
        }

        DWORD cloaked{};
        const HRESULT cloak_result = DwmGetWindowAttribute(
            window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        candidate.cloaked_known = SUCCEEDED(cloak_result);
        candidate.cloaked = candidate.cloaked_known && cloaked != 0;
        if (FAILED(cloak_result))
        {
            candidate.cloaked_query_failure = cloak_result;
            candidate.diagnostic += "cloaked unavailable HRESULT 0x" +
                hexadecimal_hresult(cloak_result) + "; ";
        }

        RECT rectangle{};
        const HRESULT frame_result = DwmGetWindowAttribute(
            window, DWMWA_EXTENDED_FRAME_BOUNDS, &rectangle, sizeof(rectangle));
        if (SUCCEEDED(frame_result))
            candidate.used_extended_frame_bounds = true;
        else
        {
            candidate.bounds_query_failure = frame_result;
            SetLastError(ERROR_SUCCESS);
            if (!GetWindowRect(window, &rectangle))
            {
                const DWORD error = GetLastError();
                candidate.bounds_win32_failure = error;
                candidate.diagnostic += "bounds unavailable: DWM HRESULT 0x" +
                    hexadecimal_hresult(frame_result) +
                    ", GetWindowRect Win32 " + std::to_string(error) + "; ";
                rectangle = {};
            }
            else
                candidate.diagnostic += "bounds used GetWindowRect fallback; ";
        }
        candidate.bounds = {rectangle.left, rectangle.top, rectangle.right, rectangle.bottom};
        classify_window_candidate(candidate);
        context.candidates.push_back(std::move(candidate));
        return TRUE;
    }
    catch (...)
    {
        context.error = std::current_exception();
        return FALSE;
    }
}
}

std::vector<WindowCandidate> enumerate_window_candidates()
{
    EnumerationContext context;
    SetLastError(ERROR_SUCCESS);
    if (!EnumWindows(enumerate_callback, reinterpret_cast<LPARAM>(&context)))
    {
        if (context.error) std::rethrow_exception(context.error);
        const DWORD error = GetLastError();
        throw winrt::hresult_error(HRESULT_FROM_WIN32(
            error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error), L"EnumWindows failed.");
    }
    return std::move(context.candidates);
}

bool is_window_candidate_current(const WindowCandidate& candidate)
{
    const auto window = reinterpret_cast<HWND>(candidate.hwnd_value);
    if (!IsWindow(window)) return false;

    DWORD process_id{};
    if (GetWindowThreadProcessId(window, &process_id) == 0 ||
        process_id != candidate.process_id)
        return false;

    if (candidate.class_name.starts_with(L"<class unavailable:")) return true;
    return window_class(window) == candidate.class_name;
}

}

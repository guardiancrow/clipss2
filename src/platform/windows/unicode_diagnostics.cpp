#include "platform/windows/unicode_diagnostics.h"

#include <windows.h>

#include <iostream>
#include <algorithm>
#include <limits>
#include <ostream>
#include <stdexcept>

namespace clipss2::platform::windows
{
namespace
{
std::wstring utf16_from_utf8(const std::string_view value)
{
    if (value.empty()) return {};
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::length_error("UTF-8 diagnostic exceeds conversion bounds.");
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0);
    if (required == 0) throw std::invalid_argument("Invalid UTF-8 diagnostic text.");
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
            result.data(), required) != required)
        throw std::runtime_error("Diagnostic UTF-8 to UTF-16 conversion failed.");
    return result;
}

void write_utf8(
    const DWORD standard_handle, std::ostream& redirected, const std::string_view value)
{
    if (value.empty()) return;
    const std::wstring wide = utf16_from_utf8(value); // Validate the contract for both sinks.
    const HANDLE handle = GetStdHandle(standard_handle);
    // Borrow the current handle without closing/reopening it. A failed
    // diagnostic must not change the resident/capture/worker outcome.
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) return;
    redirected.flush();
    DWORD mode{};
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode))
    {
        std::size_t offset{};
        while (offset < wide.size())
        {
            const auto count = static_cast<DWORD>((std::min)(wide.size() - offset, std::size_t{16384}));
            DWORD written{};
            if (!WriteConsoleW(handle, wide.data() + offset, count, &written, nullptr) || written == 0) return;
            offset += written;
        }
        return;
    }
    // SetStdHandle does not synchronize the CRT descriptor cached by iostream.
    // Files/pipes receive UTF-8 bytes through the current Win32 handle.
    std::size_t offset{};
    while (offset < value.size())
    {
        const auto count = static_cast<DWORD>((std::min)(value.size() - offset, std::size_t{16384}));
        DWORD written{};
        if (!WriteFile(handle, value.data() + offset, count, &written, nullptr) || written == 0) return;
        offset += written;
    }
}
}

std::string utf8_from_utf16(const std::wstring_view value)
{
    if (value.empty()) return {};
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::length_error("UTF-16 diagnostic exceeds conversion bounds.");
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (required == 0) throw std::invalid_argument("Invalid UTF-16 diagnostic text.");
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
            result.data(), required, nullptr, nullptr) != required)
        throw std::runtime_error("Diagnostic UTF-16 to UTF-8 conversion failed.");
    return result;
}

void write_stdout_utf8(const std::string_view value)
{
    write_utf8(STD_OUTPUT_HANDLE, std::cout, value);
}

void write_stderr_utf8(const std::string_view value)
{
    write_utf8(STD_ERROR_HANDLE, std::cerr, value);
}
}

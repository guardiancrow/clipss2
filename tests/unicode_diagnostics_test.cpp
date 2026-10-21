#include "platform/windows/unicode_diagnostics.h"

#include <windows.h>

#include <array>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace clipss2::platform::windows;

namespace
{
void require(const bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

template<std::size_t Size>
std::string utf8_literal(const char8_t (&value)[Size])
{
    return {reinterpret_cast<const char*>(value), Size - 1};
}

void verify_console_utf16(const std::wstring_view value)
{
    const HANDLE screen = CreateConsoleScreenBuffer(
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, CONSOLE_TEXTMODE_BUFFER, nullptr);
    require(screen != INVALID_HANDLE_VALUE, "console screen buffer unavailable");
    const HANDLE original = GetStdHandle(STD_OUTPUT_HANDLE);
    struct Restore final
    {
        HANDLE original;
        HANDLE screen;
        ~Restore() { (void)SetStdHandle(STD_OUTPUT_HANDLE, original); CloseHandle(screen); }
    } restore{original, screen};
    require(SetStdHandle(STD_OUTPUT_HANDLE, screen), "stdout console substitution failed");
    require(SetConsoleCursorPosition(screen, {}), "console cursor reset failed");

    CONSOLE_SCREEN_BUFFER_INFO before{};
    require(GetConsoleScreenBufferInfo(screen, &before),
        "console cursor query failed");
    write_stdout_utf8(utf8_from_utf16(value));
    CONSOLE_SCREEN_BUFFER_INFO after{};
    require(GetConsoleScreenBufferInfo(screen, &after),
        "console cursor query after write failed");
    require(after.dwCursorPosition.Y == before.dwCursorPosition.Y &&
            after.dwCursorPosition.X >= before.dwCursorPosition.X,
        "console output unexpectedly wrapped");
    const auto occupied_cells = static_cast<DWORD>(
        after.dwCursorPosition.X - before.dwCursorPosition.X);
    std::wstring observed(occupied_cells, L'\0');
    DWORD read{};
    require(ReadConsoleOutputCharacterW(
        screen, observed.data(), occupied_cells,
        before.dwCursorPosition, &read),
        "Unicode console readback failed");
    observed.resize(read);
    observed.erase(std::remove(observed.begin(), observed.end(), L'\0'), observed.end());
    require(observed == value, "Unicode console output mismatch");
}
}

int run_tests()
{
    require(utf8_from_utf16(L"Untitled - Notepad") == "Untitled - Notepad",
        "ASCII conversion changed");
    require(utf8_from_utf16(L"タイトルなし - メモ帳") ==
        utf8_literal(u8"タイトルなし - メモ帳"), "Japanese UTF-8 conversion failed");
    require(utf8_from_utf16(L"clipss2 - 日本語 Test 123") ==
        utf8_literal(u8"clipss2 - 日本語 Test 123"), "mixed UTF-8 conversion failed");
    require(utf8_from_utf16(L"補助平面 😀") == utf8_literal(u8"補助平面 😀"),
        "supplementary-plane UTF-8 conversion failed");

    bool invalid_rejected{};
    try { (void)utf8_from_utf16(std::wstring_view{L"\xd800", 1}); }
    catch (const std::invalid_argument&) { invalid_rejected = true; }
    require(invalid_rejected, "invalid UTF-16 diagnostic was accepted");

    verify_console_utf16(L"タイトルなし - メモ帳");
    verify_console_utf16(L"clipss2 - 日本語 Test 123");
    std::cout << "Unicode diagnostic UTF-8/console contract: passed\n";
    return 0;
}

int main()
{
    try { return run_tests(); }
    catch (const std::exception& error)
    {
        std::cerr << "Unicode diagnostic test failed: " << error.what() << '\n';
        return 1;
    }
}

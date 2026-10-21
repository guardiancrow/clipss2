#pragma once

#include <windows.h>

#include <string>

namespace clipss2::platform::windows
{
struct HotkeyBinding
{
    UINT modifiers;
    UINT virtual_key;
};

inline constexpr HotkeyBinding resident_capture_hotkey{
    MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT,
    VK_F11};
inline constexpr UINT resident_show_settings_message = WM_APP + 0x126;

[[nodiscard]] inline std::wstring resident_capture_hotkey_text()
{
    std::wstring text;
    if ((resident_capture_hotkey.modifiers & MOD_CONTROL) != 0) text += L"Ctrl+";
    if ((resident_capture_hotkey.modifiers & MOD_SHIFT) != 0) text += L"Shift+";
    if ((resident_capture_hotkey.modifiers & MOD_ALT) != 0) text += L"Alt+";
    if (resident_capture_hotkey.virtual_key >= VK_F1 &&
        resident_capture_hotkey.virtual_key <= VK_F24)
        text += L'F' + std::to_wstring(
            resident_capture_hotkey.virtual_key - VK_F1 + 1);
    else
        text += L"Key" + std::to_wstring(resident_capture_hotkey.virtual_key);
    return text;
}

// Default launch adds a best-effort notification; explicit --resident stays quiet.
int run_resident_mode(bool notify_startup = false);
int run_settings_mode();
}

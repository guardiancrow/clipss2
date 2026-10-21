#pragma once

#include "platform/windows/product_settings.h"

#include <windows.h>

namespace clipss2::platform::windows
{
inline constexpr wchar_t settings_window_class_name[] = L"clipss2.Settings.Window";
inline constexpr UINT settings_audit_message = WM_APP + 0x160;

enum class SettingsUiResult { cancelled, saved };

// Runs one process-local modal Settings session. All edits remain pending until
// the atomic ProductSettings save succeeds.
[[nodiscard]] SettingsUiResult run_settings_ui(
    ProductSettings& settings, HWND owner = nullptr);
}

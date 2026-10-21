#include "audit/audit_policy.h"
#include "platform/windows/tray_icon.h"

#include "platform/windows/error_diagnostics.h"
#include "platform/windows/resident_mode.h"
#include "platform/windows/ui_localization.h"

#include <shellapi.h>
#include <windowsx.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace clipss2::platform::windows
{
namespace
{
constexpr UINT capture_command = 1;
constexpr UINT exit_command = 2;
constexpr UINT region_command = 3;
constexpr UINT monitor_command = 4;
constexpr UINT settings_command = 5;

NOTIFYICONDATAW notification_data(
    HWND owner, HICON icon, const bool include_content)
{
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = owner;
    data.uID = tray_icon_id;
    if (include_content)
    {
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        data.uCallbackMessage = tray_callback_message;
        data.hIcon = icon;
        (void)wcscpy_s(data.szTip, L"clipss2");
    }
    return data;
}

class PopupMenu final
{
public:
    PopupMenu() : value_(CreatePopupMenu())
    {
        if (value_ == nullptr)
            throw DiagnosticError(format_win32_error(
                "CreatePopupMenu tray menu failed", GetLastError()));
    }
    ~PopupMenu() { if (value_ != nullptr) (void)DestroyMenu(value_); }
    PopupMenu(const PopupMenu&) = delete;
    PopupMenu& operator=(const PopupMenu&) = delete;
    [[nodiscard]] HMENU get() const noexcept { return value_; }
private:
    HMENU value_{};
};
}

TrayCallback decode_tray_callback(
    const WPARAM word, const LPARAM parameter) noexcept
{
    return {
        LOWORD(parameter),
        HIWORD(parameter),
        {GET_X_LPARAM(word), GET_Y_LPARAM(word)}};
}

TrayIcon::~TrayIcon()
{
    std::string ignored;
    (void)remove(ignored);
}

NOTIFYICONDATAW startup_notification_data(HWND owner, const UiLanguage language)
{
    auto data = notification_data(owner, nullptr, false);
    // Realtime prevents a stale startup success being queued long after launch.
    data.uFlags = NIF_INFO | NIF_REALTIME;
    data.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND | NIIF_RESPECT_QUIET_TIME;
    (void)wcscpy_s(data.szInfoTitle,
        ui_string(language, UiStringId::startup_notification_title).data());
    (void)wcscpy_s(data.szInfo,
        ui_string(language, UiStringId::startup_notification_body).data());
    return data;
}

bool TrayIcon::notify_startup(std::string& diagnostic)
{
    if (!lifecycle_active_)
    {
        diagnostic = "Tray registration is not active.";
        return false;
    }
    wchar_t injected_failure[2]{};
    if (clipss2::audit::environment_variable(L"_CLIPSS2_AUDIT_STARTUP_NOTIFICATION_FAIL",
            injected_failure, 2) == 1 && injected_failure[0] == L'1')
    {
        diagnostic = "Injected notification failure (private audit).";
        return false;
    }
    auto data = startup_notification_data(owner_, resolve_ui_language());
    SetLastError(ERROR_SUCCESS);
    if (!Shell_NotifyIconW(NIM_MODIFY, &data))
    {
        diagnostic = format_win32_error("Shell_NotifyIconW startup notification failed", GetLastError());
        return false;
    }
    return true;
}

void TrayIcon::add(HWND owner)
{
    if (owner == nullptr || lifecycle_active_)
        throw std::logic_error("Tray icon add requested with invalid ownership state.");
    owner_ = owner;
    // LoadIcon with a predefined resource returns a shared icon. It must not be
    // passed to DestroyIcon.
    shared_icon_ = LoadIconW(nullptr, IDI_APPLICATION);
    if (shared_icon_ == nullptr)
        throw DiagnosticError(format_win32_error(
            "LoadIconW shared tray icon failed", GetLastError()));

    std::string diagnostic;
    if (!add_to_shell(diagnostic))
        throw DiagnosticError(std::move(diagnostic));
    lifecycle_active_ = true;
    if (!set_version(diagnostic))
    {
        NOTIFYICONDATAW data = notification_data(owner_, shared_icon_, false);
        (void)Shell_NotifyIconW(NIM_DELETE, &data);
        lifecycle_active_ = false;
        owner_ = nullptr;
        shared_icon_ = nullptr;
        throw DiagnosticError(std::move(diagnostic));
    }
}

bool TrayIcon::add_to_shell(std::string& diagnostic) noexcept
{
    NOTIFYICONDATAW data = notification_data(owner_, shared_icon_, true);
    SetLastError(ERROR_SUCCESS);
    if (!Shell_NotifyIconW(NIM_ADD, &data))
    {
        diagnostic = format_win32_error(
            "Shell_NotifyIconW NIM_ADD failed", GetLastError());
        return false;
    }
    return true;
}

bool TrayIcon::set_version(std::string& diagnostic) noexcept
{
    NOTIFYICONDATAW data = notification_data(owner_, shared_icon_, false);
    data.uVersion = NOTIFYICON_VERSION_4;
    SetLastError(ERROR_SUCCESS);
    if (!Shell_NotifyIconW(NIM_SETVERSION, &data))
    {
        diagnostic = format_win32_error(
            "Shell_NotifyIconW NIM_SETVERSION failed", GetLastError());
        return false;
    }
    return true;
}

bool TrayIcon::restore_after_taskbar_created(std::string& diagnostic) noexcept
{
    if (!lifecycle_active_) return false;
    // Makes a synthetic/repeated TaskbarCreated idempotent for the stable
    // hWnd/uID identity, while NIM_DELETE is harmless after a real shell reset.
    NOTIFYICONDATAW data = notification_data(owner_, shared_icon_, false);
    (void)Shell_NotifyIconW(NIM_DELETE, &data);
    if (!add_to_shell(diagnostic)) return false;
    if (!set_version(diagnostic))
    {
        (void)Shell_NotifyIconW(NIM_DELETE, &data);
        return false;
    }
    return true;
}

bool TrayIcon::remove(std::string& diagnostic) noexcept
{
    if (!lifecycle_active_) return true;
    NOTIFYICONDATAW data = notification_data(owner_, shared_icon_, false);
    SetLastError(ERROR_SUCCESS);
    const bool removed = Shell_NotifyIconW(NIM_DELETE, &data) != FALSE;
    if (!removed)
        diagnostic = format_win32_error(
            "Shell_NotifyIconW NIM_DELETE failed", GetLastError());
    lifecycle_active_ = false;
    owner_ = nullptr;
    shared_icon_ = nullptr;
    return removed;
}

TrayCommand TrayIcon::show_context_menu(
    POINT anchor, const bool capture_enabled, std::string& diagnostic)
{
    if (!lifecycle_active_) return TrayCommand::none;
    if (anchor.x == -1 && anchor.y == -1 && !GetCursorPos(&anchor))
        anchor = {};

    PopupMenu menu;
    const UiLanguage language = resolve_ui_language();
    const std::wstring capture_text =
        std::wstring(ui_string(language, UiStringId::tray_capture_window)) + L"\t" + resident_capture_hotkey_text();
    const UINT capture_flags = MF_STRING | (capture_enabled ? MF_ENABLED : MF_GRAYED);
    if (!AppendMenuW(menu.get(), capture_flags, capture_command, capture_text.c_str()) ||
        !AppendMenuW(menu.get(), capture_flags, region_command, ui_string(language, UiStringId::tray_capture_region).data()) ||
        !AppendMenuW(menu.get(), capture_flags, monitor_command, ui_string(language, UiStringId::tray_capture_monitor).data()) ||
        !AppendMenuW(menu.get(), MF_SEPARATOR, 0, nullptr) ||
        !AppendMenuW(menu.get(), capture_flags, settings_command, ui_string(language, UiStringId::tray_settings).data()) ||
        !AppendMenuW(menu.get(), MF_STRING, exit_command, ui_string(language, UiStringId::tray_exit).data()))
        throw DiagnosticError(format_win32_error(
            "AppendMenuW tray menu failed", GetLastError()));

    // This is the documented notification-area menu pattern. Windows may
    // legitimately decline the foreground change for a synthetic callback;
    // the menu operation remains valid.
    (void)SetForegroundWindow(owner_);
    SetLastError(ERROR_SUCCESS);
    audit_active_menu_ = menu.get();
    audit_menu_command_ = 0;
    const UINT physical_command = TrackPopupMenuEx(
        menu.get(), TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        anchor.x, anchor.y, owner_, nullptr);
    const UINT command = audit_menu_command_ ? audit_menu_command_ : physical_command;
    audit_active_menu_ = nullptr;
    audit_menu_command_ = 0;
    if (command == 0 && GetLastError() != ERROR_SUCCESS)
        diagnostic = format_win32_error(
            "TrackPopupMenuEx tray menu failed", GetLastError());
    if (!PostMessageW(owner_, WM_NULL, 0, 0) && diagnostic.empty())
        diagnostic = format_win32_error(
            "PostMessageW WM_NULL after tray menu failed", GetLastError());

    switch (command)
    {
    case capture_command: return TrayCommand::capture_window;
    case region_command: return TrayCommand::capture_region;
    case monitor_command: return TrayCommand::capture_monitor;
    case settings_command: return TrayCommand::settings;
    case exit_command: return TrayCommand::exit;
    default: return TrayCommand::none;
    }
}

std::string notification_focus_diagnostic(bool succeeded)
{
    return succeeded ? std::string{} :
        "Shell_NotifyIconW NIM_SETFOCUS returned FALSE; extended last-error is not specified by the API";
}

void TrayIcon::return_focus_to_notification_area(
    std::string& diagnostic) noexcept
{
    if (!lifecycle_active_) return;
    NOTIFYICONDATAW data = notification_data(owner_, shared_icon_, false);
    SetLastError(ERROR_SUCCESS);
    const bool succeeded = Shell_NotifyIconW(NIM_SETFOCUS, &data) != FALSE;
    if (!succeeded && diagnostic.empty()) diagnostic = notification_focus_diagnostic(false);
}

bool TrayIcon::select_menu_for_audit(const UINT command)
{
    wchar_t enabled[2]{};
    if (clipss2::audit::environment_variable(L"_CLIPSS2_AUDIT_TRAY_INTERNAL_INPUT", enabled, 2) != 1 ||
        enabled[0] != L'1' || !audit_active_menu_) return false;
    if (command != 0)
    {
        const UINT flags = GetMenuState(audit_active_menu_, command, MF_BYCOMMAND);
        if (flags == static_cast<UINT>(-1) || (flags & (MF_DISABLED | MF_GRAYED)))
            return false;
    }
    if (!EndMenu()) throw std::runtime_error("Owned tray audit EndMenu failed.");
    audit_menu_command_ = command;
    return true;
}
}

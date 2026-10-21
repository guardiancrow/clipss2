#pragma once

#include <windows.h>
#include <shellapi.h>

#include <string>

namespace clipss2::platform::windows
{
enum class UiLanguage;
inline constexpr UINT tray_callback_message = WM_APP + 0x130;
inline constexpr UINT tray_icon_id = 1;

enum class TrayCommand
{
    none,
    capture_window,
    capture_region,
    capture_monitor,
    settings,
    exit,
};

struct TrayCallback
{
    UINT event;
    UINT icon_id;
    POINT anchor;
};

[[nodiscard]] TrayCallback decode_tray_callback(WPARAM word, LPARAM parameter) noexcept;
[[nodiscard]] std::string notification_focus_diagnostic(bool succeeded);
[[nodiscard]] NOTIFYICONDATAW startup_notification_data(HWND owner, UiLanguage language);

class TrayIcon final
{
public:
    TrayIcon() = default;
    ~TrayIcon();
    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    void add(HWND owner);
    [[nodiscard]] bool notify_startup(std::string& diagnostic);
    [[nodiscard]] bool restore_after_taskbar_created(
        std::string& diagnostic) noexcept;
    [[nodiscard]] bool remove(std::string& diagnostic) noexcept;
    [[nodiscard]] TrayCommand show_context_menu(
        POINT anchor, bool capture_enabled, std::string& diagnostic);
    void return_focus_to_notification_area(std::string& diagnostic) noexcept;
    // Test-only owned-thread command selection; disabled unless explicitly opted in.
    [[nodiscard]] bool select_menu_for_audit(UINT command);
    [[nodiscard]] bool lifecycle_active() const noexcept
    {
        return lifecycle_active_;
    }

private:
    [[nodiscard]] bool add_to_shell(std::string& diagnostic) noexcept;
    [[nodiscard]] bool set_version(std::string& diagnostic) noexcept;

    HWND owner_{};
    HICON shared_icon_{};
    bool lifecycle_active_{};
    HMENU audit_active_menu_{}; // Borrowed only while show_context_menu owns PopupMenu.
    UINT audit_menu_command_{};
};
}

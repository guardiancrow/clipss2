#include "platform/windows/tray_icon.h"
#include "platform/windows/ui_localization.h"

#include <cstdlib>
#include <iostream>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    constexpr int x = -120;
    constexpr int y = 450;
    const auto word = static_cast<WPARAM>(
        MAKELONG(static_cast<WORD>(x), static_cast<WORD>(y)));
    const auto parameter = static_cast<LPARAM>(
        MAKELONG(NIN_KEYSELECT, clipss2::platform::windows::tray_icon_id));
    const auto decoded = clipss2::platform::windows::decode_tray_callback(
        word, parameter);
    require(decoded.event == NIN_KEYSELECT, "version-4 event decode failed");
    require(decoded.icon_id == clipss2::platform::windows::tray_icon_id,
            "version-4 icon ID decode failed");
    require(decoded.anchor.x == x && decoded.anchor.y == y,
            "version-4 signed coordinates decode failed");
    std::cout << "NOTIFYICON_VERSION_4 callback decode: passed\n";
    using namespace clipss2::platform::windows;
    require(notification_focus_diagnostic(true).empty(), "successful focus must not warn");
    const auto focus = notification_focus_diagnostic(false);
    require(focus.find("returned FALSE") != focus.npos && focus.find("not specified") != focus.npos &&
            focus.find("Win32 error") == focus.npos, "BOOL focus failure must not claim a Win32 cause");
    for (const auto language : {UiLanguage::english, UiLanguage::japanese})
    {
        const auto notice = startup_notification_data(reinterpret_cast<HWND>(1), language);
        require(notice.hWnd == reinterpret_cast<HWND>(1) && notice.uID == tray_icon_id,
                "notification must address the registered tray identity");
        require((notice.uFlags & (NIF_INFO | NIF_REALTIME)) == (NIF_INFO | NIF_REALTIME),
                "startup must use a realtime native notification");
        require((notice.dwInfoFlags & NIIF_NOSOUND) != 0 &&
                (notice.dwInfoFlags & NIIF_RESPECT_QUIET_TIME) != 0,
                "notification must not interrupt the user");
        require(ui_string(language, UiStringId::startup_notification_title) == notice.szInfoTitle &&
                ui_string(language, UiStringId::startup_notification_body) == notice.szInfo,
                "localized startup notification text");
        require(wcslen(notice.szInfoTitle) < 64 && wcslen(notice.szInfo) < 256,
                "notification text must fit the Shell buffers");
    }
    TrayIcon inactive;
    std::string diagnostic;
    require(!inactive.notify_startup(diagnostic) && !diagnostic.empty(),
            "unregistered tray must not announce startup success");
    std::cout << "Localized non-interrupting startup notification contract: passed\n";
    return 0;
}

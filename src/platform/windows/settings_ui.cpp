#include "audit/audit_policy.h"
#include "platform/windows/settings_ui.h"

#include "platform/windows/error_diagnostics.h"
#include "platform/windows/output_storage.h"
#include "platform/windows/settings_location.h"
#include "platform/windows/settings_layout.h"
#include "platform/windows/ui_localization.h"
#include "platform/windows/unicode_diagnostics.h"

#include <windows.h>
#include <commctrl.h>
#if CLIPSS2_AUDIT_FEATURES
#include <oleacc.h>
#include <wrl/client.h>
#endif

#include <algorithm>
#include <array>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <iostream>

namespace clipss2::platform::windows
{
namespace
{
constexpr int path_id = 100;
constexpr int change_id = 101;
constexpr int reset_id = 102;
constexpr int filename_id = 103;
constexpr int language_id = 104;
constexpr int warning_id = 105;
constexpr int settings_path_id = 106;
constexpr int settings_folder_id = 107;
constexpr wchar_t settings_content_class[] = L"clipss2.Settings.Content";
constexpr UINT_PTR audit_timer_id = 1;
constexpr UINT_PTR audit_error_timer_id = 2;

BOOL CALLBACK close_owned_message_box(HWND candidate, LPARAM owner) noexcept
{
    wchar_t type[16]{};
    if (GetClassNameW(candidate, type, static_cast<int>(std::size(type))) &&
        std::wstring_view{type} == L"#32770" &&
        GetWindow(candidate, GW_OWNER) == reinterpret_cast<HWND>(owner))
    {
        (void)PostMessageW(candidate, WM_CLOSE, 0, 0);
        return FALSE;
    }
    return TRUE;
}

int scaled(const int value, const UINT dpi)
{
    return MulDiv(value, static_cast<int>(dpi == 0 ? 96 : dpi), 96);
}

class Font final
{
public:
    ~Font() { reset(); }
    Font() = default;
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;
    void reset(HFONT value = nullptr)
    {
        if (value_) (void)DeleteObject(value_);
        value_ = value;
    }
    [[nodiscard]] HFONT get() const noexcept { return value_; }
    void swap(Font& other) noexcept { std::swap(value_, other.value_); }
private:
    HFONT value_{};
};

class SettingsWindow final
{
public:
    SettingsWindow(ProductSettings& settings, HWND owner)
        : settings_(settings), owner_(owner), pending_(settings.preferences()),
          language_(resolve_ui_language(settings.preferences().ui_language)) {}

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    SettingsUiResult run()
    {
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        if (!instance) throw std::runtime_error(format_win32_error(
            "GetModuleHandleW Settings failed", GetLastError()));
        WNDCLASSEXW type{};
        type.cbSize = sizeof(type);
        type.lpfnWndProc = procedure;
        type.hInstance = instance;
        type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        type.lpszClassName = settings_window_class_name;
        const ATOM atom = RegisterClassExW(&type);
        const bool registered = atom != 0;
        if (!registered && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throw std::runtime_error(format_win32_error(
                "RegisterClassExW Settings failed", GetLastError()));
        type.lpfnWndProc = content_procedure;
        type.lpszClassName = settings_content_class;
        const bool content_registered = RegisterClassExW(&type) != 0;
        if (!content_registered && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throw std::runtime_error("Register Settings content failed.");

        window_ = CreateWindowExW(
            WS_EX_CONTROLPARENT, settings_window_class_name,
            ui_string(language_, UiStringId::settings_title).data(),
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            CW_USEDEFAULT, CW_USEDEFAULT, 1, 1, owner_, nullptr, instance, this);
        if (!window_)
        {
            if (registered) (void)UnregisterClassW(settings_window_class_name, instance);
            if (content_registered) (void)UnregisterClassW(settings_content_class, instance);
            throw std::runtime_error(format_win32_error(
                "CreateWindowExW Settings failed", GetLastError()));
        }
        ShowWindow(window_, SW_SHOWNORMAL);
        (void)SetForegroundWindow(window_);

        std::optional<int> quit_code;
        MSG message{};
        while (!finished_)
        {
            const BOOL result = GetMessageW(&message, nullptr, 0, 0);
            if (result == -1)
                throw std::runtime_error(format_win32_error(
                    "GetMessageW Settings failed", GetLastError()));
            if (result == 0) { quit_code = static_cast<int>(message.wParam); break; }
            if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE)
            {
                cancel();
                continue;
            }
            if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN &&
                !combo_is_open())
            {
                const HWND focus = GetFocus();
                if (focus == settings_folder_) open_folder();
                else if (focus == cancel_) cancel();
                else if (focus == change_) change_folder();
                else if (focus == reset_) { pending_.output_directory.explicit_path.reset(); update_path(); }
                else save();
                continue;
            }
            if (!IsDialogMessageW(window_, &message))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        if (window_ && IsWindow(window_)) (void)DestroyWindow(window_);
        if (registered) (void)UnregisterClassW(settings_window_class_name, instance);
        if (content_registered) (void)UnregisterClassW(settings_content_class, instance);
        if (quit_code) PostQuitMessage(*quit_code);
        if (callback_error_) std::rethrow_exception(callback_error_);
        return result_;
    }

    static LRESULT CALLBACK procedure(
        HWND window, UINT message, WPARAM word, LPARAM parameter) noexcept
    {
        if (message == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(parameter);
            auto* self = static_cast<SettingsWindow*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->window_ = window;
        }
        auto* self = reinterpret_cast<SettingsWindow*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (!self) return DefWindowProcW(window, message, word, parameter);
        try { return self->handle_message(window, message, word, parameter); }
        catch (...)
        {
            self->callback_error_ = std::current_exception();
            self->finished_ = true;
            (void)DestroyWindow(window);
            return 0;
        }
    }

private:
    static LRESULT CALLBACK content_procedure(HWND window, UINT message, WPARAM word, LPARAM parameter) noexcept
    {
        if (message == WM_NCCREATE)
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(
                static_cast<CREATESTRUCTW*>(reinterpret_cast<void*>(parameter))->lpCreateParams));
        auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (self) try
        {
            if (message == WM_COMMAND) return self->handle_message(self->window_, message, word, parameter);
            if (message == WM_PAINT) { self->paint_separators(); return 0; }
            if (message == WM_VSCROLL) { self->scroll(LOWORD(word)); return 0; }
            if (message == WM_MOUSEWHEEL) { self->scroll_to(self->scroll_position_ - GET_WHEEL_DELTA_WPARAM(word) / WHEEL_DELTA * scaled(72, self->dpi_)); return 0; }
            if (message == WM_NCDESTROY) SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        }
        catch (...) { self->callback_error_ = std::current_exception(); self->cancel(); return 0; }
        return DefWindowProcW(window, message, word, parameter);
    }
    HWND create_control(
        const wchar_t* type, const std::wstring_view text, DWORD style, int id, HWND parent = nullptr)
    {
        const HWND child = CreateWindowExW(
            0, type, std::wstring{text}.c_str(), WS_CHILD | WS_VISIBLE | style,
            0, 0, 1, 1, parent ? parent : content_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr), nullptr);
        if (!child) throw std::runtime_error(format_win32_error(
            "CreateWindowExW Settings control failed", GetLastError()));
        return child;
    }

    LRESULT handle_message(HWND window, UINT message, WPARAM word, LPARAM parameter)
    {
        switch (message)
        {
        case WM_CREATE: create_controls(); return 0;
        case WM_COMMAND:
            if ((HIWORD(word) == EN_SETFOCUS || HIWORD(word) == CBN_SETFOCUS || HIWORD(word) == BN_SETFOCUS) && parameter)
                reveal(reinterpret_cast<HWND>(parameter));
            if (HIWORD(word) == CBN_SELCHANGE && LOWORD(word) == filename_id)
                pending_.filename_policy = static_cast<FilenamePolicy>(
                    SendMessageW(filename_, CB_GETCURSEL, 0, 0));
            else if (HIWORD(word) == CBN_SELCHANGE && LOWORD(word) == language_id)
                pending_.ui_language = static_cast<UiLanguagePreference>(
                    SendMessageW(language_combo_, CB_GETCURSEL, 0, 0));
            else if (LOWORD(word) == change_id && HIWORD(word) == BN_CLICKED) change_folder();
            else if (LOWORD(word) == reset_id && HIWORD(word) == BN_CLICKED)
            {
                pending_.output_directory.explicit_path.reset();
                update_path();
            }
            else if (LOWORD(word) == IDOK && HIWORD(word) == BN_CLICKED) save();
            else if (LOWORD(word) == IDCANCEL && HIWORD(word) == BN_CLICKED) cancel();
            else if (LOWORD(word) == settings_folder_id && HIWORD(word) == BN_CLICKED) open_folder();
            return 0;
        case WM_MOUSEWHEEL:
            scroll_to(scroll_position_ - GET_WHEEL_DELTA_WPARAM(word) / WHEEL_DELTA * scaled(72, dpi_)); return 0;
        case WM_DPICHANGED:
        {
            const auto* suggested = reinterpret_cast<RECT*>(parameter);
            apply_dpi(HIWORD(word), nullptr, suggested);
            return 0;
        }
        case settings_audit_message:
            run_audit_action();
            return 0;
        case WM_TIMER:
            if (word == audit_timer_id)
            {
                (void)KillTimer(window_, audit_timer_id);
                (void)PostMessageW(window_, settings_audit_message, 0, 0);
            }
            else if (word == audit_error_timer_id)
            {
                (void)KillTimer(window_, audit_error_timer_id);
                (void)EnumThreadWindows(
                    GetCurrentThreadId(), close_owned_message_box,
                    reinterpret_cast<LPARAM>(window_));
            }
            return 0;
        case WM_CLOSE: cancel(); return 0;
        case WM_DESTROY: window_ = nullptr; return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            return DefWindowProcW(window, message, word, parameter);
        default: return DefWindowProcW(window, message, word, parameter);
        }
    }

    void create_controls()
    {
        content_ = CreateWindowExW(WS_EX_CONTROLPARENT, settings_content_class, L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN, 0, 0, 1, 1,
            window_, nullptr, GetModuleHandleW(nullptr), this);
        if (!content_) throw std::runtime_error("Create Settings content failed.");
        save_label_ = create_control(L"STATIC",
            ui_string(language_, UiStringId::settings_save_location), SS_LEFT, -1);
        path_ = create_control(L"EDIT", L"", ES_READONLY | ES_AUTOHSCROLL | WS_TABSTOP | WS_BORDER,
            path_id);
        change_ = create_control(L"BUTTON", ui_string(language_, UiStringId::settings_change),
            BS_PUSHBUTTON | BS_NOTIFY | WS_TABSTOP, change_id);
        reset_ = create_control(L"BUTTON", ui_string(language_, UiStringId::settings_reset),
            BS_PUSHBUTTON | BS_NOTIFY | WS_TABSTOP, reset_id);
        filename_label_ = create_control(L"STATIC",
            ui_string(language_, UiStringId::settings_file_naming), SS_LEFT, -1);
        filename_ = create_control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
            filename_id);
        for (const auto id : {UiStringId::settings_filename_timestamp,
                 UiStringId::settings_filename_sequence, UiStringId::settings_filename_guid})
            (void)SendMessageW(filename_, CB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(ui_string(language_, id).data()));
        (void)SendMessageW(filename_, CB_SETCURSEL,
            static_cast<WPARAM>(pending_.filename_policy), 0);
        language_label_ = create_control(L"STATIC",
            ui_string(language_, UiStringId::settings_language), SS_LEFT, -1);
        language_combo_ = create_control(L"COMBOBOX", L"",
            CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, language_id);
        for (const auto id : {UiStringId::settings_language_system,
                 UiStringId::settings_language_english, UiStringId::settings_language_japanese})
            (void)SendMessageW(language_combo_, CB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(ui_string(language_, id).data()));
        (void)SendMessageW(language_combo_, CB_SETCURSEL,
            static_cast<WPARAM>(pending_.ui_language), 0);
        section_ = create_control(L"STATIC", ui_string(language_, UiStringId::settings_file_section), SS_LEFT, -1);
        mode_ = create_control(L"STATIC", L"", SS_LEFT, -1);
        settings_path_label_ = create_control(L"STATIC", ui_string(language_, UiStringId::settings_file_location), SS_LEFT, -1);
        settings_path_ = create_control(L"EDIT", L"", ES_READONLY | ES_AUTOHSCROLL | WS_TABSTOP | WS_BORDER, settings_path_id);
        file_state_ = create_control(L"STATIC", L"", SS_LEFT, -1);
        warning_ = create_control(L"STATIC", L"", SS_LEFT, warning_id);
        settings_folder_ = create_control(L"BUTTON", ui_string(language_, UiStringId::settings_open_folder), BS_PUSHBUTTON | BS_NOTIFY | WS_TABSTOP, settings_folder_id);
        parent_hint_ = create_control(L"STATIC", ui_string(language_, UiStringId::settings_parent_unavailable), SS_LEFT, -1);
        ok_ = create_control(L"BUTTON", ui_string(language_, UiStringId::settings_ok),
            BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, window_);
        cancel_ = create_control(L"BUTTON", ui_string(language_, UiStringId::settings_cancel),
            BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, window_);
        const bool writable = settings_.load_result().persistence_enabled;
        EnableWindow(ok_, writable);
        update_path();
        update_location();
        apply_dpi(GetDpiForWindow(window_));
        SetFocus(path_);
        audit_action_ = environment(L"_CLIPSS2_AUDIT_SETTINGS_ACTION");
        if (!audit_action_.empty())
        {
            UINT delay = 50;
            const std::wstring delay_text = environment(L"_CLIPSS2_AUDIT_SETTINGS_DELAY_MS");
            if (!delay_text.empty())
            {
                const unsigned long parsed = std::wcstoul(delay_text.c_str(), nullptr, 10);
                delay = static_cast<UINT>(std::clamp(parsed, 10UL, 30000UL));
            }
            if (!SetTimer(window_, audit_timer_id, delay, nullptr))
                throw std::runtime_error(format_win32_error(
                    "SetTimer Settings audit failed", GetLastError()));
        }
    }

    auto body_controls() const
    {
        return std::array{save_label_, path_, change_, reset_, filename_label_, filename_,
            language_label_, language_combo_, section_, mode_, settings_path_label_, settings_path_,
            file_state_, warning_, settings_folder_, parent_hint_};
    }

    auto heading_controls() const
    {
        return std::array{save_label_, filename_label_, language_label_, section_};
    }

    bool is_heading(HWND control) const
    {
        const auto headings = heading_controls();
        return std::find(headings.begin(), headings.end(), control) != headings.end();
    }

    void paint_separators() const noexcept
    {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(content_, &paint);
        if (dc)
        {
            RECT client{}; GetClientRect(content_, &client);
            const int margin = scaled(18, dpi_);
            for (const int y : separator_y_)
            {
                const RECT line{margin, y - scroll_position_, client.right - margin,
                    y - scroll_position_ + std::max(1, scaled(1, dpi_))};
                FillRect(dc, &line, GetSysColorBrush(COLOR_3DSHADOW));
            }
        }
        EndPaint(content_, &paint);
    }

    void update_location(const SettingsMetadataProbe& probe = {})
    {
        location_ = settings_location_view(settings_.store(), settings_.load_result(), probe);
        SetWindowTextW(mode_, ui_string(language_, location_.mode).data());
        SetWindowTextW(settings_path_, location_.path.c_str());
        EnableWindow(settings_path_, location_.resolved);
        std::wstring state{ui_string(language_, settings_presence_text(location_.file.presence))};
        if (location_.file.presence == SettingsPresence::unknown && location_.file.error)
            state += L" (Win32: " + std::to_wstring(location_.file.error) + L")";
        if (save_failed_) state += L"\n" + std::wstring{ui_string(language_, UiStringId::settings_unsaved)};
        SetWindowTextW(file_state_, state.c_str());
        std::wstring warning;
        if (save_failed_) warning = ui_string(language_, UiStringId::settings_write_guidance);
        else if (location_.guidance != UiStringId::count) warning = ui_string(language_, location_.guidance);
        SetWindowTextW(warning_, warning.c_str());
        ShowWindow(warning_, warning.empty() ? SW_HIDE : SW_SHOW);
        const bool folder = location_.resolved && location_.parent.presence == SettingsPresence::directory;
        EnableWindow(settings_folder_, folder && !folder_opening_);
        ShowWindow(parent_hint_, folder ? SW_HIDE : SW_SHOW);
    }

    void open_folder()
    {
        if (folder_opening_ || !location_.resolved) return;
        folder_opening_ = true;
        EnableWindow(settings_folder_, FALSE);
#if CLIPSS2_AUDIT_FEATURES
        const HRESULT result = open_settings_parent(settings_.store(), true, {}, folder_spy_);
#else
        const HRESULT result = open_settings_parent(settings_.store(), true);
#endif
        folder_opening_ = false;
        if (!window_ || finished_) return;
        EnableWindow(settings_folder_, location_.parent.presence == SettingsPresence::directory);
        if (FAILED(result))
        {
            wchar_t code[40]{};
            swprintf_s(code, L"\nHRESULT: 0x%08lX", static_cast<unsigned long>(result));
            const auto message = std::wstring{ui_string(language_, UiStringId::settings_folder_error)} + code;
            MessageBoxW(window_, message.c_str(), ui_string(language_, UiStringId::settings_folder_error_title).data(), MB_OK | MB_ICONINFORMATION);
        }
    }

    void scroll_to(int requested)
    {
        const int next = std::clamp(requested, 0, std::max(0, content_height_ - viewport_height_));
        const int delta = scroll_position_ - next;
        scroll_position_ = next;
        for (const HWND control : body_controls())
        {
            RECT rectangle{}; GetWindowRect(control, &rectangle);
            MapWindowPoints(nullptr, content_, reinterpret_cast<POINT*>(&rectangle), 2);
            SetWindowPos(control, nullptr, rectangle.left, rectangle.top + delta, 0, 0,
                SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
        }
        SetScrollPos(content_, SB_VERT, scroll_position_, TRUE);
        InvalidateRect(content_, nullptr, TRUE);
    }

    void scroll(UINT command)
    {
        int next = scroll_position_;
        if (command == SB_LINEUP) next -= scaled(24, dpi_);
        else if (command == SB_LINEDOWN) next += scaled(24, dpi_);
        else if (command == SB_PAGEUP) next -= viewport_height_;
        else if (command == SB_PAGEDOWN) next += viewport_height_;
        else if (command == SB_TOP) next = 0;
        else if (command == SB_BOTTOM) next = content_height_;
        else if (command == SB_THUMBTRACK || command == SB_THUMBPOSITION)
        {
            SCROLLINFO info{sizeof(info), SIF_TRACKPOS}; GetScrollInfo(content_, SB_VERT, &info); next = info.nTrackPos;
        }
        scroll_to(next);
    }

    void reveal(HWND control)
    {
        if (GetParent(control) != content_) return;
        RECT rectangle{}; GetWindowRect(control, &rectangle);
        MapWindowPoints(nullptr, content_, reinterpret_cast<POINT*>(&rectangle), 2);
        if (rectangle.top < 0) scroll_to(scroll_position_ + rectangle.top - scaled(8, dpi_));
        else if (rectangle.bottom > viewport_height_)
            scroll_to(scroll_position_ + rectangle.bottom - viewport_height_ + scaled(8, dpi_));
    }

    void position_root(const RECT& rectangle)
    {
        RECT current{}; GetWindowRect(window_, &current);
        if (settings_same_rect(current, rectangle)) return;
        if (!SetWindowPos(window_, nullptr, rectangle.left, rectangle.top,
                rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
                SWP_NOACTIVATE | SWP_NOZORDER))
            throw std::runtime_error("Position Settings window failed.");
#if CLIPSS2_AUDIT_FEATURES
        ++audit_root_updates_;
#endif
    }

    void apply_dpi(UINT dpi, const RECT* fixture_work = nullptr, const RECT* suggested = nullptr)
    {
        SettingsLayoutRequest request{dpi ? dpi : 96};
        if (suggested) request.suggested = *suggested;
#if CLIPSS2_AUDIT_FEATURES
        if (fixture_work) request.work = *fixture_work;
        else if (audit_work_) request.work = audit_work_;
#else
        (void)fixture_work;
#endif
        layout_queue_.submit(request);
        if (layout_queue_.busy()) return;
        try
        {
            while (const auto next = layout_queue_.take())
            {
                layout_once(*next);
                layout_queue_.finish();
            }
        }
        catch (...) { layout_queue_.clear(); throw; }
    }

    void layout_once(const SettingsLayoutRequest& request)
    {
        const auto controls = body_controls();
        HWND anchor{};
        int anchor_offset = scroll_position_;
        const UINT old_dpi = dpi_;
        for (const HWND control : controls)
        {
            if (!(GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE)) continue;
            RECT rectangle{}; GetWindowRect(control, &rectangle);
            MapWindowPoints(nullptr, content_, reinterpret_cast<POINT*>(&rectangle), 2);
            if (rectangle.bottom > 0) { anchor = control; anchor_offset = -rectangle.top; break; }
        }
        const HWND focus = GetFocus();
        if (!font_.get() || dpi_ != request.dpi)
        {
            Font next_font, next_heading;
            next_font.reset(create_system_ui_font(request.dpi));
            LOGFONTW description{};
            if (!next_font.get() || !GetObjectW(next_font.get(), sizeof(description), &description))
                throw std::runtime_error("Create Settings message font failed.");
            description.lfWeight = FW_BOLD;
            next_heading.reset(CreateFontIndirectW(&description));
            if (!next_heading.get()) throw std::runtime_error("Create Settings heading font failed.");
            for (const HWND control : controls)
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(is_heading(control) ? next_heading.get() : next_font.get()), TRUE);
            for (const HWND control : {ok_, cancel_}) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(next_font.get()), TRUE);
            font_.swap(next_font); heading_font_.swap(next_heading);
            // Locals release both previous fonts only after all controls have switched.
        }
        dpi_ = request.dpi;
        MONITORINFO monitor{sizeof(monitor)};
        const HMONITOR target = request.suggested
            ? MonitorFromRect(&*request.suggested, MONITOR_DEFAULTTONEAREST)
            : MonitorFromWindow(!positioned_ && owner_ ? owner_ : window_, MONITOR_DEFAULTTONEAREST);
        if (!GetMonitorInfoW(target, &monitor))
            throw std::runtime_error("Settings work area unavailable.");
#if CLIPSS2_AUDIT_FEATURES
        if (request.work) monitor.rcWork = *request.work;
        if (audit_nested_)
        {
            const auto nested = std::exchange(audit_nested_, std::nullopt);
            apply_dpi(nested->dpi, nested->work ? &*nested->work : nullptr,
                nested->suggested ? &*nested->suggested : nullptr);
        }
        ++audit_layout_updates_;
#endif
        const int padding = scaled(16, dpi_);
        if (request.suggested)
            position_root(settings_transition_rect(*request.suggested, monitor.rcWork, padding));
        RECT frame{};
        if (!AdjustWindowRectExForDpi(&frame, static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_STYLE)), FALSE,
                static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_EXSTYLE)), GetDpiForWindow(window_)))
            throw std::runtime_error("Settings frame measurement failed.");
        const int margin = scaled(18, dpi_), gap = scaled(8, dpi_);
        RECT actual_client{}; GetClientRect(window_, &actual_client);
        const int client_width = request.suggested ? static_cast<int>(actual_client.right) :
            std::max(1, std::min(scaled(600, dpi_), static_cast<int>(monitor.rcWork.right - monitor.rcWork.left - (frame.right - frame.left) - 2 * padding)));
        const HDC dc = GetDC(window_);
        if (!dc) throw std::runtime_error("Settings text measurement DC unavailable.");
        const auto previous = SelectObject(dc, font_.get());
        TEXTMETRICW metrics{}; GetTextMetricsW(dc, &metrics);
        const int line = std::max(scaled(20, dpi_), static_cast<int>(metrics.tmHeight) + scaled(4, dpi_));
        const int field = std::max(scaled(26, dpi_), line + scaled(6, dpi_));
        const int button = std::max(scaled(28, dpi_), line + scaled(8, dpi_));
        const int footer = button + 2 * margin;
        const int maximum_body = std::max(1, request.suggested ? static_cast<int>(actual_client.bottom) - footer :
            static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top - (frame.bottom - frame.top) - 2 * padding - footer));
        place(content_, 0, 0, client_width, maximum_body);
        RECT content_client{}; GetClientRect(content_, &content_client);
        const int width = std::max(1, static_cast<int>(content_client.right) - 2 * margin);
        int y = margin;
        auto label = [&](HWND control) {
            const int length = GetWindowTextLengthW(control);
            std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
            GetWindowTextW(control, value.data(), length + 1);
            RECT measured{0, 0, width, 0};
            const auto assigned = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0)));
            DrawTextW(dc, value.c_str(), length, &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(dc, assigned);
            const int height = std::max(line, static_cast<int>(measured.bottom));
            place(control, margin, y, width, height); y += height + gap;
        };
        unsigned boundary_index{};
        auto boundary = [&] {
            const int group_gap = scaled(16, dpi_);
            separator_y_[boundary_index++] = y - gap + (group_gap - std::max(1, scaled(1, dpi_))) / 2;
            y += group_gap - gap; // Existing trailing 8 DIP is already part of this boundary.
        };
        label(save_label_);
        place(path_, margin, y, width, field); y += field + gap;
        const int side = (width - gap) / 2;
        const auto fits = [&](HWND control) {
            const int length = GetWindowTextLengthW(control);
            std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
            GetWindowTextW(control, value.data(), length + 1);
            SIZE extent{};
            return GetTextExtentPoint32W(dc, value.data(), length, &extent) && extent.cx + scaled(28, dpi_) <= side;
        };
        if (fits(change_) && fits(reset_))
        {
            place(change_, margin, y, side, button); place(reset_, margin + side + gap, y, side, button); y += button + gap;
        }
        else
        {
            place(change_, margin, y, width, button); y += button + gap;
            place(reset_, margin, y, width, button); y += button + gap;
        }
        boundary();
        label(filename_label_); place(filename_, margin, y, width, scaled(180, dpi_)); y += field + gap;
        boundary();
        label(language_label_); place(language_combo_, margin, y, width, scaled(180, dpi_)); y += field + gap;
        boundary();
        label(section_); label(mode_); label(settings_path_label_);
        place(settings_path_, margin, y, width, field); y += field + gap;
        label(file_state_);
        if (save_failed_ || location_.guidance != UiStringId::count) label(warning_);
        place(settings_folder_, margin, y, width, button); y += button + gap;
        if (!location_.resolved || location_.parent.presence != SettingsPresence::directory) label(parent_hint_);
        content_height_ = y + margin;
        viewport_height_ = request.suggested ? maximum_body : std::min(content_height_, maximum_body);
        scroll_position_ = 0;
        place(content_, 0, 0, client_width, viewport_height_);
        SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL};
        info.nMax = content_height_ - 1; info.nPage = static_cast<UINT>(viewport_height_); info.nPos = 0;
        SetScrollInfo(content_, SB_VERT, &info, TRUE);
        RECT anchored{};
        if (anchor)
        {
            GetWindowRect(anchor, &anchored);
            MapWindowPoints(nullptr, content_, reinterpret_cast<POINT*>(&anchored), 2);
        }
        scroll_to(settings_scroll_offset(anchor_offset, old_dpi, dpi_, anchored.top,
            anchor ? anchored.bottom - anchored.top : content_height_, content_height_, viewport_height_));
        const int action_width = std::min(scaled(116, dpi_), (client_width - 3 * margin) / 2);
        place(cancel_, client_width - margin - action_width, viewport_height_ + margin, action_width, button);
        place(ok_, client_width - 2 * margin - 2 * action_width, viewport_height_ + margin, action_width, button);
        SelectObject(dc, previous); ReleaseDC(window_, dc);
        if (!request.suggested)
        {
            RECT bounds{}; GetWindowRect(window_, &bounds);
            const int root_width = client_width + frame.right - frame.left;
            const int height = viewport_height_ + footer + frame.bottom - frame.top;
            if (!positioned_)
            {
                bounds.left = monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - root_width) / 2;
                bounds.top = monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2;
            }
            positioned_ = true; // Set before synchronous position notifications, including initial centering.
            bounds.right = bounds.left + root_width; bounds.bottom = bounds.top + height;
            position_root(bounds);
        }
        if (focus && GetParent(focus) == content_) reveal(focus);
        InvalidateRect(window_, nullptr, TRUE);
    }

    static void place(HWND control, int x, int y, int width, int height)
    {
        (void)SetWindowPos(control, nullptr, x, y, width, height,
            SWP_NOACTIVATE | SWP_NOZORDER);
    }

    void update_path()
    {
        std::wstring display;
        if (pending_.output_directory.explicit_path)
            display = pending_.output_directory.explicit_path->native();
        else if (settings_.store().mode == SettingsMode::portable)
            display = (settings_.store().executable_directory / L"captures").native();
        else
            display = ui_string(language_, UiStringId::settings_not_selected);
        SetWindowTextW(path_, display.c_str());
        (void)SendMessageW(path_, EM_SETSEL, 0, 0);
    }

    std::filesystem::path picker_initial() const
    {
        if (pending_.output_directory.explicit_path)
            return *pending_.output_directory.explicit_path;
        if (settings_.store().mode == SettingsMode::portable)
        {
            const auto captures = settings_.store().executable_directory / L"captures";
            const DWORD attributes = GetFileAttributesW(captures.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
                return captures;
            return settings_.store().executable_directory;
        }
        return resolved_pictures_directory();
    }

    void change_folder()
    {
        const auto chosen = pick_output_folder_owned(picker_initial(), window_);
        if (!chosen) return;
        validate_output_path(*chosen);
        pending_.output_directory.explicit_path = *chosen;
        update_path();
    }

    static std::wstring environment(const wchar_t* name)
    {
        const DWORD length = clipss2::audit::environment_variable(name, nullptr, 0);
        if (length == 0) return {};
        std::wstring value(length, L'\0');
        const DWORD copied = clipss2::audit::environment_variable(name, value.data(), length);
        if (!copied || copied >= length) return {};
        value.resize(copied);
        return value;
    }

    void apply_audit_pending()
    {
        const std::wstring folder = environment(L"_CLIPSS2_AUDIT_SETTINGS_FOLDER");
        if (!folder.empty())
        {
            std::filesystem::path chosen{folder};
            validate_output_path(chosen);
            pending_.output_directory.explicit_path = std::move(chosen);
            update_path();
        }
        const std::wstring filename = environment(L"_CLIPSS2_AUDIT_SETTINGS_FILENAME");
        if (!filename.empty())
        {
            if (filename == L"timestamp") pending_.filename_policy = FilenamePolicy::timestamp;
            else if (filename == L"sequence") pending_.filename_policy = FilenamePolicy::sequence;
            else if (filename == L"guid") pending_.filename_policy = FilenamePolicy::guid;
            else throw std::invalid_argument("Invalid Settings audit filename policy.");
            (void)SendMessageW(filename_, CB_SETCURSEL,
                static_cast<WPARAM>(pending_.filename_policy), 0);
        }
        const std::wstring language = environment(L"_CLIPSS2_AUDIT_SETTINGS_LANGUAGE");
        if (!language.empty())
        {
            if (language == L"system") pending_.ui_language = UiLanguagePreference::system;
            else if (language == L"english") pending_.ui_language = UiLanguagePreference::english;
            else if (language == L"japanese") pending_.ui_language = UiLanguagePreference::japanese;
            else throw std::invalid_argument("Invalid Settings audit language.");
            (void)SendMessageW(language_combo_, CB_SETCURSEL,
                static_cast<WPARAM>(pending_.ui_language), 0);
        }
    }

    void run_audit_action()
    {
        wchar_t enabled[2]{};
        if (clipss2::audit::environment_variable(L"_CLIPSS2_AUDIT_SETTINGS_INTERNAL_INPUT",
                enabled, 2) != 1 || enabled[0] != L'1')
            return;
#if CLIPSS2_AUDIT_FEATURES
        if (audit_action_.starts_with(L"location-")) { audit_location(); return; }
#endif
        if (audit_action_ == L"resident-language-cycle")
        {
            if (pending_.ui_language == UiLanguagePreference::system)
            {
                pending_.ui_language = UiLanguagePreference::english;
                (void)SendMessageW(language_combo_, CB_SETCURSEL,
                    static_cast<WPARAM>(pending_.ui_language), 0);
                save();
            }
            else if (pending_.ui_language == UiLanguagePreference::english)
            {
                pending_.ui_language = UiLanguagePreference::japanese;
                (void)SendMessageW(language_combo_, CB_SETCURSEL,
                    static_cast<WPARAM>(pending_.ui_language), 0);
                save();
            }
            else
            {
                change_folder();
                cancel();
            }
            return;
        }
        if (audit_action_ == L"keyboard-escape")
        {
            (void)PostMessageW(window_, WM_KEYDOWN, VK_ESCAPE, 0);
            return;
        }
        if (audit_action_ == L"keyboard-tab-enter")
        {
            (void)PostMessageW(window_, WM_KEYDOWN, VK_TAB, 0);
            (void)PostMessageW(window_, WM_KEYDOWN, VK_RETURN, 0);
            return;
        }
        if (audit_action_ == L"window-close")
        {
            (void)PostMessageW(window_, WM_CLOSE, 0, 0);
            return;
        }
        if (audit_action_ == L"artifact") { cancel(); return; }
        if (audit_action_ == L"artifact-filename" ||
            audit_action_ == L"artifact-language")
        {
            const HWND combo = audit_action_ == L"artifact-filename" ?
                filename_ : language_combo_;
            (void)SendMessageW(combo, CB_SHOWDROPDOWN, TRUE, 0);
            audit_action_ = L"artifact";
            if (!SetTimer(window_, audit_timer_id, 1800, nullptr))
                throw std::runtime_error(format_win32_error(
                    "SetTimer Settings artifact failed", GetLastError()));
            return;
        }
        if (audit_action_ == L"picker-cancel") { change_folder(); cancel(); return; }
        apply_audit_pending();
        if (audit_action_ == L"cancel-all") { cancel(); return; }
        if (audit_action_ == L"reset-cancel")
        {
            pending_.output_directory.explicit_path.reset();
            update_path();
            cancel();
            return;
        }
        if (audit_action_ == L"reset-ok")
        {
            pending_.output_directory.explicit_path.reset();
            update_path();
            save();
            return;
        }
        if (audit_action_ == L"ok-all") { save(); return; }
        if (audit_action_ == L"save-failure")
        {
            if (!SetTimer(window_, audit_error_timer_id, 600, nullptr))
                throw std::runtime_error(format_win32_error(
                    "SetTimer Settings error audit failed", GetLastError()));
            save();
            if (!finished_) cancel();
            return;
        }
        throw std::invalid_argument("Invalid Settings audit action.");
    }

    void save()
    {
        if (!settings_.load_result().persistence_enabled) return;
        SettingsWriteFault fault = SettingsWriteFault::none;
        wchar_t injected[2]{};
        if (clipss2::audit::environment_variable(L"_CLIPSS2_AUDIT_SETTINGS_WRITE_FAILURE",
                injected, 2) == 1 && injected[0] == L'1')
            fault = SettingsWriteFault::temp_write;
        const auto written = settings_.save_from_settings(pending_, fault);
        if (!written.persisted)
        {
            save_failed_ = true;
            update_location(); apply_dpi(dpi_);
            const std::wstring detail = std::wstring{ui_string(language_, UiStringId::settings_write_guidance)} +
                L"\n\n" + utf16_from_utf8_for_message(written.diagnostic);
            MessageBoxW(window_, detail.c_str(),
                ui_string(language_, UiStringId::settings_save_error_title).data(),
                MB_OK | MB_ICONERROR);
            return;
        }
        save_failed_ = false;
        update_location();
#if CLIPSS2_AUDIT_FEATURES
        if (audit_action_.starts_with(L"location-"))
        {
            if (location_.file.presence != SettingsPresence::file) throw std::runtime_error("Settings UI saved-state mismatch.");
            std::cout << "Settings UI audit: save-state=exists\n";
        }
#endif
        result_ = SettingsUiResult::saved;
        finished_ = true;
        (void)DestroyWindow(window_);
    }

    static std::wstring utf16_from_utf8_for_message(const std::string& text)
    {
        if (text.empty()) return {};
        const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (!size) return L"Settings write failed.";
        std::wstring result(static_cast<std::size_t>(size), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                static_cast<int>(text.size()), result.data(), size) != size)
            return L"Settings write failed.";
        return result;
    }

    void cancel()
    {
        result_ = SettingsUiResult::cancelled;
        finished_ = true;
        if (window_ && IsWindow(window_)) (void)DestroyWindow(window_);
    }

    bool combo_is_open() const
    {
        return SendMessageW(filename_, CB_GETDROPPEDSTATE, 0, 0) != 0 ||
            SendMessageW(language_combo_, CB_GETDROPPEDSTATE, 0, 0) != 0;
    }

    ProductSettings& settings_;
    HWND owner_{};
    HWND window_{};
    HWND content_{};
    HWND save_label_{};
    HWND path_{};
    HWND change_{};
    HWND reset_{};
    HWND filename_label_{};
    HWND filename_{};
    HWND language_label_{};
    HWND language_combo_{};
    HWND warning_{};
    HWND section_{}, mode_{}, settings_path_label_{}, settings_path_{}, file_state_{}, settings_folder_{}, parent_hint_{};
    HWND ok_{};
    HWND cancel_{};
    ProductPreferences pending_;
    SettingsLocationView location_;
    int content_height_{}, viewport_height_{}, scroll_position_{};
    bool save_failed_{}, folder_opening_{};
    UiLanguage language_;
    UINT dpi_{96};
    Font font_, heading_font_;
    std::array<int, 3> separator_y_{};
    SettingsLayoutQueue layout_queue_;
    bool positioned_{};
    bool finished_{};
    SettingsUiResult result_{SettingsUiResult::cancelled};
    std::exception_ptr callback_error_;
    std::wstring audit_action_;
#if CLIPSS2_AUDIT_FEATURES
    SettingsFolderShell folder_spy_;
    std::optional<RECT> audit_work_;
    std::optional<SettingsLayoutRequest> audit_nested_;
    unsigned audit_root_updates_{}, audit_layout_updates_{};

    void audit_grouping()
    {
        const auto check = [](bool value, const char* reason) { if (!value) throw std::runtime_error(reason); };
        const auto rectangle = [&](HWND control) {
            RECT value{}; GetWindowRect(control, &value);
            MapWindowPoints(nullptr, content_, reinterpret_cast<POINT*>(&value), 2);
            OffsetRect(&value, 0, scroll_position_); return value;
        };
        HWND sibling = GetWindow(content_, GW_CHILD);
        for (const HWND control : body_controls())
        {
            check(sibling == control && GetParent(control) == content_, "Settings semantic creation order changed.");
            sibling = GetWindow(sibling, GW_HWNDNEXT);
        }
        check(!sibling, "Settings decoration introduced a child HWND.");
        for (const HWND heading : heading_controls())
        {
            const auto assigned = reinterpret_cast<HFONT>(SendMessageW(heading, WM_GETFONT, 0, 0));
            LOGFONTW bold{}, regular{};
            GetObjectW(assigned, sizeof(bold), &bold); GetObjectW(font_.get(), sizeof(regular), &regular);
            check(assigned == heading_font_.get() && bold.lfWeight == FW_BOLD &&
                bold.lfHeight == regular.lfHeight && bold.lfWidth == regular.lfWidth &&
                std::wstring_view{bold.lfFaceName} == regular.lfFaceName, "Settings heading font is not same-size system bold.");
        }
        const std::array previous{reset_, filename_, language_combo_};
        const std::array next{filename_label_, language_label_, section_};
        for (std::size_t index = 0; index != separator_y_.size(); ++index)
        {
            const auto before = rectangle(previous[index]), after = rectangle(next[index]);
            const int thickness = std::max(1, scaled(1, dpi_));
            // A native closed COMBOBOX may be shorter than its reserved field row.
            check(after.top - before.bottom >= scaled(16, dpi_) &&
                separator_y_[index] == after.top - scaled(16, dpi_) + (scaled(16, dpi_) - thickness) / 2 &&
                separator_y_[index] > before.bottom &&
                separator_y_[index] + thickness < after.top, "Settings separator is not inside its single group gap.");
        }
        const auto change = rectangle(change_), reset = rectangle(reset_);
        check((change.top == reset.top && change.right < reset.left) ||
            (change.bottom < reset.top && change.left == reset.left), "Settings output buttons overlap.");
        const std::array tab{path_, change_, reset_, filename_, language_combo_, settings_path_, settings_folder_, ok_, cancel_};
        for (std::size_t index = 0; index != tab.size(); ++index)
        {
            check((GetWindowLongPtrW(tab[index], GWL_STYLE) & WS_TABSTOP) != 0, "Settings Tab stop removed.");
            if (!IsWindowEnabled(tab[index])) continue;
            std::size_t following = (index + 1) % tab.size();
            while (!IsWindowEnabled(tab[following])) following = (following + 1) % tab.size();
            check(GetNextDlgTabItem(window_, tab[index], FALSE) == tab[following] &&
                GetNextDlgTabItem(window_, tab[following], TRUE) == tab[index], "Settings nine-stop Tab sequence changed.");
        }
    }

    void audit_accessibility()
    {
        const auto check = [](bool value, const char* reason) { if (!value) throw std::runtime_error(reason); };
        // Fixture-only queries against this dialog's native controls. No new shipping import.
        const HMODULE module = LoadLibraryExW(L"oleacc.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        check(module != nullptr, "Settings MSAA system module unavailable.");
        struct ReleaseModule { HMODULE value; ~ReleaseModule() { FreeLibrary(value); } } release{module};
        const auto accessible = reinterpret_cast<decltype(&AccessibleObjectFromWindow)>(GetProcAddress(module, "AccessibleObjectFromWindow"));
        check(accessible != nullptr, "Settings MSAA entry unavailable.");
        const auto inspect = [&](HWND control, LONG expected_role, std::wstring_view expected_name, bool readonly = false) {
            Microsoft::WRL::ComPtr<IAccessible> object;
            check(SUCCEEDED(accessible(control, static_cast<DWORD>(OBJID_CLIENT), __uuidof(IAccessible),
                reinterpret_cast<void**>(object.GetAddressOf()))) && object, "Settings MSAA object unavailable.");
            VARIANT self{}; self.vt = VT_I4; self.lVal = CHILDID_SELF;
            VARIANT role{}, state{};
            check(SUCCEEDED(object->get_accRole(self, &role)) && role.vt == VT_I4 && role.lVal == expected_role,
                "Settings MSAA role mismatch.");
            BSTR name{};
            const HRESULT named = object->get_accName(self, &name);
            const bool matches = SUCCEEDED(named) && name && std::wstring_view{name, SysStringLen(name)} == expected_name;
            SysFreeString(name);
            check(matches, "Settings MSAA name/label association mismatch.");
            check(SUCCEEDED(object->get_accState(self, &state)) && state.vt == VT_I4 &&
                static_cast<bool>(state.lVal & STATE_SYSTEM_UNAVAILABLE) == !IsWindowEnabled(control), "Settings MSAA disabled state mismatch.");
            if (readonly) check((state.lVal & STATE_SYSTEM_READONLY) != 0, "Settings MSAA readonly state missing.");
        };
        inspect(path_, ROLE_SYSTEM_TEXT, ui_string(language_, UiStringId::settings_save_location), true);
        inspect(settings_path_, ROLE_SYSTEM_TEXT, ui_string(language_, UiStringId::settings_file_location), true);
        inspect(filename_, ROLE_SYSTEM_COMBOBOX, ui_string(language_, UiStringId::settings_file_naming));
        inspect(language_combo_, ROLE_SYSTEM_COMBOBOX, ui_string(language_, UiStringId::settings_language));
        for (const auto [control, id] : std::array{
            std::pair{change_, UiStringId::settings_change}, std::pair{reset_, UiStringId::settings_reset},
            std::pair{settings_folder_, UiStringId::settings_open_folder}, std::pair{ok_, UiStringId::settings_ok},
            std::pair{cancel_, UiStringId::settings_cancel}})
            inspect(control, ROLE_SYSTEM_PUSHBUTTON, ui_string(language_, id));
        if (GetWindowLongPtrW(warning_, GWL_STYLE) & WS_VISIBLE)
            inspect(warning_, ROLE_SYSTEM_STATICTEXT, ui_string(language_, save_failed_ ? UiStringId::settings_write_guidance : location_.guidance));
        check(GetWindow(path_, GW_HWNDPREV) == save_label_ && GetWindow(filename_, GW_HWNDPREV) == filename_label_ &&
            GetWindow(language_combo_, GW_HWNDPREV) == language_label_ && GetWindow(settings_path_, GW_HWNDPREV) == settings_path_label_,
            "Settings native label adjacency changed.");
    }

    void audit_dpi_transitions()
    {
        const auto check = [](bool value, const char* reason) { if (!value) throw std::runtime_error(reason); };
        RECT original{}; GetWindowRect(window_, &original);
        // Deliberately does not contain the titlebar: a work-area size budget is
        // not a request to move an owned window. All messages stay inside this fixture.
        const RECT work{original.left + 200, original.top - 80, original.left + 2000, original.top + 1120};
        audit_work_ = work;
        for (const HWND edit : {path_, settings_path_})
            SendMessageW(edit, EM_SETSEL, 0, std::min(3, GetWindowTextLengthW(edit)));
        unsigned sample_mask{};
        const auto transition = [&](UINT dpi) {
            RECT rectangle{original.left, original.top, original.left + scaled(600, dpi),
                original.top + std::min(scaled(500, dpi), 1000)};
            SendMessageW(window_, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&rectangle));
            RECT after{}; GetWindowRect(window_, &after);
            check(after.left == original.left && after.top == original.top, "DPI transition moved suggested top-left.");
            check(dpi_ == dpi && !layout_queue_.busy(), "DPI layout did not settle.");
            const auto count = audit_root_updates_;
            SendMessageW(window_, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&rectangle));
            check(audit_root_updates_ == count, "Repeated DPI notification repositioned root.");
            const auto regular = font_.get(), bold = heading_font_.get();
            apply_dpi(dpi, &work, &rectangle);
            check(font_.get() == regular && heading_font_.get() == bold, "Same-DPI layout recreated fonts.");
            audit_grouping();
            SCROLLINFO range{sizeof(range), SIF_RANGE | SIF_PAGE | SIF_POS}; GetScrollInfo(content_, SB_VERT, &range);
            check(range.nMax + 1 == content_height_ && range.nPage == static_cast<UINT>(std::min(viewport_height_, content_height_)) &&
                range.nPos == scroll_position_, "DPI scroll range/page/position mismatch.");
            const std::array samples{96U, 120U, 144U, 160U, 192U};
            const unsigned sample = 1U << static_cast<unsigned>(std::find(samples.begin(), samples.end(), dpi) - samples.begin());
            for (const HWND edit : {path_, settings_path_})
            {
                const auto assigned = reinterpret_cast<HFONT>(SendMessageW(edit, WM_GETFONT, 0, 0));
                check(assigned == font_.get(), "EDIT has stale DPI font.");
                LOGFONTW actual{}, expected{};
                const HFONT reference = create_system_ui_font(dpi);
                check(reference != nullptr, "DPI reference font missing.");
                GetObjectW(assigned, sizeof(actual), &actual); GetObjectW(reference, sizeof(expected), &expected); DeleteObject(reference);
                check(actual.lfHeight == expected.lfHeight && actual.lfWidth == expected.lfWidth, "EDIT LOGFONT differs from requested DPI.");
                RECT client{}, format{}; GetClientRect(edit, &client); SendMessageW(edit, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&format));
                check(format.left >= 0 && format.right <= client.right && format.right > format.left && client.bottom > 0, "EDIT formatting width invalid.");
                const HDC dc = GetDC(edit); const auto prior = SelectObject(dc, assigned); TEXTMETRICW metrics{};
                const BOOL measured = GetTextMetricsW(dc, &metrics); SelectObject(dc, prior); ReleaseDC(edit, dc);
                check(measured && metrics.tmHeight >= std::abs(actual.lfHeight), "EDIT text metrics invalid.");
                DWORD begin{}, end{}; SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&begin), reinterpret_cast<LPARAM>(&end));
                check(begin == 0 && end == static_cast<DWORD>(std::min(3, GetWindowTextLengthW(edit))), "DPI transition reset EDIT selection.");
                if (!(sample_mask & sample))
                    std::cout << "Settings DPI metrics: requested=" << dpi << " window=" << GetDpiForWindow(window_)
                        << " edit=" << (edit == path_ ? "image" : "settings") << " fontHeight=" << actual.lfHeight
                        << " textHeight=" << metrics.tmHeight << " client=" << client.right << 'x' << client.bottom
                        << " formatWidth=" << format.right - format.left << '\n';
            }
            sample_mask |= sample;
            RECT image{}, settings{}; GetWindowRect(path_, &image); GetWindowRect(settings_path_, &settings);
            check(image.right - image.left == settings.right - settings.left && image.bottom - image.top == settings.bottom - settings.top,
                "Path EDIT layout contracts differ.");
        };
        SetFocus(cancel_); // Preserve a scrolled anchor rather than reveal a different body control.
        for (const UINT dpi : {96U, 120U, 144U, 160U, 192U, 96U, 192U, 96U}) transition(dpi);
        scroll_to(std::max(0, content_height_ - viewport_height_) / 2);
        for (const UINT dpi : {192U, 96U})
        {
            HWND anchor{}; int offset{}; const UINT from = dpi_;
            for (const HWND control : body_controls())
            {
                RECT rectangle{}; GetWindowRect(control, &rectangle); MapWindowPoints(nullptr, content_, reinterpret_cast<POINT*>(&rectangle), 2);
                if ((GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE) && rectangle.bottom > 0)
                { anchor = control; offset = -rectangle.top; break; }
            }
            transition(dpi);
            if (anchor && scroll_position_ > 0 && scroll_position_ < content_height_ - viewport_height_)
            {
                RECT rectangle{}; GetWindowRect(anchor, &rectangle); MapWindowPoints(nullptr, content_, reinterpret_cast<POINT*>(&rectangle), 2);
                const int expected = -std::min(MulDiv(offset, dpi, from), static_cast<int>(rectangle.bottom - rectangle.top - 1));
                check(std::abs(rectangle.top - expected) <= 2, "DPI scroll anchor drifted.");
            }
        }
        SetFocus(settings_folder_);
        transition(192);
        if (IsWindowEnabled(settings_folder_))
        {
            RECT focus{}; GetWindowRect(settings_folder_, &focus); MapWindowPoints(nullptr, content_, reinterpret_cast<POINT*>(&focus), 2);
            check(focus.top >= 0 && focus.bottom <= viewport_height_, "DPI transition lost focused control.");
        }
        SetFocus(cancel_);
        RECT current{}; GetWindowRect(window_, &current);
        const auto before = audit_layout_updates_;
        audit_nested_ = SettingsLayoutRequest{160, current, work};
        apply_dpi(96, &work, &current);
        check(dpi_ == 160 && audit_layout_updates_ == before + 2 && !layout_queue_.busy(), "Nested DPI requests did not serialize.");
        const auto duplicate = audit_layout_updates_;
        audit_nested_ = SettingsLayoutRequest{160, current, work};
        apply_dpi(160, &work, &current);
        check(audit_layout_updates_ == duplicate + 1, "Identical nested DPI request did not coalesce.");
        GetWindowRect(window_, &current);
        SendMessageW(window_, WM_ENTERSIZEMOVE, 0, 0); SendMessageW(window_, WM_EXITSIZEMOVE, 0, 0);
        RECT after{}; GetWindowRect(window_, &after);
        check(settings_same_rect(current, after), "Move loop completion changed position.");
        transition(96); transition(192); transition(96); // Warm native font/control caches.
        const DWORD resources = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        for (unsigned index = 0; index != 16; ++index) { transition(192); transition(96); }
        check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= resources + 1, "DPI cycles leaked GDI resources.");
        audit_work_.reset();
        apply_dpi(GetDpiForWindow(window_));
        std::cout << "Settings DPI transition audit: fonts=5 directions=both nested=serialized repeat=stable GDI=stable synthetic-only\n";
    }

    void audit_location()
    {
        const auto check = [](bool value, const char* reason) { if (!value) throw std::runtime_error(reason); };
        const auto text = [](HWND control) {
            const int length = GetWindowTextLengthW(control);
            std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
            GetWindowTextW(control, value.data(), length + 1); value.resize(length); return value;
        };
        if (audit_action_ == L"location-metadata-unknown")
            update_location([](const auto&) { return SettingsMetadata{SettingsPresence::unknown, ERROR_ACCESS_DENIED}; });
        check(text(settings_path_) == settings_.store().path.native(), "Settings UI path differs from fixed store.");
        check(text(mode_) == ui_string(language_, location_.mode), "Settings UI mode mismatch.");
        check(text(file_state_).starts_with(ui_string(language_, settings_presence_text(location_.file.presence))), "Settings UI file state mismatch.");
        check(IsWindowEnabled(ok_) == static_cast<BOOL>(settings_.load_result().persistence_enabled), "Settings UI protection mismatch.");
        const auto style = GetWindowLongPtrW(settings_path_, GWL_STYLE);
        check((style & (ES_READONLY | ES_AUTOHSCROLL | WS_TABSTOP)) == (ES_READONLY | ES_AUTOHSCROLL | WS_TABSTOP), "Settings path edit style.");
        const auto original = settings_.preferences();
        for (const UINT dpi : {96U, 120U, 144U, 160U, 192U})
        {
            const RECT fhd{0, 0, 1920, 1040}; apply_dpi(dpi, &fhd);
            audit_grouping();
            RECT bounds{}; GetWindowRect(window_, &bounds);
            check(bounds.right - bounds.left <= 1920 && bounds.bottom - bounds.top <= 1040, "Settings UI exceeds FHD budget.");
            RECT client{}; GetClientRect(content_, &client);
            const HDC dc = GetDC(window_); const auto prior = SelectObject(dc, font_.get());
            for (const HWND control : body_controls())
            {
                if (!(GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE)) continue;
                RECT rectangle{}; GetWindowRect(control, &rectangle);
                MapWindowPoints(nullptr, content_, reinterpret_cast<POINT*>(&rectangle), 2);
                check(rectangle.left >= 0 && rectangle.right <= client.right, "Settings body horizontal clipping.");
                wchar_t type[32]{}; GetClassNameW(control, type, 32);
                if (_wcsicmp(type, L"Static") == 0)
                {
                    SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0)));
                    RECT measured{0, 0, rectangle.right - rectangle.left, 0}; const auto value = text(control);
                    DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
                    check(measured.bottom <= rectangle.bottom - rectangle.top, "Settings label vertical clipping.");
                }
                for (const HWND other : body_controls())
                {
                    if (other == control || !(GetWindowLongPtrW(other, GWL_STYLE) & WS_VISIBLE)) continue;
                    RECT other_rectangle{}, overlap{}; GetWindowRect(other, &other_rectangle);
                    MapWindowPoints(nullptr, content_, reinterpret_cast<POINT*>(&other_rectangle), 2);
                    check(!IntersectRect(&overlap, &rectangle, &other_rectangle), "Settings body controls overlap.");
                }
            }
            SelectObject(dc, prior); ReleaseDC(window_, dc);
            GetClientRect(window_, &client);
            for (const HWND control : {ok_, cancel_})
            {
                RECT rectangle{}; GetWindowRect(control, &rectangle); MapWindowPoints(nullptr, window_, reinterpret_cast<POINT*>(&rectangle), 2);
                if (!(rectangle.top >= viewport_height_ && rectangle.bottom <= client.bottom && rectangle.right <= client.right))
                    throw std::runtime_error("Settings footer clipping: dpi=" + std::to_string(dpi) +
                        " bottom=" + std::to_string(rectangle.bottom) + " client=" + std::to_string(client.bottom));
            }
            SetFocus(path_);
            bool reached_path = false, reached_cancel = false;
            for (unsigned index = 0; index < 12; ++index)
            {
                const HWND current = GetFocus();
                const HWND expected = GetNextDlgTabItem(window_, current, FALSE);
                check(GetNextDlgTabItem(window_, expected, TRUE) == current, "Settings reverse Tab order mismatch.");
                MSG message{}; message.hwnd = current; message.message = WM_KEYDOWN; message.wParam = VK_TAB;
                check(IsDialogMessageW(window_, &message) != FALSE && GetFocus() == expected, "Settings Tab navigation mismatch.");
                if (expected == settings_path_) reached_path = true;
                if (expected == cancel_) reached_cancel = true;
                if (GetParent(expected) == content_)
                {
                    RECT rectangle{}; GetWindowRect(expected, &rectangle); MapWindowPoints(nullptr, content_, reinterpret_cast<POINT*>(&rectangle), 2);
                    check(rectangle.top >= 0 && rectangle.bottom <= viewport_height_, "Settings focus outside viewport.");
                }
            }
            check(reached_cancel && (!location_.resolved || reached_path), "Settings controls not keyboard accessible.");
            SetFocus(cancel_);
            const int saved_scroll = scroll_position_;
            RECT footer{}; GetWindowRect(ok_, &footer);
            const int maximum = std::max(0, content_height_ - viewport_height_);
            for (const int position : {0, maximum / 2, maximum,
                separator_y_[0], separator_y_[1], separator_y_[2]})
            {
                scroll_to(position); audit_grouping();
                RECT after{}; GetWindowRect(ok_, &after);
                check(settings_same_rect(footer, after), "Settings scroll moved fixed footer.");
            }
            scroll_to(saved_scroll);
        }
        audit_dpi_transitions();
        audit_accessibility();
        std::cout << "Settings grouping audit: headings=4 separators=3 HWND=16 Tab=9 DPI=5 MSAA=pass\n";
        for (const HWND edit : {path_, settings_path_})
        {
            if (!IsWindowEnabled(edit)) continue;
            SetFocus(edit);
            SendMessageW(edit, EM_SETSEL, 0, -1);
            DWORD begin{}, end{}; SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&begin), reinterpret_cast<LPARAM>(&end));
            check(begin == 0 && end == text(edit).size(), "Readonly path selection mismatch.");
            SendMessageW(edit, EM_SETSEL, end, end); SendMessageW(edit, EM_SCROLLCARET, 0, 0);
            // Use EDIT's own shaped character positions, not a separate GDI
            // width estimate. The long-path fixture must actually scroll.
            SendMessageW(edit, WM_KEYDOWN, VK_END, 0);
            const auto last_x = static_cast<short>(LOWORD(SendMessageW(edit, EM_POSFROMCHAR, end - 1, 0)));
            RECT client{}; GetClientRect(edit, &client);
            check(last_x >= 0 && last_x < client.right, "Readonly path end is outside the edit viewport.");
            if (text(edit).size() > 300)
                check(static_cast<short>(LOWORD(SendMessageW(edit, EM_POSFROMCHAR, 0, 0))) < 0,
                    "Long settings path did not scroll its start outside the edit viewport.");
        }
        unsigned calls{};
        folder_spy_ = [&](const auto& parent) { check(parent == settings_.store().path.parent_path(), "Settings Shell parent mismatch."); ++calls; return audit_action_ == L"location-shell-failure" ? E_ACCESSDENIED : S_OK; };
        check(calls == 0, "Settings display opened Shell.");
        if (IsWindowEnabled(settings_folder_))
        {
            if (audit_action_ == L"location-shell-failure") SetTimer(window_, audit_error_timer_id, 300, nullptr);
            SendMessageW(window_, WM_COMMAND, MAKEWPARAM(settings_folder_id, BN_CLICKED), reinterpret_cast<LPARAM>(settings_folder_));
            check(calls == 1 && settings_.preferences() == original && !finished_, "Settings folder action changed transaction.");
        }
        folder_spy_ = {}; // No borrowed lambda survives this audit.
        std::cout << "Settings UI audit: mode=" << (location_.resolved ? settings_.store().mode == SettingsMode::portable ? "portable" : "per-user" : "unresolved")
            << " presence=" << static_cast<int>(location_.file.presence)
            << " guidance=" << static_cast<int>(location_.guidance) << " shell-spy=" << calls << " layout=5 keyboard=pass\n";
        if (audit_action_ == L"location-save") { apply_audit_pending(); save(); return; }
        if (audit_action_ == L"location-failure" || audit_action_ == L"location-retry")
        {
            apply_audit_pending(); const auto pending = pending_;
            SetTimer(window_, audit_error_timer_id, 300, nullptr); save();
            check(!finished_ && save_failed_ && settings_.preferences() == original && pending_ == pending, "Settings failed save lost pending or changed memory.");
            check(text(file_state_).find(ui_string(language_, UiStringId::settings_unsaved)) != std::wstring::npos, "Settings unsaved warning missing.");
            SetFocus(cancel_);
            for (const UINT dpi : {96U, 120U, 144U, 160U, 192U})
            {
                const RECT fhd{0, 0, 1920, 1040}; apply_dpi(dpi, &fhd);
                audit_grouping(); audit_accessibility();
                RECT rectangle{}; GetWindowRect(warning_, &rectangle);
                RECT measured{0, 0, rectangle.right - rectangle.left, 0};
                const HDC dc = GetDC(warning_);
                const auto prior = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(warning_, WM_GETFONT, 0, 0)));
                const auto value = text(warning_);
                DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
                SelectObject(dc, prior); ReleaseDC(warning_, dc);
                check(measured.bottom <= rectangle.bottom - rectangle.top, "Settings save failure guidance clipped.");
            }
            std::cout << "Settings UI audit: failure=pending-preserved\n";
            if (audit_action_ == L"location-retry")
            {
                SetEnvironmentVariableW(L"_CLIPSS2_AUDIT_SETTINGS_WRITE_FAILURE", nullptr); save(); return;
            }
        }
        cancel();
    }
#endif
};
}

SettingsUiResult run_settings_ui(ProductSettings& settings, HWND owner)
{
    if (const HWND existing = FindWindowW(settings_window_class_name, nullptr))
    {
        DWORD process{};
        (void)GetWindowThreadProcessId(existing, &process);
        if (process == GetCurrentProcessId())
        {
            ShowWindow(existing, SW_RESTORE);
            (void)SetForegroundWindow(existing);
            return SettingsUiResult::cancelled;
        }
    }
    return SettingsWindow{settings, owner}.run();
}
}

#include "platform/windows/window_selector_ui.h"
#include "audit/audit_policy.h"
#include "platform/windows/selector_window_resources.h"

#include "platform/windows/window_candidates.h"
#include "platform/windows/window_selection_input.h"
#include "platform/windows/display_monitor.h"
#include "platform/windows/ui_localization.h"
#include "platform/windows/unicode_diagnostics.h"

#include <dwmapi.h>
#include <windows.h>
#include <shellscalingapi.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <unordered_set>
#include <vector>

#include <winrt/base.h>

namespace clipss2::platform::windows
{
namespace
{
constexpr COLORREF transparency_key = RGB(1, 2, 3);
constexpr int hud_offset_dip = 16;
constexpr std::size_t z_order_iteration_limit = 4096;
constexpr UINT_PTR lost_release_timer_id = 1;
constexpr UINT lost_release_interval_ms = 250;

std::wstring ascii_wide(std::string_view value)
{
    return {value.begin(), value.end()};
}

std::wstring localized_jpeg_row(
    UiLanguage language, const JpegSessionPolicy& policy)
{
    using namespace codec::jpeg;
    const auto& selection = policy.selection();
    if (!std::holds_alternative<JpegProfileSelection>(selection))
        return L"P: JPEG: " + std::wstring(ui_string(language, UiStringId::profile_default)) + L" (Turbo)";
    const auto& explicit_selection = std::get<JpegProfileSelection>(selection);
    UiStringId profile = UiStringId::profile_fast;
    if (explicit_selection.profile == JpegProfile::balanced) profile = UiStringId::profile_balanced;
    else if (explicit_selection.profile == JpegProfile::smallest) profile = UiStringId::profile_smallest;
    std::wstring encoder;
    if (explicit_selection.backend == JpegBackendId::automatic)
    {
        encoder = ui_string(language, UiStringId::automatic);
        try
        {
            const auto backend = resolve_jpeg_product_selection(selection).selection.backend;
            encoder += L" (" + ascii_wide(
                backend == JpegBackendId::turbo ? "Turbo" : jpeg_backend_name(backend)) + L")";
        }
        catch (const std::invalid_argument&)
        {
            encoder += L" (" + std::wstring(ui_string(language, UiStringId::unavailable)) + L")";
        }
    }
    else encoder = ascii_wide(explicit_selection.backend == JpegBackendId::turbo
        ? "Turbo" : jpeg_backend_name(explicit_selection.backend));
    return L"P: JPEG: " + std::wstring(ui_string(language, profile)) + L"   E: " +
        std::wstring(ui_string(language, UiStringId::encoder)) + L": " + encoder;
}

std::string hwnd_text(std::uintptr_t value)
{
    std::ostringstream stream;
    stream << "0x" << std::hex << value;
    return stream.str();
}

using namespace selector_resources;

struct LiveBounds
{
    WindowBounds bounds;
    HWND window{};
    bool used_dwm{};
};

struct StatusLayout
{
    int window_width{};
    int window_height{};
    int client_width{};
    int client_height{};
};

std::optional<LiveBounds> live_bounds(const WindowCandidate& candidate)
{
    if (!is_window_candidate_current(candidate)) return std::nullopt;
    const auto window = reinterpret_cast<HWND>(candidate.hwnd_value);
    RECT rectangle{};
    const HRESULT dwm_result = DwmGetWindowAttribute(
        window, DWMWA_EXTENDED_FRAME_BOUNDS, &rectangle, sizeof(rectangle));
    bool used_dwm = SUCCEEDED(dwm_result);
    if (!used_dwm)
    {
        SetLastError(ERROR_SUCCESS);
        if (!GetWindowRect(window, &rectangle))
        {
            std::cout << "Live bounds unavailable: DWM HRESULT 0x" << std::hex
                      << static_cast<std::uint32_t>(dwm_result) << std::dec
                      << ", GetWindowRect Win32 " << GetLastError() << '\n';
            return std::nullopt;
        }
        std::cout << "Live bounds fallback: DWM HRESULT 0x" << std::hex
                  << static_cast<std::uint32_t>(dwm_result) << std::dec
                  << ", GetWindowRect succeeded\n";
    }
    WindowBounds bounds{rectangle.left, rectangle.top, rectangle.right, rectangle.bottom};
    if (bounds.empty()) return std::nullopt;
    return LiveBounds{bounds, window, used_dwm};
}

class SelectorUi final
{
public:
    SelectorUi(
        WindowSelectionSession session,
        HWND original_foreground,
        ScreenPosition initial_cursor,
        WindowSelectionAction action,
        const ProductPreferences& initial)
        : session_(std::move(session)),
          original_foreground_(original_foreground),
          input_(initial_cursor),
          destination_policy_(
              action == WindowSelectionAction::copy
                  ? capture::CaptureDestination::clipboard
                  : capture::CaptureDestination::file,
              action == WindowSelectionAction::capture),
          file_format_policy_(initial.file_format),
          jpeg_policy_(initial.jpeg_selection),
          avif_profile_(initial.avif_profile), jxl_profile_(initial.jxl_profile),
          action_(action), language_(resolve_ui_language()) {}

    SelectorUi(const SelectorUi&) = delete;
    SelectorUi& operator=(const SelectorUi&) = delete;

    void create(
        HINSTANCE instance,
        const wchar_t* status_class,
        const wchar_t* highlight_class,
        const wchar_t* tint_class,
        const wchar_t* shield_class)
    {
        highlight_.reset(CreateWindowExW(
            WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST |
                WS_EX_TRANSPARENT,
            highlight_class, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
            instance, this));
        if (highlight_.get() == nullptr) winrt::throw_last_error();
        if (!SetLayeredWindowAttributes(
                highlight_.get(), transparency_key, 255, LWA_COLORKEY))
            winrt::throw_last_error();

        tint_.reset(CreateWindowExW(
            WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST |
                WS_EX_TRANSPARENT,
            tint_class, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
            instance, this));
        if (tint_.get() == nullptr) winrt::throw_last_error();
        if (!SetLayeredWindowAttributes(tint_.get(), 0, 48, LWA_ALPHA))
            winrt::throw_last_error();

        status_.reset(CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            status_class, ui_string(language_, UiStringId::window_selection_title).data(), WS_POPUP | WS_BORDER,
            0, 0, 0, 0, nullptr, nullptr, instance, this));
        if (status_.get() == nullptr) winrt::throw_last_error();

        if (!show_current_or_advance(true))
            throw std::runtime_error("No live selectable window is available.");

        const auto monitors = enumerate_display_monitors();
        shields_.reserve(monitors.size());
        for (const auto& monitor : monitors)
        {
            const auto& rectangle = monitor.monitor_rectangle;
            OwnedWindow shield;
            shield.reset(CreateWindowExW(
                WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
                    WS_EX_NOREDIRECTIONBITMAP,
                shield_class, L"", WS_POPUP,
                rectangle.left, rectangle.top,
                rectangle.right - rectangle.left,
                rectangle.bottom - rectangle.top,
                nullptr, nullptr, instance, this));
            if (shield.get() == nullptr) winrt::throw_last_error();
            ShowWindow(shield.get(), SW_SHOWNOACTIVATE);
            std::cout << "Input shield " << shields_.size() << ": device="
                      << utf8_from_utf16(monitor.device_name) << " rect=("
                      << rectangle.left << ',' << rectangle.top << ")-("
                      << rectangle.right << ',' << rectangle.bottom << ")\n";
            shields_.push_back(std::move(shield));
        }
        std::cout << "Input shield count: " << shields_.size()
                  << "\nMouse input mode: event-driven (WM_MOUSEMOVE/down/up)\n";
        position_windows();
        ShowWindow(status_.get(), SW_SHOW);
        UpdateWindow(status_.get());
        SetFocus(status_.get());
        ready_ = true;
        std::cout << "Status keyboard focus: "
                  << (GetFocus() == status_.get() ? "acquired" : "not acquired") << '\n';
        std::cout << "Target click forwarded: no\n";
    }

    std::optional<WindowSelectionResult> run()
    {
        MSG message{};
        while (true)
        {
            const BOOL result = GetMessageW(&message, nullptr, 0, 0);
            if (result == -1) winrt::throw_last_error();
            if (result == 0) break;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (callback_error_) std::rethrow_exception(callback_error_);

        const HWND final_foreground = GetForegroundWindow();
        std::cout << "Foreground after selector close: "
                  << hwnd_text(reinterpret_cast<std::uintptr_t>(final_foreground));
        if (final_foreground == original_foreground_) std::cout << " (original restored by Windows)";
        std::cout << '\n';

        if (session_.state() == SelectionSessionState::confirmed && confirmed_)
        {
            std::cout << "Window selection: confirmed\n"
                      << "Selected HWND: "
                      << hwnd_text(confirmed_->candidate.hwnd_value) << '\n';
#if CLIPSS2_AUDIT_FEATURES
            write_stdout_utf8("Selected title: " + utf8_from_utf16(confirmed_->candidate.title) + "\n");
#endif
            std::cout << "Cursor capture: "
                      << (confirmed_->include_cursor ? "on" : "off") << '\n';
            return confirmed_;
        }
        std::cout << "Window selection: cancelled\n";
        return std::nullopt;
    }

    static LRESULT CALLBACK status_procedure(
        HWND window, UINT message, WPARAM word, LPARAM parameter) noexcept
    {
        return dispatch(window, message, word, parameter, false);
    }

    static LRESULT CALLBACK highlight_procedure(
        HWND window, UINT message, WPARAM word, LPARAM parameter) noexcept
    {
        return dispatch(window, message, word, parameter, true);
    }

    static LRESULT CALLBACK tint_procedure(
        HWND window, UINT message, WPARAM word, LPARAM parameter) noexcept
    {
        return visual_dispatch(window, message, word, parameter, true);
    }

    static LRESULT CALLBACK shield_procedure(
        HWND window, UINT message, WPARAM word, LPARAM parameter) noexcept
    {
        if (message == WM_NCCREATE)
        {
            const auto* creation = reinterpret_cast<CREATESTRUCTW*>(parameter);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
        }
        auto* self = reinterpret_cast<SelectorUi*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (self == nullptr) return DefWindowProcW(window, message, word, parameter);
        try { return self->shield_message(window, message, word, parameter); }
        catch (...)
        {
            self->callback_error_ = std::current_exception();
            PostQuitMessage(1);
            return 0;
        }
    }

private:
    static LRESULT visual_dispatch(
        HWND window, UINT message, WPARAM word, LPARAM parameter, bool tint) noexcept
    {
        if (message == WM_NCCREATE)
        {
            const auto* creation = reinterpret_cast<CREATESTRUCTW*>(parameter);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
        }
        auto* self = reinterpret_cast<SelectorUi*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (self == nullptr) return DefWindowProcW(window, message, word, parameter);
        try
        {
            return tint ? self->tint_message(window, message, word, parameter)
                        : self->highlight_message(window, message, word, parameter);
        }
        catch (...)
        {
            self->callback_error_ = std::current_exception();
            PostQuitMessage(1);
            return 0;
        }
    }

    static LRESULT dispatch(
        HWND window,
        UINT message,
        WPARAM word,
        LPARAM parameter,
        bool highlight) noexcept
    {
        if (message == WM_NCCREATE)
        {
            const auto* creation = reinterpret_cast<CREATESTRUCTW*>(parameter);
            SetWindowLongPtrW(
                window, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
        }
        auto* self = reinterpret_cast<SelectorUi*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (self == nullptr) return DefWindowProcW(window, message, word, parameter);
        try
        {
            return highlight ? self->highlight_message(window, message, word, parameter)
                             : self->status_message(window, message, word, parameter);
        }
        catch (...)
        {
            self->callback_error_ = std::current_exception();
            PostQuitMessage(1);
            return 0;
        }
    }

    LRESULT status_message(HWND window, UINT message, WPARAM word, LPARAM parameter)
    {
        switch (message)
        {
        case WM_DISPLAYCHANGE:
            cancel_for_display_change();
            return 0;
        case WM_KEYDOWN:
            if (word == VK_TAB)
            {
                if (gesture_.active()) return 0;
                const bool backwards = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                input_.record_keyboard_input(cursor_position());
                std::cout << "Selection source: keyboard\n";
                if (backwards ? session_.previous() : session_.next())
                    show_current_or_advance(!backwards);
                return 0;
            }
            if (word == VK_RETURN)
            {
                if (gesture_.active()) return 0;
                std::cout << "Confirm source: keyboard\n";
                confirm();
                return 0;
            }
            if (word == VK_ESCAPE)
            {
                if (gesture_.active() && !physical_left_button_down())
                {
                    (void)gesture_.recover_lost_release(false);
                    stop_lost_release_watchdog();
                    std::cout << "Mouse press: lost release recovered; selection cancelled\n";
                    cancel();
                    return 0;
                }
                if (gesture_.request_cancel())
                {
                    std::cout << "Mouse press: cancel pending; release will be consumed\n";
                    refresh_pressed_visual();
                }
                else cancel();
                return 0;
            }
            if (word == 'C')
            {
                const bool repeated = (static_cast<std::uintptr_t>(parameter) &
                    (std::uintptr_t{1} << 30)) != 0;
                const bool modified = (GetKeyState(VK_CONTROL) & 0x8000) != 0 ||
                    (GetKeyState(VK_MENU) & 0x8000) != 0;
                if (repeated || modified ||
                    !cursor_policy_.toggle(gesture_.active())) return 0;
                std::cout << "Cursor capture option: "
                          << (cursor_policy_.include_cursor() ? "on" : "off")
                          << '\n';
                position_status(cursor_position());
                InvalidateRect(status_.get(), nullptr, TRUE);
                UpdateWindow(status_.get());
                return 0;
            }
            if (word == 'F' && action_ == WindowSelectionAction::capture)
            {
                const bool repeated = (static_cast<std::uintptr_t>(parameter) &
                    (std::uintptr_t{1} << 30)) != 0;
                const bool modified = (GetKeyState(VK_CONTROL) & 0x8000) != 0 ||
                    (GetKeyState(VK_MENU) & 0x8000) != 0;
                if (!file_format_policy_.toggle(gesture_.active(), repeated, modified)) return 0;
                std::cout << "File format option: "
                          << image::image_file_format_name(file_format_policy_.format()) << '\n';
                position_status(cursor_position());
                InvalidateRect(status_.get(), nullptr, TRUE);
                UpdateWindow(status_.get());
                return 0;
            }
            if ((word == 'P' || word == 'E') && action_ == WindowSelectionAction::capture)
            {
                const bool repeated = (static_cast<std::uintptr_t>(parameter) &
                    (std::uintptr_t{1} << 30)) != 0;
                const bool modified = (GetKeyState(VK_CONTROL) & 0x8000) != 0 ||
                    (GetKeyState(VK_MENU) & 0x8000) != 0;
                const bool enabled = file_format_policy_.format() == image::ImageFileFormat::jpeg;
                const bool changed = word == 'P'
                    ? (enabled ? jpeg_policy_.cycle_profile(true, gesture_.active(), repeated, modified)
                       : cycle_codec_profile(file_format_policy_.format(), avif_profile_, jxl_profile_,
                           gesture_.active(), repeated, modified))
                    : jpeg_policy_.cycle_encoder(enabled, gesture_.active(), repeated, modified);
                if (!changed) return 0;
                if (enabled) std::cout << "JPEG session option: " << jpeg_policy_.hud_text() << '\n';
                else std::cout << "Codec profile: " << image::image_file_format_name(file_format_policy_.format())
                    << ' ' << capture::file_compression_token(file_format_policy_.format() == image::ImageFileFormat::avif
                        ? avif_profile_ : jxl_profile_) << '\n';
                position_status(cursor_position());
                InvalidateRect(status_.get(), nullptr, TRUE);
                UpdateWindow(status_.get());
                return 0;
            }
            if (word == 'O')
            {
                const bool repeated = (static_cast<std::uintptr_t>(parameter) &
                    (std::uintptr_t{1} << 30)) != 0;
                const bool modified = (GetKeyState(VK_CONTROL) & 0x8000) != 0 ||
                    (GetKeyState(VK_MENU) & 0x8000) != 0;
                if (repeated || modified ||
                    !destination_policy_.cycle(!gesture_.active())) return 0;
                std::cout << "Capture destination: "
                          << capture::capture_destination_name(
                                 destination_policy_.destination()) << '\n';
                position_status(cursor_position());
                InvalidateRect(status_.get(), nullptr, TRUE);
                UpdateWindow(status_.get());
                return 0;
            }
            break;
        case WM_ACTIVATE:
            if (ready_ && LOWORD(word) == WA_INACTIVE)
            {
                if (gesture_.request_cancel())
                {
                    std::cout << "External focus loss: cancel pending until mouse release\n";
                    refresh_pressed_visual();
                }
                else
                {
                    std::cout << "External focus loss: selection cancelled\n";
                    cancel();
                }
                return 0;
            }
            break;
        case WM_DPICHANGED:
            handle_status_dpi_changed(
                window, static_cast<UINT>(LOWORD(word)),
                *reinterpret_cast<const RECT*>(parameter));
            return 0;
        case WM_MOUSEMOVE:
            handle_mouse_move(cursor_position());
            return 0;
        case WM_LBUTTONDOWN:
            // The HUD is selector UI, never a capture target.
            return 0;
        case WM_LBUTTONUP:
            handle_mouse_up(cursor_position());
            return 0;
        case WM_TIMER:
            if (word == lost_release_timer_id)
            {
                recover_lost_release();
                return 0;
            }
            break;
        case WM_PAINT:
            paint_status(window);
            return 0;
        case WM_CLOSE:
            cancel();
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(window, message, word, parameter);
    }

    LRESULT highlight_message(HWND window, UINT message, WPARAM word, LPARAM parameter)
    {
        switch (message)
        {
        case WM_DISPLAYCHANGE:
            cancel_for_display_change();
            return 0;
        case WM_NCHITTEST: return HTTRANSPARENT;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT:
            paint_highlight(window);
            return 0;
        default:
            return DefWindowProcW(window, message, word, parameter);
        }
    }

    LRESULT tint_message(HWND window, UINT message, WPARAM word, LPARAM parameter)
    {
        switch (message)
        {
        case WM_DISPLAYCHANGE:
            cancel_for_display_change();
            return 0;
        case WM_NCHITTEST: return HTTRANSPARENT;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT:
        {
            const PaintSession paint{window};
            RECT client{};
            if (!GetClientRect(window, &client)) winrt::throw_last_error();
            COLORREF color = GetSysColor(COLOR_HIGHLIGHT);
            if (color == RGB(0, 0, 0)) color = RGB(0, 120, 215);
            const OwnedBrush brush{color};
            FillRect(paint.context(), &client, brush.get());
            return 0;
        }
        default: return DefWindowProcW(window, message, word, parameter);
        }
    }

    LRESULT shield_message(HWND window, UINT message, WPARAM word, LPARAM parameter)
    {
        switch (message)
        {
        case WM_DISPLAYCHANGE:
            cancel_for_display_change();
            return 0;
        case WM_NCHITTEST: return HTCLIENT;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT:
            ValidateRect(window, nullptr);
            return 0;
        case WM_MOUSEMOVE:
            handle_mouse_move(cursor_position());
            return 0;
        case WM_LBUTTONDOWN:
            handle_mouse_down(cursor_position());
            return 0;
        case WM_LBUTTONUP:
            handle_mouse_up(cursor_position());
            return 0;
        case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP:
        case WM_XBUTTONDOWN: case WM_XBUTTONUP:
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
            return 0;
        default:
            return DefWindowProcW(window, message, word, parameter);
        }
    }

    bool show_current_or_advance(bool forward)
    {
        const std::size_t attempts = session_.selectable_count();
        for (std::size_t attempt = 0; attempt < attempts; ++attempt)
        {
            const auto* candidate = session_.selected_candidate();
            if (candidate)
            {
                if (auto geometry = live_bounds(*candidate))
                {
                    live_ = *geometry;
                    selected_title_ = candidate->title;
                    position_highlight();
                    position_status(cursor_position());
                    InvalidateRect(status_.get(), nullptr, TRUE);
                    UpdateWindow(status_.get());
                    InvalidateRect(highlight_.get(), nullptr, TRUE);
                    std::ostringstream diagnostic;
                    diagnostic << "Current candidate: index="
                               << *session_.selected_index() << " HWND="
                               << hwnd_text(candidate->hwnd_value);
#if CLIPSS2_AUDIT_FEATURES
                    diagnostic << " title=\"" << utf8_from_utf16(candidate->title) << '"';
#endif
                    diagnostic << " bounds-source="
                               << (geometry->used_dwm ? "DWM" : "GetWindowRect")
                               << " source="
                               << selection_input_source_name(input_.source())
                               << '\n';
                    write_stdout_utf8(diagnostic.str());
                    return true;
                }
                std::cout << "Skipped stale/unavailable candidate: index="
                          << *session_.selected_index() << " HWND="
                          << hwnd_text(candidate->hwnd_value) << '\n';
            }
            if (!(forward ? session_.next() : session_.previous())) break;
        }
        ShowWindow(highlight_.get(), SW_HIDE);
        selected_title_ = ui_string(language_, UiStringId::no_live_window);
        InvalidateRect(status_.get(), nullptr, TRUE);
        return false;
    }

    void position_windows()
    {
        position_highlight();
        position_status(cursor_position());
        // Visual-only windows must remain above all shield receivers.
        SetWindowPos(tint_.get(), HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetWindowPos(highlight_.get(), HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetWindowPos(status_.get(), HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    void position_highlight()
    {
        const UINT target_dpi = std::max(GetDpiForWindow(live_->window), 96U);
        const int base_border = gesture_.active() ? 6 : 4;
        border_thickness_ = std::max(MulDiv(base_border, static_cast<int>(target_dpi), 96), 2);
        const auto& bounds = live_->bounds;
        const int highlight_width = bounds.right - bounds.left + border_thickness_ * 2;
        const int highlight_height = bounds.bottom - bounds.top + border_thickness_ * 2;
        if (!SetWindowPos(
                highlight_.get(), HWND_TOPMOST,
                bounds.left - border_thickness_, bounds.top - border_thickness_,
                highlight_width, highlight_height,
                SWP_NOACTIVATE | SWP_SHOWWINDOW))
            winrt::throw_last_error();

        if (!SetWindowPos(
                tint_.get(), HWND_TOPMOST,
                bounds.left, bounds.top,
                bounds.right - bounds.left, bounds.bottom - bounds.top,
                SWP_NOACTIVATE | SWP_NOOWNERZORDER))
            winrt::throw_last_error();
        if (gesture_.active() && gesture_.inside() && !gesture_.cancel_pending())
            ShowWindow(tint_.get(), SW_SHOWNOACTIVATE);
        else
            ShowWindow(tint_.get(), SW_HIDE);
    }

    void position_status(ScreenPosition cursor)
    {
        const POINT probe{cursor.x, cursor.y};
        const HMONITOR monitor = MonitorFromPoint(probe, MONITOR_DEFAULTTONEAREST);
        MONITORINFO information{sizeof(information)};
        if (!GetMonitorInfoW(monitor, &information)) winrt::throw_last_error();
        UINT dpi_x{};
        UINT dpi_y{};
        winrt::check_hresult(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y));
        const UINT dpi = std::max(dpi_x, 96U);
        const StatusLayout panel = update_status_layout(dpi);
        const int offset = MulDiv(hud_offset_dip, static_cast<int>(dpi), 96);
        int x = cursor.x + offset;
        if (x + panel.window_width > information.rcMonitor.right)
            x = cursor.x - offset - panel.window_width;
        const int monitor_left = static_cast<int>(information.rcMonitor.left);
        const int maximum_x = std::max(
            monitor_left,
            static_cast<int>(information.rcMonitor.right) - panel.window_width);
        x = std::clamp(x, monitor_left, maximum_x);
        // Clamp using the fully measured conditional JPEG row, not the previous
        // panel size. The lower-edge placement also protects the final Esc row.
        const int maximum_y = std::max(static_cast<int>(information.rcMonitor.top),
            static_cast<int>(information.rcMonitor.bottom) - panel.window_height);
        const int y = std::clamp(cursor.y + offset,
            static_cast<int>(information.rcMonitor.top), maximum_y);
        if (!SetWindowPos(
                status_.get(), HWND_TOPMOST, x, y,
                panel.window_width, panel.window_height,
                SWP_NOACTIVATE))
            winrt::throw_last_error();
    }

    [[nodiscard]] std::wstring status_text() const
    {
        std::wstring gesture_text;
        if (gesture_.cancel_pending() || (gesture_.active() && !gesture_.inside()))
            gesture_text = ui_string(language_, UiStringId::release_cancel);
        else if (gesture_.active())
        {
            if (action_ != WindowSelectionAction::confirm)
                gesture_text = ui_string(language_, UiStringId::release_capture);
            else
                gesture_text = ui_string(language_, UiStringId::release_confirm);
        }
        else
        {
            if (action_ != WindowSelectionAction::confirm)
                gesture_text = ui_string(language_, UiStringId::click_enter_capture);
            else
                gesture_text = ui_string(language_, UiStringId::click_enter_confirm);
        }
        std::wstring text = std::wstring(ui_string(language_, UiStringId::window_selection_title)) +
            L"\n\n" + selected_title_ + L"\n\n" + gesture_text + L"\n" +
            std::wstring(ui_string(language_, UiStringId::tab_select));
        if (!gesture_.active())
            text += cursor_policy_.include_cursor()
                ? L"\n" + std::wstring(ui_string(language_, UiStringId::cursor_on))
                : L"\n" + std::wstring(ui_string(language_, UiStringId::cursor_off));
        if (action_ == WindowSelectionAction::capture)
        {
            switch (destination_policy_.destination())
            {
            case capture::CaptureDestination::file: text += L"\n" + std::wstring(ui_string(language_, UiStringId::destination_file)); break;
            case capture::CaptureDestination::clipboard: text += L"\n" + std::wstring(ui_string(language_, UiStringId::destination_clipboard)); break;
            case capture::CaptureDestination::both: text += L"\n" + std::wstring(ui_string(language_, UiStringId::destination_both)); break;
            }
            const auto format_string = file_format_policy_.format() == image::ImageFileFormat::png
                ? UiStringId::format_png : file_format_policy_.format() == image::ImageFileFormat::jpeg
                ? UiStringId::format_jpeg : UiStringId::format_webp;
            text += L"\n" + std::wstring(file_format_policy_.format() == image::ImageFileFormat::avif
                ? language_ == UiLanguage::japanese ? L"F: \u5f62\u5f0f: AVIF" : L"F: File format: AVIF"
                : file_format_policy_.format() == image::ImageFileFormat::jxl
                ? language_ == UiLanguage::japanese ? L"F: \u5f62\u5f0f: JPEG XL" : L"F: File format: JPEG XL"
                : ui_string(language_, format_string));
            if (destination_policy_.destination() == capture::CaptureDestination::clipboard)
                text += ui_string(language_, UiStringId::file_only);
            if (file_format_policy_.format() == image::ImageFileFormat::jpeg)
            {
                text += L"\n" + localized_jpeg_row(language_, jpeg_policy_);
            }
            else if (file_format_policy_.format() == image::ImageFileFormat::avif ||
                     file_format_policy_.format() == image::ImageFileFormat::jxl)
                text += L"\n" + codec_profile_hud(language_, file_format_policy_.format(),
                    file_format_policy_.format() == image::ImageFileFormat::avif ? avif_profile_ : jxl_profile_);
        }
        else if (action_ == WindowSelectionAction::copy)
        {
            text += L"\n" + std::wstring(ui_string(language_, UiStringId::output_clipboard));
        }
        return text + L"\n" + std::wstring(ui_string(language_, UiStringId::escape_cancel));
    }

    StatusLayout update_status_layout(UINT dpi)
    {
        dpi = std::max(dpi, 96U);
        if (status_font_.get() == nullptr || status_dpi_ != dpi)
        {
            HFONT font = create_system_ui_font(dpi);
            if (font == nullptr) winrt::throw_last_error();
            status_font_.reset(font);
            status_dpi_ = dpi;
        }

        status_padding_ = MulDiv(16, static_cast<int>(dpi), 96);
        const int maximum_text_width = MulDiv(488, static_cast<int>(dpi), 96);
        RECT text_bounds{0, 0, maximum_text_width, 0};
        const WindowDeviceContext window_context{status_.get()};
        const std::wstring content = status_text();
        const HGDIOBJ previous_font =
            SelectObject(window_context.get(), status_font_.get());
        if (previous_font == nullptr || previous_font == HGDI_ERROR)
            throw std::runtime_error("Could not select the status font.");
        const int measured_height = DrawTextW(
            window_context.get(), content.c_str(), static_cast<int>(content.size()),
            &text_bounds,
            DT_CALCRECT | DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(window_context.get(), previous_font);
        if (measured_height == 0)
            throw std::runtime_error("Could not measure the status text.");

        const int client_width = maximum_text_width + status_padding_ * 2;
        const int client_height = text_bounds.bottom + status_padding_ * 2;
        RECT window_bounds{0, 0, client_width, client_height};
        const DWORD style = static_cast<DWORD>(
            GetWindowLongPtrW(status_.get(), GWL_STYLE));
        const DWORD extended_style = static_cast<DWORD>(
            GetWindowLongPtrW(status_.get(), GWL_EXSTYLE));
        if (!AdjustWindowRectExForDpi(
                &window_bounds, style, FALSE, extended_style, dpi))
            winrt::throw_last_error();

        return {
            window_bounds.right - window_bounds.left,
            window_bounds.bottom - window_bounds.top,
            client_width,
            client_height};
    }

    void handle_status_dpi_changed(
        HWND window, UINT message_dpi, const RECT& suggested_bounds)
    {
        const UINT window_dpi = GetDpiForWindow(window);
        const UINT dpi = message_dpi == 0 ? window_dpi : message_dpi;
        const StatusLayout layout = update_status_layout(dpi);
        if (!SetWindowPos(
                window, HWND_TOPMOST,
                suggested_bounds.left, suggested_bounds.top,
                layout.window_width, layout.window_height,
                SWP_NOACTIVATE))
            winrt::throw_last_error();
        InvalidateRect(window, nullptr, TRUE);
        UpdateWindow(window);
        std::cout << "Status DPI changed: " << dpi
                  << " (GetDpiForWindow=" << window_dpi << ')'
                  << "; client=" << layout.client_width << 'x'
                  << layout.client_height << '\n';
    }

    void paint_status(HWND window)
    {
        const PaintSession paint{window};
        const HDC context = paint.context();
        RECT client{};
        GetClientRect(window, &client);
        FillRect(context, &client, GetSysColorBrush(COLOR_WINDOW));
        SetBkMode(context, TRANSPARENT);
        SetTextColor(context, GetSysColor(COLOR_WINDOWTEXT));
        const HGDIOBJ previous_font = SelectObject(context, status_font_.get());
        if (previous_font == nullptr || previous_font == HGDI_ERROR)
            throw std::runtime_error("Could not select the status font.");
        RECT text{
            status_padding_, status_padding_,
            client.right - status_padding_, client.bottom - status_padding_};
        const std::wstring content = status_text();
        const int drawn_height = DrawTextW(
            context, content.c_str(), static_cast<int>(content.size()), &text,
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_NOCLIP);
        SelectObject(context, previous_font);
        if (drawn_height == 0)
            throw std::runtime_error("Could not draw the status text.");
    }

    void paint_highlight(HWND window)
    {
        const PaintSession paint{window};
        const HDC context = paint.context();
        RECT client{};
        GetClientRect(window, &client);
        const OwnedBrush transparent{transparency_key};
        FillRect(context, &client, transparent.get());

        COLORREF color = GetSysColor(COLOR_HIGHLIGHT);
        if (color == transparency_key || color == RGB(0, 0, 0))
            color = RGB(0, 120, 215);
        const OwnedBrush border{color};
        RECT top{0, 0, client.right, border_thickness_};
        RECT bottom{0, client.bottom - border_thickness_, client.right, client.bottom};
        RECT left{0, border_thickness_, border_thickness_, client.bottom - border_thickness_};
        RECT right{client.right - border_thickness_, border_thickness_,
                   client.right, client.bottom - border_thickness_};
        FillRect(context, &top, border.get());
        FillRect(context, &bottom, border.get());
        FillRect(context, &left, border.get());
        FillRect(context, &right, border.get());
    }

    void confirm(
        std::optional<bool> include_cursor = std::nullopt,
        std::optional<capture::CaptureDestination> destination = std::nullopt,
        std::optional<image::ImageFileFormat> file_format = std::nullopt,
        std::optional<codec::jpeg::JpegProductSelection> jpeg_selection = std::nullopt)
    {
        const WindowCandidate* current = session_.selected_candidate();
        if (current == nullptr) return;
        // Fix the identity before validation. Stale recovery may display a new
        // candidate, but this Confirm action can only ever confirm this copy.
        const WindowCandidate selected = *current;
        const auto selected_cursor = include_cursor.value_or(cursor_policy_.include_cursor());
        const auto selected_destination = destination.value_or(destination_policy_.destination());
        const auto selected_format = file_format.value_or(file_format_policy_.format());
        const auto selected_jpeg = jpeg_selection.value_or(jpeg_policy_.selection());
        const auto result = session_.confirm(
            selected.hwnd_value, [&selected](std::uintptr_t value)
        {
            return selected.hwnd_value == value &&
                   is_window_candidate_current(selected);
        });
        if (result == ConfirmationStatus::confirmed)
        {
            confirmed_ = WindowSelectionResult{
                selected,
                selected_cursor, selected_destination, selected_format, selected_jpeg,
                avif_profile_, jxl_profile_};
            close_windows();
        }
        else if (result == ConfirmationStatus::stale_window)
        {
            show_current_or_advance(true);
        }
    }

    static ScreenPosition cursor_position()
    {
        POINT cursor{};
        if (!GetCursorPos(&cursor)) winrt::throw_last_error();
        return {cursor.x, cursor.y};
    }

    [[nodiscard]] bool is_visual_window(HWND window) const noexcept
    {
        if (window == highlight_.get() || window == tint_.get()) return true;
        return std::any_of(shields_.begin(), shields_.end(), [window](const auto& shield)
        {
            return shield.get() == window;
        });
    }

    [[nodiscard]] std::optional<std::uintptr_t> resolve_hover(
        ScreenPosition position) const
    {
        const POINT point{position.x, position.y};
        const auto& candidates = session_.candidates();
        std::unordered_set<HWND> visited;
        HWND window = GetTopWindow(nullptr);
        for (std::size_t iteration = 0;
             window != nullptr && iteration < z_order_iteration_limit;
             ++iteration)
        {
            if (!visited.insert(window).second) break;
            const HWND next = GetWindow(window, GW_HWNDNEXT);
            if (!IsWindow(window) || !IsWindowVisible(window) || IsIconic(window))
            {
                window = next;
                continue;
            }
            RECT bounds{};
            if (!GetWindowRect(window, &bounds) || IsRectEmpty(&bounds) ||
                !PtInRect(&bounds, point))
            {
                window = next;
                continue;
            }
            DWORD cloaked{};
            const HRESULT cloak_result = DwmGetWindowAttribute(
                window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
            if (SUCCEEDED(cloak_result) && cloaked != 0)
            {
                window = next;
                continue;
            }
            if (FAILED(cloak_result)) return std::nullopt;

            if (window == status_.get()) return std::nullopt;
            if (is_visual_window(window))
            {
                window = next;
                continue;
            }

            DWORD process_id{};
            if (GetWindowThreadProcessId(window, &process_id) == 0 ||
                process_id == GetCurrentProcessId())
                return std::nullopt;
            const auto value = reinterpret_cast<std::uintptr_t>(window);
            const auto candidate = std::find_if(
                candidates.begin(), candidates.end(), [value](const auto& item)
                {
                    return item.hwnd_value == value;
                });
            if (candidate == candidates.end() || !candidate->selectable ||
                !is_window_candidate_current(*candidate))
                return std::nullopt;
            return value;
        }
        return std::nullopt;
    }

    [[nodiscard]] const WindowCandidate* snapshot_candidate(
        std::uintptr_t value) const noexcept
    {
        const auto& candidates = session_.candidates();
        const auto candidate = std::find_if(
            candidates.begin(), candidates.end(), [value](const auto& item)
            {
                return item.hwnd_value == value;
            });
        return candidate == candidates.end() ? nullptr : &*candidate;
    }

    void select_hover(std::uintptr_t hwnd_value)
    {
        const bool changed = session_.selected_candidate() == nullptr ||
            session_.selected_candidate()->hwnd_value != hwnd_value;
        const bool source_changed = input_.source() != SelectionInputSource::mouse;
        if (!session_.set_candidate_by_hwnd(hwnd_value)) return;
        input_.record_mouse_selection();
        if (source_changed) std::cout << "Selection source: mouse\n";
        if (changed)
        {
            std::cout << "Hover candidate: " << hwnd_text(hwnd_value) << '\n';
            show_current_or_advance(true);
        }
    }

    void handle_mouse_move(ScreenPosition position)
    {
        position_status(position);
        if (!input_.observe_cursor(position)) return;
        const auto hover = resolve_hover(position);
        if (gesture_.active())
        {
            if (gesture_.move_over(hover))
            {
                std::cout << "Pressed-inside: " << std::boolalpha
                          << gesture_.inside() << '\n';
                refresh_pressed_visual();
            }
            return;
        }
        if (hover) select_hover(*hover);
    }

    void handle_mouse_down(ScreenPosition position)
    {
        (void)input_.observe_cursor(position);
        const auto hover = resolve_hover(position);
        if (!hover || !gesture_.press(*hover)) return;
        select_hover(*hover);
        pressed_include_cursor_ = cursor_policy_.include_cursor();
        pressed_destination_ = destination_policy_.destination();
        pressed_file_format_ = file_format_policy_.format();
        pressed_jpeg_selection_ = jpeg_policy_.selection();
        start_lost_release_watchdog();
        std::cout << "Pressed candidate: " << hwnd_text(*hover)
                  << "\nPressed-inside: true\n";
        refresh_pressed_visual();
    }

    void handle_mouse_up(ScreenPosition position)
    {
        (void)input_.observe_cursor(position);
        const auto hover = resolve_hover(position);
        const auto pressed = gesture_.candidate();
        const auto* candidate = pressed ? snapshot_candidate(*pressed) : nullptr;
        const bool live = candidate != nullptr && is_window_candidate_current(*candidate);
        const auto action = gesture_.release(hover, live);
        if (pressed) stop_lost_release_watchdog();
        if (action == MouseReleaseAction::cancel_selection)
        {
            cancel();
            return;
        }
        if (action == MouseReleaseAction::confirm && pressed)
        {
            (void)session_.set_candidate_by_hwnd(*pressed);
            std::cout << "Confirm source: mouse\nTarget click forwarded: no\n";
            confirm(pressed_include_cursor_, pressed_destination_, pressed_file_format_, pressed_jpeg_selection_);
            return;
        }
        refresh_pressed_visual();
        if (pressed && !live) (void)show_current_or_advance(true);
        if (hover) select_hover(*hover);
    }

    void refresh_pressed_visual()
    {
        if (live_)
        {
            position_highlight();
            InvalidateRect(highlight_.get(), nullptr, TRUE);
            InvalidateRect(tint_.get(), nullptr, TRUE);
        }
        position_status(cursor_position());
        InvalidateRect(status_.get(), nullptr, TRUE);
        UpdateWindow(status_.get());
    }

    [[nodiscard]] static bool physical_left_button_down() noexcept
    {
        return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    }

    void start_lost_release_watchdog()
    {
        if (lost_release_timer_active_) return;
        if (SetTimer(status_.get(), lost_release_timer_id,
                     lost_release_interval_ms, nullptr) == 0)
            winrt::throw_last_error();
        lost_release_timer_active_ = true;
    }

    void stop_lost_release_watchdog() noexcept
    {
        if (!lost_release_timer_active_) return;
        (void)KillTimer(status_.get(), lost_release_timer_id);
        lost_release_timer_active_ = false;
    }

    void recover_lost_release()
    {
        const auto action = gesture_.recover_lost_release(
            physical_left_button_down());
        if (action == MouseRecoveryAction::none) return;
        stop_lost_release_watchdog();
        std::cout << "Mouse press: lost release recovered; no confirmation\n";
        if (action == MouseRecoveryAction::cancel_selection)
        {
            cancel();
            return;
        }
        refresh_pressed_visual();
    }

    void cancel() noexcept
    {
        session_.cancel();
        close_windows();
    }

    void cancel_for_display_change() noexcept
    {
        if (!ready_ || display_change_handled_) return;
        display_change_handled_ = true;
        std::cout << "Display configuration changed: selection cancelled\n";
        cancel();
    }

    void close_windows() noexcept
    {
        ready_ = false;
        stop_lost_release_watchdog();
        if (tint_.get() != nullptr) ShowWindow(tint_.get(), SW_HIDE);
        if (highlight_.get() != nullptr) ShowWindow(highlight_.get(), SW_HIDE);
        shields_.clear();
        tint_.reset();
        highlight_.reset();
        if (status_.get() != nullptr && IsWindow(status_.get()))
            DestroyWindow(status_.get());
    }

    WindowSelectionSession session_;
    HWND original_foreground_{};
    SelectionInputArbitrator input_;
    MouseSelectionGesture gesture_;
    CursorCapturePolicy cursor_policy_;
    capture::CaptureDestinationPolicy destination_policy_;
    FileFormatPolicy file_format_policy_;
    JpegSessionPolicy jpeg_policy_;
    capture::FileCompression avif_profile_, jxl_profile_;
    codec::jpeg::JpegProductSelection pressed_jpeg_selection_{codec::jpeg::JpegCurrentDefault{}};
    image::ImageFileFormat pressed_file_format_{image::ImageFileFormat::png};
    bool pressed_include_cursor_{};
    capture::CaptureDestination pressed_destination_{capture::CaptureDestination::file};
    OwnedFont status_font_;
    OwnedWindow highlight_;
    OwnedWindow tint_;
    OwnedWindow status_;
    std::vector<OwnedWindow> shields_;
    std::optional<LiveBounds> live_;
    std::optional<WindowSelectionResult> confirmed_;
    std::wstring selected_title_;
    UINT status_dpi_{};
    int status_padding_{};
    int border_thickness_{4};
    WindowSelectionAction action_{WindowSelectionAction::confirm};
    UiLanguage language_{UiLanguage::english};
    bool ready_{};
    bool display_change_handled_{};
    bool lost_release_timer_active_{};
    std::exception_ptr callback_error_;
};
}

std::optional<WindowSelectionResult> select_window(WindowSelectionAction action, const ProductPreferences& initial)
{
    const ProductPreferences preferences = action == WindowSelectionAction::capture ? initial : ProductPreferences{};
    validate_product_preferences(preferences);
    const HWND original_foreground = GetForegroundWindow();
    const auto original_value = reinterpret_cast<std::uintptr_t>(original_foreground);
    WindowSelectionSession session{enumerate_window_candidates()};
    session.start(original_value);
    POINT initial_cursor{};
    if (!GetCursorPos(&initial_cursor)) winrt::throw_last_error();

    std::cout << "Window selector UI: starting\n"
              << "Original foreground HWND: " << hwnd_text(original_value) << '\n'
              << "Snapshot candidates: " << session.candidates().size() << '\n'
              << "Selectable candidates: " << session.selectable_count() << '\n';
    const DPI_AWARENESS_CONTEXT dpi_context = GetThreadDpiAwarenessContext();
    std::cout << "Thread DPI awareness: "
              << (AreDpiAwarenessContextsEqual(
                      dpi_context, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
                      ? "PerMonitorV2" : "not PerMonitorV2")
              << " (awareness="
              << static_cast<int>(GetAwarenessFromDpiAwarenessContext(dpi_context))
              << ")\n";

    if (!session.selected_candidate())
        throw std::runtime_error("No selectable window is available.");

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    if (instance == nullptr) winrt::throw_last_error();
    const std::wstring suffix = L"." + std::to_wstring(GetCurrentProcessId());
    RegisteredWindowClass status_class{
        instance, L"clipss2.WindowSelector.Status" + suffix,
        SelectorUi::status_procedure, GetSysColorBrush(COLOR_WINDOW)};
    RegisteredWindowClass highlight_class{
        instance, L"clipss2.WindowSelector.Highlight" + suffix,
        SelectorUi::highlight_procedure, nullptr};
    RegisteredWindowClass tint_class{
        instance, L"clipss2.WindowSelector.Tint" + suffix,
        SelectorUi::tint_procedure, nullptr};
    RegisteredWindowClass shield_class{
        instance, L"clipss2.WindowSelector.Shield" + suffix,
        SelectorUi::shield_procedure, nullptr};
    std::optional<WindowSelectionResult> result;
    {
        SelectorUi ui{
            std::move(session), original_foreground,
            {initial_cursor.x, initial_cursor.y}, action, preferences};
        ui.create(instance, status_class.name(), highlight_class.name(),
                  tint_class.name(), shield_class.name());
        result = ui.run();
    }
    std::cout << "Selector UI teardown: complete\n";
    return result;
}

int run_window_selector_ui()
{
    (void)select_window();
    std::cout << "Capture side effect: none\n";
    return 0;
}
}

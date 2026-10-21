#include "audit/audit_policy.h"
#include "platform/windows/resident_mode.h"

#include "platform/windows/capture_window.h"
#include "platform/windows/error_diagnostics.h"
#include "platform/windows/unicode_diagnostics.h"
#include "platform/windows/resource_counters.h"
#include "platform/windows/resident_state.h"
#include "platform/windows/settings_ui.h"
#include "platform/windows/tray_icon.h"

#include <windows.h>

#include <winrt/base.h>

#include <atomic>
#include <array>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace clipss2::platform::windows
{
namespace
{
constexpr int capture_hotkey_id = 1;
constexpr UINT begin_capture_message = WM_APP + 0x120;
constexpr UINT resource_snapshot_message = WM_APP + 0x121;
constexpr UINT state_query_message = WM_APP + 0x122;
constexpr UINT tray_capture_test_message = WM_APP + 0x123;
constexpr UINT tray_menu_query_message = WM_APP + 0x124;
constexpr UINT tray_settings_test_message = WM_APP + 0x127;
constexpr wchar_t resident_window_title[] = L"clipss2 resident control";

enum class CaptureRequestSource
{
    hotkey,
    tray,
};

std::atomic<HWND> console_shutdown_window{};

std::wstring current_executable_path()
{
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size())
        throw DiagnosticError(format_win32_error(
            "GetModuleFileNameW resident discovery failed", GetLastError()));
    return {buffer.data(), length};
}

struct ResidentSearch
{
    std::wstring expected_image;
    HWND found{};
};

BOOL CALLBACK find_resident_window(HWND window, LPARAM parameter) noexcept
{
    auto& search = *reinterpret_cast<ResidentSearch*>(parameter);
    wchar_t class_name[128]{};
    if (!GetClassNameW(window, class_name, static_cast<int>(std::size(class_name))))
        return TRUE;
    DWORD process{};
    (void)GetWindowThreadProcessId(window, &process);
    if (!process || process == GetCurrentProcessId()) return TRUE;
    const std::wstring expected_class = L"clipss2.Resident.Control." +
        std::to_wstring(process);
    if (expected_class != class_name) return TRUE;

    const HANDLE opened = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process);
    if (!opened) return TRUE;
    std::array<wchar_t, 32768> image{};
    DWORD length = static_cast<DWORD>(image.size());
    const bool queried = QueryFullProcessImageNameW(
        opened, 0, image.data(), &length) != FALSE;
    (void)CloseHandle(opened);
    if (!queried) return TRUE;
    if (CompareStringOrdinal(image.data(), static_cast<int>(length),
            search.expected_image.data(), static_cast<int>(search.expected_image.size()),
            TRUE) != CSTR_EQUAL)
        return TRUE;
    search.found = window;
    return FALSE;
}

HWND find_owned_resident()
{
    ResidentSearch search{current_executable_path()};
    (void)EnumWindows(find_resident_window, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

BOOL WINAPI console_control_handler(const DWORD event) noexcept
{
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT) return FALSE;
    const HWND window = console_shutdown_window.load(std::memory_order_acquire);
    if (window == nullptr) return FALSE;
    return PostMessageW(window, WM_CLOSE, 0, 0) ? TRUE : FALSE;
}

class RegisteredResidentClass final
{
public:
    RegisteredResidentClass(HINSTANCE instance, std::wstring name, WNDPROC procedure)
        : instance_(instance), name_(std::move(name))
    {
        WNDCLASSEXW value{};
        value.cbSize = sizeof(value);
        value.lpfnWndProc = procedure;
        value.hInstance = instance_;
        value.lpszClassName = name_.c_str();
        if (RegisterClassExW(&value) == 0)
            throw DiagnosticError(format_win32_error(
                "RegisterClassExW resident control failed", GetLastError()));
        registered_ = true;
    }

    ~RegisteredResidentClass()
    {
        if (registered_) (void)UnregisterClassW(name_.c_str(), instance_);
    }

    RegisteredResidentClass(const RegisteredResidentClass&) = delete;
    RegisteredResidentClass& operator=(const RegisteredResidentClass&) = delete;

    [[nodiscard]] const wchar_t* name() const noexcept { return name_.c_str(); }

private:
    HINSTANCE instance_{};
    std::wstring name_;
    bool registered_{};
};

class RegisteredHotkey final
{
public:
    void register_for(HWND window)
    {
        if (registered_) throw std::logic_error("Resident hotkey is already registered.");
        SetLastError(ERROR_SUCCESS);
        if (!RegisterHotKey(
                window, capture_hotkey_id, resident_capture_hotkey.modifiers,
                resident_capture_hotkey.virtual_key))
        {
            const DWORD error = GetLastError();
            throw DiagnosticError(format_win32_error(
                "RegisterHotKey Ctrl+Shift+F11 failed", error));
        }
        window_ = window;
        registered_ = true;
    }

    [[nodiscard]] bool unregister(std::string& diagnostic) noexcept
    {
        if (!registered_) return true;
        SetLastError(ERROR_SUCCESS);
        if (!UnregisterHotKey(window_, capture_hotkey_id))
        {
            diagnostic = format_win32_error(
                "UnregisterHotKey Ctrl+Shift+F11 failed", GetLastError());
            return false;
        }
        registered_ = false;
        window_ = nullptr;
        return true;
    }

    ~RegisteredHotkey()
    {
        if (registered_) (void)UnregisterHotKey(window_, capture_hotkey_id);
    }

    RegisteredHotkey() = default;
    RegisteredHotkey(const RegisteredHotkey&) = delete;
    RegisteredHotkey& operator=(const RegisteredHotkey&) = delete;

private:
    HWND window_{};
    bool registered_{};
};

class ConsoleControlRegistration final
{
public:
    void install(HWND window)
    {
        // Query console attachment independently of redirected standard handles.
        // Tray/session shutdown does not require a console.
        if (GetConsoleCP() == 0) return;
        console_shutdown_window.store(window, std::memory_order_release);
        if (!SetConsoleCtrlHandler(console_control_handler, TRUE))
        {
            console_shutdown_window.store(nullptr, std::memory_order_release);
            throw DiagnosticError(format_win32_error(
                "SetConsoleCtrlHandler install failed", GetLastError()));
        }
        installed_ = true;
    }

    ~ConsoleControlRegistration()
    {
        console_shutdown_window.store(nullptr, std::memory_order_release);
        if (installed_) (void)SetConsoleCtrlHandler(console_control_handler, FALSE);
    }

    ConsoleControlRegistration() = default;
    ConsoleControlRegistration(const ConsoleControlRegistration&) = delete;
    ConsoleControlRegistration& operator=(const ConsoleControlRegistration&) = delete;

private:
    bool installed_{};
};

class ResidentController final
{
public:
    void create(HINSTANCE instance, const wchar_t* class_name)
    {
        window_ = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            class_name, resident_window_title, WS_OVERLAPPED,
            CW_USEDEFAULT, CW_USEDEFAULT, 0, 0,
            nullptr, nullptr, instance, this);
        if (window_ == nullptr)
            throw DiagnosticError(format_win32_error(
                "CreateWindowExW resident control failed", GetLastError()));
        taskbar_created_message_ = RegisterWindowMessageW(L"TaskbarCreated");
        if (taskbar_created_message_ == 0)
            throw DiagnosticError(format_win32_error(
                "RegisterWindowMessageW TaskbarCreated failed", GetLastError()));
        hotkey_.register_for(window_);
        tray_.add(window_);
        std::cout << "Tray icon NIM_ADD: succeeded\n"
                  << "Tray icon NIM_SETVERSION: NOTIFYICON_VERSION_4\n"
                  << "Tray tooltip: clipss2\n";
        console_control_.install(window_);
    }

    int run()
    {
        std::wcout << L"Resident mode active.\n"
                   << L"Capture window: " << resident_capture_hotkey_text() << L'\n';
        MSG message{};
        for (;;)
        {
            const BOOL result = GetMessageW(&message, nullptr, 0, 0);
            if (result == -1)
                throw DiagnosticError(format_win32_error(
                    "GetMessageW resident loop failed", GetLastError()));
            if (result == 0) break;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (callback_error_) std::rethrow_exception(callback_error_);
        return exit_code_;
    }

    void announce_startup() noexcept
    {
        try
        {
            std::string diagnostic;
            if (tray_.notify_startup(diagnostic))
                std::cout << "Startup notification: requested (display is controlled by Windows)\n";
            else
                write_stderr_utf8("Startup notification: failed; resident remains active: " + diagnostic + '\n');
        }
        catch (...)
        {
            // Notification failure must not unwind a fully registered resident.
            write_stderr_utf8("Startup notification: failed; resident remains active\n");
        }
        std::cout.flush();
        std::cerr.flush();
    }

    ~ResidentController()
    {
        console_shutdown_window.store(nullptr, std::memory_order_release);
        std::string ignored;
        (void)tray_.remove(ignored);
        (void)hotkey_.unregister(ignored);
        if (window_ != nullptr && IsWindow(window_)) (void)DestroyWindow(window_);
    }

    ResidentController() = default;
    ResidentController(const ResidentController&) = delete;
    ResidentController& operator=(const ResidentController&) = delete;

    static LRESULT CALLBACK procedure(
        HWND window, UINT message, WPARAM word, LPARAM parameter) noexcept
    {
        if (message == WM_NCCREATE)
        {
            const auto* creation = reinterpret_cast<CREATESTRUCTW*>(parameter);
            auto* self = static_cast<ResidentController*>(creation->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->window_ = window;
        }
        auto* self = reinterpret_cast<ResidentController*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (self == nullptr) return DefWindowProcW(window, message, word, parameter);
        try
        {
            return self->handle_message(window, message, word, parameter);
        }
        catch (...)
        {
            self->callback_error_ = std::current_exception();
            self->exit_code_ = 1;
            try
            {
                self->finish_failed_operation();
                self->request_shutdown();
            }
            catch (...)
            {
                if (self->window_ != nullptr)
                    (void)DestroyWindow(self->window_);
                PostQuitMessage(1);
            }
            return 0;
        }
    }

private:
    LRESULT handle_message(
        HWND window, UINT message, WPARAM word, LPARAM parameter)
    {
        if (message == taskbar_created_message_)
        {
            std::string diagnostic;
            if (tray_.restore_after_taskbar_created(diagnostic))
                std::cout << "Tray icon TaskbarCreated recovery: succeeded\n";
            else
                write_stderr_utf8("Tray icon TaskbarCreated recovery failed: " + diagnostic + "; resident remains active\n");
            return 0;
        }
        switch (message)
        {
        case WM_HOTKEY:
            if (static_cast<int>(word) != capture_hotkey_id) return 0;
            request_capture(CaptureRequestSource::hotkey);
            return 0;

        case tray_callback_message:
            handle_tray_callback(word, parameter);
            return 0;

        case begin_capture_message:
            begin_capture();
            return 0;

#if CLIPSS2_AUDIT_FEATURES
        case resource_snapshot_message:
            print_resource_snapshot_for_audit(static_cast<unsigned int>(word));
            return 0;

        case state_query_message:
            return static_cast<LRESULT>(state_.state());

        case tray_capture_test_message:
            request_capture(CaptureRequestSource::tray,word==1,word==2);
            return 0;

        case tray_menu_query_message:
            return menu_open_ ? 1 : 0;
#endif

        case resident_show_settings_message:
            show_settings();
            return 0;

#if CLIPSS2_AUDIT_FEATURES
        case tray_settings_test_message:
            std::cout << "Tray context menu command: Settings\n";
            show_settings();
            return 0;

        case WM_APP + 0x125:
        {
            wchar_t enabled[2]{};
            if (!menu_open_ || clipss2::audit::environment_variable(
                L"_CLIPSS2_AUDIT_TRAY_INTERNAL_INPUT", enabled, 2) != 1 || enabled[0] != L'1') return 0;
            constexpr UINT commands[]{1, 3, 4, 5, 2}; // Actual HMENU command IDs, separators skipped.
            if (word == VK_DOWN) audit_menu_position_ = (audit_menu_position_ + 1) % 5;
            else if (word == VK_ESCAPE) (void)tray_.select_menu_for_audit(0);
            else if (word == VK_RETURN && audit_menu_position_ >= 0)
                (void)tray_.select_menu_for_audit(commands[audit_menu_position_]);
            return 0;
        }
#endif

        case WM_QUERYENDSESSION:
            // A cancelled query must leave the resident running.
            return TRUE;

        case WM_ENDSESSION:
            if (word != FALSE) request_shutdown();
            return 0;

        case WM_CLOSE:
            request_shutdown();
            return 0;

        case WM_DESTROY:
            window_ = nullptr;
            PostQuitMessage(exit_code_);
            return 0;

        case WM_NCDESTROY:
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            return DefWindowProcW(window, message, word, parameter);

        default:
            return DefWindowProcW(window, message, word, parameter);
        }
    }

    void request_capture(const CaptureRequestSource source, const bool region=false, const bool monitor=false)
    {
        const char* source_name =
            source == CaptureRequestSource::hotkey ? "hotkey" : "tray";
        if (!state_.try_begin_capture())
        {
            std::cout << "Resident " << source_name
                      << " capture request: ignored while busy\n";
            return;
        }
        if (menu_open_) (void)EndMenu();
        region_mode_=region;
        monitor_mode_=monitor;
        if (!PostMessageW(window_, begin_capture_message, 0, 0))
        {
            const DWORD error = GetLastError();
            state_.selection_cancelled();
            throw DiagnosticError(format_win32_error(
                "PostMessageW begin capture failed", error));
        }
        std::cout << "Resident " << source_name << " capture request: accepted\n";
    }

    void handle_tray_callback(const WPARAM word, const LPARAM parameter)
    {
        const TrayCallback callback = decode_tray_callback(word, parameter);
        if (callback.icon_id != tray_icon_id) return;
        if (callback.event != WM_CONTEXTMENU && callback.event != NIN_SELECT &&
            callback.event != NIN_KEYSELECT) return;

        menu_open_ = true;
        audit_menu_position_ = -1;
        std::wcout << L"Tray context menu: opened; Capture Window\t"
                   << resident_capture_hotkey_text()
                   << L"; Capture Region; Capture Monitor; Settings; Exit\n";
        std::string diagnostic;
        TrayCommand command{};
        try
        {
            command = tray_.show_context_menu(
                callback.anchor, state_.state() == ResidentState::idle, diagnostic);
        }
        catch (...)
        {
            menu_open_ = false;
            throw;
        }
        menu_open_ = false;
        wchar_t audit_input[2]{};
        if (clipss2::audit::environment_variable(L"_CLIPSS2_AUDIT_TRAY_INTERNAL_INPUT", audit_input, 2) != 1 || audit_input[0] != L'1')
            tray_.return_focus_to_notification_area(diagnostic);
        if (!diagnostic.empty())
            write_stderr_utf8("Tray menu diagnostic: " + diagnostic + '\n');

        if (command == TrayCommand::capture_window)
        {
            std::cout << "Tray context menu command: Capture Window\n";
            request_capture(CaptureRequestSource::tray);
        }
        else if (command == TrayCommand::exit)
        {
            std::cout << "Tray context menu command: Exit\n";
            if (!PostMessageW(window_, WM_CLOSE, 0, 0))
                throw DiagnosticError(format_win32_error(
                    "PostMessageW tray Exit failed", GetLastError()));
        }
        else if (command == TrayCommand::capture_region)
        {
            std::cout << "Tray context menu command: Capture Region\n";
            request_capture(CaptureRequestSource::tray,true);
        }
        else if (command == TrayCommand::capture_monitor)
        {
            std::cout << "Tray context menu command: Capture Monitor\n";
            request_capture(CaptureRequestSource::tray,false,true);
        }
        else if (command == TrayCommand::settings)
        {
            std::cout << "Tray context menu command: Settings\n";
            show_settings();
        }
        else
            std::cout << "Tray context menu: dismissed\n";
    }

    void begin_capture()
    {
        if (state_.state() != ResidentState::selecting) return;
        std::cout << "Resident capture: selecting\n";
        try
        {
            const auto execution = monitor_mode_ ? run_monitor_capture_product_flow(
                [this] { state_.selection_confirmed(); }, &settings_) : region_mode_ ? run_region_capture_product_flow(
                [this] { state_.selection_confirmed(); }, &settings_) : run_window_capture_product_flow(
                false, [this] { state_.selection_confirmed(); }, &settings_);
            if (execution.worker_launched)
            {
                state_.capture_finished();
                const int capture_exit = report_product_capture_execution(execution);
                if (capture_exit == 0)
                    std::cout << "Resident capture succeeded\n";
                else
                    write_stderr_utf8("Resident capture failed; resident remains active\n");
            }
            else
            {
                state_.selection_cancelled();
                (void)report_product_capture_execution(execution);
                std::cout << "Resident capture cancelled; resident remains active\n";
            }
        }
        catch (...)
        {
            finish_failed_operation();
            write_stderr_utf8("Resident capture failed: " + describe_exception(std::current_exception()) +
                "; resident remains active\n");
        }
        if (state_.state() == ResidentState::shutting_down) begin_shutdown();
    }

    void show_settings()
    {
        if (!state_.try_begin_settings())
        {
            if (state_.state() == ResidentState::configuring)
            {
                if (const HWND existing = FindWindowW(settings_window_class_name, nullptr))
                {
                    ShowWindow(existing, SW_RESTORE);
                    (void)SetForegroundWindow(existing);
                }
            }
            std::cout << "Resident settings request: ignored while busy\n";
            return;
        }
        std::cout << "Resident settings request: accepted\n";
        try
        {
            (void)run_settings_ui(settings_, window_);
            state_.settings_finished();
            std::cout << "Resident settings dialog: closed\n";
        }
        catch (...)
        {
            state_.settings_finished();
            throw;
        }
        if (state_.state() == ResidentState::shutting_down) begin_shutdown();
    }

#if CLIPSS2_AUDIT_FEATURES
    void print_resource_snapshot_for_audit(const unsigned int sequence)
    {
        const auto resources = collect_resource_snapshot();
        const auto types = count_handle_types(collect_process_handles());
        const auto alpc = types.find(L"ALPC Port");
        std::cout << "Resident resource snapshot " << sequence
                  << ": handles=" << resources.handles
                  << ", ALPC=" << (alpc == types.end() ? 0 : alpc->second)
                  << ", USER=" << resources.user_objects
                  << ", GDI=" << resources.gdi_objects
                  << ", working-set=" << resources.working_set_bytes
                  << ", private=" << resources.private_bytes << '\n';
        std::cout.flush();
    }
#endif

    void finish_failed_operation()
    {
        if (state_.state() == ResidentState::selecting)
            state_.selection_cancelled();
        else if (state_.state() == ResidentState::capturing)
            state_.capture_finished();
    }

    void request_shutdown()
    {
        if ((region_mode_ || monitor_mode_) && state_.state()==ResidentState::selecting)
        {
            const std::wstring selector_class = (monitor_mode_
                ? L"clipss2.MonitorSelector.Status." : L"clipss2.RegionSelector.Status.") +
                std::to_wstring(GetCurrentProcessId());
            const auto selector=FindWindowW(selector_class.c_str(),nullptr);
            if (selector) (void)PostMessageW(selector,WM_CLOSE,0,0);
        }
        if (state_.request_shutdown())
            begin_shutdown();
        else
            std::cout << "Resident shutdown requested; waiting for current operation\n";
    }

    void begin_shutdown()
    {
        if (shutdown_started_) return;
        shutdown_started_ = true;
        std::string diagnostic;
        if (!tray_.remove(diagnostic))
        {
            // Explorer may already have discarded the icon. Shutdown remains
            // valid, but the failure is explicit.
            write_stderr_utf8(diagnostic + '\n');
        }
        else
        {
            std::cout << "Tray icon NIM_DELETE: succeeded\n";
        }
        diagnostic.clear();
        if (!hotkey_.unregister(diagnostic))
        {
            exit_code_ = 1;
            write_stderr_utf8(diagnostic + '\n');
        }
        else
        {
            std::cout << "Resident hotkey unregistered\n";
        }
        console_shutdown_window.store(nullptr, std::memory_order_release);
        const HWND window = window_;
        if (window != nullptr && !DestroyWindow(window))
        {
            exit_code_ = 1;
            throw DiagnosticError(format_win32_error(
                "DestroyWindow resident control failed", GetLastError()));
        }
    }

    HWND window_{};
    RegisteredHotkey hotkey_;
    TrayIcon tray_;
    ConsoleControlRegistration console_control_;
    ResidentStateMachine state_;
    ProductSettings settings_; // Loaded once at controller construction, no hot reload.
    UINT taskbar_created_message_{};
    bool menu_open_{};
    int audit_menu_position_{-1};
    bool region_mode_{};
    bool monitor_mode_{};
    bool shutdown_started_{};
    int exit_code_{};
    std::exception_ptr callback_error_;
};
}

int run_resident_mode(const bool notify_startup)
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    if (instance == nullptr)
        throw DiagnosticError(format_win32_error(
            "GetModuleHandleW resident mode failed", GetLastError()));
    const std::wstring class_name = L"clipss2.Resident.Control." +
        std::to_wstring(GetCurrentProcessId());
    RegisteredResidentClass resident_class{
        instance, class_name, ResidentController::procedure};
    ResidentController controller;
    controller.create(instance, resident_class.name());
    if (notify_startup) controller.announce_startup();
    return controller.run();
}

int run_settings_mode()
{
    if (const HWND resident = find_owned_resident())
    {
        if (!PostMessageW(resident, resident_show_settings_message, 0, 0))
            throw DiagnosticError(format_win32_error(
                "PostMessageW resident Settings failed", GetLastError()));
        std::cout << "Settings request forwarded to resident\n";
        return 0;
    }
    ProductSettings settings;
    (void)run_settings_ui(settings);
    return 0;
}
}

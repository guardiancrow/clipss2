#include "audit/audit_policy.h"
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <winrt/base.h>
#include "platform/windows/capture_window.h"
#include "platform/windows/capture_worker.h"
#include "platform/windows/error_diagnostics.h"
#include "platform/windows/resident_mode.h"
#include "platform/windows/unicode_diagnostics.h"

#if CLIPSS2_AUDIT_FEATURES
#include "audit/artifact_directory.h"
#include "audit/jpeg_output_audit.h"
#include "platform/windows/alpha_capture_audit.h"
#include "platform/windows/capture_border_audit.h"
#include "platform/windows/capture_worker_audit.h"
#include "platform/windows/color_self_test.h"
#include "platform/windows/d3d_device.h"
#include "platform/windows/display_color_audit.h"
#include "platform/windows/lossy_worker_audit.h"
#include "platform/windows/resource_lifetime_audit.h"
#include "platform/windows/wgc_handle_isolation_audit.h"
#include "platform/windows/wgc_item_reuse_audit.h"
#include "platform/windows/window_candidate_self_test.h"
#include "platform/windows/window_selector_ui.h"
#include <winrt/Windows.Graphics.Capture.h>

namespace
{
std::optional<int> run_fixture_command(int argc, char** argv)
{
    using namespace clipss2::platform::windows;
    const std::string_view mode = argc >= 2 ? argv[1] : "";
    if (mode == "--audit-fixture-info" && argc == 2)
    {
        std::cout << "clipss2 audit fixture: test-only, CLIPSS2_AUDIT_FEATURES=1\n";
        return 0;
    }
    if (mode == "--lossy-worker-audit")
    {
        if (argc != 3) throw std::invalid_argument("Private lossy worker audit requires a NEW output directory.");
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        return run_lossy_worker_audit(std::filesystem::path{argv[2]});
    }
    if (mode == "--jpeg-output-audit")
    {
        if (argc != 2) throw std::invalid_argument("Usage: audit fixture --jpeg-output-audit");
        return clipss2::audit::run_jpeg_output_audit(clipss2::audit::artifact_directory() / "jpeg-output-audit");
    }
    const bool audit = mode == "--wgc-handle-isolation-audit" || mode == "--wgc-item-reuse-audit" ||
        mode == "--capture-worker-audit" || mode == "--window-select" || mode == "--display-color-audit" ||
        mode == "--capture-border-audit" || mode == "--color-self-test" || mode == "--alpha-capture-audit" ||
        mode == "--resource-lifetime-audit" || mode == "--color-self-test-all-monitors" ||
        mode == "--window-candidate-self-test";
    if (!audit) return {};
    if (argc != 2 && !(argc == 3 && (mode == "--wgc-handle-isolation-audit" ||
        mode == "--wgc-item-reuse-audit" || mode == "--capture-worker-audit")))
        throw std::invalid_argument("Invalid audit fixture arguments.");
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    const std::string_view probe = argc == 3 ? argv[2] : "";
    if (mode == "--wgc-handle-isolation-audit") return run_wgc_handle_isolation_audit(probe.empty() ? "staged" : probe);
    if (mode == "--wgc-item-reuse-audit") return run_wgc_item_reuse_audit(probe.empty() ? "same-item-capture" : probe);
    if (mode == "--capture-worker-audit") return run_capture_worker_audit(probe.empty() ? "one-shot" : probe);
    if (mode == "--window-select") return run_window_selector_ui();
    if (mode == "--display-color-audit") return run_display_color_audit();
    if (mode == "--capture-border-audit") return run_capture_border_audit();
    if (!winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported())
        throw std::runtime_error("Windows.Graphics.Capture is not supported.");
    const auto device = D3dDevice::create();
    if (mode == "--color-self-test") return run_color_self_test(device);
    if (mode == "--alpha-capture-audit") return run_alpha_capture_audit(device);
    if (mode == "--resource-lifetime-audit") return run_resource_lifetime_audit(device);
    if (mode == "--color-self-test-all-monitors") return run_color_self_test_all_monitors(device);
    return run_window_candidate_self_test();
}
}
#endif

int main(int argc, char** argv)
{
    try
    {
        const std::string_view mode = argc >= 2 ? argv[1] : "";
        if (argc == 2 && mode == "--help")
        {
            std::cout << "clipss2: no arguments starts the tray resident.\n"
                      << "Usage: clipss2 [--resident|--settings|--capture-window|--capture-region|"
                         "--capture-monitor|--capture-window-clipboard|--help]\n"
                      << "Resident hotkey: Ctrl+Shift+F11. No capture occurs until requested and confirmed.\n";
            return 0;
        }
        if (mode == "--capture-worker")
        {
            std::vector<std::string_view> arguments;
            arguments.reserve(argc > 2 ? static_cast<std::size_t>(argc - 2) : 0U);
            for (int index = 2; index < argc; ++index) arguments.emplace_back(argv[index]);
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            return clipss2::platform::windows::run_capture_worker(arguments);
        }
#if CLIPSS2_AUDIT_FEATURES
        if (const auto result = run_fixture_command(argc, argv)) return *result;
#endif
        if (argc > 2 || (argc == 2 && mode != "--resident" && mode != "--settings" &&
            mode != "--capture-window" && mode != "--capture-region" && mode != "--capture-monitor" &&
            mode != "--capture-window-clipboard"))
            throw std::invalid_argument(
                "Usage: clipss2 [--resident|--settings|--capture-window|--capture-region|--capture-monitor|--capture-window-clipboard|--help]");
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        std::cout << "C++/WinRT initialization: succeeded\n";
        if (mode == "--capture-window") return clipss2::platform::windows::run_capture_window();
        if (mode == "--capture-region") return clipss2::platform::windows::run_capture_region();
        if (mode == "--capture-monitor") return clipss2::platform::windows::run_capture_monitor();
        if (mode == "--capture-window-clipboard") return clipss2::platform::windows::run_capture_window_clipboard();
        if (mode == "--settings") return clipss2::platform::windows::run_settings_mode();
        return clipss2::platform::windows::run_resident_mode(argc == 1);
    }
    catch (...)
    {
        clipss2::platform::windows::write_stderr_utf8(
            "Error: " + clipss2::platform::windows::describe_exception(std::current_exception()) + '\n');
    }
    return 1;
}

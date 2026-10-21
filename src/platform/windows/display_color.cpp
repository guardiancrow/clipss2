#include "platform/windows/display_color.h"
#include "platform/windows/error_diagnostics.h"
#include "platform/windows/display_color_fallback.h"
#include "platform/windows/display_color_note.h"
#include "audit/audit_policy.h"

#include <windows.graphics.display.interop.h>

#include <stdexcept>
#include <utility>

#include <winrt/base.h>
#include <winrt/Windows.Graphics.Display.h>

namespace clipss2::platform::windows
{
namespace
{
using winrt::Windows::Graphics::Display::AdvancedColorKind;
using winrt::Windows::Graphics::Display::DisplayInformation;

const char* factory_stage(ColorCheck check) noexcept
{
    return check == ColorCheck::precheck ? "color.precheck.factory" : "color.postcheck.factory";
}
const char* query_stage(ColorCheck check) noexcept
{
    return check == ColorCheck::precheck ? "color.precheck.query" : "color.postcheck.query";
}
winrt::com_ptr<IDisplayInformationStaticsInterop> acquire_color_factory(ColorCheck check)
{
    try {
    return capture_stage({factory_stage(check), "DisplayInformation activation factory",
        "IDisplayInformationStaticsInterop IID=7449121C-382B-4705-8DA7-A795BA482013"}, [] {
#if CLIPSS2_AUDIT_FEATURES
        wchar_t test[32]{};
        clipss2::audit::environment_variable(L"_CLIPSS2_AUDIT_COLOR_FACTORY_UNAVAILABLE", test, 32);
        if (std::wstring_view{test} == L"E_NOINTERFACE") throw winrt::hresult_error(E_NOINTERFACE);
        if (std::wstring_view{test} == L"REGDB_E_CLASSNOTREG") throw winrt::hresult_error(REGDB_E_CLASSNOTREG);
#endif
        winrt::hresult_error error;
        auto factory = winrt::try_get_activation_factory<DisplayInformation, IDisplayInformationStaticsInterop>(error);
        if (!factory) throw error; // Preserve activation HRESULT; never assume SDR.
        return factory;
    });
    } catch (const CaptureStageError& error) {
        if (error.result() == E_NOINTERFACE || error.result() == REGDB_E_CLASSNOTREG) return {};
        throw; // Access denied/unexpected failures never select a different provider.
    }
}

std::string advanced_color_kind_name(AdvancedColorKind kind)
{
    switch (kind)
    {
    case AdvancedColorKind::StandardDynamicRange:
        return "StandardDynamicRange";
    case AdvancedColorKind::WideColorGamut:
        return "WideColorGamut";
    case AdvancedColorKind::HighDynamicRange:
        return "HighDynamicRange";
    default:
        return "Unknown(" + std::to_string(static_cast<int>(kind)) + ')';
    }
}

DisplayColorInfo make_display_color_info(
    const DisplayInformation& display_information,
    std::string acquisition_method,
    std::string diagnostic_note)
{
    const auto advanced_color = display_information.GetAdvancedColorInfo();
    return DisplayColorInfo{
        std::move(acquisition_method),
        std::move(diagnostic_note),
        advanced_color_kind_name(advanced_color.CurrentAdvancedColorKind()),
        advanced_color.IsAdvancedColorKindAvailable(
            AdvancedColorKind::StandardDynamicRange),
        advanced_color.IsAdvancedColorKindAvailable(
            AdvancedColorKind::WideColorGamut),
        advanced_color.IsAdvancedColorKindAvailable(
            AdvancedColorKind::HighDynamicRange),
        advanced_color.SdrWhiteLevelInNits(),
        advanced_color.MinLuminanceInNits(),
        advanced_color.MaxLuminanceInNits(),
        advanced_color.MaxAverageFullFrameLuminanceInNits()};
}
}

DisplayColorInfo display_color_info_for_window(const WindowCaptureTarget& target, ColorCheck check)
{
    return capture_stage({query_stage(check), "GetForWindow / GetForMonitor fallback and GetAdvancedColorInfo",
        "IDisplayInformationStaticsInterop / IDisplayInformation5 / IAdvancedColorInfo"}, [&] {
    const auto window = reinterpret_cast<HWND>(target.hwnd_value);
    if (window == nullptr || !IsWindow(window))
    {
        throw std::runtime_error("Display diagnostics target HWND is null or invalid.");
    }

    const auto interop = acquire_color_factory(check);
    const auto monitor = reinterpret_cast<std::uintptr_t>(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST));
    if (!interop) return capture_stage({check == ColorCheck::precheck ? "color.precheck.fallback" : "color.postcheck.fallback",
        "provider=DisplayConfig+DXGI; capability=interop-unavailable; window monitor mapping"}, [&] {
        require_single_monitor_window(target.hwnd_value, monitor);
        return fallback_display_color(monitor, check);
    });
    DisplayInformation display_information{nullptr};
    const HRESULT window_result = interop->GetForWindow(
        window,
        winrt::guid_of<DisplayInformation>(),
        winrt::put_abi(display_information));

    std::string acquisition_method = "IDisplayInformationStaticsInterop::GetForWindow";
    std::string diagnostic_note;
    if (FAILED(window_result))
    {
        const HMONITOR fallback_monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        if (fallback_monitor == nullptr)
        {
            throw winrt::hresult_error(
                window_result,
                L"DisplayInformation GetForWindow failed and no monitor was found.");
        }

        display_information = nullptr;
        winrt::check_hresult(interop->GetForMonitor(
            fallback_monitor,
            winrt::guid_of<DisplayInformation>(),
            winrt::put_abi(display_information)));
        acquisition_method = "IDisplayInformationStaticsInterop::GetForMonitor fallback";

        diagnostic_note = detail::window_color_fallback_note(window_result);
    }

    auto result = make_display_color_info(
        display_information,
        std::move(acquisition_method),
        std::move(diagnostic_note));
    result.stability_token = monitor_color_identity(monitor);
    return result;
    });
}

DisplayColorInfo display_color_info_for_monitor(std::uintptr_t monitor_value, ColorCheck check)
{
    return capture_stage({query_stage(check), "GetForMonitor and GetAdvancedColorInfo",
        "IDisplayInformationStaticsInterop / IDisplayInformation5 / IAdvancedColorInfo"}, [&] {
    const auto monitor = reinterpret_cast<HMONITOR>(monitor_value);
    if (monitor == nullptr)
        throw std::invalid_argument("Display diagnostics HMONITOR is null.");

    const auto interop = acquire_color_factory(check);
    if (!interop) return fallback_display_color(monitor_value, check);
    DisplayInformation display_information{nullptr};
    winrt::check_hresult(interop->GetForMonitor(
        monitor,
        winrt::guid_of<DisplayInformation>(),
        winrt::put_abi(display_information)));
    auto result = make_display_color_info(
        display_information,
        "IDisplayInformationStaticsInterop::GetForMonitor",
        {});
    result.stability_token = monitor_color_identity(monitor_value);
    return result;
    });
}

void require_unchanged_capture_color(const DisplayColorInfo& before, const DisplayColorInfo& after)
{
    if (before.acquisition_method != after.acquisition_method || before.stability_token.empty() ||
        before.stability_token != after.stability_token || before.advanced_color_kind != after.advanced_color_kind ||
        before.sdr_available != after.sdr_available || before.wcg_available != after.wcg_available ||
        before.hdr_available != after.hdr_available || before.sdr_white_level_in_nits != after.sdr_white_level_in_nits ||
        before.min_luminance_in_nits != after.min_luminance_in_nits || before.max_luminance_in_nits != after.max_luminance_in_nits ||
        before.max_average_full_frame_luminance_in_nits != after.max_average_full_frame_luminance_in_nits)
        throw DiagnosticError("Color provider/monitor/state changed between precheck and postcheck; capture/output not permitted");
}
}

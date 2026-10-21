#include "platform/windows/display_monitor.h"

#include <windows.h>
#include <shellscalingapi.h>
#include <dxgi1_6.h>

#include <algorithm>
#include <exception>
#include <stdexcept>

#include <winrt/base.h>

namespace clipss2::platform::windows
{
namespace
{
struct EnumerationState
{
    std::vector<DisplayMonitorInfo> monitors;
    std::exception_ptr error;
};

BOOL CALLBACK enumerate_callback(HMONITOR monitor, HDC, RECT*, LPARAM parameter) noexcept
{
    auto& state = *reinterpret_cast<EnumerationState*>(parameter);
    try
    {
        MONITORINFOEXW native{};
        native.cbSize = sizeof(native);
        if (!GetMonitorInfoW(monitor, &native))
            winrt::throw_last_error();

        UINT dpi_x{};
        UINT dpi_y{};
        winrt::check_hresult(GetDpiForMonitor(
            monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y));
        state.monitors.push_back(DisplayMonitorInfo{
            state.monitors.size(),
            reinterpret_cast<std::uintptr_t>(monitor),
            native.szDevice,
            (native.dwFlags & MONITORINFOF_PRIMARY) != 0,
            {native.rcMonitor.left, native.rcMonitor.top,
             native.rcMonitor.right, native.rcMonitor.bottom},
            {native.rcWork.left, native.rcWork.top,
             native.rcWork.right, native.rcWork.bottom},
            dpi_x,
            dpi_y});
        return TRUE;
    }
    catch (...)
    {
        state.error = std::current_exception();
        return FALSE;
    }
}

void add_dxgi_outputs(std::vector<DisplayMonitorInfo>& monitors)
{
    winrt::com_ptr<IDXGIFactory1> factory;
    winrt::check_hresult(CreateDXGIFactory1(IID_PPV_ARGS(factory.put())));
    for (UINT adapter_index = 0;; ++adapter_index)
    {
        winrt::com_ptr<IDXGIAdapter1> adapter;
        const HRESULT adapter_result = factory->EnumAdapters1(adapter_index, adapter.put());
        if (adapter_result == DXGI_ERROR_NOT_FOUND) break;
        winrt::check_hresult(adapter_result);
        for (UINT output_index = 0;; ++output_index)
        {
            winrt::com_ptr<IDXGIOutput> output;
            const HRESULT output_result = adapter->EnumOutputs(output_index, output.put());
            if (output_result == DXGI_ERROR_NOT_FOUND) break;
            winrt::check_hresult(output_result);
            DXGI_OUTPUT_DESC desc{};
            winrt::check_hresult(output->GetDesc(&desc));
            auto found = std::find_if(monitors.begin(), monitors.end(), [&](const auto& item) {
                return item.monitor_value == reinterpret_cast<std::uintptr_t>(desc.Monitor);
            });
            if (found == monitors.end()) continue;
            try
            {
                const auto output6 = output.as<IDXGIOutput6>();
                DXGI_OUTPUT_DESC1 desc1{};
                winrt::check_hresult(output6->GetDesc1(&desc1));
                found->dxgi_output = DxgiOutputInfo{
                    desc1.DeviceName,
                    static_cast<unsigned int>(desc1.ColorSpace),
                    desc1.BitsPerColor,
                    desc1.MinLuminance,
                    desc1.MaxLuminance,
                    desc1.MaxFullFrameLuminance};
            }
            catch (const winrt::hresult_error& error)
            {
                found->dxgi_note = winrt::to_string(error.message());
            }
        }
    }
    for (auto& monitor : monitors)
    {
        if (!monitor.dxgi_output && monitor.dxgi_note.empty())
            monitor.dxgi_note = "No matching attached IDXGIOutput6 was found.";
    }
}
}

std::vector<DisplayMonitorInfo> enumerate_display_monitors()
{
    EnumerationState state;
    if (!EnumDisplayMonitors(
            nullptr, nullptr, enumerate_callback,
            reinterpret_cast<LPARAM>(&state)))
    {
        if (state.error) std::rethrow_exception(state.error);
        winrt::throw_last_error();
    }
    if (state.monitors.empty())
        throw std::runtime_error("EnumDisplayMonitors returned no monitors.");
    try
    {
        add_dxgi_outputs(state.monitors);
    }
    catch (const winrt::hresult_error& error)
    {
        const auto note = "DXGI enumeration unavailable: " + winrt::to_string(error.message());
        for (auto& monitor : state.monitors) monitor.dxgi_note = note;
    }
    return state.monitors;
}
}

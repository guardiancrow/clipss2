#include "platform/windows/display_color_audit.h"

#include "platform/windows/display_color.h"
#include "platform/windows/display_monitor.h"

#include <windows.h>
#include <dxgicommon.h>
#include <iomanip>
#include <iostream>

#include <winrt/base.h>

namespace clipss2::platform::windows
{
namespace
{
void print_rectangle(const DisplayRectangle& rectangle)
{
    std::cout << '(' << rectangle.left << ", " << rectangle.top << ") - ("
              << rectangle.right << ", " << rectangle.bottom << ')';
}

const char* color_space_name(unsigned int value)
{
    switch (static_cast<DXGI_COLOR_SPACE_TYPE>(value))
    {
    case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:
        return "RGB_FULL_G22_NONE_P709";
    case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:
        return "RGB_FULL_G10_NONE_P709";
    case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
        return "RGB_FULL_G2084_NONE_P2020";
    case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020:
        return "RGB_FULL_G22_NONE_P2020";
    default:
        return "other/unknown";
    }
}
}

int run_display_color_audit()
{
    const auto monitors = enumerate_display_monitors();
    std::cout << "Display color audit: " << monitors.size() << " monitor(s)\n";
    for (const auto& monitor : monitors)
    {
        std::cout << "\nMonitor " << monitor.index << ":\nHMONITOR: 0x" << std::hex
                  << monitor.monitor_value << std::dec << "\nDevice: ";
        std::wcout << monitor.device_name << L'\n';
        std::cout << "Primary: " << std::boolalpha << monitor.primary << "\nMonitor rect: ";
        print_rectangle(monitor.monitor_rectangle);
        std::cout << "\nWork rect: ";
        print_rectangle(monitor.work_rectangle);
        std::cout << "\nDPI: " << monitor.dpi_x << " x " << monitor.dpi_y << '\n';
        try
        {
            const auto color = display_color_info_for_monitor(monitor.monitor_value);
            std::cout << "Advanced Color source: " << color.acquisition_method
                      << "\nCurrent kind: " << color.advanced_color_kind
                      << "\nSDR available: " << color.sdr_available
                      << "\nWCG available: " << color.wcg_available
                      << "\nHDR available: " << color.hdr_available
                      << "\nSDR white: " << color.sdr_white_level_in_nits << " nits"
                      << "\nLuminance: min=" << color.min_luminance_in_nits
                      << ", max=" << color.max_luminance_in_nits
                      << ", max average full frame="
                      << color.max_average_full_frame_luminance_in_nits << " nits\n";
        }
        catch (const winrt::hresult_error& error)
        {
            std::wcout << L"Advanced Color information unavailable: "
                       << error.message().c_str() << L" (HRESULT 0x" << std::hex
                       << static_cast<unsigned long>(error.code()) << std::dec << L")\n";
        }
        if (monitor.dxgi_output)
        {
            const auto& dxgi = *monitor.dxgi_output;
            std::cout << "DXGI output: ";
            std::wcout << dxgi.device_name << L'\n';
            std::cout << "DXGI ColorSpace: " << color_space_name(dxgi.color_space)
                      << " (" << dxgi.color_space << ')'
                      << "\nDXGI BitsPerColor: " << dxgi.bits_per_color
                      << "\nDXGI luminance: min=" << dxgi.min_luminance
                      << ", max=" << dxgi.max_luminance
                      << ", max full frame=" << dxgi.max_full_frame_luminance << '\n';
        }
        else
        {
            std::cout << "DXGI cross-check unavailable: " << monitor.dxgi_note << '\n';
        }
    }
    std::cout << "\nNote: Advanced Color is the primary API; DXGI values are a secondary"
                 " snapshot and are not required to be identical.\n";
    return 0;
}
}

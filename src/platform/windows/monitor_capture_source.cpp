#include "monitor_capture_source.h"
#include <windows.h>
#include <winrt/base.h>
#include <exception>
#include <vector>
#include <algorithm>
namespace clipss2::platform::windows
{
std::string display_topology_snapshot()
{
    struct State { std::vector<std::string> records; std::exception_ptr error; } state;
    const auto callback=[](HMONITOR monitor,HDC,RECT*,LPARAM parameter) noexcept -> BOOL
    {
        auto& s=*reinterpret_cast<State*>(parameter);
        try
        {
            MONITORINFOEXW info{};info.cbSize=sizeof(info);
            if (!GetMonitorInfoW(monitor,&info)) winrt::throw_last_error();
            DEVMODEW mode{};mode.dmSize=sizeof(mode);
            if (!EnumDisplaySettingsExW(info.szDevice,ENUM_CURRENT_SETTINGS,&mode,0))
                throw std::runtime_error("Cannot snapshot current display mode; capture canceled.");
            std::string name;
            for (const wchar_t c:std::wstring_view{info.szDevice})
            {if(c>127)throw std::runtime_error("Unsupported display identity in topology snapshot.");name.push_back(static_cast<char>(c));}
            s.records.push_back(name+":"+std::to_string(info.rcMonitor.left)+","+std::to_string(info.rcMonitor.top)+","+
                std::to_string(info.rcMonitor.right)+","+std::to_string(info.rcMonitor.bottom)+":"+
                std::to_string(mode.dmDisplayOrientation)+","+std::to_string(mode.dmPelsWidth)+","+
                std::to_string(mode.dmPelsHeight)+","+std::to_string(mode.dmDisplayFrequency));
            return TRUE;
        }
        catch (...) {s.error=std::current_exception();return FALSE;}
    };
    if(!EnumDisplayMonitors(nullptr,nullptr,callback,reinterpret_cast<LPARAM>(&state)))
    {if(state.error)std::rethrow_exception(state.error);winrt::throw_last_error();}
    if(state.records.empty())throw std::runtime_error("No display topology available.");
    std::sort(state.records.begin(),state.records.end());
    std::string result;
    for(const auto& record:state.records){if(!result.empty())result+=';';result+=record;}
    return result;
}

std::uintptr_t resolve_monitor_source(const MonitorSource& source)
{
    validate_monitor_source(source);
    struct State { const MonitorSource& source; HMONITOR monitor{}; unsigned matches{}; std::exception_ptr error; } state{source};
    const auto callback = [](HMONITOR monitor, HDC, RECT*, LPARAM parameter) noexcept -> BOOL
    {
        auto& s = *reinterpret_cast<State*>(parameter);
        try
        {
            MONITORINFOEXW info{}; info.cbSize = sizeof(info);
            if (!GetMonitorInfoW(monitor, &info)) winrt::throw_last_error();
            if (s.source.device_name == info.szDevice)
            {
                ++s.matches;
                const PixelRectangle bounds{info.rcMonitor.left,info.rcMonitor.top,info.rcMonitor.right,info.rcMonitor.bottom};
                if (bounds != s.source.expected_bounds) throw std::runtime_error("Monitor bounds changed; stale capture source.");
                s.monitor = monitor;
            }
            return TRUE;
        }
        catch (...) { s.error = std::current_exception(); return FALSE; }
    };
    if (!EnumDisplayMonitors(nullptr,nullptr,callback,reinterpret_cast<LPARAM>(&state)))
    { if (state.error) std::rethrow_exception(state.error); winrt::throw_last_error(); }
    if (state.matches != 1 || !state.monitor) throw std::runtime_error("Missing or ambiguous monitor capture source.");
    if(!source.expected_topology.empty() && display_topology_snapshot()!=source.expected_topology)
        throw std::runtime_error("Display topology changed; stale capture source.");
    return reinterpret_cast<std::uintptr_t>(state.monitor);
}
}

#include "platform/windows/display_color_fallback.h"
#include "platform/windows/display_color_policy.h"
#include "platform/windows/error_diagnostics.h"
#include "audit/audit_policy.h"

#include <windows.h>
#include <dxgi1_6.h>
#include <winrt/base.h>
#include <algorithm>
#include <sstream>
#include <vector>

namespace clipss2::platform::windows
{
namespace
{
[[noreturn]] void unknown(const char* reason)
{
    throw DiagnosticError(std::string{"provider=DisplayConfig+DXGI; capability=interop-unavailable; state=Unknown; "} +
        reason + "; capture/output not permitted");
}
void require_ccd(LONG code, const char* operation)
{
    if (code != ERROR_SUCCESS)
        throw_hresult_failure(operation, HRESULT_FROM_WIN32(code));
}
bool same_luid(LUID a, LUID b) noexcept { return a.LowPart == b.LowPart && a.HighPart == b.HighPart; }
std::string rect_token(const RECT& r)
{
    return std::to_string(r.left) + ',' + std::to_string(r.top) + ',' +
        std::to_string(r.right) + ',' + std::to_string(r.bottom);
}
MONITORINFOEXW monitor_info(std::uintptr_t value)
{
    MONITORINFOEXW info{}; info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(reinterpret_cast<HMONITOR>(value), &info))
        require_win32_success(false, GetLastError(), "provider=DisplayConfig+DXGI; GetMonitorInfoW color mapping");
    return info;
}
struct Topology
{
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::string token;
};
Topology active_topology()
{
    // No DATABASE_CURRENT/default inactive paths and no Win11-only query flags.
    for (unsigned attempt = 0; attempt != 3; ++attempt)
    {
        UINT32 path_count{}, mode_count{};
        require_ccd(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count),
            "provider=DisplayConfig; GetDisplayConfigBufferSizes");
        if (!path_count || path_count > 256 || mode_count > 1024) unknown("mapping=invalid topology size");
        Topology topology{std::vector<DISPLAYCONFIG_PATH_INFO>(path_count), {}};
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
        const auto result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, topology.paths.data(),
            &mode_count, modes.data(), nullptr);
        if (result == ERROR_INSUFFICIENT_BUFFER) continue;
        require_ccd(result, "provider=DisplayConfig; QueryDisplayConfig active paths");
        topology.paths.resize(path_count); modes.resize(mode_count);
        std::vector<std::string> records;
        for (const auto& p : topology.paths)
        {
            if (!(p.flags & DISPLAYCONFIG_PATH_ACTIVE) || !p.targetInfo.targetAvailable)
                unknown("mapping=inactive target");
            std::ostringstream row;
            const auto luid = [&](LUID id) { row << id.HighPart << ':' << id.LowPart << ':'; };
            luid(p.sourceInfo.adapterId); row << p.sourceInfo.id << ':' << p.sourceInfo.statusFlags << ':';
            luid(p.targetInfo.adapterId);
            row << p.targetInfo.id << ':' << p.flags << ':' << p.targetInfo.statusFlags << ':'
                << p.targetInfo.outputTechnology << ':' << p.targetInfo.rotation << ':' << p.targetInfo.scaling << ':'
                << p.targetInfo.refreshRate.Numerator << ':' << p.targetInfo.refreshRate.Denominator << ':'
                << p.targetInfo.scanLineOrdering << ':';
            for (const auto index : {p.sourceInfo.modeInfoIdx, p.targetInfo.modeInfoIdx})
            {
                if (index == DISPLAYCONFIG_PATH_MODE_IDX_INVALID || index >= modes.size())
                    unknown("mapping=missing active mode");
                const auto& m = modes[index]; luid(m.adapterId); row << m.id << ':' << m.infoType << ':';
                if (m.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)
                    row << m.sourceMode.width << ':' << m.sourceMode.height << ':' << m.sourceMode.pixelFormat
                        << ':' << m.sourceMode.position.x << ':' << m.sourceMode.position.y << ':';
                else if (m.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET)
                {
                    const auto& v = m.targetMode.targetVideoSignalInfo;
                    row << v.pixelRate << ':' << v.hSyncFreq.Numerator << ':' << v.hSyncFreq.Denominator << ':'
                        << v.vSyncFreq.Numerator << ':' << v.vSyncFreq.Denominator << ':'
                        << v.activeSize.cx << ':' << v.activeSize.cy << ':' << v.totalSize.cx << ':' << v.totalSize.cy
                        << ':' << v.videoStandard << ':' << v.scanLineOrdering << ':';
                }
                else unknown("mapping=unexpected mode type");
            }
            records.push_back(row.str());
        }
        std::sort(records.begin(), records.end());
        for (const auto& row : records) topology.token += row + ';';
        return topology;
    }
    unknown("mapping=unstable topology");
}
template<class T> T device_info(DISPLAYCONFIG_DEVICE_INFO_TYPE type, LUID adapter, UINT32 id)
{
    T result{};
    result.header = {type, sizeof(T), adapter, id};
    const auto code = DisplayConfigGetDeviceInfo(&result.header);
    if (code != ERROR_SUCCESS)
        throw_hresult_failure("provider=DisplayConfig; DisplayConfigGetDeviceInfo type=" + std::to_string(type),
            HRESULT_FROM_WIN32(code));
    return result;
}
struct TargetState
{
    DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color;
    DISPLAYCONFIG_TARGET_DEVICE_NAME name;
    std::string token;
};
TargetState target_state(const DISPLAYCONFIG_PATH_INFO& path)
{
    const auto& target = path.targetInfo;
    auto color = device_info<DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO>(
        DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO, target.adapterId, target.id);
    auto name = device_info<DISPLAYCONFIG_TARGET_DEVICE_NAME>(
        DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, target.adapterId, target.id);
    // EDID identifiers do not validate all EDID colorimetry. The separate CCD
    // active-state bits remain essential; DXGI's SDR default is never sufficient.
    std::ostringstream token;
    token << color.value << ':' << color.colorEncoding << ':' << color.bitsPerColorChannel << ':'
        << name.flags.value << ':' << name.edidManufactureId << ':' << name.edidProductCodeId << ':'
        << name.connectorInstance << ':' << name.outputTechnology << ':';
    token << winrt::to_string(std::wstring_view{name.monitorDevicePath});
    return {color, name, token.str()};
}
}

std::string monitor_color_identity(std::uintptr_t monitor)
{
    const auto info = monitor_info(monitor);
    return std::to_string(monitor) + ':' + winrt::to_string(std::wstring_view{info.szDevice}) + ':' + rect_token(info.rcMonitor);
}

void require_single_monitor_window(std::uintptr_t window, std::uintptr_t monitor)
{
    RECT bounds{};
    if (!GetWindowRect(reinterpret_cast<HWND>(window), &bounds)) winrt::throw_last_error();
    struct State { HMONITOR expected; unsigned count{}; bool other{}; } state{reinterpret_cast<HMONITOR>(monitor)};
    const auto callback = [](HMONITOR value, HDC, RECT*, LPARAM data) -> BOOL {
        auto& s = *reinterpret_cast<State*>(data); ++s.count; s.other |= value != s.expected; return TRUE;
    };
    if (!EnumDisplayMonitors(nullptr, &bounds, callback, reinterpret_cast<LPARAM>(&state))) winrt::throw_last_error();
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top || state.count != 1 || state.other)
        unknown("mapping=window spans monitors or has no unique visible monitor");
}

DisplayColorInfo fallback_display_color(std::uintptr_t monitor, ColorCheck check)
{
    const auto stage = check == ColorCheck::precheck ? "color.precheck.fallback" : "color.postcheck.fallback";
    return capture_stage({stage, "provider=DisplayConfig+DXGI; capability=interop-unavailable; active color and monitor mapping",
        "IDXGIOutput6 / DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO"}, [&] {
        const auto info = monitor_info(monitor);
        const auto topology = active_topology();
        std::vector<DISPLAYCONFIG_PATH_INFO> matching;
        for (const auto& path : topology.paths)
        {
            const auto name = device_info<DISPLAYCONFIG_SOURCE_DEVICE_NAME>(DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,
                path.sourceInfo.adapterId, path.sourceInfo.id);
            if (std::wstring_view{name.viewGdiDeviceName} == info.szDevice) matching.push_back(path);
        }
        if (matching.size() != 1) unknown("mapping=missing or clone DisplayConfig source");
        const auto& path = matching.front();
        if (std::count_if(topology.paths.begin(), topology.paths.end(), [&](const auto& other) {
            return same_luid(other.targetInfo.adapterId, path.targetInfo.adapterId) && other.targetInfo.id == path.targetInfo.id;
        }) != 1) unknown("mapping=ambiguous DisplayConfig target");
        const auto state = target_state(path);
        winrt::com_ptr<IDXGIFactory1> factory;
        winrt::check_hresult(CreateDXGIFactory1(IID_PPV_ARGS(factory.put())));
        DXGI_OUTPUT_DESC1 description{};
        unsigned output_matches{};
        bool identities_match{};
        for (UINT a = 0;; ++a)
        {
            winrt::com_ptr<IDXGIAdapter1> adapter;
            const auto ar = factory->EnumAdapters1(a, adapter.put());
            if (ar == DXGI_ERROR_NOT_FOUND) break;
            winrt::check_hresult(ar);
            DXGI_ADAPTER_DESC1 adapter_desc{};
            winrt::check_hresult(adapter->GetDesc1(&adapter_desc));
            for (UINT o = 0;; ++o)
            {
                winrt::com_ptr<IDXGIOutput> output;
                const auto hr = adapter->EnumOutputs(o, output.put());
                if (hr == DXGI_ERROR_NOT_FOUND) break;
                winrt::check_hresult(hr);
                DXGI_OUTPUT_DESC desc{}; winrt::check_hresult(output->GetDesc(&desc));
                if (desc.Monitor != reinterpret_cast<HMONITOR>(monitor)) continue;
                ++output_matches;
                winrt::check_hresult(output.as<IDXGIOutput6>()->GetDesc1(&description));
                identities_match = same_luid(adapter_desc.AdapterLuid, path.sourceInfo.adapterId) &&
                    same_luid(adapter_desc.AdapterLuid, path.targetInfo.adapterId) && description.AttachedToDesktop &&
                    description.Monitor == desc.Monitor && std::wstring_view{description.DeviceName} == info.szDevice &&
                    EqualRect(&description.DesktopCoordinates, &info.rcMonitor);
            }
        }
        FallbackColorEvidence evidence{static_cast<unsigned>(matching.size()), output_matches, identities_match,
            state.name.flags.value == 5u && state.name.monitorDevicePath[0] != 0,
            factory->IsCurrent() && topology.token == active_topology().token &&
                state.token == target_state(path).token && monitor_color_identity(monitor) ==
                    (std::to_string(monitor) + ':' + winrt::to_string(std::wstring_view{info.szDevice}) + ':' + rect_token(info.rcMonitor)),
            state.color.value, static_cast<unsigned>(state.color.colorEncoding), state.color.bitsPerColorChannel,
            description.BitsPerColor, static_cast<unsigned>(description.ColorSpace)};
#if CLIPSS2_AUDIT_FEATURES
        wchar_t test[48]{};
        clipss2::audit::environment_variable(L"_CLIPSS2_AUDIT_COLOR_CASE", test, 48);
        const std::wstring_view scenario{test};
        // Synthetic API evidence for owned runtime tests only. Real developer
        // desktops can be forced/virtual targets and must remain fail-closed.
        const bool synthetic = scenario == L"sdr" || scenario == L"hdr" || scenario == L"wcg" ||
            scenario == L"unknown" || scenario == L"missing-edid" || scenario == L"clone" ||
            scenario == L"mapping" || scenario == L"dxgi-failure" || scenario == L"displayconfig-failure" ||
            scenario == L"changed-post";
        if (!scenario.empty() && scenario != L"real" && !synthetic) unknown("invalid owned color fixture scenario");
        if (synthetic) evidence = {1, 1, true, true, true, 0, 0, 8, 8, 0};
        if (scenario == L"hdr") { evidence.advanced_flags = 3; evidence.dxgi_color_space = 12; }
        else if (scenario == L"wcg") evidence.advanced_flags = 7;
        else if (scenario == L"unknown") evidence.advanced_flags = 3;
        else if (scenario == L"missing-edid") evidence.edid_ids_valid = false;
        else if (scenario == L"clone") evidence.matching_paths = 2;
        else if (scenario == L"mapping") evidence.identities_match = false;
        else if (scenario == L"dxgi-failure") throw_hresult_failure("provider=DXGI; GetDesc1 fixture", DXGI_ERROR_DEVICE_REMOVED);
        else if (scenario == L"displayconfig-failure") require_ccd(ERROR_ACCESS_DENIED, "provider=DisplayConfig; device info fixture");
#endif
        DisplayColorInfo result;
        result.acquisition_method = "DisplayConfig+DXGI";
        result.advanced_color_kind = fallback_color_kind(evidence);
        if (result.advanced_color_kind != "StandardDynamicRange")
            throw DiagnosticError("provider=DisplayConfig+DXGI; capability=interop-unavailable; mapping=" +
                std::string{evidence.identities_match && evidence.matching_paths == 1 && evidence.matching_outputs == 1 ? "unique" : "failed"} +
                "; state=" + result.advanced_color_kind + "; advancedFlags=" + std::to_string(evidence.advanced_flags) +
                "; edidFlags=" + std::to_string(state.name.flags.value) + "; edid=" + std::to_string(evidence.edid_ids_valid) +
                "; stable=" + std::to_string(evidence.stable) + "; encoding=" + std::to_string(evidence.color_encoding) +
                "; CCDbits=" + std::to_string(evidence.display_bits) + "; DXGIbits=" + std::to_string(evidence.dxgi_bits) +
                "; DXGIcolor=" + std::to_string(evidence.dxgi_color_space) +
                "; active color/EDID/topology evidence not safe for SDR; capture/output not permitted");
        result.sdr_available = true;
        result.min_luminance_in_nits = description.MinLuminance;
        result.max_luminance_in_nits = description.MaxLuminance;
        result.max_average_full_frame_luminance_in_nits = description.MaxFullFrameLuminance;
        result.stability_token = monitor_color_identity(monitor) + ':' + topology.token + ':' + state.token + ':' +
            std::to_string(description.ColorSpace) + ':' + std::to_string(description.BitsPerColor);
#if CLIPSS2_AUDIT_FEATURES
        if (scenario == L"changed-post" && check == ColorCheck::postcheck) result.stability_token += ":fixture-change";
#endif
        return result;
    });
}
}

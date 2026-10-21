#include "platform/windows/resource_counters.h"

#if CLIPSS2_AUDIT_FEATURES
#include <windows.h>
#include <processsnapshot.h>
#include <psapi.h>

#include <iostream>
#include <stdexcept>

#include "platform/windows/error_diagnostics.h"

namespace clipss2::platform::windows
{
namespace
{
thread_local PssAuditCounters counters;
std::wstring pss_string(const wchar_t* text, const std::uint16_t byte_length)
{
    if (!text || byte_length == 0) return {};
    std::wstring result{text, byte_length / sizeof(wchar_t)};
    while (!result.empty() && result.back() == L'\0') result.pop_back();
    return result;
}

class PssSnapshot final
{
public:
    explicit PssSnapshot(HPSS value) noexcept : value_(value) { ++counters.snapshots; }
    ~PssSnapshot() {
        if (value_) {
            if (PssFreeSnapshot(GetCurrentProcess(), value_) != ERROR_SUCCESS) ++counters.cleanup_failures;
            --counters.snapshots;
        }
    }
    PssSnapshot(const PssSnapshot&) = delete;
    PssSnapshot& operator=(const PssSnapshot&) = delete;
    [[nodiscard]] HPSS get() const noexcept { return value_; }
private:
    HPSS value_{};
};

class PssWalkMarker final
{
public:
    explicit PssWalkMarker(HPSSWALK value) noexcept : value_(value) { ++counters.markers; }
    ~PssWalkMarker() {
        if (value_) {
            if (PssWalkMarkerFree(value_) != ERROR_SUCCESS) ++counters.cleanup_failures;
            --counters.markers;
        }
    }
    PssWalkMarker(const PssWalkMarker&) = delete;
    PssWalkMarker& operator=(const PssWalkMarker&) = delete;
    [[nodiscard]] HPSSWALK get() const noexcept { return value_; }
private:
    HPSSWALK value_{};
};
}

ResourceSnapshot collect_resource_snapshot()
{
    ResourceSnapshot value{};
    const HANDLE process = GetCurrentProcess();
    DWORD handles{};
    const BOOL handles_succeeded = GetProcessHandleCount(process, &handles);
    const DWORD handles_error = handles_succeeded ? ERROR_SUCCESS : GetLastError();
    require_win32_success(
        handles_succeeded != FALSE, handles_error, "GetProcessHandleCount failed");
    value.handles = handles;
    // A process with no GUI allocations legitimately reports zero. Unlike the
    // other APIs sampled here, GetGuiResources does not provide an unambiguous
    // last-error contract that distinguishes that value from failure.
    value.user_objects = GetGuiResources(process, GR_USEROBJECTS);
    value.gdi_objects = GetGuiResources(process, GR_GDIOBJECTS);
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    const BOOL memory_succeeded = GetProcessMemoryInfo(
        process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
    const DWORD memory_error = memory_succeeded ? ERROR_SUCCESS : GetLastError();
    require_win32_success(
        memory_succeeded != FALSE, memory_error, "GetProcessMemoryInfo failed");
    value.working_set_bytes = memory.WorkingSetSize;
    value.private_bytes = memory.PrivateUsage;
    return value;
}

PssAuditCounters pss_counters_for_audit() noexcept { return counters; }
void reset_pss_counters_for_audit() noexcept { counters = {}; }

std::vector<ProcessHandleRecord> collect_process_handles(std::string_view phase)
{
    wchar_t failure[8]{};
    const auto length = audit::environment_variable(L"_CLIPSS2_AUDIT_RESOURCE_FAIL", failure, 8);
    if ((phase == "pre" && length == 3 && std::wstring_view{failure} == L"pre") ||
        (phase == "post" && length == 4 && std::wstring_view{failure} == L"post"))
        throw DiagnosticError(format_win32_error("PssCaptureSnapshot failed", ERROR_ACCESS_DENIED));
    HPSS raw_snapshot{};
    ++counters.calls;
    DWORD result = PssCaptureSnapshot(
        GetCurrentProcess(),
        static_cast<PSS_CAPTURE_FLAGS>(
            PSS_CAPTURE_HANDLES | PSS_CAPTURE_HANDLE_TYPE_SPECIFIC_INFORMATION),
        0, &raw_snapshot);
    if (result != ERROR_SUCCESS)
        throw DiagnosticError(format_win32_error("PssCaptureSnapshot failed", result));
    const PssSnapshot snapshot{raw_snapshot};
    inject_capture_stage_failure_for_audit("resource.pss.after-snapshot");

    HPSSWALK raw_marker{};
    result = PssWalkMarkerCreate(nullptr, &raw_marker);
    if (result != ERROR_SUCCESS)
        throw DiagnosticError(format_win32_error("PssWalkMarkerCreate failed", result));
    const PssWalkMarker marker{raw_marker};
    inject_capture_stage_failure_for_audit("resource.pss.after-marker");

    std::vector<ProcessHandleRecord> handles;
    PSS_HANDLE_ENTRY entry{};
    while ((result = PssWalkSnapshot(
                snapshot.get(), PSS_WALK_HANDLES, marker.get(), &entry,
                sizeof(entry))) == ERROR_SUCCESS)
    {
        handles.push_back(ProcessHandleRecord{
            reinterpret_cast<std::uintptr_t>(entry.Handle),
            pss_string(entry.TypeName, entry.TypeNameLength),
            pss_string(entry.ObjectName, entry.ObjectNameLength),
            entry.GrantedAccess});
    }
    if (result != ERROR_NO_MORE_ITEMS)
        throw DiagnosticError(format_win32_error("PssWalkSnapshot failed", result));
    inject_capture_stage_failure_for_audit("resource.pss.after-walk");
    return handles;
}
std::map<std::wstring, std::size_t> count_handle_types(
    const std::vector<ProcessHandleRecord>& handles)
{
    std::map<std::wstring, std::size_t> counts;
    for (const auto& handle : handles)
        ++counts[handle.type.empty() ? L"<unknown>" : handle.type];
    return counts;
}

void print_resource_snapshot(const char* label, const ResourceSnapshot& value)
{
    std::cout << label << ": handles=" << value.handles
              << ", USER=" << value.user_objects
              << ", GDI=" << value.gdi_objects
              << ", working-set=" << value.working_set_bytes
              << ", private=" << value.private_bytes << '\n';
}

void print_handle_type_deltas(
    const std::map<std::wstring, std::size_t>& baseline,
    const std::map<std::wstring, std::size_t>& final)
{
    std::wcout << L"Handle type deltas:";
    bool any = false;
    std::map<std::wstring, std::size_t> all = baseline;
    all.insert(final.begin(), final.end());
    for (const auto& [type, ignored] : all)
    {
        (void)ignored;
        const auto before_it = baseline.find(type);
        const auto after_it = final.find(type);
        const auto before = before_it == baseline.end() ? 0 : before_it->second;
        const auto after = after_it == final.end() ? 0 : after_it->second;
        if (after == before) continue;
        any = true;
        std::wcout << L' ' << type << L'='
                   << static_cast<std::int64_t>(after) -
                          static_cast<std::int64_t>(before);
    }
    if (!any) std::wcout << L" none";
    std::wcout << L'\n';
}
}
#endif

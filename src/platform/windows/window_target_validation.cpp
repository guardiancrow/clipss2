#include "platform/windows/window_target_validation.h"
#include "platform/windows/frame_geometry.h"
#include "platform/windows/error_diagnostics.h"
#include <windows.h>
#include <algorithm>
#include <array>

namespace clipss2::platform::windows
{
std::string window_identity_failure_reason(const WindowCandidate& candidate)
{
    // Called only after the unchanged identity predicate rejected the target.
    // Re-observation may race with state changes; it must never override rejection.
    const auto window = reinterpret_cast<HWND>(candidate.hwnd_value);
    if (!IsWindow(window)) return "reason=hwnd-invalid";
    DWORD pid{}; SetLastError(ERROR_SUCCESS);
    if (!GetWindowThreadProcessId(window, &pid))
        return "reason=pid-query-failed; " + format_win32_error("GetWindowThreadProcessId", GetLastError());
    if (pid != candidate.process_id) return "reason=pid-mismatch";
    std::array<wchar_t, 256> buffer{}; SetLastError(ERROR_SUCCESS);
    const auto size = GetClassNameW(window, buffer.data(), static_cast<int>(buffer.size()));
    if (!size) return "reason=class-query-failed; " + format_win32_error("GetClassNameW", GetLastError());
    if (std::wstring_view{buffer.data(), static_cast<std::size_t>(size)} != candidate.class_name)
        return "reason=class-mismatch";
    return "reason=identity-rejected-before-diagnostic-observation";
}

std::string window_snapshot_failure_reason(const WindowCandidate& expected,
    std::span<const WindowCandidate> current)
{
    // Same successful condition as the previous live_target any_of.
    if (std::any_of(current.begin(), current.end(), [&](const auto& item) {
        return item.hwnd_value == expected.hwnd_value && item.selectable &&
            item.process_id == expected.process_id && item.class_name == expected.class_name;
    })) return {};
    const auto found = std::find_if(current.begin(), current.end(), [&](const auto& item) {
        return item.hwnd_value == expected.hwnd_value;
    });
    if (found == current.end()) return "reason=snapshot-target-missing";
    if (found->process_id != expected.process_id) return "reason=snapshot-pid-mismatch";
    if (found->class_name != expected.class_name) return "reason=snapshot-class-mismatch";
    std::string reason = "reason=target-not-selectable; rejection=";
    for (const auto rejection : found->rejection_reasons)
    {
        if (!reason.ends_with('=')) reason += ',';
        reason += rejection_reason_name(rejection);
    }
    if (found->rejection_reasons.empty()) reason += "unclassified";
    if (found->cloaked_query_failure)
        reason += "; " + format_hresult("DwmGetWindowAttribute CLOAKED", *found->cloaked_query_failure);
    if (found->bounds_query_failure && found->bounds.empty())
        reason += "; " + format_hresult("DwmGetWindowAttribute FRAME_BOUNDS", *found->bounds_query_failure);
    if (found->bounds_win32_failure)
        reason += "; " + format_win32_error("GetWindowRect", *found->bounds_win32_failure);
    return reason;
}

void require_live_window_capture_target(const WindowCandidate& candidate, std::string_view checkpoint)
{
    capture_stage({"target.validate", "Window target identity / current selectability"}, [&] {
        std::string reason;
        if (candidate.class_name.empty() || candidate.class_name.starts_with(L"<class unavailable:"))
            reason = "reason=expected-class-unavailable";
        else if (!is_window_candidate_current(candidate)) reason = window_identity_failure_reason(candidate);
        else reason = window_snapshot_failure_reason(candidate, enumerate_window_candidates());
        if (!reason.empty()) throw DiagnosticError("checkpoint=" + std::string{checkpoint} + "; " + reason +
            "; capture/output not permitted");
    });
}

void require_window_capture_geometry(std::int32_t cw, std::int32_t ch, std::uint32_t tw,
    std::uint32_t th, std::uint32_t pw, std::uint32_t ph, std::size_t bytes)
{
    capture_stage({"capture.geometry", "Window frame content / texture / CPU size validation"}, [&] {
        (void)window_frame_crop(cw, ch, tw, th);
        if (pw != static_cast<std::uint32_t>(cw) || ph != static_cast<std::uint32_t>(ch))
            throw DiagnosticError("reason=window-frame-size-mismatch; content=" + std::to_string(cw) + 'x' + std::to_string(ch) +
                "; texture=" + std::to_string(tw) + 'x' + std::to_string(th) + "; cpu=" + std::to_string(pw) + 'x' +
                std::to_string(ph) + "; capture/output not permitted");
        require_readback_buffer(bytes, geometry_product(geometry_product(pw, 4), ph));
    });
}
}

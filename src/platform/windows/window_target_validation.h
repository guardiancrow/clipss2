#pragma once
#include "platform/windows/window_candidates.h"
#include <span>
#include <string_view>

namespace clipss2::platform::windows
{
// Diagnostic seams mirror existing predicates; they do not approve new targets.
std::string window_identity_failure_reason(const WindowCandidate& candidate);
std::string window_snapshot_failure_reason(const WindowCandidate& expected,
    std::span<const WindowCandidate> current);
void require_live_window_capture_target(const WindowCandidate& candidate, std::string_view checkpoint);
void require_window_capture_geometry(std::int32_t content_width, std::int32_t content_height,
    std::uint32_t texture_width, std::uint32_t texture_height,
    std::uint32_t cpu_width, std::uint32_t cpu_height, std::size_t buffer_bytes);
}

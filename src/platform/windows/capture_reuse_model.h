#pragma once

#include <cstddef>

namespace clipss2::platform::windows
{
struct CaptureReusePlanCounts
{
    std::size_t item_creations{};
    std::size_t session_creations{};
};

enum class CaptureReuseStrategy
{
    item_reuse_stops_growth,
    same_target_only,
    ineffective,
    unresolved,
};

[[nodiscard]] CaptureReusePlanCounts current_lifecycle_counts(
    std::size_t capture_count) noexcept;
[[nodiscard]] CaptureReusePlanCounts same_item_lifecycle_counts(
    std::size_t capture_count, bool item_closed) noexcept;
[[nodiscard]] CaptureReuseStrategy classify_capture_reuse(
    long long same_item_additional_alpc,
    long long repeated_item_alpc,
    long long target_churn_alpc) noexcept;
}

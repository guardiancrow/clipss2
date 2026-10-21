#include "platform/windows/capture_reuse_model.h"

namespace clipss2::platform::windows
{
CaptureReusePlanCounts current_lifecycle_counts(
    const std::size_t capture_count) noexcept
{
    return {capture_count, capture_count};
}

CaptureReusePlanCounts same_item_lifecycle_counts(
    const std::size_t capture_count, const bool item_closed) noexcept
{
    if (item_closed) return {};
    return {capture_count == 0 ? 0U : 1U, capture_count};
}

CaptureReuseStrategy classify_capture_reuse(
    const long long same_item_additional_alpc,
    const long long repeated_item_alpc,
    const long long target_churn_alpc) noexcept
{
    if (same_item_additional_alpc > 0)
        return CaptureReuseStrategy::ineffective;
    if (same_item_additional_alpc == 0 && repeated_item_alpc > 0 &&
        target_churn_alpc > 0)
        return CaptureReuseStrategy::same_target_only;
    if (same_item_additional_alpc == 0 && repeated_item_alpc == 0 &&
        target_churn_alpc == 0)
        return CaptureReuseStrategy::item_reuse_stops_growth;
    return CaptureReuseStrategy::unresolved;
}
}

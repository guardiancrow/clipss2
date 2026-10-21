#include "platform/windows/resource_lifetime_model.h"

#include <algorithm>
#include <stdexcept>

namespace clipss2::platform::windows
{
namespace
{
template<class Value>
bool monotonically_growing(
    const Value ResourceSnapshot::* member,
    const ResourceSnapshot& baseline,
    std::span<const ResourceSnapshot> samples)
{
    Value previous = baseline.*member;
    bool increased = false;
    for (const auto& sample : samples)
    {
        const Value current = sample.*member;
        if (current < previous) return false;
        increased = increased || current > previous;
        previous = current;
    }
    return increased;
}

std::int64_t delta(std::uint32_t final, std::uint32_t baseline)
{
    return static_cast<std::int64_t>(final) - baseline;
}

std::int64_t byte_delta(std::uint64_t final, std::uint64_t baseline)
{
    return final >= baseline
        ? static_cast<std::int64_t>(final - baseline)
        : -static_cast<std::int64_t>(baseline - final);
}
}

ResourceAuditEvaluation evaluate_resource_samples(
    const ResourceSnapshot& baseline,
    const std::span<const ResourceSnapshot> samples)
{
    if (samples.empty())
        throw std::invalid_argument("Resource audit requires at least one sample.");
    const auto& final = samples.back();
    ResourceAuditEvaluation result{};
    result.handle_delta = delta(final.handles, baseline.handles);
    result.user_delta = delta(final.user_objects, baseline.user_objects);
    result.gdi_delta = delta(final.gdi_objects, baseline.gdi_objects);
    result.maximum_working_set_bytes = baseline.working_set_bytes;
    result.maximum_private_bytes = baseline.private_bytes;
    for (const auto& sample : samples)
    {
        result.maximum_working_set_bytes =
            std::max(result.maximum_working_set_bytes, sample.working_set_bytes);
        result.maximum_private_bytes =
            std::max(result.maximum_private_bytes, sample.private_bytes);
    }
    result.working_set_delta =
        byte_delta(final.working_set_bytes, baseline.working_set_bytes);
    result.private_delta = byte_delta(final.private_bytes, baseline.private_bytes);
    result.handles_monotonically_growing =
        monotonically_growing(&ResourceSnapshot::handles, baseline, samples);
    result.user_monotonically_growing =
        monotonically_growing(&ResourceSnapshot::user_objects, baseline, samples);
    result.gdi_monotonically_growing =
        monotonically_growing(&ResourceSnapshot::gdi_objects, baseline, samples);
    result.working_set_monotonically_growing = monotonically_growing(
        &ResourceSnapshot::working_set_bytes, baseline, samples);
    result.private_monotonically_growing = monotonically_growing(
        &ResourceSnapshot::private_bytes, baseline, samples);

    constexpr std::int64_t handle_tolerance = 2;
    constexpr std::int64_t gui_tolerance = 1;
    result.resource_counts_passed =
        result.handle_delta <= handle_tolerance && result.user_delta <= gui_tolerance &&
        result.gdi_delta <= gui_tolerance && !result.handles_monotonically_growing &&
        !result.user_monotonically_growing && !result.gdi_monotonically_growing;

    constexpr std::uint64_t informational_memory_threshold = 16ULL * 1024 * 1024;
    result.memory_growth_observed =
        final.private_bytes > baseline.private_bytes + informational_memory_threshold ||
        final.working_set_bytes >
            baseline.working_set_bytes + informational_memory_threshold;
    return result;
}
}

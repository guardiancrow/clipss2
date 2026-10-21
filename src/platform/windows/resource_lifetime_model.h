#pragma once

#include <cstdint>
#include <span>

namespace clipss2::platform::windows
{
struct ResourceSnapshot
{
    std::uint32_t handles{};
    std::uint32_t user_objects{};
    std::uint32_t gdi_objects{};
    std::uint64_t working_set_bytes{};
    std::uint64_t private_bytes{};
};

struct ResourceAuditEvaluation
{
    std::int64_t handle_delta{};
    std::int64_t user_delta{};
    std::int64_t gdi_delta{};
    std::uint64_t maximum_working_set_bytes{};
    std::uint64_t maximum_private_bytes{};
    std::int64_t working_set_delta{};
    std::int64_t private_delta{};
    bool handles_monotonically_growing{};
    bool user_monotonically_growing{};
    bool gdi_monotonically_growing{};
    bool working_set_monotonically_growing{};
    bool private_monotonically_growing{};
    bool resource_counts_passed{};
    bool memory_growth_observed{};
};

[[nodiscard]] ResourceAuditEvaluation evaluate_resource_samples(
    const ResourceSnapshot& baseline,
    std::span<const ResourceSnapshot> samples);
}

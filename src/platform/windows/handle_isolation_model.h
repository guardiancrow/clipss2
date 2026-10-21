#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace clipss2::platform::windows
{
enum class HandleCleanupClassification
{
    no_growth,
    persistent_growth,
    delayed_cleanup,
    finite_process_cache,
    measurement_artifact,
    inconclusive,
};

struct QuiescenceDeltas
{
    std::int64_t immediate{};
    std::int64_t after_100ms{};
    std::int64_t after_1s{};
    std::int64_t after_5s{};
};

[[nodiscard]] HandleCleanupClassification classify_handle_growth(
    std::int64_t measurement_control_delta,
    const QuiescenceDeltas& deltas,
    std::span<const std::int64_t> long_run_cumulative_deltas);
[[nodiscard]] std::string_view classification_name(
    HandleCleanupClassification classification) noexcept;
[[nodiscard]] std::size_t first_persistent_growth_stage(
    std::span<const std::int64_t> stage_deltas) noexcept;
}

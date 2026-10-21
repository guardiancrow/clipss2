#include "platform/windows/handle_isolation_model.h"

#include <limits>

namespace clipss2::platform::windows
{
HandleCleanupClassification classify_handle_growth(
    const std::int64_t measurement_control_delta,
    const QuiescenceDeltas& deltas,
    const std::span<const std::int64_t> long_run)
{
    if (measurement_control_delta != 0)
        return HandleCleanupClassification::measurement_artifact;
    if (deltas.immediate > 0 && deltas.after_5s <= 0)
        return HandleCleanupClassification::delayed_cleanup;
    if (deltas.after_5s <= 0)
        return HandleCleanupClassification::no_growth;
    if (long_run.size() < 3)
        return HandleCleanupClassification::inconclusive;

    const auto halfway = long_run[long_run.size() / 2];
    const auto final = long_run.back();
    if (final <= halfway)
        return HandleCleanupClassification::finite_process_cache;
    if (final - halfway >= static_cast<std::int64_t>(long_run.size() / 4))
        return HandleCleanupClassification::persistent_growth;
    return HandleCleanupClassification::inconclusive;
}

std::string_view classification_name(
    const HandleCleanupClassification classification) noexcept
{
    switch (classification)
    {
    case HandleCleanupClassification::no_growth: return "NO PERSISTENT GROWTH";
    case HandleCleanupClassification::persistent_growth: return "PERSISTENT LINEAR GROWTH";
    case HandleCleanupClassification::delayed_cleanup: return "DELAYED CLEANUP";
    case HandleCleanupClassification::finite_process_cache: return "FINITE PROCESS CACHE";
    case HandleCleanupClassification::measurement_artifact: return "MEASUREMENT ARTIFACT";
    case HandleCleanupClassification::inconclusive: return "INCONCLUSIVE";
    }
    return "INCONCLUSIVE";
}

std::size_t first_persistent_growth_stage(
    const std::span<const std::int64_t> stage_deltas) noexcept
{
    for (std::size_t index = 0; index < stage_deltas.size(); ++index)
        if (stage_deltas[index] > 0) return index;
    return std::numeric_limits<std::size_t>::max();
}
}

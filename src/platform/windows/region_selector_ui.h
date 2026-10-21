#pragma once
#include "product_settings.h"
#include "capture_source.h"
namespace clipss2::platform::windows
{
struct RegionSelectionResult
{
    MonitorSource source;
    ProductPreferences preferences; // Frozen at down, including output intent.
    bool include_cursor{};
    capture::CaptureDestination destination{capture::CaptureDestination::file};
};
// Only value snapshots escape. All overlay windows/classes/fonts are destroyed.
[[nodiscard]] std::optional<RegionSelectionResult> select_region(const ProductPreferences& initial);
// Same shield/HUD lifetime machinery; no crop in the frozen source.
[[nodiscard]] std::optional<RegionSelectionResult> select_monitor(const ProductPreferences& initial);
}

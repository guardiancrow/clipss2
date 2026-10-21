#pragma once
#include "platform/windows/window_candidates.h"
#include "platform/windows/product_settings.h"

namespace clipss2::platform::windows
{
enum class WindowSelectionAction
{
    confirm,
    capture,
    copy,
};

// Runs the explicit window selector prototype. It returns a diagnostic exit
// code and deliberately has no dependency on the capture pipeline.
int run_window_selector_ui();
// Returns a snapshot identity and cursor policy, or nullopt on cancellation.
// All selector windows, fonts and registered classes are destroyed first.
[[nodiscard]] std::optional<WindowSelectionResult> select_window(
    WindowSelectionAction action = WindowSelectionAction::confirm,
    const ProductPreferences& initial = {});
}

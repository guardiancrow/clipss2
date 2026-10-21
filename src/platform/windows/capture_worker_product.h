#pragma once

#include "platform/windows/capture_worker_client.h"
#include "platform/windows/window_candidates.h"

#include <functional>
#include <optional>

namespace clipss2::platform::windows
{
using CaptureWorkerLauncher = std::function<CaptureWorkerClientResult(
    const CaptureWorkerRequest&)>;

struct ProductCaptureExecution
{
    bool worker_launched{};
    std::optional<CaptureWorkerClientResult> worker_result;
    int process_exit_code{};
};

[[nodiscard]] CaptureWorkerRequest capture_worker_request_from_selection(
    const WindowSelectionResult& selection,
    bool fixed_clipboard,
    bool quiet = false);
[[nodiscard]] ProductCaptureExecution execute_product_capture(
    const std::optional<WindowSelectionResult>& selection,
    bool fixed_clipboard,
    const CaptureWorkerLauncher& launcher);
}

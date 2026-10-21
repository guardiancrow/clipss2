#pragma once

#include "capture/capture_destination.h"
#include "platform/windows/capture_worker_product.h"
#include "platform/windows/window_candidates.h"
#include "platform/windows/product_settings.h"

#include <functional>

namespace clipss2::platform::windows
{
[[nodiscard]] capture::CaptureOutputExecution capture_window_target(
    const WindowCandidate& candidate,
    bool include_cursor,
    capture::CaptureDestination destination,
    image::ImageFileFormat file_format,
    bool diagnostic_output = true,
    codec::jpeg::JpegProductSelection jpeg_selection = codec::jpeg::JpegCurrentDefault{},
    std::optional<FileOutputOptions> file_output = {}, capture::FileCompression file_compression = {});
int run_capture_window();
int run_capture_window_clipboard();
int run_capture_region();
int run_capture_monitor();
[[nodiscard]] ProductCaptureExecution run_monitor_capture_product_flow(
    const std::function<void()>& before_worker = {}, ProductSettings* settings = nullptr);
[[nodiscard]] capture::CaptureOutputExecution capture_monitor_target(const CaptureWorkerRequest& request);
[[nodiscard]] ProductCaptureExecution run_region_capture_product_flow(
    const std::function<void()>& before_worker = {}, ProductSettings* settings = nullptr);
[[nodiscard]] ProductCaptureExecution run_window_capture_product_flow(
    bool fixed_clipboard,
    const std::function<void()>& before_worker = {},
    ProductSettings* settings = nullptr);
int report_product_capture_execution(const ProductCaptureExecution& execution);
}

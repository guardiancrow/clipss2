#include "platform/windows/capture_worker_product.h"

#include <stdexcept>

namespace clipss2::platform::windows
{
CaptureWorkerRequest capture_worker_request_from_selection(
    const WindowSelectionResult& selection,
    const bool fixed_clipboard,
    const bool quiet)
{
    CaptureWorkerRequest request{
        selection.candidate.hwnd_value,
        selection.candidate.process_id,
        selection.candidate.class_name,
        selection.include_cursor,
        CaptureBorderPolicy::system_required,
        fixed_clipboard ? capture::CaptureDestination::clipboard :
                          selection.destination,
        selection.file_format,
        quiet,
        selection.jpeg_selection};
    request.file_compression = selection.file_format == image::ImageFileFormat::avif ? selection.avif_profile :
        selection.file_format == image::ImageFileFormat::jxl ? selection.jxl_profile : capture::FileCompression{};
    return request;
}

ProductCaptureExecution execute_product_capture(
    const std::optional<WindowSelectionResult>& selection,
    const bool fixed_clipboard,
    const CaptureWorkerLauncher& launcher)
{
    if (!selection) return {};
    if (!launcher) throw std::invalid_argument("Capture worker launcher is missing.");

    const auto result = launcher(
        capture_worker_request_from_selection(*selection, fixed_clipboard));
    return {
        true,
        result,
        result.status == CaptureWorkerClientStatus::success ? 0 : 1};
}
}

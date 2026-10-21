#include "platform/windows/capture_worker_product.h"

#include <cstdint>
#include <optional>

using namespace clipss2::platform::windows;

namespace
{
WindowSelectionResult selection(
    const bool cursor,
    const clipss2::capture::CaptureDestination destination)
{
    WindowCandidate candidate;
    candidate.hwnd_value = static_cast<std::uintptr_t>(0x1234);
    candidate.process_id = 42;
    candidate.class_name = L"clipss2.Test Worker";
    return {candidate, cursor, destination, clipss2::image::ImageFileFormat::jpeg};
}

CaptureWorkerClientResult result(const CaptureWorkerClientStatus status)
{
    CaptureWorkerClientResult value;
    value.status = status;
    return value;
}
}

int main()
{
    WindowSelectionResult profiles;
    profiles.file_format = clipss2::image::ImageFileFormat::avif;
    profiles.avif_profile = clipss2::capture::parse_file_compression("lossy-balanced");
    profiles.jxl_profile = clipss2::capture::parse_file_compression("lossy-high");
    const auto avif_request = capture_worker_request_from_selection(profiles,false);
    profiles.file_format = clipss2::image::ImageFileFormat::jxl;
    const auto jxl_request = capture_worker_request_from_selection(profiles,false);
    profiles.avif_profile = {}; profiles.jxl_profile = {};
    if (avif_request.file_compression != clipss2::capture::parse_file_compression("lossy-balanced") ||
        jxl_request.file_compression != clipss2::capture::parse_file_compression("lossy-high")) return 1;
    int launches{};
    const CaptureWorkerLauncher successful = [&](const CaptureWorkerRequest& request) {
        ++launches;
        if (request.hwnd_value != static_cast<std::uintptr_t>(0x1234) ||
            request.expected_process_id != 42 ||
            request.expected_class != L"clipss2.Test Worker" ||
            !request.include_cursor ||
            request.border_policy != CaptureBorderPolicy::system_required ||
            request.destination != clipss2::capture::CaptureDestination::both ||
            request.file_format != clipss2::image::ImageFileFormat::jpeg ||
            request.quiet)
            return result(CaptureWorkerClientStatus::invalid_request);
        return result(CaptureWorkerClientStatus::success);
    };

    const auto cancelled = execute_product_capture(std::nullopt, false, successful);
    if (cancelled.worker_launched || cancelled.worker_result ||
        cancelled.process_exit_code != 0 || launches != 0)
        return 1;

    const auto selected = selection(
        true, clipss2::capture::CaptureDestination::both);
    auto settings_snapshot = selected;
    settings_snapshot.jpeg_selection = clipss2::codec::jpeg::JpegProfileSelection{
        clipss2::codec::jpeg::JpegProfile::balanced, clipss2::codec::jpeg::JpegBackendId::automatic};
    const auto frozen = capture_worker_request_from_selection(settings_snapshot, false);
    settings_snapshot.jpeg_selection = clipss2::codec::jpeg::JpegProfileSelection{
        clipss2::codec::jpeg::JpegProfile::fast, clipss2::codec::jpeg::JpegBackendId::turbo};
    settings_snapshot.include_cursor = false;
    settings_snapshot.file_format = clipss2::image::ImageFileFormat::png;
    if (!frozen.include_cursor || frozen.file_format != clipss2::image::ImageFileFormat::jpeg ||
        frozen.jpeg_selection == settings_snapshot.jpeg_selection) return 1;
    if (!std::holds_alternative<clipss2::codec::jpeg::JpegCurrentDefault>(
        capture_worker_request_from_selection(selection(false, clipss2::capture::CaptureDestination::file), false).jpeg_selection)) return 1;
    const auto succeeded = execute_product_capture(selected, false, successful);
    if (!succeeded.worker_launched || !succeeded.worker_result ||
        succeeded.worker_result->status != CaptureWorkerClientStatus::success ||
        succeeded.process_exit_code != 0 || launches != 1)
        return 1;

    bool fixed_clipboard_seen{};
    const auto fixed = execute_product_capture(
        selected, true, [&](const CaptureWorkerRequest& request) {
            fixed_clipboard_seen =
                request.destination == clipss2::capture::CaptureDestination::clipboard;
            return result(CaptureWorkerClientStatus::success);
        });
    if (!fixed_clipboard_seen || fixed.process_exit_code != 0) return 1;

    for (const auto status : {
             CaptureWorkerClientStatus::stale_target,
             CaptureWorkerClientStatus::invalid_request,
             CaptureWorkerClientStatus::launch_failure,
             CaptureWorkerClientStatus::wait_failure,
             CaptureWorkerClientStatus::capture_failure,
             CaptureWorkerClientStatus::file_failure,
             CaptureWorkerClientStatus::clipboard_failure,
             CaptureWorkerClientStatus::partial_success,
             CaptureWorkerClientStatus::timeout,
             CaptureWorkerClientStatus::unexpected_exit})
    {
        const auto failed = execute_product_capture(
            selected, false, [status](const CaptureWorkerRequest&) {
                return result(status);
            });
        if (!failed.worker_launched || failed.process_exit_code == 0 ||
            !failed.worker_result || failed.worker_result->status != status)
            return 1;
    }

    for (const auto destination : {
             clipss2::capture::CaptureDestination::file,
             clipss2::capture::CaptureDestination::clipboard,
             clipss2::capture::CaptureDestination::both})
    {
        const auto request = capture_worker_request_from_selection(
            selection(false, destination), false);
        if (request.destination != destination || request.include_cursor) return 1;
    }
}

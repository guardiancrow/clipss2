#pragma once

#include "capture/capture_destination.h"
#include "image/image_file_format.h"
#include "codec/jpeg/jpeg_product_selection.h"
#include "platform/windows/capture_border_policy.h"
#include "platform/windows/storage_preferences.h"
#include "platform/windows/capture_source.h"
#include "capture/file_compression.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clipss2::platform::windows
{
enum class CaptureWorkerExit : unsigned long
{
    success = 0,
    stale_target = 2,
    capture_failure = 3,
    file_failure = 4,
    clipboard_failure = 5,
    partial_success = 6,
    timeout = 7,
    invalid_argument = 64,
};

struct CaptureWorkerRequest
{
    std::uintptr_t hwnd_value{};
    std::uint32_t expected_process_id{};
    std::wstring expected_class;
    bool include_cursor{};
    CaptureBorderPolicy border_policy{CaptureBorderPolicy::system_required};
    capture::CaptureDestination destination{capture::CaptureDestination::file};
    image::ImageFileFormat file_format{image::ImageFileFormat::unknown};
    bool quiet{};
    codec::jpeg::JpegProductSelection jpeg_selection{codec::jpeg::JpegCurrentDefault{}};
    std::optional<FileOutputOptions> file_output;
    // Legacy Window envelope stays byte-compatible. Monitor is a distinct source,
    // never a fake HWND in the worker/capture pipeline.
    std::optional<MonitorSource> monitor_source;
    // Immutable selected compression resolved by the parent from codec preferences.
    capture::FileCompression file_compression;
    [[nodiscard]] CaptureSource capture_source() const
    { if (monitor_source) return *monitor_source; return WindowSource{hwnd_value, expected_process_id, expected_class}; }
};

[[nodiscard]] std::string encode_worker_class(std::wstring_view value);
[[nodiscard]] CaptureWorkerRequest parse_capture_worker_request(
    std::span<const std::string_view> arguments);
[[nodiscard]] std::vector<std::string> serialize_capture_worker_request(
    const CaptureWorkerRequest& request);
[[nodiscard]] const char* capture_worker_exit_name(CaptureWorkerExit value) noexcept;
}

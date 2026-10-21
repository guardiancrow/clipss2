#pragma once

#include "platform/windows/capture_worker_protocol.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace clipss2::platform::windows
{
enum class CaptureWorkerClientStatus
{
    success,
    stale_target,
    invalid_request,
    capture_failure,
    file_failure,
    clipboard_failure,
    partial_success,
    timeout,
    launch_failure,
    wait_failure,
    unexpected_exit,
};

struct CaptureWorkerClientResult
{
    CaptureWorkerClientStatus status{CaptureWorkerClientStatus::unexpected_exit};
    std::uint32_t exit_code{};
    std::chrono::microseconds duration{};
    bool forced_termination{};
    std::string diagnostic;
};

[[nodiscard]] const char* capture_worker_client_status_name(
    CaptureWorkerClientStatus status) noexcept;
[[nodiscard]] std::filesystem::path current_executable_path();
[[nodiscard]] std::wstring quote_windows_command_line_argument(
    std::wstring_view argument);
[[nodiscard]] std::wstring build_capture_worker_command_line(
    const std::filesystem::path& executable,
    const CaptureWorkerRequest& request);
[[nodiscard]] CaptureWorkerClientResult launch_capture_worker_at(
    const std::filesystem::path& executable,
    const CaptureWorkerRequest& request,
    std::chrono::milliseconds timeout = std::chrono::seconds{10}) noexcept;
[[nodiscard]] CaptureWorkerClientResult launch_capture_worker(
    const CaptureWorkerRequest& request,
    std::chrono::milliseconds timeout = std::chrono::seconds{10}) noexcept;
}

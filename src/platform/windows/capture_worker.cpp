#include "platform/windows/capture_worker.h"

#include "platform/windows/capture_worker_protocol.h"
#include "platform/windows/capture_worker_diagnostics.h"
#include "platform/windows/capture_window.h"
#include "platform/windows/error_diagnostics.h"
#include "platform/windows/unicode_diagnostics.h"
#include "platform/windows/resource_counters.h"
#include "platform/windows/monitor_capture_source.h"
#include "platform/windows/window_target_validation.h"

#include <exception>
#include <chrono>
#include <windows.h>
#include <psapi.h>
#include <winrt/base.h>
#include <iostream>
#include <map>

namespace clipss2::platform::windows
{
namespace
{
#if CLIPSS2_AUDIT_FEATURES
std::int64_t alpc_delta(
    const std::vector<ProcessHandleRecord>& before,
    const std::vector<ProcessHandleRecord>& after)
{
    const auto left = count_handle_types(before);
    const auto right = count_handle_types(after);
    const std::wstring type = L"ALPC Port";
    const auto a = left.contains(type) ? left.at(type) : 0;
    const auto b = right.contains(type) ? right.at(type) : 0;
    return static_cast<std::int64_t>(b) - static_cast<std::int64_t>(a);
}

bool resource_option(const wchar_t* name) noexcept
{
    wchar_t value[2]{};
    return audit::environment_variable(name, value, 2) == 1 && value[0] == L'1';
}
#endif

class WorkerResourceDiagnostics final
{
public:
    explicit WorkerResourceDiagnostics(bool quiet) noexcept
#if CLIPSS2_AUDIT_FEATURES
        : enabled_(resource_option(L"_CLIPSS2_AUDIT_RESOURCE_DIAGNOSTICS")),
          output_(!quiet), trace_(!quiet && resource_option(L"_CLIPSS2_AUDIT_RESOURCE_TRACE"))
#endif
    { (void)quiet; }
    ~WorkerResourceDiagnostics() noexcept
    {
#if CLIPSS2_AUDIT_FEATURES
        if (trace_) worker_diagnostic_output([] {
            const auto counts = pss_counters_for_audit();
            std::cout << "Resource diagnostic PSS calls: " << counts.calls
                      << "; live snapshots=" << counts.snapshots << "; live markers=" << counts.markers
                      << "; cleanup failures=" << counts.cleanup_failures << '\n';
        });
#endif
    }
    void before() noexcept
    {
#if CLIPSS2_AUDIT_FEATURES
        if (enabled_) (void)worker_diagnostic({"pre.capture.diagnostics", "Collect process handles"}, output_, [&] {
            before_ = collect_process_handles("pre");
        });
#endif
    }
    void after() noexcept
    {
#if CLIPSS2_AUDIT_FEATURES
        if (enabled_) (void)worker_diagnostic({"post.delivery.diagnostics", "Process handles / memory after delivery"}, output_, [&] {
            const auto handles_after = collect_process_handles("post");
            PROCESS_MEMORY_COUNTERS_EX memory{};
            memory.cb = sizeof(memory);
            const BOOL succeeded = GetProcessMemoryInfo(GetCurrentProcess(),
                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
            const DWORD error = succeeded ? ERROR_SUCCESS : GetLastError();
            require_win32_success(succeeded != FALSE, error, "GetProcessMemoryInfo failed");
            if (output_) {
                std::cout << "Child ALPC delta: ";
                if (before_) std::cout << alpc_delta(*before_, handles_after);
                else std::cout << "unavailable";
                std::cout << "\nChild peak working-set bytes: " << memory.PeakWorkingSetSize
                          << "\nChild peak commit proxy bytes: " << memory.PeakPagefileUsage << '\n';
            }
            const auto counters = pss_counters_for_audit();
            if (counters.cleanup_failures)
                throw DiagnosticError("reason=pss-resource-cleanup-failed");
        });
#endif
    }
private:
#if CLIPSS2_AUDIT_FEATURES
    bool enabled_{};
    bool output_{};
    bool trace_{};
    std::optional<std::vector<ProcessHandleRecord>> before_;
#endif
};
}

int run_capture_worker(
    const std::span<const std::string_view> arguments) noexcept
{
    const auto worker_started = std::chrono::steady_clock::now();
    CaptureWorkerRequest request;
    try
    {
        request = parse_capture_worker_request(arguments);
    }
    catch (...)
    {
        worker_diagnostic_output([] {
            write_stderr_utf8("Capture worker invalid argument: " + describe_exception(std::current_exception()) + '\n');
        });
        return static_cast<int>(CaptureWorkerExit::invalid_argument);
    }

    WorkerResourceDiagnostics diagnostics{request.quiet};
    try
    {
        WindowCandidate candidate;
        candidate.hwnd_value = request.hwnd_value;
        candidate.process_id = request.expected_process_id;
        candidate.class_name = request.expected_class;
        candidate.title = L"clipss2 worker target";
        const bool current = capture_stage({"target.validate", "Revalidate request target identity"}, [&] {
            if (!request.monitor_source) return is_window_candidate_current(candidate);
            try { (void)resolve_monitor_source(*request.monitor_source); return true; }
            catch (...) { return false; } // Existing stale-target exit contract.
        });
        if (!current)
        {
            if (!request.quiet)
            {
                worker_diagnostic_output([&] {
                try {
                write_stderr_utf8("Capture worker result: stale target; no output published; stage=target.validate; checkpoint=worker-entry; " +
                    (request.monitor_source ? std::string{"reason=monitor-source-stale"} : window_identity_failure_reason(candidate)) + '\n');
                } catch (...) {
                    // Diagnostic re-observation/formatting cannot change stale exit=2.
                    write_stderr_utf8("Capture worker result: stale target; no output published; stage=target.validate; "
                        "checkpoint=worker-entry; reason=identity-stale-diagnostic-unavailable\n");
                }
                });
            }
            return static_cast<int>(CaptureWorkerExit::stale_target);
        }

        const auto* manual = std::get_if<codec::jpeg::JpegProfileSelection>(&request.jpeg_selection);
        if (manual && manual->backend != codec::jpeg::JpegBackendId::automatic &&
            capture::capture_output_plan(request.destination).file && request.file_format == image::ImageFileFormat::jpeg)
        {
            // Manual identity is already fixed by the request. Reject invalid
            // pairings early; do not run Automatic ordering a second time.
            try { (void)codec::jpeg::resolve_jpeg_product_selection(request.jpeg_selection); }
            catch (const std::exception&)
            {
                if (!request.quiet) worker_diagnostic_output([] {
                    write_stderr_utf8(describe_exception(std::current_exception()) + '\n');
                });
                return static_cast<int>(CaptureWorkerExit::invalid_argument);
            }
        }

        diagnostics.before();
        const auto delivery = capture_stage({"capture.request", "Capture request dispatch / uncategorized failure"}, [&] {
            return request.monitor_source ? capture_monitor_target(request) : capture_window_target(
                candidate, request.include_cursor, request.destination, request.file_format,
                !request.quiet, request.jpeg_selection, request.file_output, request.file_compression);
        });
        const auto result = worker_delivery_exit(delivery);
        if (!request.quiet && request.file_compression.mode == capture::FileCompressionMode::lossy)
            worker_diagnostic_output([&] {
            std::cout << "Worker pipeline ms: " << std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - worker_started).count() << '\n';
            });
        if (!request.quiet)
        {
            worker_diagnostic_output([&] {
                std::cout << "Capture worker result: " << capture_worker_exit_name(result) << '\n';
            });
        }
        diagnostics.after();
        return static_cast<int>(result);
    }
    catch (...)
    {
        if (!request.quiet)
            worker_diagnostic_output([] {
                write_stderr_utf8("Capture worker failure: " + describe_exception(std::current_exception()) + '\n');
            });
        return static_cast<int>(CaptureWorkerExit::capture_failure);
    }
}
}

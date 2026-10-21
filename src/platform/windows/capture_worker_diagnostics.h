#pragma once

#include "platform/windows/capture_worker_protocol.h"
#include "platform/windows/error_diagnostics.h"
#include "platform/windows/unicode_diagnostics.h"

namespace clipss2::platform::windows
{
// Delivery classification is independent of optional diagnostic observations.
inline CaptureWorkerExit worker_delivery_exit(const capture::CaptureOutputExecution& delivery) noexcept
{
    if (delivery.outcome == capture::CaptureOutputOutcome::succeeded)
        return CaptureWorkerExit::success;
    if (delivery.outcome == capture::CaptureOutputOutcome::partial_success)
        return CaptureWorkerExit::partial_success;
    if (delivery.file_attempted && !delivery.file_succeeded)
        return CaptureWorkerExit::file_failure;
    if (delivery.clipboard_attempted && !delivery.clipboard_succeeded)
        return CaptureWorkerExit::clipboard_failure;
    return CaptureWorkerExit::capture_failure;
}

template<class Function>
bool worker_diagnostic(CaptureStage stage, bool output, Function&& action) noexcept
{
    try
    {
        capture_stage(stage, std::forward<Function>(action));
        return true;
    }
    catch (...)
    {
        // Formatting and the warning sink belong to the same nonfatal boundary.
        if (output)
        {
            try { write_stderr_utf8("Capture worker diagnostic warning: " +
                describe_exception(std::current_exception()) + '\n'); }
            catch (...) {}
        }
        return false;
    }
}

template<class Function>
void worker_diagnostic_output(Function&& action) noexcept
{
    try { std::forward<Function>(action)(); }
    catch (...) {} // Only diagnostic formatting/output, never capture or delivery.
}
}

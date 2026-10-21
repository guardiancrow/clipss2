#pragma once

#include <utility>

namespace clipss2::capture {

enum class CaptureDestination {
    file,
    clipboard,
    both,
};

struct CaptureOutputPlan {
    bool file{};
    bool clipboard{};
};

enum class CaptureOutputOutcome {
    succeeded,
    failed,
    partial_success,
};

struct CaptureOutputExecution {
    CaptureOutputOutcome outcome{CaptureOutputOutcome::failed};
    bool file_attempted{};
    bool file_succeeded{};
    bool clipboard_attempted{};
    bool clipboard_succeeded{};
};

[[nodiscard]] CaptureDestination next_capture_destination(CaptureDestination destination) noexcept;
[[nodiscard]] char const* capture_destination_name(CaptureDestination destination) noexcept;
[[nodiscard]] CaptureOutputPlan capture_output_plan(CaptureDestination destination) noexcept;

class CaptureDestinationPolicy {
public:
    explicit CaptureDestinationPolicy(
        CaptureDestination initial = CaptureDestination::file,
        bool mutable_destination = true) noexcept;

    [[nodiscard]] CaptureDestination destination() const noexcept;
    [[nodiscard]] bool mutable_destination() const noexcept;
    bool cycle(bool input_allowed) noexcept;

private:
    CaptureDestination destination_;
    bool mutable_destination_;
};

template <typename FileSink, typename ClipboardSink>
[[nodiscard]] CaptureOutputExecution execute_capture_output_plan(
    CaptureDestination destination,
    FileSink&& file_sink,
    ClipboardSink&& clipboard_sink) {
    auto const plan = capture_output_plan(destination);
    CaptureOutputExecution result{};

    if (plan.file) {
        result.file_attempted = true;
        result.file_succeeded = std::forward<FileSink>(file_sink)();
        if (!result.file_succeeded) {
            result.outcome = CaptureOutputOutcome::failed;
            return result;
        }
    }

    if (plan.clipboard) {
        result.clipboard_attempted = true;
        result.clipboard_succeeded = std::forward<ClipboardSink>(clipboard_sink)();
        if (!result.clipboard_succeeded) {
            result.outcome = plan.file ? CaptureOutputOutcome::partial_success
                                       : CaptureOutputOutcome::failed;
            return result;
        }
    }

    result.outcome = CaptureOutputOutcome::succeeded;
    return result;
}

}  // namespace clipss2::capture

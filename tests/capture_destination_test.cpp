#include "capture/capture_destination.h"

#include <cstdlib>

namespace {

void require(bool condition) {
    if (!condition) {
        std::abort();
    }
}

}  // namespace

int main() {
    using namespace clipss2::capture;

    CaptureDestinationPolicy policy{};
    require(policy.destination() == CaptureDestination::file);
    require(policy.cycle(true));
    require(policy.destination() == CaptureDestination::clipboard);
    require(policy.cycle(true));
    require(policy.destination() == CaptureDestination::both);
    require(policy.cycle(true));
    require(policy.destination() == CaptureDestination::file);
    require(!policy.cycle(false));
    require(policy.destination() == CaptureDestination::file);

    int candidate = 42;
    bool cursor = true;
    int input_source = 7;
    require(policy.cycle(true));
    require(candidate == 42 && cursor && input_source == 7);

    CaptureDestinationPolicy fixed{CaptureDestination::clipboard, false};
    require(!fixed.cycle(true));
    require(fixed.destination() == CaptureDestination::clipboard);

    int file_calls = 0;
    int clipboard_calls = 0;
    auto result = execute_capture_output_plan(
        CaptureDestination::both,
        [&] { ++file_calls; return true; },
        [&] { ++clipboard_calls; return true; });
    require(result.outcome == CaptureOutputOutcome::succeeded);
    require(file_calls == 1 && clipboard_calls == 1);

    file_calls = 0;
    clipboard_calls = 0;
    result = execute_capture_output_plan(
        CaptureDestination::both,
        [&] { ++file_calls; return false; },
        [&] { ++clipboard_calls; return true; });
    require(result.outcome == CaptureOutputOutcome::failed);
    require(file_calls == 1 && clipboard_calls == 0);
    require(!result.clipboard_attempted);

    result = execute_capture_output_plan(
        CaptureDestination::both,
        [] { return true; },
        [] { return false; });
    require(result.outcome == CaptureOutputOutcome::partial_success);
    require(result.file_succeeded && result.clipboard_attempted && !result.clipboard_succeeded);

    auto const file_plan = capture_output_plan(CaptureDestination::file);
    require(file_plan.file && !file_plan.clipboard);
    auto const clipboard_plan = capture_output_plan(CaptureDestination::clipboard);
    require(!clipboard_plan.file && clipboard_plan.clipboard);
    auto const both_plan = capture_output_plan(CaptureDestination::both);
    require(both_plan.file && both_plan.clipboard);
}

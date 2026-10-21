#include "capture/capture_destination.h"

namespace clipss2::capture {

CaptureDestination next_capture_destination(CaptureDestination destination) noexcept {
    switch (destination) {
    case CaptureDestination::file:
        return CaptureDestination::clipboard;
    case CaptureDestination::clipboard:
        return CaptureDestination::both;
    case CaptureDestination::both:
        return CaptureDestination::file;
    }
    return CaptureDestination::file;
}

char const* capture_destination_name(CaptureDestination destination) noexcept {
    switch (destination) {
    case CaptureDestination::file:
        return "File";
    case CaptureDestination::clipboard:
        return "Clipboard";
    case CaptureDestination::both:
        return "Both";
    }
    return "File";
}

CaptureOutputPlan capture_output_plan(CaptureDestination destination) noexcept {
    switch (destination) {
    case CaptureDestination::file:
        return {.file = true, .clipboard = false};
    case CaptureDestination::clipboard:
        return {.file = false, .clipboard = true};
    case CaptureDestination::both:
        return {.file = true, .clipboard = true};
    }
    return {.file = true, .clipboard = false};
}

CaptureDestinationPolicy::CaptureDestinationPolicy(
    CaptureDestination initial,
    bool mutable_destination) noexcept
    : destination_(initial), mutable_destination_(mutable_destination) {}

CaptureDestination CaptureDestinationPolicy::destination() const noexcept {
    return destination_;
}

bool CaptureDestinationPolicy::mutable_destination() const noexcept {
    return mutable_destination_;
}

bool CaptureDestinationPolicy::cycle(bool input_allowed) noexcept {
    if (!mutable_destination_ || !input_allowed) {
        return false;
    }
    destination_ = next_capture_destination(destination_);
    return true;
}

}  // namespace clipss2::capture

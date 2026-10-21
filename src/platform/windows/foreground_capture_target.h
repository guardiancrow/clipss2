#pragma once
#include "platform/windows/one_frame_capture.h"

namespace clipss2::platform::windows
{
// Foreground policy belongs only to the legacy diagnostic entry point.
[[nodiscard]] WindowCaptureTarget foreground_window_capture_target();
}

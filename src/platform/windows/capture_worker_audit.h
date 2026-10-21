#pragma once

#include <string_view>

namespace clipss2::platform::windows
{
int run_capture_worker_audit(std::string_view probe);
}

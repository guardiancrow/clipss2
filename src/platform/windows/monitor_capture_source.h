#pragma once
#include "capture_source.h"
#include <cstdint>
namespace clipss2::platform::windows
{
// Enumerates fresh native handles. No nearest/primary fallback.
[[nodiscard]] std::uintptr_t resolve_monitor_source(const MonitorSource& source);
[[nodiscard]] std::string display_topology_snapshot();
}

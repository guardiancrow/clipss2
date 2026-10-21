#pragma once
#include <cstddef>
#include <stdexcept>

namespace clipss2::platform::windows
{
// Enumeration is immutable for one selector session. No spatial or OS-number mapping.
[[nodiscard]] inline std::size_t next_monitor_index(std::size_t current,std::size_t count,bool reverse)
{
    if (!count || current>=count) throw std::invalid_argument("Invalid monitor candidate cycle.");
    if (reverse) return current?current-1:count-1;
    return current==count-1?0:current+1;
}
}

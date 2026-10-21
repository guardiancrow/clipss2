#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <string_view>
#include "audit/audit_policy.h"

#include "platform/windows/resource_lifetime_model.h"

namespace clipss2::platform::windows
{
struct ProcessHandleRecord
{
    std::uintptr_t value{};
    std::wstring type;
    std::wstring object_name;
    std::uint32_t granted_access{};
};

[[nodiscard]] ResourceSnapshot collect_resource_snapshot();
[[nodiscard]] std::vector<ProcessHandleRecord> collect_process_handles(std::string_view phase = {});
[[nodiscard]] std::map<std::wstring, std::size_t> count_handle_types(
    const std::vector<ProcessHandleRecord>& handles);
void print_resource_snapshot(const char* label, const ResourceSnapshot& value);
void print_handle_type_deltas(
    const std::map<std::wstring, std::size_t>& baseline,
    const std::map<std::wstring, std::size_t>& final);
#if CLIPSS2_AUDIT_FEATURES
struct PssAuditCounters
{
    unsigned calls{};
    unsigned snapshots{};
    unsigned markers{};
    unsigned cleanup_failures{};
};
[[nodiscard]] PssAuditCounters pss_counters_for_audit() noexcept;
void reset_pss_counters_for_audit() noexcept;
#endif
}

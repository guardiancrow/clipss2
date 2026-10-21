#pragma once

#include "audit/audit_policy.h"

#include <filesystem>
#include <stdexcept>
#include <string>

#if !CLIPSS2_AUDIT_FEATURES
#error Artifact directory is restricted to audit fixtures
#endif

namespace clipss2::audit
{
inline std::filesystem::path artifact_directory()
{
    wchar_t buffer[32768]{};
    const auto length = environment_variable(
        L"_CLIPSS2_AUDIT_ARTIFACT_DIR", buffer, 32768);
    if (length == 0 || length >= 32768)
        throw std::invalid_argument("Audit fixture requires an explicit isolated artifact directory.");
    std::filesystem::path result{std::wstring{buffer, length}};
    if (!result.is_absolute())
        throw std::invalid_argument("Audit artifact directory must be absolute.");
    return result;
}
}

#pragma once
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <cstdint>

namespace clipss2::platform::windows
{
enum class SettingsMode { per_user, portable };
enum class FilenamePolicy { timestamp, sequence, guid };
// nullopt = mode-dependent Default; otherwise exact absolute filesystem path.
struct OutputDirectoryPreference
{
    std::optional<std::filesystem::path> explicit_path;
    bool operator==(const OutputDirectoryPreference&) const = default;
};
struct FileOutputOptions
{
    std::filesystem::path directory;
    FilenamePolicy filename_policy{FilenamePolicy::timestamp};
    bool create_directory{}; // Portable Default only.
    bool operator==(const FileOutputOptions&) const = default;
};
inline const char* filename_policy_token(FilenamePolicy value)
{
    switch (value)
    {
    case FilenamePolicy::timestamp: return "timestamp";
    case FilenamePolicy::sequence: return "sequence";
    case FilenamePolicy::guid: return "guid";
    }
    throw std::invalid_argument("Unknown filename policy.");
}
inline FilenamePolicy parse_filename_policy(std::string_view token)
{
    if (token == "timestamp") return FilenamePolicy::timestamp;
    if (token == "sequence") return FilenamePolicy::sequence;
    if (token == "guid") return FilenamePolicy::guid;
    throw std::invalid_argument("Unknown filename policy token.");
}
inline void validate_output_path(const std::filesystem::path& path)
{
    if (!path.is_absolute() || path.native().find_first_of(L"\r\n") != std::wstring::npos ||
        path.native().find(L'\0') != std::wstring::npos)
        throw std::invalid_argument("Output path must be an absolute, single-line filesystem path.");
    // No canonicalization, environment expansion, case folding or provider heuristics.
}
}

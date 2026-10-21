#pragma once
#include "platform/windows/storage_preferences.h"
#include <windows.h>
#include <functional>
#include <string>

namespace clipss2::platform::windows
{
struct SettingsStore
{
    SettingsMode mode{SettingsMode::per_user};
    std::filesystem::path path;
    std::filesystem::path executable_directory;
};
// Nullopt means definite absence; probe errors must throw, never select another store.
using SettingsAttributeProbe = std::function<std::optional<DWORD>(const std::filesystem::path&)>;
[[nodiscard]] SettingsStore resolve_settings_store(const std::filesystem::path& executable_directory,
    const std::function<std::filesystem::path()>& per_user_path,
    const SettingsAttributeProbe& probe = {});
// Checks the bounded GetModuleFileNameW result before deriving an absolute directory.
[[nodiscard]] std::filesystem::path settings_executable_directory(std::wstring_view buffer, DWORD count);
[[nodiscard]] SettingsStore default_settings_store();
[[nodiscard]] std::string path_utf8(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path path_from_utf8(std::string_view value);
using FolderPicker = std::function<std::optional<std::filesystem::path>(const std::filesystem::path&)>;
// Nullopt means user cancellation. Pictures is resolved only if a picker is needed.
[[nodiscard]] std::optional<FileOutputOptions> resolve_file_output(
    const SettingsStore& store, OutputDirectoryPreference& preference, FilenamePolicy policy,
    const FolderPicker& picker,
    const std::function<std::filesystem::path()>& initial_location);
[[nodiscard]] std::optional<std::filesystem::path> pick_output_folder(
    const std::filesystem::path& initial);
[[nodiscard]] std::optional<std::filesystem::path> pick_output_folder_owned(
    const std::filesystem::path& initial, HWND owner);
[[nodiscard]] std::filesystem::path resolved_pictures_directory();
}

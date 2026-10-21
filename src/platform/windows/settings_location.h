#pragma once
#include "platform/windows/product_settings.h"
#include <functional>

namespace clipss2::platform::windows
{
enum class SettingsPresence { missing, file, directory, unknown };
struct SettingsMetadata
{
    SettingsPresence presence{SettingsPresence::unknown};
    DWORD error{};
};
using SettingsMetadataProbe = std::function<SettingsMetadata(const std::filesystem::path&)>;
using SettingsFolderShell = std::function<HRESULT(const std::filesystem::path&)>;
struct SettingsLocationView
{
    bool resolved{};
    std::filesystem::path path;
    UiStringId mode{UiStringId::settings_mode_unresolved};
    SettingsMetadata file;
    SettingsMetadata parent;
    UiStringId guidance{UiStringId::count};
};
[[nodiscard]] SettingsMetadata settings_metadata(const std::filesystem::path& path) noexcept;
// Observes metadata only. Never resolves a store, reads contents, or changes ProductSettings.
[[nodiscard]] SettingsLocationView settings_location_view(const SettingsStore& store,
    const SettingsLoadResult& loaded, const SettingsMetadataProbe& probe = {});
[[nodiscard]] UiStringId settings_presence_text(SettingsPresence presence) noexcept;
// No action (including metadata probes) without an explicit request. Shell is replaceable in tests.
[[nodiscard]] HRESULT open_settings_parent(const SettingsStore& store, bool requested,
    const SettingsMetadataProbe& probe = {}, const SettingsFolderShell& shell = {}) noexcept;
}

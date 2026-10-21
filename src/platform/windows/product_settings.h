#pragma once
#include "platform/windows/window_candidates.h"
#include "platform/windows/output_storage.h"
#include "platform/windows/ui_localization.h"
#include <filesystem>
#include <string_view>

namespace clipss2::platform::windows
{
// User intent + approved directory only. No destination, cursor, target identity
// or output history. Store mode is process state, not a persisted preference.
struct ProductPreferences
{
    image::ImageFileFormat file_format{image::ImageFileFormat::png};
    codec::jpeg::JpegProductSelection jpeg_selection{codec::jpeg::JpegCurrentDefault{}};
    FilenamePolicy filename_policy{FilenamePolicy::timestamp};
    OutputDirectoryPreference output_directory;
    UiLanguagePreference ui_language{UiLanguagePreference::system};
    capture::FileCompression avif_profile;
    capture::FileCompression jxl_profile;
    bool operator==(const ProductPreferences&) const = default;
};
[[nodiscard]] inline capture::FileCompression selected_file_compression(const ProductPreferences& value)
{
    if (value.file_format == image::ImageFileFormat::avif) return value.avif_profile;
    if (value.file_format == image::ImageFileFormat::jxl) return value.jxl_profile;
    return {};
}
enum class SettingsStatus
{
    no_settings, loaded, invalid_settings, unsupported_schema,
    known_backend_unavailable, unsupported_selection, read_failure
};
struct SettingsLoadResult
{
    ProductPreferences preferences;
    SettingsStatus status{SettingsStatus::no_settings};
    bool persistence_enabled{true};
    std::string diagnostic;
};
constexpr std::size_t maximum_settings_bytes = 64 * 1024;
[[nodiscard]] SettingsLoadResult parse_product_settings(std::string_view bytes,
    std::span<const codec::jpeg::JpegBackendCapabilities> catalog =
        codec::jpeg::current_product_jpeg_backends());
void validate_product_preferences(const ProductPreferences& preferences);
[[nodiscard]] std::string serialize_product_settings(const ProductPreferences& preferences);
[[nodiscard]] std::filesystem::path default_product_settings_path();
[[nodiscard]] SettingsLoadResult load_product_settings(const std::filesystem::path& path);
struct SettingsWriteResult { bool persisted{}; std::string diagnostic; };
// Internal, bounded fault injection for the native writer's tests only.
enum class SettingsWriteFault { none, temp_write, flush, replace };
[[nodiscard]] SettingsWriteResult write_product_settings(const std::filesystem::path& path,
    const ProductPreferences& preferences, SettingsWriteFault fault = SettingsWriteFault::none);

// One object per product process. Explicit path injection keeps tests isolated.
class ProductSettings final
{
public:
    ProductSettings();
    explicit ProductSettings(std::filesystem::path path);
    [[nodiscard]] const ProductPreferences& preferences() const noexcept { return loaded_.preferences; }
    [[nodiscard]] const SettingsLoadResult& load_result() const noexcept { return loaded_; }
    [[nodiscard]] const SettingsStore& store() const noexcept { return store_; }
    void commit(const WindowSelectionResult& snapshot);
    void commit(ProductPreferences snapshot);
    [[nodiscard]] SettingsWriteResult save_from_settings(
        ProductPreferences snapshot,
        SettingsWriteFault fault = SettingsWriteFault::none);
private:
    SettingsStore store_;
    std::filesystem::path path_;
    SettingsLoadResult loaded_;
};
}

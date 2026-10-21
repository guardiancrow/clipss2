#pragma once

#include <span>
#include <string>
#include <string_view>

#include <windows.h>
#include "capture/file_compression.h"

namespace clipss2::platform::windows
{
enum class UiLanguage { english, japanese };
enum class UiLanguagePreference { system, english, japanese };

enum class UiStringId
{
    window_selection_title,
    region_selection_title,
    monitor_selection_title,
    no_live_window,
    release_cancel,
    release_capture,
    release_confirm,
    click_enter_capture,
    click_enter_confirm,
    tab_select,
    cursor_on,
    cursor_off,
    destination_file,
    destination_clipboard,
    destination_both,
    format_png,
    format_jpeg,
    format_webp,
    file_only,
    output_clipboard,
    escape_cancel,
    region_drag,
    monitor_prefix,
    pixels,
    monitor_navigation,
    monitor_confirm,
    profile_default,
    profile_fast,
    profile_balanced,
    profile_smallest,
    encoder,
    automatic,
    unavailable,
    tray_capture_window,
    tray_capture_region,
    tray_capture_monitor,
    tray_settings,
    tray_exit,
    output_folder_title,
    settings_title,
    settings_save_location,
    settings_change,
    settings_reset,
    settings_not_selected,
    settings_file_naming,
    settings_filename_timestamp,
    settings_filename_sequence,
    settings_filename_guid,
    settings_language,
    settings_language_system,
    settings_language_english,
    settings_language_japanese,
    settings_ok,
    settings_cancel,
    settings_save_error_title,
    settings_newer_schema_read_only,
    settings_resident_active,
    compression_lossless,
    compression_high,
    compression_balanced,
    compression_small,
    startup_notification_title,
    startup_notification_body,
    settings_location_read_only,
    settings_file_section,
    settings_mode_normal,
    settings_mode_portable,
    settings_mode_unresolved,
    settings_file_location,
    settings_file_missing,
    settings_file_exists,
    settings_file_unknown,
    settings_file_not_regular,
    settings_open_folder,
    settings_parent_unavailable,
    settings_folder_error,
    settings_read_error,
    settings_marker_error,
    settings_oversized,
    settings_protected,
    settings_defaults_warning,
    settings_unsaved,
    settings_write_guidance,
    settings_folder_error_title,
    count
};

[[nodiscard]] std::wstring_view ui_string(UiLanguage language, UiStringId id);
[[nodiscard]] std::wstring codec_profile_hud(UiLanguage language,
    image::ImageFileFormat format, capture::FileCompression value);
[[nodiscard]] UiLanguage ui_language_from_preferred_languages(
    std::span<const std::wstring_view> languages) noexcept;
[[nodiscard]] UiLanguage resolve_ui_language(UiLanguagePreference preference);
[[nodiscard]] UiLanguage resolve_ui_language();
[[nodiscard]] UiLanguagePreference current_ui_language_preference() noexcept;
void set_ui_language_preference(UiLanguagePreference preference) noexcept;
[[nodiscard]] HFONT create_system_ui_font(UINT dpi);
}

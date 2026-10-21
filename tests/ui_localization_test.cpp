#include "platform/windows/ui_localization.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace clipss2::platform::windows;

namespace
{
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
}

int main()
{
    for (std::size_t index = 0; index < static_cast<std::size_t>(UiStringId::count); ++index)
    {
        const auto id = static_cast<UiStringId>(index);
        require(!ui_string(UiLanguage::english, id).empty(), "English catalog entry missing");
        require(!ui_string(UiLanguage::japanese, id).empty(), "Japanese catalog entry missing");
    }
    require(ui_string(UiLanguage::english, UiStringId::tray_capture_window) == L"Capture Window",
        "English wording changed");
    require(ui_string(UiLanguage::japanese, UiStringId::tray_capture_region) == L"領域をキャプチャ",
        "Japanese source literal round-trip failed");
    require(ui_string(UiLanguage::english, UiStringId::settings_title) == L"Settings" &&
        ui_string(UiLanguage::english, UiStringId::settings_filename_sequence) == L"Sequential number" &&
        ui_string(UiLanguage::english, UiStringId::settings_language_system) == L"System default",
        "English Settings wording changed");
    require(ui_string(UiLanguage::english, UiStringId::format_webp) == L"F: File format: WebP" &&
        ui_string(UiLanguage::japanese, UiStringId::format_webp) == L"F: 形式: WebP",
        "WebP format name/localized HUD prefix changed");
    require(ui_string(UiLanguage::japanese, UiStringId::settings_title) == L"設定" &&
        ui_string(UiLanguage::japanese, UiStringId::settings_filename_sequence) == L"通し番号" &&
        ui_string(UiLanguage::japanese, UiStringId::settings_language_system) == L"システム設定",
        "Japanese Settings wording changed");
    bool unknown_rejected{};
    constexpr std::array labels{
        std::pair{UiStringId::settings_save_location, std::pair{L"Capture image folder", L"キャプチャ画像の保存先"}},
        std::pair{UiStringId::settings_file_naming, std::pair{L"File naming rules", L"ファイル命名規則"}},
        std::pair{UiStringId::settings_language, std::pair{L"Display language", L"表示言語"}},
        std::pair{UiStringId::settings_file_section, std::pair{L"Settings file", L"設定ファイル"}},
        std::pair{UiStringId::settings_file_location, std::pair{L"Settings file location", L"設定ファイルの保存先"}},
        std::pair{UiStringId::settings_open_folder, std::pair{L"Open settings folder", L"設定の保存先フォルダーを開く"}},
        std::pair{UiStringId::settings_folder_error_title, std::pair{L"Settings folder", L"設定フォルダー"}}};
    for (const auto& [id, translations] : labels)
        require(ui_string(UiLanguage::english, id) == translations.first &&
            ui_string(UiLanguage::japanese, id) == translations.second, "Settings grouping translation mismatch");
    try { (void)ui_string(UiLanguage::english, static_cast<UiStringId>(999)); }
    catch (const std::out_of_range&) { unknown_rejected = true; }
    require(unknown_rejected, "Unknown string ID was not rejected");

    constexpr std::array ja{std::wstring_view{L"ja-JP"}};
    constexpr std::array ja_primary{std::wstring_view{L"ja"}};
    constexpr std::array en{std::wstring_view{L"en-US"}};
    require(ui_language_from_preferred_languages(ja) == UiLanguage::japanese, "ja-JP detection failed");
    require(ui_language_from_preferred_languages(ja_primary) == UiLanguage::japanese, "ja detection failed");
    require(ui_language_from_preferred_languages(en) == UiLanguage::english, "non-ja fallback failed");
    require(ui_language_from_preferred_languages({}) == UiLanguage::english, "empty fallback failed");

    set_ui_language_preference(UiLanguagePreference::english);
    require(current_ui_language_preference() == UiLanguagePreference::english &&
        resolve_ui_language() == UiLanguage::english, "English preference failed");
    set_ui_language_preference(UiLanguagePreference::japanese);
    require(current_ui_language_preference() == UiLanguagePreference::japanese &&
        resolve_ui_language() == UiLanguage::japanese, "Japanese preference failed");
    set_ui_language_preference(UiLanguagePreference::system);

    _wputenv_s(L"_CLIPSS2_AUDIT_UI_LANGUAGE", L"en");
    require(resolve_ui_language() == UiLanguage::english, "en override failed");
    _wputenv_s(L"_CLIPSS2_AUDIT_UI_LANGUAGE", L"ja");
    require(resolve_ui_language() == UiLanguage::japanese, "ja override failed");
    _wputenv_s(L"_CLIPSS2_AUDIT_UI_LANGUAGE", L"invalid");
    bool invalid_rejected{};
    try { (void)resolve_ui_language(); }
    catch (const std::invalid_argument&) { invalid_rejected = true; }
    require(invalid_rejected, "invalid override did not fail safely");
    _wputenv_s(L"_CLIPSS2_AUDIT_UI_LANGUAGE", L"invalid-and-too-long");
    invalid_rejected = false;
    try { (void)resolve_ui_language(); }
    catch (const std::invalid_argument&) { invalid_rejected = true; }
    require(invalid_rejected, "oversized invalid override did not fail safely");
    _wputenv_s(L"_CLIPSS2_AUDIT_UI_LANGUAGE", L"");
    set_ui_language_preference(UiLanguagePreference::system);

    HFONT font = create_system_ui_font(192);
    require(font != nullptr, "system UI font unavailable");
    HDC dc = CreateCompatibleDC(nullptr);
    require(dc != nullptr, "measurement DC unavailable");
    const auto old = SelectObject(dc, font);
    SIZE extent{};
    const auto japanese = ui_string(UiLanguage::japanese, UiStringId::window_selection_title);
    require(GetTextExtentPoint32W(dc, japanese.data(), static_cast<int>(japanese.size()), &extent) &&
        extent.cx > 0 && extent.cy > 0, "Japanese measurement failed");
    std::array<WORD, 8> glyphs{};
    require(GetGlyphIndicesW(dc, L"日本語領域", 5, glyphs.data(), GGI_MARK_NONEXISTING_GLYPHS) != GDI_ERROR,
        "Japanese glyph query failed");
    for (std::size_t index = 0; index < 5; ++index)
        require(glyphs[index] != 0xffff, "Japanese glyph missing");
    SelectObject(dc, old); DeleteDC(dc); DeleteObject(font);
    for (const UINT dpi : {96U,192U})
    for (const auto language : {UiLanguage::english,UiLanguage::japanese})
    for (const auto format : {clipss2::image::ImageFileFormat::avif,clipss2::image::ImageFileFormat::jxl})
    {
        font = create_system_ui_font(dpi); require(font != nullptr,"profile font missing");
        dc = CreateCompatibleDC(nullptr); require(dc != nullptr,"profile DC missing");
        const auto previous = SelectObject(dc,font);
        const auto row = codec_profile_hud(language,format,
            clipss2::capture::parse_file_compression("lossy-balanced"));
        require(row.find(format == clipss2::image::ImageFileFormat::avif ? L"AVIF" : L"JPEG XL") != row.npos &&
            row.find(ui_string(language,UiStringId::compression_balanced)) != row.npos,"incomplete profile HUD");
        RECT measured{};
        require(DrawTextW(dc,row.c_str(),-1,&measured,DT_CALCRECT|DT_NOPREFIX)>0 &&
            measured.right>0 && measured.bottom>0,"profile layout measurement failed");
        const int padding = MulDiv(12,static_cast<int>(dpi),96);
        RECT painted{padding,padding,measured.right+padding,measured.bottom+padding};
        require(DrawTextW(dc,row.c_str(),-1,&painted,DT_CALCRECT|DT_NOPREFIX)==measured.bottom &&
            painted.right == measured.right+padding,"profile text changed size inside padded client");
        std::cout << "Codec profile layout: dpi=" << dpi << " language=" << (language==UiLanguage::english?"en":"ja")
            << " codec=" << clipss2::image::image_file_format_token(format)
            << " client=" << measured.right+padding*2 << 'x' << measured.bottom+padding*2 << '\n';
        SelectObject(dc,previous); DeleteDC(dc); DeleteObject(font);
    }
    std::cout << "Localization catalogs/detection/override/UTF-8/glyph measurement: passed\n";
}

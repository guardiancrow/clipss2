#include "audit/audit_policy.h"
#include "platform/windows/ui_localization.h"

#include <array>
#include <atomic>
#include <cwctype>
#include <stdexcept>
#include <vector>

namespace clipss2::platform::windows
{
namespace
{
using Catalog = std::array<std::wstring_view, static_cast<std::size_t>(UiStringId::count)>;

constexpr Catalog english{
    L"clipss2 - Window selection", L"clipss2 - Region selection", L"clipss2 - Monitor selection",
    L"No live selectable window", L"Release: Cancel", L"Release to capture", L"Release to confirm",
    L"Click / Enter: Capture", L"Click / Enter: Confirm", L"Tab / Shift+Tab: Select",
    L"C: Cursor On", L"C: Cursor Off", L"O: File", L"O: Clipboard", L"O: Both",
    L"F: File format: PNG", L"F: File format: JPEG", L"F: File format: WebP", L" (file only)", L"Output: Clipboard",
    L"Esc: Cancel", L"Region: drag to capture", L"Monitor ", L"pixels",
    L"Tab / Shift+Tab: Select", L"Enter / Click: Confirm", L"Default", L"Fast", L"Balanced",
    L"Smallest", L"Encoder", L"Auto", L"unavailable", L"Capture Window", L"Capture Region",
    L"Capture Monitor", L"Settings...", L"Exit", L"Choose capture output folder",
    L"Settings", L"Capture image folder", L"Change...", L"Reset to default", L"Not selected",
    L"File naming rules", L"Date and time", L"Sequential number", L"GUID", L"Display language",
    L"System default", L"English", L"Japanese", L"OK", L"Cancel",
    L"Could not save settings", L"Settings from a newer version are protected. Use a compatible version and preserve the original file.",
    L"Settings are already managed by the running clipss2 resident instance.",
    L"Lossless", L"Lossy High", L"Lossy Balanced", L"Lossy Small",
    L"clipss2 is ready", L"Start a capture from the system tray.",
    L"Cannot resolve the settings location. Check settings.conf / clipss2.portable and folder access. Restart after fixing the problem.",
    L"Settings file", L"Mode: Per-user", L"Mode: Portable", L"Mode: Unresolved",
    L"Settings file location", L"File: Not created (created when settings are saved)",
    L"File: Exists (write access is not verified)", L"File: Cannot verify existence",
    L"File: The path is a directory, not a settings file", L"Open settings folder",
    L"The parent folder is missing or cannot be verified. It will not be created by this button.",
    L"Could not open the folder. Check that it exists, is accessible, and Windows Shell is available.",
    L"Settings could not be read. Saving is disabled for this run. Check the file and its access, then restart.",
    L"clipss2.portable is invalid or unreadable. Check the marker and folder access, then restart. No other settings location will be used.",
    L"The settings file exceeds the size limit. Saving is disabled to protect it. Back it up and check it before restarting.",
    L"Saving is disabled to protect the settings. Check the file before restarting.",
    L"The saved settings could not be applied. Safe defaults are in use; saving will replace them with the displayed choices.",
    L"Save failed: Changes in this dialog are still unsaved.",
    L"Could not save settings. Your changes remain in this dialog. Check folder permissions, read-only files, available space, and media or network access. No other settings location was used.",
    L"Settings folder"
};

constexpr Catalog japanese{
    L"clipss2 - ウィンドウの選択", L"clipss2 - 領域の選択", L"clipss2 - モニターの選択",
    L"選択できるウィンドウがありません", L"離す: キャンセル", L"離すとキャプチャ", L"離すと確定",
    L"クリック / Enter: キャプチャ", L"クリック / Enter: 確定", L"Tab / Shift+Tab: 選択",
    L"C: カーソル オン", L"C: カーソル オフ", L"O: 保存先: ファイル", L"O: 保存先: クリップボード",
    L"O: 保存先: 両方", L"F: 形式: PNG", L"F: 形式: JPEG", L"F: 形式: WebP", L" (ファイルのみ)",
    L"保存先: クリップボード", L"Esc: キャンセル", L"ドラッグして領域を選択", L"モニター ",
    L"ピクセル", L"Tab: 次 / Shift+Tab: 前", L"Enter / クリック: 確定", L"既定", L"高速",
    L"バランス", L"最小サイズ", L"エンコーダー", L"自動", L"利用不可",
    L"ウィンドウをキャプチャ", L"領域をキャプチャ", L"モニターをキャプチャ", L"設定...", L"終了",
    L"キャプチャの保存先フォルダーを選択", L"設定", L"キャプチャ画像の保存先", L"変更...", L"既定に戻す",
    L"未選択", L"ファイル命名規則", L"日時", L"通し番号", L"GUID", L"表示言語", L"システム設定",
    L"English", L"日本語", L"OK", L"キャンセル", L"設定を保存できませんでした",
    L"新しいバージョンで作成された設定は変更できません。対応する版を使用し、元ファイルを保全してください。",
    L"設定は実行中の clipss2 常駐プロセスで管理されています。",
    L"可逆", L"非可逆・高品質", L"非可逆・バランス", L"非可逆・小サイズ",
    L"clipss2 が起動しました", L"タスクトレイからキャプチャを開始できます。",
    L"設定保存先を解決できません。settings.conf / clipss2.portable とフォルダーへのアクセスを確認し、修正後に再起動してください。",
    L"設定ファイル", L"動作モード: 通常", L"動作モード: ポータブル", L"動作モード: 未解決",
    L"設定ファイルの保存先", L"ファイル: 未作成（設定の保存時に作成されます）",
    L"ファイル: 存在します（書き込み可否は未確認）", L"ファイル: 存在を確認できません",
    L"ファイル: 設定ファイルの位置にフォルダーがあります", L"設定の保存先フォルダーを開く",
    L"親フォルダーが未作成、または存在を確認できません。この操作では作成しません。",
    L"フォルダーを開けませんでした。存在とアクセス、およびWindows Shellの利用可否を確認してください。",
    L"設定を読み込めなかったため、この実行中は保存できません。ファイルとアクセスを確認し、再起動してください。",
    L"clipss2.portableが不正、または読み取れません。マーカーとフォルダーへのアクセスを確認し、再起動してください。別の設定先へは切り替えません。",
    L"設定ファイルがサイズ上限を超えています。保護のため保存できません。バックアップして内容を確認し、再起動してください。",
    L"設定を保護するため保存できません。ファイルを確認し、再起動してください。",
    L"保存済み設定を適用できず、初期値を使用しています。保存すると画面の選択内容へ置き換わります。",
    L"保存失敗: この画面の変更は未保存です。",
    L"設定を保存できませんでした。変更内容はこの画面に残っています。フォルダーのアクセス権、読み取り専用属性、空き容量、媒体やネットワークを確認してください。別の設定先へは保存していません。",
    L"設定フォルダー"
};

std::atomic active_preference{UiLanguagePreference::system};

bool primary_is_japanese(std::wstring_view language) noexcept
{
    const auto separator = language.find_first_of(L"-_");
    language = language.substr(0, separator);
    return language.size() == 2 && std::towlower(language[0]) == L'j' &&
        std::towlower(language[1]) == L'a';
}
}

std::wstring_view ui_string(const UiLanguage language, const UiStringId id)
{
    const auto index = static_cast<std::size_t>(id);
    if (index >= english.size()) throw std::out_of_range("Unknown UI string ID.");
    return language == UiLanguage::japanese ? japanese[index] : english[index];
}

std::wstring codec_profile_hud(UiLanguage language, image::ImageFileFormat format,
    capture::FileCompression value)
{
    if (format != image::ImageFileFormat::avif && format != image::ImageFileFormat::jxl)
        throw std::invalid_argument("Codec profile HUD supports AVIF/JXL only.");
    (void)capture::file_compression_token(value);
    UiStringId id = UiStringId::compression_lossless;
    if (value.mode == capture::FileCompressionMode::lossy)
    {
        switch (value.profile)
        {
        case codec::LossyProfile::high: id = UiStringId::compression_high; break;
        case codec::LossyProfile::balanced: id = UiStringId::compression_balanced; break;
        case codec::LossyProfile::small_file: id = UiStringId::compression_small; break;
        }
    }
    return std::wstring{format == image::ImageFileFormat::avif ? L"P: AVIF \u2014 " : L"P: JPEG XL \u2014 "} +
        std::wstring{ui_string(language, id)};
}

UiLanguage ui_language_from_preferred_languages(
    const std::span<const std::wstring_view> languages) noexcept
{
    for (const auto language : languages)
        if (!language.empty()) return primary_is_japanese(language)
            ? UiLanguage::japanese : UiLanguage::english;
    return UiLanguage::english;
}

UiLanguage resolve_ui_language(const UiLanguagePreference preference)
{
    if (preference == UiLanguagePreference::english) return UiLanguage::english;
    if (preference == UiLanguagePreference::japanese) return UiLanguage::japanese;

    wchar_t override_value[8]{};
    const DWORD override_length = clipss2::audit::environment_variable(
        L"_CLIPSS2_AUDIT_UI_LANGUAGE", override_value,
        static_cast<DWORD>(std::size(override_value)));
    if (override_length != 0)
    {
        if (override_length >= std::size(override_value))
            throw std::invalid_argument("Invalid _CLIPSS2_AUDIT_UI_LANGUAGE value; expected en or ja.");
        const std::wstring_view value{override_value, override_length};
        if (value == L"en") return UiLanguage::english;
        if (value == L"ja") return UiLanguage::japanese;
        throw std::invalid_argument("Invalid _CLIPSS2_AUDIT_UI_LANGUAGE value; expected en or ja.");
    }

    ULONG count{};
    ULONG size{};
    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) || size == 0)
        return UiLanguage::english;
    std::vector<wchar_t> buffer(size);
    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, buffer.data(), &size) || count == 0)
        return UiLanguage::english;
    return ui_language_from_preferred_languages(
        std::array<std::wstring_view, 1>{std::wstring_view{buffer.data()}});
}

UiLanguage resolve_ui_language()
{
    return resolve_ui_language(current_ui_language_preference());
}

UiLanguagePreference current_ui_language_preference() noexcept
{
    return active_preference.load(std::memory_order_relaxed);
}

void set_ui_language_preference(const UiLanguagePreference value) noexcept
{
    active_preference.store(value, std::memory_order_relaxed);
}

HFONT create_system_ui_font(const UINT dpi)
{
    NONCLIENTMETRICSW metrics{sizeof(metrics)};
    if (!SystemParametersInfoForDpi(
            SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0,
            dpi == 0 ? 96 : dpi))
        return nullptr;
    metrics.lfMessageFont.lfCharSet = DEFAULT_CHARSET;
    return CreateFontIndirectW(&metrics.lfMessageFont);
}
}

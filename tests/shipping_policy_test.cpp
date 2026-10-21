#include "audit/audit_policy.h"
#include "platform/windows/capture_worker_protocol.h"
#include "platform/windows/output_storage.h"
#include "platform/windows/ui_localization.h"

#include <filesystem>
#include <string_view>
#include <vector>
#include <stdexcept>

static_assert(CLIPSS2_AUDIT_FEATURES == 0);
namespace
{
template<class F> bool rejects(F action)
{
    try { action(); } catch (const std::invalid_argument&) { return true; }
    return false;
}
auto parse(const std::vector<std::string>& args)
{
    std::vector<std::string_view> views{args.begin(), args.end()};
    return clipss2::platform::windows::parse_capture_worker_request(views);
}
}
int main()
{
    using namespace clipss2::platform::windows;
    const auto original_store = default_settings_store();
    const wchar_t* names[]{
        L"_CLIPSS2_AUDIT_SETTINGS_PATH", L"_CLIPSS2_AUDIT_EXE_DIRECTORY",
        L"_CLIPSS2_AUDIT_UI_LANGUAGE", L"_CLIPSS2_AUDIT_ARTIFACT_DIR"};
    for (const auto name : names)
    {
        if (!SetEnvironmentVariableW(name, L"invalid")) return 1;
        wchar_t buffer[32]{};
        if (clipss2::audit::environment_variable(name, buffer, 32) != 0) return 1;
    }
    const auto poisoned_store = default_settings_store();
    if (poisoned_store.path != original_store.path ||
        poisoned_store.executable_directory != original_store.executable_directory) return 1;
    if (resolve_ui_language(UiLanguagePreference::english) != UiLanguage::english ||
        resolve_ui_language(UiLanguagePreference::japanese) != UiLanguage::japanese) return 1;
    (void)resolve_ui_language(UiLanguagePreference::system); // Invalid audit override is ignored.

    CaptureWorkerRequest request;
    request.hwnd_value = 1; request.expected_process_id = 1; request.expected_class = L"owned";
    request.file_format = clipss2::image::ImageFileFormat::png;
    request.destination = clipss2::capture::CaptureDestination::file;
    if (!rejects([&] { (void)serialize_capture_worker_request(request); })) return 1;
    const std::vector<std::string> legacy{"1", "1", "0041", "off", "system-required", "file", "png", "quiet"};
    if (!rejects([&] { (void)parse(legacy); })) return 1;
    auto clipboard = legacy; clipboard[5] = "clipboard";
    if (parse(clipboard).file_output) return 1;
    request.file_output = FileOutputOptions{std::filesystem::path{L"C:\\isolated"}, FilenamePolicy::timestamp, true};
    for (const auto destination : {clipss2::capture::CaptureDestination::file, clipss2::capture::CaptureDestination::both})
    {
        request.destination = destination;
        const auto decoded = parse(serialize_capture_worker_request(request));
        if (decoded.file_output != request.file_output || decoded.destination != destination) return 1;
    }
    for (const auto name : names) (void)SetEnvironmentVariableW(name, nullptr);
    return 0;
}

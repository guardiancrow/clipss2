#include "platform/windows/settings_location.h"
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
#include <future>
#include <thread>

namespace clipss2::platform::windows
{
namespace
{
bool resolved(const SettingsStore& store) noexcept
{
    try { validate_output_path(store.path); return !store.path.parent_path().empty(); }
    catch (...) { return false; }
}
SettingsMetadata observe(const std::filesystem::path& path, const SettingsMetadataProbe& probe) noexcept
{
    try { return probe ? probe(path) : settings_metadata(path); }
    catch (...) { return {SettingsPresence::unknown, ERROR_GEN_FAILURE}; }
}
UiStringId guidance(const SettingsStore& store, const SettingsLoadResult& loaded)
{
    if (!resolved(store))
        // Existing resolver diagnostics have no structured reason code. Only
        // recognize its own fixed marker prefix; never infer access denial.
        return loaded.diagnostic.starts_with("ReadFailure/path: Portable marker")
            ? UiStringId::settings_marker_error : UiStringId::settings_location_read_only;
    if (loaded.status == SettingsStatus::read_failure) return UiStringId::settings_read_error;
    if (loaded.status == SettingsStatus::unsupported_schema) return UiStringId::settings_newer_schema_read_only;
    if (!loaded.persistence_enabled)
        return loaded.diagnostic == "InvalidSettings: oversized file"
            ? UiStringId::settings_oversized : UiStringId::settings_protected;
    if (loaded.status != SettingsStatus::loaded && loaded.status != SettingsStatus::no_settings)
        return UiStringId::settings_defaults_warning;
    return UiStringId::count;
}
HRESULT shell_folder(const std::filesystem::path& parent)
{
    // The product main is MTA. Shell extensions may require STA. Only an
    // explicit button action creates this short-lived task; no startup thread.
    std::packaged_task<HRESULT()> task{[parent] {
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (FAILED(initialized)) return initialized;
        struct Uninitialize { ~Uninitialize() { CoUninitialize(); } } uninitialize;
        PIDLIST_ABSOLUTE folder{};
        SFGAOF attributes{};
        const HRESULT parsed = SHParseDisplayName(parent.c_str(), nullptr, &folder, SFGAO_FOLDER | SFGAO_FILESYSTEM, &attributes);
        struct FreeFolder { PIDLIST_ABSOLUTE value; ~FreeFolder() { CoTaskMemFree(value); } } free_folder{folder};
        if (FAILED(parsed)) return parsed;
        if (!folder || (attributes & (SFGAO_FOLDER | SFGAO_FILESYSTEM)) != (SFGAO_FOLDER | SFGAO_FILESYSTEM)) return E_INVALIDARG;
        SHELLEXECUTEINFOW info{sizeof(info)};
        info.fMask = SEE_MASK_IDLIST | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        info.lpVerb = L"open";
        info.lpIDList = folder; // Parsed filesystem folder only; no command line, file association, or parameters.
        info.nShow = SW_SHOWNORMAL;
        if (ShellExecuteExW(&info)) return S_OK;
        const DWORD error = GetLastError();
        return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
    }};
    auto result = task.get_future();
    std::jthread thread{std::move(task)};
    std::optional<int> quit;
    while (result.wait_for(std::chrono::milliseconds{20}) != std::future_status::ready)
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT) quit = static_cast<int>(message.wParam);
            else { TranslateMessage(&message); DispatchMessageW(&message); }
        }
    }
    if (quit) PostQuitMessage(*quit);
    return result.get();
}
}
SettingsMetadata settings_metadata(const std::filesystem::path& path) noexcept
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES)
        return {(attributes & FILE_ATTRIBUTE_DIRECTORY) ? SettingsPresence::directory : SettingsPresence::file, 0};
    const DWORD error = GetLastError();
    return {(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        ? SettingsPresence::missing : SettingsPresence::unknown, error};
}
SettingsLocationView settings_location_view(const SettingsStore& store,
    const SettingsLoadResult& loaded, const SettingsMetadataProbe& probe)
{
    SettingsLocationView view;
    view.guidance = guidance(store, loaded);
    if (!resolved(store)) return view;
    view.resolved = true; view.path = store.path;
    view.mode = store.mode == SettingsMode::portable ? UiStringId::settings_mode_portable : UiStringId::settings_mode_normal;
    view.file = observe(store.path, probe);
    view.parent = observe(store.path.parent_path(), probe);
    return view;
}
UiStringId settings_presence_text(SettingsPresence presence) noexcept
{
    switch (presence)
    {
    case SettingsPresence::missing: return UiStringId::settings_file_missing;
    case SettingsPresence::file: return UiStringId::settings_file_exists;
    case SettingsPresence::directory: return UiStringId::settings_file_not_regular;
    default: return UiStringId::settings_file_unknown;
    }
}
HRESULT open_settings_parent(const SettingsStore& store, bool requested,
    const SettingsMetadataProbe& probe, const SettingsFolderShell& shell) noexcept
{
    if (!requested) return S_FALSE;
    try
    {
        if (!resolved(store)) return E_INVALIDARG;
        const auto parent = store.path.parent_path();
        const auto metadata = observe(parent, probe);
        if (metadata.presence != SettingsPresence::directory)
            return HRESULT_FROM_WIN32(metadata.error ? metadata.error : ERROR_DIRECTORY);
        return shell ? shell(parent) : shell_folder(parent);
    }
    catch (...) { return E_FAIL; }
}
}

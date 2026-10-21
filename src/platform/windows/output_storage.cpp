#include "audit/audit_policy.h"
#include "platform/windows/output_storage.h"
#include "platform/windows/portable_settings_marker.h"
#include "platform/windows/ui_localization.h"
#include "platform/windows/unicode_diagnostics.h"
#include <Windows.h>
#include <ShlObj.h>
#include <winrt/base.h>
#include <future>
#include <chrono>
#include <thread>
#include <memory>
#include <vector>
#include <limits>
#include <iostream>
#include <array>

namespace clipss2::platform::windows
{
namespace
{
std::optional<DWORD> settings_attributes(const std::filesystem::path& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) return attributes;
    const DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return {};
    throw std::runtime_error("Settings location attribute probe failed: " + std::to_string(error));
}
void validate_portable_marker(const std::filesystem::path& path, DWORD attributes)
{
    constexpr DWORD forbidden = FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT;
    if (attributes & forbidden) throw std::runtime_error("Portable marker must be a regular non-reparse file.");
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Portable marker cannot be opened: " + std::to_string(GetLastError()));
    struct Close { HANDLE file; ~Close() { CloseHandle(file); } } close{file};
    BY_HANDLE_FILE_INFORMATION info{};
    LARGE_INTEGER size{};
    if (GetFileType(file) != FILE_TYPE_DISK || !GetFileInformationByHandle(file, &info) ||
        (info.dwFileAttributes & forbidden) || !GetFileSizeEx(file, &size) ||
        size.QuadPart != static_cast<LONGLONG>(portable_settings_marker_content.size()))
        throw std::runtime_error("Portable marker type or length is invalid.");
    std::array<char, portable_settings_marker_content.size() + 1> bytes{};
    DWORD read{};
    if (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) ||
        std::string_view{bytes.data(), read} != portable_settings_marker_content)
        throw std::runtime_error("Portable marker content is invalid or unreadable (expected clipss2-portable-v1).");
}
std::filesystem::path known_folder(REFKNOWNFOLDERID id)
{
    PWSTR raw{};
    const auto result = SHGetKnownFolderPath(id, 0, nullptr, &raw);
    const auto release = [](wchar_t* p) { CoTaskMemFree(p); };
    const std::unique_ptr<wchar_t, decltype(release)> owned{raw, release};
    if (FAILED(result) || !owned) throw std::runtime_error("Known Folder resolution failed: " + std::to_string(result));
    return owned.get();
}
std::optional<std::filesystem::path> audit_path(const wchar_t* name)
{
#if CLIPSS2_AUDIT_FEATURES
    SetLastError(ERROR_SUCCESS);
    const DWORD length = clipss2::audit::environment_variable(name, nullptr, 0);
    if (!length)
    {
        if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) return {};
        throw std::runtime_error("Empty or inaccessible audit storage path.");
    }
    std::wstring text(length, L'\0');
    const DWORD count = clipss2::audit::environment_variable(name, text.data(), length);
    if (!count || count >= length) throw std::runtime_error("Audit storage path changed while resolving.");
    text.resize(count);
    std::filesystem::path path{text}; validate_output_path(path); return path;
#else
    (void)name;
    return {};
#endif
}
}
std::string path_utf8(const std::filesystem::path& path)
{
    return utf8_from_utf16(path.native());
}
std::filesystem::path path_from_utf8(std::string_view bytes)
{
    if (bytes.empty()) return {};
    if (bytes.size() > INT_MAX) throw std::length_error("Path exceeds conversion bounds.");
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (!length) throw std::invalid_argument("Invalid UTF-8 path.");
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), wide.data(), length) != length)
        throw std::invalid_argument("Path UTF-16 conversion failed.");
    return wide;
}
SettingsStore resolve_settings_store(const std::filesystem::path& exe, const std::function<std::filesystem::path()>& per_user,
    const SettingsAttributeProbe& probe)
{
    validate_output_path(exe);
    const auto local = exe / L"settings.conf";
    const auto attributes = [&](const auto& path) { return probe ? probe(path) : settings_attributes(path); };
    if (attributes(local)) return {SettingsMode::portable, local, exe};
    const auto marker = exe / portable_settings_marker_name;
    if (const auto present = attributes(marker))
    {
        validate_portable_marker(marker, *present);
        return {SettingsMode::portable, local, exe};
    }
    const auto path = per_user(); validate_output_path(path);
    return {SettingsMode::per_user, path, exe};
}
std::filesystem::path settings_executable_directory(std::wstring_view buffer, DWORD count)
{
    if (!count || count >= buffer.size()) throw std::runtime_error("Executable path resolution failed.");
    const auto path = std::filesystem::path{buffer.substr(0, count)}.parent_path();
    validate_output_path(path);
    return path;
}
SettingsStore default_settings_store()
{
    // Private isolated store bypasses both real locations; optional fake exe tree
    // exercises actual portable precedence without modifying the built binary tree.
    const auto fake_exe = audit_path(L"_CLIPSS2_AUDIT_EXE_DIRECTORY");
    const auto isolated = audit_path(L"_CLIPSS2_AUDIT_SETTINGS_PATH");
    std::filesystem::path exe;
    if (fake_exe) exe = *fake_exe;
    else
    {
        std::vector<wchar_t> buffer(32768);
        const DWORD count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        exe = settings_executable_directory(std::wstring_view{buffer.data(), buffer.size()}, count);
    }
    if (isolated && !fake_exe) return {SettingsMode::per_user, *isolated, exe};
    return resolve_settings_store(exe, [&] { return isolated ? *isolated : known_folder(FOLDERID_LocalAppData) / L"clipss2" / L"settings.conf"; });
}
std::filesystem::path resolved_pictures_directory() { return known_folder(FOLDERID_Pictures); }
std::optional<FileOutputOptions> resolve_file_output(const SettingsStore& store,
    OutputDirectoryPreference& preference, FilenamePolicy policy, const FolderPicker& picker,
    const std::function<std::filesystem::path()>& initial_location)
{
    (void)filename_policy_token(policy);
    validate_output_path(store.executable_directory); // Failed store resolution must not invoke a fallback picker.
    if (!preference.explicit_path && store.mode == SettingsMode::portable)
        return FileOutputOptions{store.executable_directory / L"captures", policy, true};
    if (preference.explicit_path)
    {
        validate_output_path(*preference.explicit_path);
        const DWORD attrs = GetFileAttributesW(preference.explicit_path->c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
            return FileOutputOptions{*preference.explicit_path, policy, false};
        const DWORD error = attrs == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
        if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
            // Access/metadata errors do not prove disappearance or writeability.
            // Preserve configured intent; native publication reports the actual
            // File failure, with no picker/fallback and normal accepted commit.
            return FileOutputOptions{*preference.explicit_path, policy, false};
    }
    const auto chosen = picker(initial_location());
    if (!chosen) return {};
    validate_output_path(*chosen);
    const DWORD attrs = GetFileAttributesW(chosen->c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY))
        throw std::runtime_error("Selected output folder is not an existing directory.");
    preference.explicit_path = *chosen;
    return FileOutputOptions{*chosen, policy, false};
}
std::optional<std::filesystem::path> pick_output_folder_owned(
    const std::filesystem::path& initial, const HWND owner)
{
    // Product/worker main is MTA. Shell dialog runs on a bounded-lifetime STA
    // thread with its own modal loop; no selector objects or borrowed state.
    std::packaged_task<std::optional<std::filesystem::path>()> task{[initial, owner] {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
        auto dialog = winrt::create_instance<IFileOpenDialog>(CLSID_FileOpenDialog);
        FILEOPENDIALOGOPTIONS flags{}; winrt::check_hresult(dialog->GetOptions(&flags));
        winrt::check_hresult(dialog->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
            FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR | FOS_DONTADDTORECENT));
        winrt::check_hresult(dialog->SetTitle(
            ui_string(resolve_ui_language(), UiStringId::output_folder_title).data()));
        winrt::com_ptr<IShellItem> initial_item;
        winrt::check_hresult(SHCreateItemFromParsingName(initial.c_str(), nullptr, __uuidof(IShellItem), initial_item.put_void()));
        winrt::check_hresult(dialog->SetFolder(initial_item.get()));
        std::cout << "Output folder picker: opened\n";
        const HRESULT shown = dialog->Show(owner);
        if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return std::optional<std::filesystem::path>{};
        winrt::check_hresult(shown);
        winrt::com_ptr<IShellItem> item; winrt::check_hresult(dialog->GetResult(item.put()));
        PWSTR raw{}; const auto result = item->GetDisplayName(SIGDN_FILESYSPATH, &raw);
        const auto release = [](wchar_t* p) { CoTaskMemFree(p); };
        const std::unique_ptr<wchar_t, decltype(release)> path{raw, release};
        winrt::check_hresult(result);
        if (!path) throw std::runtime_error("Folder picker returned no filesystem path.");
        return std::optional<std::filesystem::path>{path.get()};
    }};
    auto result = task.get_future(); std::jthread thread{std::move(task)};
    struct QuitRepost
    {
        std::optional<int> code;
        ~QuitRepost() { if (code) PostQuitMessage(*code); }
    } quit;
    // Keep the resident's existing busy/shutdown/tray arbitration responsive.
    // No selector owner is retained; this nested loop only dispatches main-thread
    // messages. The owned STA task is joined before any caller state can die.
    while (result.wait_for(std::chrono::milliseconds{20}) != std::future_status::ready)
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT) quit.code = static_cast<int>(message.wParam);
            else { TranslateMessage(&message); DispatchMessageW(&message); }
        }
    }
    return result.get();
}
std::optional<std::filesystem::path> pick_output_folder(
    const std::filesystem::path& initial)
{
    return pick_output_folder_owned(initial, nullptr);
}
}

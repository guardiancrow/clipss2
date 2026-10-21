#include "platform/windows/settings_location.h"
#include "platform/windows/settings_layout.h"
#include <array>
#include <fstream>
#include <iostream>
#include <source_location>
using namespace clipss2::platform::windows;
namespace
{
void require(bool value, std::source_location where = std::source_location::current())
{ if (!value) throw std::runtime_error("Settings location assertion: " + std::to_string(where.line())); }
struct Temp
{
    std::filesystem::path path;
    Temp()
    {
        std::array<wchar_t, MAX_PATH> folder{}, name{};
        require(GetTempPathW(static_cast<DWORD>(folder.size()), folder.data()) != 0);
        require(GetTempFileNameW(folder.data(), L"c2s", 0, name.data()) != 0);
        require(DeleteFileW(name.data()) != FALSE); path = name.data();
        require(std::filesystem::create_directory(path));
    }
    ~Temp() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};
}
int main()
{
    try
    {
        // Position ownership is independent of monitor origin and DPI direction.
        for (const RECT suggested : {RECT{1820, 40, 2440, 840}, RECT{-900, -600, 320, 100},
                 RECT{700, 800, 1920, 2200}, RECT{1450, -100, 2070, 700}})
        {
            for (const RECT work : {RECT{0, 0, 1600, 900}, RECT{1600, 0, 3200, 900},
                     RECT{-1600, -900, 0, 0}, RECT{0, 900, 1600, 1800}})
            {
                const RECT result = settings_transition_rect(suggested, work, 32);
                require(result.left == suggested.left && result.top == suggested.top);
                require(result.right > result.left && result.bottom > result.top);
                require(result.right - result.left <= 1536 && result.bottom - result.top <= 836);
                require(settings_same_rect(result, settings_transition_rect(result, work, 32)));
            }
        }
        for (const UINT dpi : {96U, 120U, 144U, 160U, 192U})
        {
            const RECT input{1450, -40, 2070, 760}, work{1600, 0, 3200, 900};
            const auto result = settings_transition_rect(input, work, MulDiv(16, dpi, 96));
            require(result.left == 1450 && result.top == -40); // Boundary straddling is valid.
        }
        SettingsLayoutQueue queue;
        const SettingsLayoutRequest high{192, RECT{-1200, 800, 0, 1800}, RECT{0, 0, 1600, 900}};
        const SettingsLayoutRequest low{96, RECT{1450, -40, 2070, 760}, RECT{1600, 0, 3200, 900}};
        for (unsigned round = 0; round != 20; ++round)
        {
            queue.submit(high); require(!queue.busy());
            require(settings_same_request(*queue.take(), high) && queue.busy());
            queue.submit(high); // Identical synchronous echo must not perpetuate layout.
            queue.finish(); require(!queue.take());
            queue.submit(low); require(settings_same_request(*queue.take(), low));
            queue.submit(high); queue.submit(low); queue.submit(high); // Latest nested notification wins.
            queue.finish(); require(settings_same_request(*queue.take(), high));
            queue.finish(); require(!queue.take() && !queue.busy());
        }
        require(settings_scroll_offset(240, 192, 96, 0, 1000, 2000, 500) == 120);
        require(settings_scroll_offset(240, 96, 192, 0, 1000, 2000, 500) == 480);
        require(settings_scroll_offset(20, 192, 96, 300, 80, 1000, 200) == 310);
        require(settings_scroll_offset(200, 96, 192, 300, 40, 1000, 200) == 339); // Rewrapped anchor.
        require(settings_scroll_offset(-8, 96, 192, 300, 40, 1000, 200) == 284); // Gap before control.
        require(settings_scroll_offset(240, 96, 192, 300, 1000, 900, 800) == 100); // New page clamp.
        require(settings_scroll_offset(240, 192, 96, 0, 1000, 100, 500) == 0);
        std::cout << "Settings DPI position/straddling/origins/idempotence/nested queue/scroll anchors: PASS\n";
        Temp temporary;
        const auto directory = temporary.path / L"\u8a2d\u5b9a folder";
        require(std::filesystem::create_directory(directory));
        const SettingsStore portable{SettingsMode::portable, directory / L"settings.conf", directory};
        SettingsLoadResult loaded;
        unsigned queries{}, launches{};
        SettingsMetadataProbe probe = [&](const auto& path) {
            ++queries; return settings_metadata(path);
        };
        SettingsFolderShell shell = [&](const auto& parent) {
            require(parent == directory); ++launches; return S_OK;
        };
        auto view = settings_location_view(portable, loaded, probe);
        require(view.resolved && view.path == portable.path && view.mode == UiStringId::settings_mode_portable);
        require(view.file.presence == SettingsPresence::missing && view.parent.presence == SettingsPresence::directory);
        require(queries == 2 && launches == 0 && !std::filesystem::exists(portable.path));
        require(open_settings_parent(portable, false, probe, shell) == S_FALSE && queries == 2 && launches == 0);
        require(open_settings_parent(portable, true, probe, shell) == S_OK && queries == 3 && launches == 1);
        SettingsStore normal = portable; normal.mode = SettingsMode::per_user;
        require(settings_location_view(normal, loaded).mode == UiStringId::settings_mode_normal);
        SettingsStore unresolved;
        require(!settings_location_view(unresolved, loaded, probe).resolved && queries == 3);
        require(settings_location_view(unresolved, loaded).mode == UiStringId::settings_mode_unresolved);
        require(open_settings_parent(unresolved, true, probe, shell) == E_INVALIDARG && queries == 3 && launches == 1);
        loaded.status = SettingsStatus::read_failure; loaded.persistence_enabled = false;
        loaded.diagnostic = "ReadFailure/path: Portable marker cannot be opened: 5";
        require(settings_location_view(unresolved, loaded).guidance == UiStringId::settings_marker_error);
        loaded.diagnostic = "ReadFailure/path: Settings location attribute probe failed: 5";
        require(settings_location_view(unresolved, loaded).guidance == UiStringId::settings_location_read_only);
        require(settings_location_view(portable, loaded).guidance == UiStringId::settings_read_error);
        loaded = parse_product_settings("schema_version=8\nfuture=preserve\n");
        require(settings_location_view(portable, loaded).guidance == UiStringId::settings_newer_schema_read_only);
        loaded = parse_product_settings(std::string(maximum_settings_bytes + 1, 'x'));
        require(settings_location_view(portable, loaded).guidance == UiStringId::settings_oversized);
        loaded = parse_product_settings("invalid");
        require(settings_location_view(portable, loaded).guidance == UiStringId::settings_defaults_warning);
        loaded.persistence_enabled = false; loaded.diagnostic = "unknown protected reason";
        require(settings_location_view(portable, loaded).guidance == UiStringId::settings_protected);
        for (DWORD error : {ERROR_ACCESS_DENIED, ERROR_BAD_NETPATH, ERROR_SHARING_VIOLATION, ERROR_GEN_FAILURE})
        {
            SettingsMetadataProbe failed = [error](const auto&) { return SettingsMetadata{SettingsPresence::unknown, error}; };
            view = settings_location_view(portable, loaded, failed);
            require(view.file.presence == SettingsPresence::unknown && view.file.error == error);
            require(open_settings_parent(portable, true, failed, shell) == HRESULT_FROM_WIN32(error) && launches == 1);
        }
        SettingsMetadataProbe throws = [](const auto&) -> SettingsMetadata { throw std::runtime_error("metadata failure"); };
        require(settings_location_view(portable, loaded, throws).file.presence == SettingsPresence::unknown);
        require(FAILED(open_settings_parent(portable, true, throws, shell)) && launches == 1);
        SettingsFolderShell failed_shell = [&](const auto&) { ++launches; return E_FAIL; };
        require(open_settings_parent(portable, true, {}, failed_shell) == E_FAIL && launches == 2);
        SettingsFolderShell throws_shell = [](const auto&) -> HRESULT { throw std::runtime_error("shell failure"); };
        require(open_settings_parent(portable, true, {}, throws_shell) == E_FAIL);
        SettingsStore missing{SettingsMode::per_user, directory / L"missing" / L"settings.conf", directory};
        require(settings_location_view(missing, {}).parent.presence == SettingsPresence::missing);
        require(FAILED(open_settings_parent(missing, true, {}, shell)) && launches == 2);
        require(!std::filesystem::exists(missing.path.parent_path()));
        ProductSettings settings{portable.path};
        const auto before = settings.preferences();
        auto pending = before; pending.filename_policy = FilenamePolicy::guid;
        require(!settings.save_from_settings(pending, SettingsWriteFault::temp_write).persisted);
        require(settings.preferences() == before && !std::filesystem::exists(portable.path));
        require(settings.save_from_settings(pending).persisted && settings.preferences() == pending);
        require(settings_location_view(settings.store(), settings.load_result()).file.presence == SettingsPresence::file);
        // Capture commits do not rewrite the original NoSettings load status.
        ProductSettings capture{directory / L"capture-settings.conf"};
        require(capture.load_result().status == SettingsStatus::no_settings);
        capture.commit(pending);
        require(capture.load_result().status == SettingsStatus::no_settings);
        require(settings_location_view(capture.store(), capture.load_result()).file.presence == SettingsPresence::file);
        require(settings_location_view(capture.store(), capture.load_result()).guidance == UiStringId::count);
        for (const auto& path : {std::filesystem::path{L"\\\\server\\share\\\u8a2d\u5b9a folder\\settings.conf"},
            std::filesystem::path{L"\\\\?\\C:\\" + std::wstring(500, L'x') + L"\\settings.conf"}})
        {
            SettingsStore special{SettingsMode::portable, path, directory};
            unsigned calls{};
            SettingsMetadataProbe exists = [](const auto& p) { return SettingsMetadata{p.extension() == L".conf" ? SettingsPresence::file : SettingsPresence::directory, 0}; };
            SettingsFolderShell spy = [&](const auto& parent) { require(parent.native() == path.parent_path().native()); ++calls; return S_OK; };
            require(settings_location_view(special, {}, exists).path.native() == path.native());
            require(open_settings_parent(special, false, exists, spy) == S_FALSE && calls == 0);
            require(open_settings_parent(special, true, exists, spy) == S_OK && calls == 1);
        }
        std::cout << "Settings location metadata/load-vs-current/protection/transaction/Shell spy: PASS\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

#include "platform/windows/product_settings.h"
#include "platform/windows/file_publication.h"
#include "platform/windows/portable_settings_marker.h"
#include <Windows.h>
#include <array>
#include <barrier>
#include <fstream>
#include <iostream>
#include <regex>
#include <source_location>
#include <thread>
#include <vector>
using namespace clipss2::platform::windows;
namespace
{
void require(bool value, std::source_location where = std::source_location::current())
{ if (!value) throw std::runtime_error("Output storage assertion failed at line " + std::to_string(where.line())); }
std::string text(const std::filesystem::path& p)
{ std::ifstream f{p, std::ios::binary}; return {std::istreambuf_iterator<char>{f}, {}}; }
void raw(const std::filesystem::path& p, std::string_view value)
{ std::ofstream f{p, std::ios::binary}; f.write(value.data(), static_cast<std::streamsize>(value.size())); require(f.good()); }
struct Temp
{
    std::filesystem::path path;
    Temp()
    {
        std::array<wchar_t, MAX_PATH> folder{}, name{};
        const DWORD count = GetTempPathW(static_cast<DWORD>(folder.size()), folder.data());
        require(count && count < folder.size());
        require(GetTempFileNameW(folder.data(), L"c2o", 0, name.data()) != 0);
        require(DeleteFileW(name.data()) != FALSE); path = name.data();
        require(std::filesystem::create_directory(path));
    }
    ~Temp() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    std::filesystem::path directory(const wchar_t* name)
    { const auto p = path / name; require(std::filesystem::create_directory(p)); return p; }
};
struct HeldReservation
{
    std::filesystem::path path;
    HANDLE handle;
    explicit HeldReservation(std::filesystem::path p) : path(std::move(p)),
        handle(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr))
    { require(handle != INVALID_HANDLE_VALUE); }
    ~HeldReservation() { (void)CloseHandle(handle); (void)DeleteFileW(path.c_str()); }
    HeldReservation(const HeldReservation&) = delete;
    HeldReservation& operator=(const HeldReservation&) = delete;
};
std::vector<std::byte> payload()
{ return {std::byte{'v'}, std::byte{'a'}, std::byte{'l'}, std::byte{'i'}, std::byte{'d'}}; }
void validate(std::span<const std::byte> bytes)
{ const auto expected = payload(); require(std::equal(bytes.begin(), bytes.end(), expected.begin(), expected.end())); }
void only_finals(const std::filesystem::path& dir)
{ for (const auto& e : std::filesystem::directory_iterator(dir)) require(!e.path().filename().native().starts_with(L".")); }
std::string changed(std::string bytes, std::string_view key, std::string_view value)
{
    const auto start = bytes.find(std::string{key} + "="); require(start != std::string::npos);
    const auto offset = start + key.size() + 1;
    bytes.replace(offset, bytes.find('\n', offset) - offset, value); return bytes;
}
void portable_marker_tests(Temp& tmp)
{
    const auto exe = tmp.directory(L"marker \u8a2d\u5b9a");
    const auto user = tmp.directory(L"marker-user") / L"settings.conf";
    const auto local = exe / L"settings.conf";
    const auto marker = exe / portable_settings_marker_name;
    ProductPreferences preferences;
    preferences.filename_policy = FilenamePolicy::guid;
    require(write_product_settings(user, preferences).persisted);
    const auto original = text(user);
    unsigned calls{};
    auto fallback = [&] { ++calls; return user; };
    auto rejects = [](const auto& action) { try { action(); } catch (const std::exception&) { return true; } return false; };
    require(resolve_settings_store(exe, fallback).mode == SettingsMode::per_user && calls == 1);
    raw(exe / L"settings.conf.example", serialize_product_settings({}));
    require(resolve_settings_store(exe, fallback).mode == SettingsMode::per_user && calls == 2);
    raw(marker, portable_settings_marker_content);
    const auto store = resolve_settings_store(exe, fallback);
    require(store.mode == SettingsMode::portable && store.path == local && calls == 2);
    require(load_product_settings(local).status == SettingsStatus::no_settings);
    require(!std::filesystem::exists(local) && text(user) == original); // Load does not initialize.
    const auto cwd = std::filesystem::current_path();
    std::filesystem::current_path(tmp.path);
    const auto other_cwd = resolve_settings_store(exe, fallback);
    std::filesystem::current_path(cwd);
    require(other_cwd.path == local && calls == 2);
    require(SetFileAttributesW(marker.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE);
    require(resolve_settings_store(exe, fallback).path == local);
    require(SetFileAttributesW(marker.c_str(), FILE_ATTRIBUTE_NORMAL) != FALSE);
    for (const auto& invalid : {std::string{}, std::string{"clipss2-portable-v2\n"},
        std::string{"clipss2-portable-v1\r\n"}, std::string{"\xef\xbb\xbf"} + std::string{portable_settings_marker_content},
        std::string(4096, 'x')})
    {
        raw(marker, invalid);
        require(rejects([&] { (void)resolve_settings_store(exe, fallback); }) && calls == 2);
    }
    raw(marker, portable_settings_marker_content);
    const auto held = CreateFileW(marker.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    require(held != INVALID_HANDLE_VALUE);
    require(rejects([&] { (void)resolve_settings_store(exe, fallback); }) && calls == 2);
    require(CloseHandle(held) != FALSE);
    require(DeleteFileW(marker.c_str()) != FALSE && std::filesystem::create_directory(marker));
    require(rejects([&] { (void)resolve_settings_store(exe, fallback); }) && calls == 2);
    // Re-check the opened object, even if the initial attributes were stale.
    SettingsAttributeProbe stale = [&](const auto& p) -> std::optional<DWORD> {
        if (p == local) return {}; return FILE_ATTRIBUTE_NORMAL;
    };
    require(rejects([&] { (void)resolve_settings_store(exe, fallback, stale); }));
    require(std::filesystem::remove(marker));
    SettingsAttributeProbe reparse = [&](const auto& p) -> std::optional<DWORD> {
        if (p == local) return {}; return FILE_ATTRIBUTE_REPARSE_POINT;
    };
    require(rejects([&] { (void)resolve_settings_store(exe, fallback, reparse); }));
    for (const auto& failed : {local, marker})
    {
        SettingsAttributeProbe denied = [&](const auto& p) -> std::optional<DWORD> {
            if (p == failed) throw std::runtime_error("Injected attribute access denial"); return {};
        };
        require(rejects([&] { (void)resolve_settings_store(exe, fallback, denied); }) && calls == 2);
    }
    // A marker disappearing between probe and open is an error, not absence.
    require(rejects([&] { (void)resolve_settings_store(exe, fallback, stale); }) && calls == 2);
    require(rejects([&] { (void)resolve_settings_store(exe, []() -> std::filesystem::path {
        throw std::runtime_error("Injected Known Folder failure"); }); }));
    require(rejects([&] { (void)settings_executable_directory(L"C:\\app\\clipss2.exe", 0); }));
    require(rejects([&] { (void)settings_executable_directory(L"truncated", 9); }));
    require(rejects([&] { (void)settings_executable_directory(L"relative.exeX", 12); }));
    require(settings_executable_directory(L"C:\\app\\clipss2.exeX", 18) == L"C:\\app");
    raw(marker, portable_settings_marker_content);
    require(write_product_settings(local, preferences).persisted);
    const auto local_before = text(local);
    require(resolve_settings_store(exe, fallback).path == local && calls == 2);
    raw(marker, "invalid");
    require(resolve_settings_store(exe, fallback).path == local && text(local) == local_before && calls == 2);
    // Existing settings wins even with an invalid marker, without merge or rewrite.
    require(load_product_settings(local).preferences == preferences);
    require(DeleteFileW(local.c_str()) != FALSE && DeleteFileW(marker.c_str()) != FALSE);
    // A previously selected store is a value, not a live marker-dependent redirect.
    require(resolve_settings_store(exe, fallback).mode == SettingsMode::per_user && calls == 3);
    require(store.mode == SettingsMode::portable && store.path == local);
    require(write_product_settings(store.path, {}).persisted && text(user) == original);
    auto long_exe = std::filesystem::path{L"\\\\?\\" + tmp.path.native()};
    for (unsigned n = 0; n != 5; ++n) long_exe /= std::wstring(55, L'a');
    require(std::filesystem::create_directories(long_exe));
    raw(long_exe / portable_settings_marker_name, portable_settings_marker_content);
    const auto long_store = resolve_settings_store(long_exe, fallback);
    require(long_store.mode == SettingsMode::portable && long_store.path.native().size() > MAX_PATH && calls == 3);
    require(write_product_settings(long_store.path, {}).persisted);
    require(load_product_settings(long_store.path).status == SettingsStatus::loaded);
}
}
int main()
{
    try
    {
        Temp tmp;
        portable_marker_tests(tmp);
        const auto exe = tmp.directory(L"portable");
        const auto user = tmp.directory(L"per-user") / L"settings.conf";
        unsigned int user_resolutions{};
        auto fallback = [&] { ++user_resolutions; return user; };
        auto store = resolve_settings_store(exe, fallback);
        require(store.mode == SettingsMode::per_user && store.path == user && user_resolutions == 1);
        require(load_product_settings(store.path).status == SettingsStatus::no_settings);
        ProductPreferences different;
        different.file_format = clipss2::image::ImageFileFormat::jpeg;
        different.filename_policy = FilenamePolicy::sequence;
        require(write_product_settings(user, different).persisted);
        store = resolve_settings_store(exe, fallback);
        require(load_product_settings(store.path).preferences == different);
        const auto local = exe / L"settings.conf";
        require(write_product_settings(local, {}).persisted);
        const auto calls = user_resolutions;
        store = resolve_settings_store(exe, fallback);
        require(store.mode == SettingsMode::portable && store.path == local && user_resolutions == calls);
        require(load_product_settings(store.path).preferences == ProductPreferences{}); // No merge.
        const auto user_before = text(user);
        raw(local, "broken"); store = resolve_settings_store(exe, fallback);
        require(store.mode == SettingsMode::portable && user_resolutions == calls);
        require(load_product_settings(store.path).status == SettingsStatus::invalid_settings);
        raw(local, "schema_version=8\nfuture=yes\n");
        store = resolve_settings_store(exe, fallback);
        require(!load_product_settings(store.path).persistence_enabled);
        require(!write_product_settings(store.path, {}).persisted && text(user) == user_before);
        require(text(local) == "schema_version=8\nfuture=yes\n");
        raw(local, serialize_product_settings({}));
        const auto locked = CreateFileW(local.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        require(locked != INVALID_HANDLE_VALUE);
        require(!write_product_settings(local, different).persisted);
        require(CloseHandle(locked) != FALSE && text(user) == user_before);

        const std::string schema1 = "schema_version=1\nfile_format=jpeg\njpeg_profile=balanced\njpeg_backend=jpegli\n";
        raw(local, schema1); const auto migrated = load_product_settings(local);
        require(migrated.status == SettingsStatus::loaded && migrated.preferences.filename_policy == FilenamePolicy::timestamp &&
            !migrated.preferences.output_directory.explicit_path && text(local) == schema1);
        require(write_product_settings(local, migrated.preferences).persisted && text(local).starts_with("schema_version=7\n"));
        const auto unicode = tmp.directory(L"\u753b\u50cf=\u30b9\u30af\u30ea\u30fc\u30f3\u30b7\u30e7\u30c3\u30c8");
        ProductPreferences explicit_prefs;
        explicit_prefs.output_directory.explicit_path = unicode;
        for (const auto policy : {FilenamePolicy::timestamp, FilenamePolicy::sequence, FilenamePolicy::guid})
        {
            explicit_prefs.filename_policy = policy;
            const auto bytes = serialize_product_settings(explicit_prefs);
            require(parse_product_settings(bytes).preferences == explicit_prefs);
            require(path_from_utf8(path_utf8(unicode)) == unicode);
        }
        const auto golden = serialize_product_settings({});
        require(golden == "schema_version=7\nfile_format=png\njpeg_profile=default\njpeg_backend=automatic\n"
            "filename_policy=timestamp\noutput_directory_mode=default\noutput_directory_path=\nui_language=system\navif_profile=lossless\njxl_profile=lossless\n");
        for (const auto& [key, value] : std::array<std::pair<std::string_view, std::string_view>, 5>{{
            {"filename_policy", "unknown"}, {"output_directory_mode", "unknown"},
            {"output_directory_path", "D:\\foo"}, {"output_directory_mode", "explicit"},
            {"output_directory_path", "relative"}}})
            require(parse_product_settings(changed(golden, key, value)).status == SettingsStatus::invalid_settings);
        auto relative = changed(changed(golden, "output_directory_mode", "explicit"), "output_directory_path", "foo\\bar");
        require(parse_product_settings(relative).status == SettingsStatus::invalid_settings);
        auto invalid_utf8 = changed(changed(golden, "output_directory_mode", "explicit"), "output_directory_path", std::string{"D:\\"} + '\xff');
        require(parse_product_settings(invalid_utf8).status == SettingsStatus::invalid_settings);
        require(parse_product_settings(golden + "filename_policy=guid\n").status == SettingsStatus::invalid_settings);
        require(parse_product_settings(golden.substr(0, golden.find("output_directory_path"))).status == SettingsStatus::invalid_settings);
        require(parse_product_settings(golden + '\0').status == SettingsStatus::invalid_settings);
        require(!parse_product_settings(std::string{"\xef\xbb\xbf"} + "schema_version=8\nfuture=yes\n").persistence_enabled);
        require(parse_product_settings(std::string{"\xef\xbb\xbf"} + golden).status == SettingsStatus::invalid_settings);
        require(!parse_product_settings(std::string(maximum_settings_bytes + 1, 'x')).persistence_enabled);

        const SettingsStore per_user{SettingsMode::per_user, user, exe};
        OutputDirectoryPreference preference;
        const auto redirected = tmp.path / L"OneDrive" / L"Pictures"; // Resolver seam only, never created.
        unsigned int picks{}, initials{};
        auto initial = [&] { ++initials; return redirected; };
        FolderPicker cancel = [&](const auto& p) -> std::optional<std::filesystem::path> { ++picks; require(p == redirected); return {}; };
        require(!resolve_file_output(per_user, preference, FilenamePolicy::timestamp, cancel, initial));
        require(!preference.explicit_path && !std::filesystem::exists(redirected));
        FolderPicker choose = [&](const auto& p) -> std::optional<std::filesystem::path> { ++picks; require(p == redirected); return unicode; };
        auto options = resolve_file_output(per_user, preference, FilenamePolicy::timestamp, choose, initial);
        require(options && options->directory == unicode && !options->create_directory);
        const auto before = picks;
        require(resolve_file_output(per_user, preference, FilenamePolicy::sequence, cancel, initial)->directory == unicode && picks == before);
        preference.explicit_path = tmp.path / L"removed";
        const auto original = preference;
        require(!resolve_file_output(per_user, preference, FilenamePolicy::timestamp, cancel, initial) && preference == original);
        require(resolve_file_output(per_user, preference, FilenamePolicy::timestamp, choose, initial)->directory == unicode);
        preference = {};
        const auto portable_output = resolve_file_output(store, preference, FilenamePolicy::timestamp, cancel, initial);
        require(portable_output && portable_output->directory == exe / L"captures" && portable_output->create_directory);
        require(!std::filesystem::exists(exe / L"captures"));

        PublicationHooks fixed;
        fixed.timestamp = [] { return L"20260913_201512_184"; };
        FileOutputOptions out{unicode, FilenamePolicy::timestamp, false};
        const auto a = publish_file_payload(out, ".png", payload(), validate, fixed);
        const auto b = publish_file_payload(out, ".png", payload(), validate, fixed);
        const auto c = publish_file_payload(out, ".jpg", payload(), validate, fixed);
        require(a.filename() == L"clipss2_20260913_201512_184.png" && b.filename() == L"clipss2_20260913_201512_184_001.png");
        require(c.filename() == L"clipss2_20260913_201512_184.jpg" && text(a) == "valid");
        require(publish_file_payload(out, ".png", payload(), validate, fixed).filename() == L"clipss2_20260913_201512_184_002.png");
        const auto webp_time = tmp.directory(L"webp-time");
        const FileOutputOptions webp_time_options{webp_time, FilenamePolicy::timestamp, false};
        const auto webp_time_first = publish_file_payload(webp_time_options, ".webp", payload(), validate, fixed);
        const auto webp_time_second = publish_file_payload(webp_time_options, ".webp", payload(), validate, fixed);
        require(webp_time_first.filename() == L"clipss2_20260913_201512_184.webp" &&
            webp_time_second.filename() == L"clipss2_20260913_201512_184_001.webp" && text(webp_time_first) == "valid");
        const auto avif_time = tmp.directory(L"avif-time");
        const FileOutputOptions avif_time_options{avif_time, FilenamePolicy::timestamp, false};
        const auto avif_time_first = publish_file_payload(avif_time_options, ".avif", payload(), validate, fixed);
        const auto avif_time_second = publish_file_payload(avif_time_options, ".avif", payload(), validate, fixed);
        require(avif_time_first.filename() == L"clipss2_20260913_201512_184.avif" &&
            avif_time_second.filename() == L"clipss2_20260913_201512_184_001.avif" && text(avif_time_first) == "valid");
        const auto wide_suffix = tmp.directory(L"wide-suffix");
        raw(wide_suffix / L"clipss2_20260913_201512_184.png", "old");
        for (unsigned int n=1; n<=999; ++n)
        {
            auto suffix=std::to_wstring(n); suffix.insert(0, 3-suffix.size(), L'0');
            raw(wide_suffix / (L"clipss2_20260913_201512_184_"+suffix+L".png"), "old");
        }
        require(publish_file_payload({wide_suffix, FilenamePolicy::timestamp, false}, ".png", payload(), validate, fixed).filename() ==
            L"clipss2_20260913_201512_184_1000.png");
        const auto live_time = publish_file_payload(out, ".png", payload(), validate);
        require(std::regex_match(live_time.filename().string(), std::regex{"clipss2_[0-9]{8}_[0-9]{6}_[0-9]{3}\\.png"}));
        const auto seq = tmp.directory(L"sequence"); out = {seq, FilenamePolicy::sequence, false};
        require(publish_file_payload(out, ".png", payload(), validate).filename() == L"clipss2_000001.png");
        require(publish_file_payload(out, ".jpg", payload(), validate).filename() == L"clipss2_000002.jpg");
        require(publish_file_payload(out, ".webp", payload(), validate).filename() == L"clipss2_000003.webp");
        require(publish_file_payload(out, ".avif", payload(), validate).filename() == L"clipss2_000004.avif");
        require(publish_file_payload(out, ".jxl", payload(), validate).filename() == L"clipss2_000005.jxl");
        const auto jxl_time_dir = tmp.directory(L"jxl-time");
        const FileOutputOptions jxl_time{jxl_time_dir, FilenamePolicy::timestamp, false};
        require(publish_file_payload(jxl_time, ".jxl", payload(), validate, fixed).filename() == L"clipss2_20260913_201512_184.jxl");
        require(publish_file_payload(jxl_time, ".jxl", payload(), validate, fixed).filename() == L"clipss2_20260913_201512_184_001.jxl");
        const auto jxl_guid = publish_file_payload({tmp.directory(L"jxl-guid"), FilenamePolicy::guid, false}, ".jxl", payload(), validate);
        require(std::regex_match(jxl_guid.filename().string(),
            std::regex{"clipss2_[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\.jxl"}));
        const auto jpg_only = tmp.directory(L"jpg-only"); raw(jpg_only / L"CLIPSS2_000001.JPEG", "old");
        require(publish_file_payload({jpg_only, FilenamePolicy::sequence, false}, ".png", payload(), validate).filename() == L"clipss2_000002.png");
        const auto contested=tmp.directory(L"held-reservation");
        {
            HeldReservation other{contested / L".clipss2_000001.reserve"};
            require(publish_file_payload({contested, FilenamePolicy::sequence, false}, ".jpg", payload(), validate).filename() == L"clipss2_000002.jpg");
            require(std::filesystem::exists(other.path)); // Never delete somebody else's lease.
        }
        only_finals(contested);
        raw(seq / L"clipss2_000123.avif", "future"); raw(seq / L"unrelated_999999.jpg", "unrelated");
        raw(seq / L"clipss2_999999x.png", "invalid"); raw(seq / L"clipss2_0000123.png", "noncanonical");
        require(publish_file_payload(out, ".png", payload(), validate).filename() == L"clipss2_000124.png");
        raw(seq / L"clipss2_999999.jxl", "future");
        require(publish_file_payload(out, ".jpg", payload(), validate).filename() == L"clipss2_1000000.jpg");
        PublicationHooks collision; bool injected{};
        collision.before_move = [&](const auto& candidate) { if (!injected) { injected = true; raw(candidate, "old-final"); } };
        const auto retry = publish_file_payload(out, ".png", payload(), validate, collision);
        require(retry.filename() == L"clipss2_1000002.png" && text(seq / L"clipss2_1000001.png") == "old-final");
        const auto guids = tmp.directory(L"guids"); out = {guids, FilenamePolicy::guid, false};
        const auto guid = publish_file_payload(out, ".png", payload(), validate);
        require(std::regex_match(guid.filename().string(), std::regex{"clipss2_[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\.png"}));
        unsigned int guid_calls{};
        collision = {}; collision.guid = [&] { return guid_calls++ ? L"00000000-0000-4000-8000-000000000002" : guid.stem().native().substr(8); };
        require(publish_file_payload(out, ".png", payload(), validate, collision).filename() == L"clipss2_00000000-0000-4000-8000-000000000002.png");
        require(text(guid) == "valid");
        const auto webp_guids = tmp.directory(L"webp-guids");
        const auto webp_guid = publish_file_payload({webp_guids, FilenamePolicy::guid, false}, ".webp", payload(), validate);
        require(std::regex_match(webp_guid.filename().string(),
            std::regex{"clipss2_[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\.webp"}));
        const auto avif_guids = tmp.directory(L"avif-guids");
        const auto avif_guid = publish_file_payload({avif_guids, FilenamePolicy::guid, false}, ".avif", payload(), validate);
        require(std::regex_match(avif_guid.filename().string(),
            std::regex{"clipss2_[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\.avif"}));

        const auto failures = tmp.directory(L"failures"); out = {failures, FilenamePolicy::timestamp, false};
        for (const auto fault : {PublicationFault::write, PublicationFault::flush, PublicationFault::move})
        {
            PublicationHooks hooks; hooks.fault = fault;
            hooks.after_write = [&](const auto& pending) { require(pending.parent_path() == failures && pending.extension() == L".tmp");
                require(text(pending) == "va");
                for (const auto& e : std::filesystem::directory_iterator(failures)) require(e.path().extension() == L".tmp"); };
            bool failed{}; try { (void)publish_file_payload(out, ".png", payload(), validate, hooks); } catch (const std::runtime_error&) { failed = true; }
            require(failed && std::filesystem::is_empty(failures));
        }
        bool invalid_failed{};
        try { (void)publish_file_payload(out, ".png", payload(), [](auto) { throw std::runtime_error("invalid"); }); }
        catch (const std::runtime_error&) { invalid_failed = true; }
        require(invalid_failed && std::filesystem::is_empty(failures));
        require(publish_file_payload(*portable_output, ".jpg", payload(), validate, fixed).parent_path() == exe / L"captures");
        // Equivalent two-process race: independent native handles/CREATE_NEW/no-replace.
        for (const auto policy : {FilenamePolicy::timestamp, FilenamePolicy::sequence})
        {
            const auto race = tmp.directory(policy == FilenamePolicy::timestamp ? L"race-time" : L"race-sequence");
            out = {race, policy, false}; std::barrier gate{2};
            std::array<std::filesystem::path, 2> results; std::array<std::exception_ptr, 2> errors;
            std::array<std::filesystem::path, 2> temps;
            auto writer = [&](std::size_t index) { try {
                auto hooks = fixed;
                hooks.after_write = [&](const auto& p) { temps[index] = p; gate.arrive_and_wait(); };
                results[index] = publish_file_payload(out, policy == FilenamePolicy::sequence && index ? ".jpg" : ".png", payload(), validate, hooks);
            } catch (...) { errors[index] = std::current_exception(); } };
            std::jthread first{writer, 0}, second{writer, 1}; first.join(); second.join();
            for (const auto& error : errors) if (error) std::rethrow_exception(error);
            require(results[0] != results[1] && temps[0] != temps[1]);
            if (policy == FilenamePolicy::sequence) require(results[0].stem() != results[1].stem());
            require(text(results[0]) == "valid" && text(results[1]) == "valid"); only_finals(race);
        }
        only_finals(unicode); only_finals(seq); only_finals(guids);
        raw(seq / L"clipss2_18446744073709551615.webp", "max");
        bool overflow{}; try { (void)publish_file_payload({seq, FilenamePolicy::sequence, false}, ".png", payload(), validate); }
        catch (const std::overflow_error&) { overflow = true; } require(overflow); only_finals(seq);
        std::cout << "Portable/no-merge/schema2/Unicode/picker semantics/naming/no-replace/races/fault cleanup: passed\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

#include "platform/windows/product_settings.h"
#include <Windows.h>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <source_location>
#include <thread>
#include <vector>
using namespace clipss2::platform::windows;
using namespace clipss2::codec::jpeg;
using clipss2::image::ImageFileFormat;
namespace
{
void require(bool ok, const std::source_location location = std::source_location::current())
{ if (!ok) throw std::runtime_error("Settings test failed at line " + std::to_string(location.line())); }
std::string text(const std::filesystem::path& path)
{ std::ifstream stream(path, std::ios::binary); return {std::istreambuf_iterator<char>{stream}, {}}; }
void raw_write(const std::filesystem::path& path, std::string_view bytes)
{ std::ofstream stream(path, std::ios::binary); stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size())); require(stream.good()); }
struct TempDirectory
{
    std::filesystem::path path;
    TempDirectory()
    {
        std::array<wchar_t, MAX_PATH> directory{}, name{};
        const DWORD count = GetTempPathW(static_cast<DWORD>(directory.size()), directory.data());
        require(count != 0 && count < directory.size());
        require(GetTempFileNameW(directory.data(), L"c2s", 0, name.data()) != 0);
        require(DeleteFileW(name.data()) != FALSE);
        path = name.data(); require(std::filesystem::create_directory(path));
    }
    ~TempDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
};
}
int main()
{
    try
    {
        const ProductPreferences defaults;
        require(default_product_settings_path().is_absolute()); // Resolution only; never write user settings.
        const std::string golden = "schema_version=7\nfile_format=png\njpeg_profile=default\njpeg_backend=automatic\n"
            "filename_policy=timestamp\noutput_directory_mode=default\noutput_directory_path=\nui_language=system\navif_profile=lossless\njxl_profile=lossless\n";
        const std::string schema2 = "schema_version=2\nfile_format=png\njpeg_profile=default\njpeg_backend=automatic\n"
            "filename_policy=timestamp\noutput_directory_mode=default\noutput_directory_path=\n";
        const std::string schema3 = "schema_version=3\nfile_format=jpeg\njpeg_profile=balanced\njpeg_backend=jpegli\n"
            "filename_policy=sequence\noutput_directory_mode=default\noutput_directory_path=\nui_language=japanese\n";
        const std::string schema3_png = "schema_version=3\nfile_format=png\njpeg_profile=default\njpeg_backend=automatic\n"
            "filename_policy=guid\noutput_directory_mode=default\noutput_directory_path=\nui_language=english\n";
        const std::string schema4_webp = "schema_version=4\nfile_format=webp\njpeg_profile=default\njpeg_backend=automatic\n"
            "filename_policy=timestamp\noutput_directory_mode=default\noutput_directory_path=\nui_language=system\n";
        const std::string schema5_avif = "schema_version=5\nfile_format=avif\njpeg_profile=balanced\njpeg_backend=jpegli\n"
            "filename_policy=guid\noutput_directory_mode=default\noutput_directory_path=\nui_language=japanese\n";
        require(serialize_product_settings(defaults) == golden);
        std::vector<ProductPreferences> valid{{}, {ImageFileFormat::jpeg, JpegCurrentDefault{}},
            {ImageFileFormat::webp, JpegCurrentDefault{}}, {ImageFileFormat::avif, JpegCurrentDefault{}},
            {ImageFileFormat::jxl, JpegCurrentDefault{}}};
        for (const auto profile : {JpegProfile::fast, JpegProfile::balanced, JpegProfile::smallest})
            for (const auto backend : {JpegBackendId::automatic,
                profile == JpegProfile::fast ? JpegBackendId::turbo : JpegBackendId::jpegli})
                valid.push_back({ImageFileFormat::jpeg, JpegProfileSelection{profile, backend}});
        const ProductPreferences manual{
            ImageFileFormat::png,
            JpegProfileSelection{JpegProfile::balanced, JpegBackendId::jpegli}};
        valid.push_back(manual);
        auto webp_text = golden;
        webp_text.replace(webp_text.find("file_format=png"), 15, "file_format=webp");
        require(parse_product_settings(webp_text).preferences.file_format == ImageFileFormat::webp &&
            serialize_product_settings(parse_product_settings(webp_text).preferences) == webp_text);
        auto avif_text = golden;
        avif_text.replace(avif_text.find("file_format=png"), 15, "file_format=avif");
        require(parse_product_settings(avif_text).preferences.file_format == ImageFileFormat::avif &&
            serialize_product_settings(parse_product_settings(avif_text).preferences) == avif_text);
        for (const auto language : {UiLanguagePreference::english, UiLanguagePreference::japanese})
        {
            auto localized = defaults; localized.ui_language = language; valid.push_back(localized);
        }
        for (const auto& preference : valid)
        {
            const auto bytes = serialize_product_settings(preference);
            const auto parsed = parse_product_settings(bytes);
            require(parsed.status == SettingsStatus::loaded && parsed.preferences == preference);
            std::string crlf;
            for (const char ch : bytes) { if (ch == '\n') crlf += '\r'; crlf += ch; }
            require(parse_product_settings(crlf).preferences == preference);
            require(parse_product_settings(bytes.substr(0, bytes.size() - 1)).preferences == preference);
        }
        for (const auto avif : {"lossless", "lossy-high", "lossy-balanced", "lossy-small"})
        for (const auto jxl : {"lossless", "lossy-high", "lossy-balanced", "lossy-small"})
        for (const auto format : {ImageFileFormat::png, ImageFileFormat::avif, ImageFileFormat::jxl})
        {
            auto value = defaults;
            value.file_format = format;
            value.avif_profile = clipss2::capture::parse_file_compression(avif);
            value.jxl_profile = clipss2::capture::parse_file_compression(jxl);
            valid.push_back(value);
            require(parse_product_settings(serialize_product_settings(value)).preferences == value);
        }
        const std::array<std::pair<std::string_view, std::string_view>, 10> invalid_values{{
            {"schema_version", "x"}, {"schema_version", "0"}, {"schema_version", "-1"},
            {"file_format", "gif"}, {"jpeg_profile", "unknown"}, {"jpeg_backend", "unknown"},
            {"jpeg_backend", "turbo"}, {"ui_language", "localized"},
            {"avif_profile", "balanced"}, {"jxl_profile", "Lossy-High"}}};
        for (const auto& [key, value] : invalid_values)
        {
            auto bytes = golden;
            const std::string prefix = std::string{key} + "=";
            const auto start = bytes.find(prefix);
            bytes.replace(start + prefix.size(), bytes.find('\n', start) - start - prefix.size(), value);
            const auto rejected = parse_product_settings(bytes);
            require(rejected.status == SettingsStatus::invalid_settings && rejected.preferences == defaults);
        }
        for (const std::string bytes : {std::string{}, golden + "unknown=1\n", golden + "file_format=png\n",
            golden.substr(golden.find('\n') + 1), golden + "malformed\n", golden + "\n", golden + "x=y=z\n",
            golden + std::string(1, '\0'), std::string(maximum_settings_bytes + 1, 'x')})
            require(parse_product_settings(bytes).status == SettingsStatus::invalid_settings);
        for (const char* key : {"file_format=png\n", "jpeg_profile=default\n",
                 "jpeg_backend=automatic\n", "ui_language=system\n",
                 "avif_profile=lossless\n", "jxl_profile=lossless\n"})
        {
            auto bytes = golden; bytes.erase(bytes.find(key), std::string_view{key}.size());
            require(parse_product_settings(bytes).status == SettingsStatus::invalid_settings);
        }
        auto legacy_webp = golden;
        legacy_webp.erase(legacy_webp.find("avif_profile="));
        legacy_webp.replace(legacy_webp.find("schema_version=7"), 16, "schema_version=3");
        legacy_webp.replace(legacy_webp.find("file_format=png"), 15, "file_format=webp");
        require(parse_product_settings(legacy_webp).status == SettingsStatus::invalid_settings);
        const auto future = parse_product_settings("schema_version=8\nnew_feature=yes\n");
        require(future.status == SettingsStatus::unsupported_schema && !future.persistence_enabled && future.preferences == defaults);
        require(!parse_product_settings("schema_version=9\nfuture=yes\n").persistence_enabled);
        const auto migrated2 = parse_product_settings(schema2);
        require(migrated2.status == SettingsStatus::loaded &&
            migrated2.preferences.ui_language == UiLanguagePreference::system &&
            serialize_product_settings(migrated2.preferences) == golden);
        const auto migrated3 = parse_product_settings(schema3);
        require(migrated3.status == SettingsStatus::loaded &&
            migrated3.preferences.file_format == ImageFileFormat::jpeg &&
            migrated3.preferences.filename_policy == FilenamePolicy::sequence &&
            migrated3.preferences.ui_language == UiLanguagePreference::japanese &&
            serialize_product_settings(migrated3.preferences).starts_with("schema_version=7\n"));
        const auto migrated3_png = parse_product_settings(schema3_png);
        require(migrated3_png.status == SettingsStatus::loaded &&
            migrated3_png.preferences.file_format == ImageFileFormat::png &&
            migrated3_png.preferences.filename_policy == FilenamePolicy::guid &&
            migrated3_png.preferences.ui_language == UiLanguagePreference::english);
        const auto migrated4 = parse_product_settings(schema4_webp);
        require(migrated4.status == SettingsStatus::loaded &&
            migrated4.preferences.file_format == ImageFileFormat::webp &&
            serialize_product_settings(migrated4.preferences).starts_with("schema_version=7\n"));
        const auto migrated1 = parse_product_settings(
            "schema_version=1\nfile_format=png\njpeg_profile=default\njpeg_backend=automatic\n");
        const auto migrated5 = parse_product_settings(schema5_avif);
        require(migrated5.status == SettingsStatus::loaded && migrated5.preferences.file_format == ImageFileFormat::avif &&
            migrated5.preferences.filename_policy == FilenamePolicy::guid &&
            migrated5.preferences.ui_language == UiLanguagePreference::japanese &&
            std::get<JpegProfileSelection>(migrated5.preferences.jpeg_selection).backend == JpegBackendId::jpegli);
        auto old_jxl = schema5_avif;
        old_jxl.replace(old_jxl.find("file_format=avif"), 16, "file_format=jxl");
        require(parse_product_settings(old_jxl).status == SettingsStatus::invalid_settings);
        require(serialize_product_settings(valid[4]).starts_with("schema_version=7\nfile_format=jxl\n"));
        require(migrated1.status == SettingsStatus::loaded &&
            migrated1.preferences.filename_policy == FilenamePolicy::timestamp &&
            !migrated1.preferences.output_directory.explicit_path &&
            migrated1.preferences.ui_language == UiLanguagePreference::system);
        const std::string unsupported = "schema_version=1\nfile_format=png\njpeg_profile=balanced\njpeg_backend=turbo\n";
        require(parse_product_settings(unsupported).status == SettingsStatus::unsupported_selection);
        auto unavailable = std::vector<JpegBackendCapabilities>(current_product_jpeg_backends().begin(), current_product_jpeg_backends().end());
        for (auto& backend : unavailable) if (backend.id == JpegBackendId::jpegli) backend.available = false;
        const auto saved_manual = serialize_product_settings(manual);
        require(parse_product_settings(saved_manual, unavailable).status == SettingsStatus::known_backend_unavailable);
        require(parse_product_settings("schema_version=1\nfile_format=jpeg\njpeg_profile=smallest\njpeg_backend=mozjpeg\n").status == SettingsStatus::known_backend_unavailable);

        TempDirectory temporary;
        const auto path = temporary.path / L"\u8a2d\u5b9a" / L"settings.conf";
        require(load_product_settings(path).status == SettingsStatus::no_settings);
        std::filesystem::create_directories(path.parent_path());
        auto actual_collision = path;
        actual_collision += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L".0";
        raw_write(actual_collision, "foreign");
        require(write_product_settings(path, defaults).persisted);
        require(text(actual_collision) == "foreign"); require(std::filesystem::remove(actual_collision));
        require(text(path) == golden && load_product_settings(path).preferences == defaults);
        const HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(locked != INVALID_HANDLE_VALUE);
        const auto unreadable = load_product_settings(path);
        require(CloseHandle(locked) != FALSE);
        require(unreadable.status == SettingsStatus::read_failure && unreadable.preferences == defaults && !unreadable.persistence_enabled);
        for (const auto fault : {SettingsWriteFault::temp_write, SettingsWriteFault::flush, SettingsWriteFault::replace})
        {
            const auto result = write_product_settings(path, manual, fault);
            require(!result.persisted && text(path) == golden);
            for (const auto& entry : std::filesystem::directory_iterator(path.parent_path())) require(entry.path() == path);
        }
        for (const auto& preference : valid)
        { require(write_product_settings(path, preference).persisted); require(load_product_settings(path).preferences == preference); }
        std::array<bool, 2> written{};
        std::thread first([&] { written[0] = write_product_settings(path, valid[2]).persisted; });
        std::thread second([&] { written[1] = write_product_settings(path, manual).persisted; });
        first.join(); second.join(); require(written[0] && written[1]);
        require(load_product_settings(path).status == SettingsStatus::loaded);
        require(write_product_settings(path, defaults).persisted && text(path) == golden);
        // Collision with a file owned by somebody else never truncates/removes it.
        const auto collision = path.parent_path() / L"foreign.tmp";
        raw_write(collision, "foreign");
        require(write_product_settings(path, manual).persisted && text(collision) == "foreign");
        auto unicode_path = defaults;
        unicode_path.output_directory.explicit_path =
            temporary.path / L"日本語=Screenshots";
        const auto unicode_bytes = serialize_product_settings(unicode_path);
        require(parse_product_settings(unicode_bytes).preferences == unicode_path);
        for (const auto& legacy : std::array<std::string, 6>{
            "schema_version=1\nfile_format=png\njpeg_profile=default\njpeg_backend=automatic\n",
            schema2, schema3, schema3_png, schema4_webp, schema5_avif})
        {
            raw_write(path, legacy);
            const auto loaded = load_product_settings(path);
            require(loaded.status == SettingsStatus::loaded && text(path) == legacy);
            require(loaded.preferences.avif_profile == clipss2::capture::FileCompression{} &&
                loaded.preferences.jxl_profile == clipss2::capture::FileCompression{});
            require(write_product_settings(path, loaded.preferences).persisted &&
                text(path).starts_with("schema_version=7\n"));
        }
        raw_write(path, "schema_version=8\nnew_feature=yes\n");
        const auto protected_text = text(path);
        require(!write_product_settings(path, defaults).persisted && text(path) == protected_text);
        ProductSettings protected_store{path};
        WindowSelectionResult snapshot; snapshot.file_format = ImageFileFormat::jpeg;
        snapshot.jpeg_selection = JpegProfileSelection{JpegProfile::balanced, JpegBackendId::automatic};
        protected_store.commit(snapshot);
        require(protected_store.preferences().file_format == ImageFileFormat::jpeg && text(path) == protected_text);
        raw_write(path, "corrupt");
        ProductSettings repair{path}; require(repair.preferences() == defaults);
        const auto frozen = snapshot;
        snapshot.jpeg_selection = JpegCurrentDefault{};
        repair.commit(frozen);
        require(load_product_settings(path).preferences == ProductPreferences{frozen.file_format, frozen.jpeg_selection});
        ProductSettings restarted{path}; require(restarted.preferences() == repair.preferences());
        // No commit on cancellation: constructing another session leaves bytes unchanged.
        const auto before_cancel = text(path);
        ProductSettings cancelled{path}; require(text(path) == before_cancel);
        // Schema 6 loads without writes; first accepted preference upgrades to 7.
        for (const auto format : {"avif", "jxl"})
        {
            const std::string legacy = std::string{"schema_version=6\nfile_format="} + format +
                "\njpeg_profile=balanced\njpeg_backend=jpegli\nfilename_policy=guid\n"
                "output_directory_mode=default\noutput_directory_path=\nui_language=japanese\n";
            raw_write(path, legacy);
            ProductSettings migrated{path};
            require(migrated.load_result().status == SettingsStatus::loaded && text(path) == legacy &&
                migrated.preferences().avif_profile == clipss2::capture::FileCompression{} &&
                migrated.preferences().jxl_profile == clipss2::capture::FileCompression{} &&
                migrated.preferences().file_format == clipss2::image::parse_image_file_format(format) &&
                migrated.preferences().jpeg_selection == JpegProductSelection{JpegProfileSelection{JpegProfile::balanced,JpegBackendId::jpegli}} &&
                migrated.preferences().filename_policy == FilenamePolicy::guid &&
                migrated.preferences().ui_language == UiLanguagePreference::japanese);
            migrated.commit(migrated.preferences());
            require(text(path).starts_with("schema_version=7\n"));
        }
        auto profiles = defaults;
        profiles.avif_profile = clipss2::capture::parse_file_compression("lossy-balanced");
        profiles.jxl_profile = clipss2::capture::parse_file_compression("lossy-high");
        require(write_product_settings(path, profiles).persisted);
        ProductSettings preserving{path};
        auto pending_profiles = preserving.preferences();
        pending_profiles.filename_policy = FilenamePolicy::sequence;
        require(preserving.save_from_settings(pending_profiles).persisted &&
            load_product_settings(path).preferences == pending_profiles);
        const auto saved_profiles = text(path);
        // Settings Cancel never calls the writer; a discarded pending copy cannot mutate intent.
        auto discarded = preserving.preferences(); discarded.avif_profile = {};
        require(preserving.preferences() == pending_profiles && text(path) == saved_profiles);
        WindowSelectionResult codec_snapshot;
        codec_snapshot.avif_profile = profiles.avif_profile; codec_snapshot.jxl_profile = profiles.jxl_profile;
        preserving.commit(codec_snapshot);
        require(load_product_settings(path).preferences.avif_profile == profiles.avif_profile &&
            load_product_settings(path).preferences.jxl_profile == profiles.jxl_profile);
        const auto blocked = temporary.path / L"blocked"; raw_write(blocked, "not a directory");
        ProductSettings failed{blocked / L"settings.conf"}; failed.commit(frozen);
        require(failed.preferences() == repair.preferences() && text(blocked) == "not a directory");
        const auto before_transaction = failed.preferences();
        auto pending = before_transaction;
        pending.filename_policy = FilenamePolicy::guid;
        pending.ui_language = UiLanguagePreference::japanese;
        const auto failed_transaction = failed.save_from_settings(pending);
        require(!failed_transaction.persisted && failed.preferences() == before_transaction &&
            text(blocked) == "not a directory");
        std::cout << "Settings parser/native atomic writer/whole-object/snapshot/restart/failure tests: passed\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

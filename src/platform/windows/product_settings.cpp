#include "audit/audit_policy.h"
#include "platform/windows/product_settings.h"
#include "platform/windows/unicode_diagnostics.h"
#include <Windows.h>
#include <ShlObj.h>
#include <array>
#include <atomic>
#include <charconv>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace clipss2::platform::windows
{
namespace
{
using namespace codec::jpeg;
class FileHandle final
{
public:
    explicit FileHandle(HANDLE handle) : handle_(handle) {}
    ~FileHandle() { if (handle_ != INVALID_HANDLE_VALUE) (void)CloseHandle(handle_); }
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    HANDLE get() const noexcept { return handle_; }
    void close()
    {
        if (!CloseHandle(handle_)) throw std::runtime_error("close: Win32 error " + std::to_string(GetLastError()));
        handle_ = INVALID_HANDLE_VALUE;
    }
private:
    HANDLE handle_;
};
[[noreturn]] void fail(const char* stage)
{ throw std::runtime_error(std::string{stage} + ": Win32 error " + std::to_string(GetLastError())); }
SettingsLoadResult rejected(SettingsStatus status, std::string diagnostic, bool writable = true)
{ return {{}, status, writable, std::move(diagnostic)}; }
const char* backend_token(JpegBackendId id)
{
    switch (id)
    {
    case JpegBackendId::automatic: return "automatic";
    case JpegBackendId::turbo: return "turbo";
    case JpegBackendId::jpegli: return "jpegli";
    case JpegBackendId::mozjpeg: return "mozjpeg";
    case JpegBackendId::wic: return "wic";
    }
    throw std::invalid_argument("Unknown settings backend enum.");
}
void print_load(const SettingsLoadResult& loaded)
{
    if (loaded.status == SettingsStatus::no_settings) std::cout << "Settings: NoSettings; safe defaults\n";
    else if (loaded.status == SettingsStatus::loaded) std::cout << "Settings: Loaded\n";
    else std::cerr << "Settings warning: " << loaded.diagnostic << "; safe defaults"
        << (loaded.persistence_enabled ? "\n" : "; persistence disabled for this run\n");
}
}

SettingsLoadResult parse_product_settings(std::string_view bytes,
    std::span<const codec::jpeg::JpegBackendCapabilities> catalog)
{
    using namespace codec::jpeg;
    if (bytes.size() > maximum_settings_bytes)
        return rejected(SettingsStatus::invalid_settings, "InvalidSettings: oversized file", false);
    // Inspect version first, before interpreting future fields. Even otherwise
    // malformed newer files must not be repaired by this older writer.
    for (std::size_t offset = 0; offset < bytes.size();)
    {
        const auto end = bytes.find('\n', offset);
        auto line = bytes.substr(offset, end == std::string_view::npos ? bytes.size() - offset : end - offset);
        if (line.ends_with('\r')) line.remove_suffix(1);
        // BOM is still invalid in our canonical schema, but must not hide a
        // future version from the non-destructive protection pass.
        if (offset == 0 && line.starts_with("\xef\xbb\xbf")) line.remove_prefix(3);
        constexpr std::string_view prefix = "schema_version=";
        if (line.starts_with(prefix))
        {
            const auto token = line.substr(prefix.size());
            unsigned int version{};
            const auto [last, error] = std::from_chars(token.data(), token.data() + token.size(), version);
            if (error == std::errc::result_out_of_range ||
                (error == std::errc{} && last == token.data() + token.size() && version > 7))
                return rejected(SettingsStatus::unsupported_schema, "UnsupportedSchema: newer version", false);
        }
        if (end == std::string_view::npos) break;
        offset = end + 1;
    }
    try
    {
        if (bytes.find('\0') != std::string_view::npos) throw std::invalid_argument("embedded NUL");
        constexpr std::array keys{"schema_version", "file_format", "jpeg_profile", "jpeg_backend",
            "filename_policy", "output_directory_mode", "output_directory_path", "ui_language",
            "avif_profile", "jxl_profile"};
        std::array<std::string_view, 10> values{};
        std::array<bool, 10> seen{};
        for (std::size_t offset = 0; offset < bytes.size();)
        {
            const auto end = bytes.find('\n', offset);
            auto line = bytes.substr(offset, end == std::string_view::npos ? bytes.size() - offset : end - offset);
            if (line.ends_with('\r')) line.remove_suffix(1);
            const auto equals = line.find('=');
            if (equals == std::string_view::npos)
                throw std::invalid_argument("malformed line");
            std::size_t index{};
            while (index < keys.size() && line.substr(0, equals) != keys[index]) ++index;
            if (index == keys.size()) throw std::invalid_argument("unknown key");
            if (seen[index]) throw std::invalid_argument("duplicate key");
            seen[index] = true; values[index] = line.substr(equals + 1);
            if (end == std::string_view::npos) break;
            offset = end + 1;
        }
        if (values[0] != "1" && values[0] != "2" && values[0] != "3" &&
            values[0] != "4" && values[0] != "5" && values[0] != "6" && values[0] != "7")
            throw std::invalid_argument("invalid schema token");
        const std::size_t required = values[0] == "1" ? 4 : values[0] == "2" ? 7 : values[0] == "7" ? 10 : 8;
        for (std::size_t i = 0; i < seen.size(); ++i)
            if (seen[i] != (i < required)) throw std::invalid_argument("missing or unexpected schema key");
        ProductPreferences candidate;
        if (required >= 7)
        {
            candidate.filename_policy = parse_filename_policy(values[4]);
            if (values[5] == "default")
            {
                if (!values[6].empty()) throw std::invalid_argument("Default output path must be empty");
            }
            else if (values[5] == "explicit")
            {
                candidate.output_directory.explicit_path = path_from_utf8(values[6]);
                validate_output_path(*candidate.output_directory.explicit_path);
            }
            else throw std::invalid_argument("unknown output directory mode");
        }
        if (required >= 8)
        {
            if (values[7] == "system") candidate.ui_language = UiLanguagePreference::system;
            else if (values[7] == "english") candidate.ui_language = UiLanguagePreference::english;
            else if (values[7] == "japanese") candidate.ui_language = UiLanguagePreference::japanese;
            else throw std::invalid_argument("unknown UI language token");
        }
        if (values[1] == "jpeg") candidate.file_format = image::ImageFileFormat::jpeg;
        else if (values[1] == "webp" && (values[0] == "4" || values[0] == "5" || values[0] == "6" || values[0] == "7"))
            candidate.file_format = image::ImageFileFormat::webp;
        else if (values[1] == "avif" && (values[0] == "5" || values[0] == "6" || values[0] == "7")) candidate.file_format = image::ImageFileFormat::avif;
        else if (values[1] == "jxl" && (values[0] == "6" || values[0] == "7")) candidate.file_format = image::ImageFileFormat::jxl;
        else if (values[1] != "png") throw std::invalid_argument("unknown file format token");
        if (required == 10)
        {
            candidate.avif_profile = capture::parse_file_compression(values[8]);
            candidate.jxl_profile = capture::parse_file_compression(values[9]);
        }
        JpegBackendId backend;
        if (values[3] == "automatic") backend = JpegBackendId::automatic;
        else if (values[3] == "turbo") backend = JpegBackendId::turbo;
        else if (values[3] == "jpegli") backend = JpegBackendId::jpegli;
        else if (values[3] == "mozjpeg") backend = JpegBackendId::mozjpeg;
        else if (values[3] == "wic") backend = JpegBackendId::wic;
        else throw std::invalid_argument("unknown backend token");
        if (values[2] == "default")
        {
            if (backend != JpegBackendId::automatic) throw std::invalid_argument("Default requires Automatic");
        }
        else
        {
            JpegProfile profile;
            if (values[2] == "fast") profile = JpegProfile::fast;
            else if (values[2] == "balanced") profile = JpegProfile::balanced;
            else if (values[2] == "smallest") profile = JpegProfile::smallest;
            else throw std::invalid_argument("unknown profile token");
            candidate.jpeg_selection = JpegProfileSelection{profile, backend};
        }
        // Hidden PNG JPEG preferences must be applicable as a whole as well.
        if (backend != JpegBackendId::automatic)
        {
            bool available{};
            for (const auto& entry : catalog) if (entry.id == backend) available = entry.available;
            if (!available) return rejected(SettingsStatus::known_backend_unavailable,
                "KnownBackendUnavailable: saved " + std::string{backend_token(backend)});
        }
        try { (void)resolve_jpeg_product_selection(candidate.jpeg_selection, catalog); }
        catch (const std::invalid_argument& error)
        { return rejected(SettingsStatus::unsupported_selection, "UnsupportedSelection: " + std::string{error.what()}); }
        return {std::move(candidate), SettingsStatus::loaded, true, {}};
    }
    catch (const std::invalid_argument& error)
    { return rejected(SettingsStatus::invalid_settings, "InvalidSettings: " + std::string{error.what()}); }
}

void validate_product_preferences(const ProductPreferences& preferences)
{
    if (preferences.file_format != image::ImageFileFormat::png &&
        preferences.file_format != image::ImageFileFormat::jpeg &&
        preferences.file_format != image::ImageFileFormat::webp &&
        preferences.file_format != image::ImageFileFormat::avif &&
        preferences.file_format != image::ImageFileFormat::jxl)
        throw std::invalid_argument("Unknown settings file format enum.");
    (void)codec::jpeg::resolve_jpeg_product_selection(preferences.jpeg_selection);
    (void)capture::file_compression_token(preferences.avif_profile);
    (void)capture::file_compression_token(preferences.jxl_profile);
    (void)filename_policy_token(preferences.filename_policy);
    if (preferences.output_directory.explicit_path)
    {
        validate_output_path(*preferences.output_directory.explicit_path);
        (void)path_utf8(*preferences.output_directory.explicit_path);
    }
    switch (preferences.ui_language)
    {
    case UiLanguagePreference::system:
    case UiLanguagePreference::english:
    case UiLanguagePreference::japanese: break;
    default: throw std::invalid_argument("Unknown UI language preference enum.");
    }
}
std::string serialize_product_settings(const ProductPreferences& preferences)
{
    using namespace codec::jpeg;
    validate_product_preferences(preferences);
    const auto* profile = std::get_if<JpegProfileSelection>(&preferences.jpeg_selection);
    const char* language = preferences.ui_language == UiLanguagePreference::system ? "system" :
        preferences.ui_language == UiLanguagePreference::english ? "english" : "japanese";
    auto bytes = std::string{"schema_version=7\nfile_format="} +
        image::image_file_format_token(preferences.file_format) +
        "\njpeg_profile=" + (profile ? jpeg_profile_token(profile->profile) : "default") +
        "\njpeg_backend=" + (profile ? backend_token(profile->backend) : "automatic") +
        "\nfilename_policy=" + filename_policy_token(preferences.filename_policy) +
        "\noutput_directory_mode=" + (preferences.output_directory.explicit_path ? "explicit" : "default") +
        "\noutput_directory_path=" + (preferences.output_directory.explicit_path ? path_utf8(*preferences.output_directory.explicit_path) : "") +
        "\nui_language=" + language +
        "\navif_profile=" + capture::file_compression_token(preferences.avif_profile) +
        "\njxl_profile=" + capture::file_compression_token(preferences.jxl_profile) + "\n";
    if (bytes.size() > maximum_settings_bytes) throw std::invalid_argument("Serialized settings exceeds file size bound.");
    return bytes;
}

std::filesystem::path default_product_settings_path()
{
    return default_settings_store().path;
}
SettingsLoadResult load_product_settings(const std::filesystem::path& path)
{
    try
    {
        const FileHandle file{CreateFileW(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (file.get() == INVALID_HANDLE_VALUE)
        {
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return {};
            fail("read/open");
        }
        if (GetFileType(file.get()) != FILE_TYPE_DISK)
            throw std::runtime_error("read: settings must be a disk file");
        std::string bytes(maximum_settings_bytes + 1, '\0');
        DWORD count{};
        if (!ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr)) fail("read");
        bytes.resize(count);
        return parse_product_settings(bytes);
    }
    catch (const std::exception& error)
    { return rejected(SettingsStatus::read_failure, "ReadFailure: " + std::string{error.what()}, false); }
}

SettingsWriteResult write_product_settings(const std::filesystem::path& path,
    const ProductPreferences& preferences, SettingsWriteFault fault)
{
    std::filesystem::path temporary;
    bool owns_temporary{};
    try
    {
        const std::string bytes = serialize_product_settings(preferences);
        // Preserve a newer file even if it appeared after startup. This check
        // narrows, but does not eliminate, cross-process version races.
        const auto previous = load_product_settings(path);
        if (!previous.persistence_enabled) return {false, previous.diagnostic + "; write suppressed"};
        std::filesystem::create_directories(path.parent_path());
        static std::atomic<unsigned long long> counter{};
        HANDLE opened = INVALID_HANDLE_VALUE;
        for (unsigned int attempt = 0; attempt != 32; ++attempt)
        {
            temporary = path;
            temporary += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." +
                std::to_wstring(counter.fetch_add(1, std::memory_order_relaxed));
            opened = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (opened != INVALID_HANDLE_VALUE) break;
            if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) fail("temp/create");
            temporary.clear(); // Never remove a different writer's temp.
        }
        if (opened == INVALID_HANDLE_VALUE) throw std::runtime_error("temp/create: unique name attempts exhausted");
        owns_temporary = true;
        FileHandle file{opened};
        DWORD count{};
        if (fault == SettingsWriteFault::temp_write) throw std::runtime_error("temp/write: injected failure");
        if (!WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr)) fail("temp/write");
        if (count != bytes.size()) throw std::runtime_error("temp/write: short write");
        if (fault == SettingsWriteFault::flush) throw std::runtime_error("flush: injected failure");
        if (!FlushFileBuffers(file.get())) fail("flush");
        file.close();
        if (fault == SettingsWriteFault::replace) throw std::runtime_error("replace: injected failure");
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) fail("replace");
        temporary.clear();
        return {true, {}};
    }
    catch (const std::exception& error)
    {
        std::string diagnostic = "WriteFailure: " + std::string{error.what()};
        if (owns_temporary && !temporary.empty() && !DeleteFileW(temporary.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND)
            diagnostic += "; temp cleanup failed: Win32 error " + std::to_string(GetLastError());
        return {false, std::move(diagnostic)};
    }
}
ProductSettings::ProductSettings()
{
    try { store_ = default_settings_store(); path_ = store_.path; loaded_ = load_product_settings(path_); }
    catch (const std::exception& error)
    { loaded_ = rejected(SettingsStatus::read_failure, "ReadFailure/path: " + std::string{error.what()}, false); }
    print_load(loaded_);
    set_ui_language_preference(loaded_.preferences.ui_language);
    wchar_t verbose[2]{};
    if (clipss2::audit::environment_variable(L"_CLIPSS2_AUDIT_STORAGE_VERBOSE", verbose, 2) == 1 && verbose[0] == L'1')
        write_stdout_utf8(
            "Settings mode: " + std::string(path_.empty() ? "unresolved" :
                store_.mode == SettingsMode::portable ? "portable" : "per-user") +
            "\nSettings path: " + path_utf8(path_) + '\n');
}
ProductSettings::ProductSettings(std::filesystem::path path)
    : store_{SettingsMode::per_user, path, path.parent_path()},
      path_(std::move(path)), loaded_(load_product_settings(path_))
{
    print_load(loaded_);
    set_ui_language_preference(loaded_.preferences.ui_language);
}
void ProductSettings::commit(const WindowSelectionResult& snapshot)
{
    auto accepted = preferences();
    accepted.file_format = snapshot.file_format; accepted.jpeg_selection = snapshot.jpeg_selection;
    accepted.avif_profile = snapshot.avif_profile; accepted.jxl_profile = snapshot.jxl_profile;
    commit(std::move(accepted));
}
void ProductSettings::commit(ProductPreferences accepted)
{
    validate_product_preferences(accepted);
    loaded_.preferences = std::move(accepted); // Preference, not success history.
    set_ui_language_preference(loaded_.preferences.ui_language);
    if (!loaded_.persistence_enabled)
    {
        std::cerr << "Settings warning: updated in memory; persistence disabled for this run; capture continues\n";
        return;
    }
    const auto written = write_product_settings(path_, loaded_.preferences);
    if (written.persisted) std::cout << "Settings: persisted confirmed preference\n";
    else std::cerr << "Settings persistence failed: " << written.diagnostic
        << "; updated in memory, persistence failed; capture continues\n";
}

SettingsWriteResult ProductSettings::save_from_settings(
    ProductPreferences accepted, const SettingsWriteFault fault)
{
    validate_product_preferences(accepted);
    if (!loaded_.persistence_enabled)
        return {false, loaded_.diagnostic + "; write suppressed"};
    auto written = write_product_settings(path_, accepted, fault);
    if (!written.persisted) return written;
    loaded_.preferences = std::move(accepted);
    loaded_.status = SettingsStatus::loaded;
    loaded_.diagnostic.clear();
    set_ui_language_preference(loaded_.preferences.ui_language);
    return written;
}
}

#include "platform/windows/file_publication.h"
#include <Windows.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <iostream>
#include <vector>
#include <cstdio>

namespace clipss2::platform::windows
{
namespace
{
[[noreturn]] void fail(const char* stage, DWORD error = GetLastError())
{ throw std::runtime_error(std::string{stage} + ": Win32 error " + std::to_string(error)); }
class Handle final
{
public:
    explicit Handle(HANDLE h) : h_(h) {}
    ~Handle() { if (h_ != INVALID_HANDLE_VALUE) (void)CloseHandle(h_); }
    Handle(const Handle&) = delete; Handle& operator=(const Handle&) = delete;
    HANDLE get() const noexcept { return h_; }
    void close() { if (!CloseHandle(h_)) fail("publication/close"); h_ = INVALID_HANDLE_VALUE; }
private: HANDLE h_;
};
struct OwnedPath
{
    std::filesystem::path path;
    bool owned{};
    ~OwnedPath()
    {
        if (owned && !DeleteFileW(path.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND)
            std::cerr << "Publication temp/reservation cleanup failed: Win32 error " << GetLastError() << '\n';
    }
};
std::wstring guid_text()
{
    GUID id{}; const HRESULT result = CoCreateGuid(&id);
    if (FAILED(result)) throw std::runtime_error("CoCreateGuid failed: " + std::to_string(result));
    std::array<wchar_t, 39> text{};
    if (StringFromGUID2(id, text.data(), static_cast<int>(text.size())) != 39)
        throw std::runtime_error("GUID formatting failed.");
    std::wstring value{text.data() + 1, 36};
    for (auto& ch : value) if (ch >= L'A' && ch <= L'F') ch += L'a' - L'A';
    return value;
}
std::wstring timestamp_text()
{
    SYSTEMTIME time{}; GetLocalTime(&time);
    std::array<wchar_t, 32> text{};
    const int count = swprintf_s(text.data(), text.size(), L"%04u%02u%02u_%02u%02u%02u_%03u",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
    if (count != 19) throw std::runtime_error("Timestamp formatting failed.");
    return text.data();
}
std::wstring padded(std::uint64_t value, std::size_t width)
{
    auto text = std::to_wstring(value);
    if (text.size() < width) text.insert(0, width - text.size(), L'0');
    return text;
}
std::optional<std::uint64_t> sequence_number(std::wstring_view stem)
{
    constexpr std::wstring_view prefix = L"clipss2_";
    if (stem.size() < prefix.size()) return {};
    for (std::size_t i=0; i<prefix.size(); ++i)
    {
        auto ch=stem[i]; if (ch >= L'A' && ch <= L'Z') ch=static_cast<wchar_t>(ch + L'a' - L'A');
        if (ch != prefix[i]) return {};
    }
    const auto digits = stem.substr(prefix.size());
    if (digits.size() < 6 || digits.size() > 20) return {};
    std::uint64_t number{};
    for (const wchar_t digit : digits)
    {
        if (digit < L'0' || digit > L'9') return {};
        const auto unit = static_cast<std::uint64_t>(digit - L'0');
        if (number > (UINT64_MAX - unit) / 10) throw std::overflow_error("Sequence namespace overflows uint64.");
        number = number * 10 + unit;
    }
    if (!number || padded(number, 6) != digits) return {};
    return number;
}
std::uint64_t next_sequence(const std::filesystem::path& directory)
{
    std::uint64_t maximum{};
    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        // Unknown extensions participate; strict canonical decimal stem only.
        // Directory names also reserve the namespace, rather than retry forever.
        const auto number = sequence_number(entry.path().stem().native());
        if (number) maximum = std::max(maximum, *number);
    }
    if (maximum == UINT64_MAX) throw std::overflow_error("Sequence namespace exhausted.");
    return maximum + 1;
}
bool collision(DWORD error) { return error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS; }
}
std::filesystem::path publish_file_payload(const FileOutputOptions& options,
    std::string_view extension, std::span<const std::byte> bytes,
    const PayloadValidator& validate, const PublicationHooks& hooks)
{
    validate_output_path(options.directory); (void)filename_policy_token(options.filename_policy);
    if (extension.size() < 2 || extension.front() != '.' ||
        !std::all_of(extension.begin() + 1, extension.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }))
        throw std::invalid_argument("Invalid effective-format extension.");
    if (bytes.empty() || !validate) throw std::invalid_argument("Missing validated file payload.");
    if (bytes.size() > MAXDWORD) throw std::length_error("Publication payload exceeds native transfer bounds.");
    if (options.create_directory) std::filesystem::create_directories(options.directory);
    OwnedPath temporary;
    HANDLE opened = INVALID_HANDLE_VALUE;
    for (unsigned int attempt = 0; attempt < 128; ++attempt)
    {
        temporary.path = options.directory / (L".clipss2-" + guid_text() + L".tmp");
        opened = CreateFileW(temporary.path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
            nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (opened != INVALID_HANDLE_VALUE) { temporary.owned = true; break; }
        if (!collision(GetLastError())) fail("publication/temp-create");
    }
    if (opened == INVALID_HANDLE_VALUE) throw std::runtime_error("Unique temp attempts exhausted.");
    Handle file{opened};
    // Deliberately split writes: no final image is visible during incomplete write.
    const DWORD split = static_cast<DWORD>(bytes.size() / 2);
    DWORD written{};
    if (!WriteFile(file.get(), bytes.data(), split, &written, nullptr) || written != split) fail("publication/write");
    if (hooks.after_write) hooks.after_write(temporary.path);
    if (hooks.fault == PublicationFault::write) throw std::runtime_error("publication/write: injected failure");
    const DWORD rest = static_cast<DWORD>(bytes.size()) - split;
    if (!WriteFile(file.get(), bytes.data() + split, rest, &written, nullptr) || written != rest) fail("publication/write");
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(file.get(), zero, nullptr, FILE_BEGIN)) fail("publication/readback-seek");
    std::vector<std::byte> persisted(bytes.size()); DWORD read{};
    if (!ReadFile(file.get(), persisted.data(), static_cast<DWORD>(persisted.size()), &read, nullptr) || read != persisted.size()) fail("publication/readback");
    if (!std::equal(bytes.begin(), bytes.end(), persisted.begin())) throw std::runtime_error("Publication readback mismatch.");
    validate(persisted); // Encoder-specific full validation before final name.
    if (hooks.fault == PublicationFault::flush) throw std::runtime_error("publication/flush: injected failure");
    if (!FlushFileBuffers(file.get())) fail("publication/flush");
    file.close();
    const std::wstring ext{extension.begin(), extension.end()};
    const auto timestamp = options.filename_policy == FilenamePolicy::timestamp ?
        (hooks.timestamp ? hooks.timestamp() : timestamp_text()) : std::wstring{};
    std::uint64_t number = options.filename_policy == FilenamePolicy::sequence ? next_sequence(options.directory) : 0;
    unsigned int ambiguous_reservation_errors{};
    for (std::uint64_t attempt = 0; attempt < 100000; ++attempt)
    {
        OwnedPath reservation;
        std::wstring stem;
        if (options.filename_policy == FilenamePolicy::timestamp)
            stem = L"clipss2_" + timestamp + (attempt ? L"_" + padded(attempt, 3) : L"");
        else if (options.filename_policy == FilenamePolicy::guid)
            stem = L"clipss2_" + (hooks.guid ? hooks.guid() : guid_text());
        else
        {
            stem = L"clipss2_" + padded(number, 6);
            reservation.path = options.directory / (L"." + stem + L".reserve");
            const HANDLE lock = CreateFileW(reservation.path.c_str(), GENERIC_WRITE, 0, nullptr,
                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (lock == INVALID_HANDLE_VALUE)
            {
                const DWORD error = GetLastError();
                bool contention = collision(error);
                // CREATE_NEW can report sharing/access denial for a contended
                // or deletion-pending lease, not just FILE_EXISTS. Probing here
                // only classifies failure; it never authorizes an overwrite.
                if (!contention && (error == ERROR_SHARING_VIOLATION || error == ERROR_ACCESS_DENIED || error == ERROR_DELETE_PENDING))
                {
                    const DWORD attrs = GetFileAttributesW(reservation.path.c_str());
                    if (attrs != INVALID_FILE_ATTRIBUTES) contention = true;
                    else if (++ambiguous_reservation_errors <= 128) contention = true;
                }
                if (!contention) fail("publication/sequence-reserve", error);
                if (number == UINT64_MAX) throw std::overflow_error("Sequence exhausted.");
                number = std::max(number + 1, next_sequence(options.directory)); continue;
            }
            reservation.owned = true; Handle held{lock}; held.close();
            const auto current = next_sequence(options.directory);
            if (current > number) { number = current; continue; }
        }
        const auto final = options.directory / (stem + ext);
        if (hooks.before_move) hooks.before_move(final);
        if (hooks.fault == PublicationFault::move) throw std::runtime_error("publication/move: injected failure");
        // Same-directory move. Critically NO MOVEFILE_REPLACE_EXISTING flag.
        if (MoveFileExW(temporary.path.c_str(), final.c_str(), MOVEFILE_WRITE_THROUGH))
        { temporary.owned = false; return final; }
        const DWORD error = GetLastError();
        if (!collision(error)) fail("publication/no-replace-move");
        if (options.filename_policy == FilenamePolicy::sequence)
        {
            if (number == UINT64_MAX) throw std::overflow_error("Sequence exhausted.");
            number = std::max(number + 1, next_sequence(options.directory));
        }
    }
    throw std::runtime_error("Publication collision retry bound exhausted.");
}
}

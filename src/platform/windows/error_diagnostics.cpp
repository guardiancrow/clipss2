#include "platform/windows/error_diagnostics.h"
#include "audit/audit_policy.h"
#include "platform/windows/unicode_diagnostics.h"

#include <dxgi.h>
#include <winrt/base.h>

#include <cstdint>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <algorithm>

namespace clipss2::platform::windows
{
namespace
{
std::string hexadecimal(const std::uint32_t value)
{
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase << std::setw(8)
           << std::setfill('0') << value;
    return stream.str();
}

std::string utf8_message(const DWORD flags, const DWORD code)
{
    wchar_t* buffer{};
    const DWORD count = FormatMessageW(
        flags | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    if (count == 0 || buffer == nullptr)
        return "No system message is available.";

    const std::unique_ptr<void, decltype(&LocalFree)> release_buffer{
        buffer, &LocalFree};
    std::wstring_view wide{buffer, count};
    while (!wide.empty() &&
           (wide.back() == L'\r' || wide.back() == L'\n' || wide.back() == L' '))
        wide.remove_suffix(1);
    try { return utf8_from_utf16(wide); }
    catch (...) { return "A system message exists but UTF-8 conversion failed."; }
}

std::string hresult_details(const HRESULT result)
{
    const auto numeric = static_cast<std::uint32_t>(result);
    std::string text = "HRESULT " + hexadecimal(numeric);
    if (const char* name = known_hresult_name(result))
        text += " (" + std::string{name} + ')';
    text += ": " + utf8_message(FORMAT_MESSAGE_FROM_SYSTEM, numeric);
    return text;
}

const char* known_win32_name(const DWORD error) noexcept
{
    switch (error)
    {
    case ERROR_SUCCESS: return "ERROR_SUCCESS";
    case ERROR_ACCESS_DENIED: return "ERROR_ACCESS_DENIED";
    case ERROR_INVALID_HANDLE: return "ERROR_INVALID_HANDLE";
    case ERROR_INVALID_PARAMETER: return "ERROR_INVALID_PARAMETER";
    case ERROR_NOT_ENOUGH_MEMORY: return "ERROR_NOT_ENOUGH_MEMORY";
    case ERROR_OUTOFMEMORY: return "ERROR_OUTOFMEMORY";
    case ERROR_TIMEOUT: return "ERROR_TIMEOUT";
    case ERROR_HOTKEY_ALREADY_REGISTERED: return "ERROR_HOTKEY_ALREADY_REGISTERED";
    default: return nullptr;
    }
}
}

const char* known_hresult_name(const HRESULT result) noexcept
{
    switch (result)
    {
    case S_OK: return "S_OK";
    case E_FAIL: return "E_FAIL";
    case E_NOINTERFACE: return "E_NOINTERFACE";
    case REGDB_E_CLASSNOTREG: return "REGDB_E_CLASSNOTREG";
    case E_INVALIDARG: return "E_INVALIDARG";
    case E_OUTOFMEMORY: return "E_OUTOFMEMORY";
    case E_ACCESSDENIED: return "E_ACCESSDENIED";
    case DXGI_ERROR_WAS_STILL_DRAWING: return "DXGI_ERROR_WAS_STILL_DRAWING";
    case DXGI_ERROR_DEVICE_HUNG: return "DXGI_ERROR_DEVICE_HUNG";
    case DXGI_ERROR_DEVICE_REMOVED: return "DXGI_ERROR_DEVICE_REMOVED";
    case DXGI_ERROR_DEVICE_RESET: return "DXGI_ERROR_DEVICE_RESET";
    case DXGI_ERROR_DRIVER_INTERNAL_ERROR: return "DXGI_ERROR_DRIVER_INTERNAL_ERROR";
    default: return nullptr;
    }
}

std::string format_hresult(const std::string_view operation, const HRESULT result)
{
    return std::string{operation} + ": " + hresult_details(result);
}

std::string format_win32_error(
    const std::string_view operation, const DWORD error)
{
    std::string result = std::string{operation} + ": Win32 error " +
        std::to_string(error) + " (" + hexadecimal(error) + ')';
    if (const char* name = known_win32_name(error))
        result += " (" + std::string{name} + ')';
    return result + ": " + utf8_message(FORMAT_MESSAGE_FROM_SYSTEM, error);
}

std::string describe_exception(const std::exception_ptr error)
{
    try
    {
        if (error)
            std::rethrow_exception(error);
    }
    catch (const DiagnosticError& exception)
    {
        return exception.what(); // Constructed here from trusted UTF-8 and static context.
    }
    catch (const winrt::hresult_error& exception)
    {
        return hresult_details(exception.code());
    }
    catch (const std::system_error& exception)
    {
        return "system_error code=" + std::to_string(exception.code().value()) +
               "; message omitted (encoding/privacy not established)";
    }
    catch (const std::exception& exception)
    {
        const std::string_view message{exception.what()};
        // Preserve bounded ASCII CLI/policy messages, never guess an ANSI code page.
        if (message.size() <= 512 && std::all_of(message.begin(), message.end(),
                [](unsigned char c) { return c >= 0x20 && c < 0x7f; }) &&
            message.find('\\') == message.npos && message.find(":/") == message.npos &&
            !message.starts_with('/') && message.find("/Users/") == message.npos &&
            message.find("/home/") == message.npos)
            return std::string{message};
        return "standard exception; message omitted (encoding/privacy not established)";
    }
    catch (...)
    {
        return "unknown non-standard exception";
    }
    return "no exception";
}

void require_win32_success(
    const bool succeeded, const DWORD error, const std::string_view operation)
{
    if (!succeeded)
        throw DiagnosticError(format_win32_error(operation, error));
}

[[noreturn]] void throw_hresult_failure(
    const std::string_view operation, const HRESULT result)
{
    throw DiagnosticError(format_hresult(operation, result), result);
}

[[noreturn]] void throw_capture_stage_failure(CaptureStage stage, std::exception_ptr error)
{
    std::string detail;
    std::optional<HRESULT> result;
    try { std::rethrow_exception(error); }
    catch (const DiagnosticError& exception)
    {
        result = exception.result();
        detail = exception.what();
        if (result && detail.find(hexadecimal(static_cast<std::uint32_t>(*result))) == detail.npos)
            detail += "; " + hresult_details(*result);
    }
    catch (const winrt::hresult_error& exception)
    {
        result = exception.code();
        detail = hresult_details(*result);
    }
    catch (const std::system_error& exception)
    {
        if (exception.code().category() == std::system_category())
            detail = format_win32_error("system operation", static_cast<DWORD>(exception.code().value()));
        else detail = "system_error code=" + std::to_string(exception.code().value());
    }
    catch (const std::exception&)
    {
        detail = "standard exception; message omitted (encoding/privacy not established)";
    }
    catch (...) { detail = "unknown non-standard exception"; }
    std::string text = "stage=" + std::string{stage.name} + "; operation=" + std::string{stage.operation};
    if (!stage.requested_interface.empty()) text += "; interface=" + std::string{stage.requested_interface};
    text += "; " + detail;
    if (stage.name.ends_with(".factory") && stage.name.starts_with("color."))
    {
        text += result == E_NOINTERFACE || result == REGDB_E_CLASSNOTREG
            ? "; capability=unavailable" : "; capability=query-failed";
        text += "; DisplayInformation desktop color interop requires Windows 11 build 22621 or later; "
                "color state unknown; capture/output not permitted";
    }
    throw CaptureStageError(std::move(text), result);
}

void inject_capture_stage_failure_for_audit(std::string_view stage)
{
#if CLIPSS2_AUDIT_FEATURES
    wchar_t requested[80]{};
    const auto size = clipss2::audit::environment_variable(
        L"_CLIPSS2_AUDIT_CAPTURE_FAIL_STAGE", requested, 80);
    if (size != stage.size() || size >= 80) return;
    for (std::size_t i = 0; i < stage.size(); ++i)
        if (requested[i] != static_cast<wchar_t>(stage[i])) return;
    wchar_t hex[11]{};
    const auto count = clipss2::audit::environment_variable(
        L"_CLIPSS2_AUDIT_CAPTURE_FAIL_HRESULT", hex, 11);
    if (count != 10 || hex[0] != L'0' || hex[1] != L'x')
        throw std::invalid_argument("Audit failure requires an explicit eight-digit HRESULT.");
    std::uint32_t value{};
    for (std::size_t i = 2; i < 10; ++i)
    {
        const auto c = hex[i];
        const int digit = c >= L'0' && c <= L'9' ? c - L'0' :
            c >= L'A' && c <= L'F' ? c - L'A' + 10 : c >= L'a' && c <= L'f' ? c - L'a' + 10 : -1;
        if (digit < 0) throw std::invalid_argument("Invalid audit HRESULT.");
        value = (value << 4) | static_cast<std::uint32_t>(digit);
    }
    if (!FAILED(static_cast<HRESULT>(value))) throw std::invalid_argument("Audit HRESULT must fail.");
    throw winrt::hresult_error(static_cast<HRESULT>(value));
#else
    (void)stage;
#endif
}
}

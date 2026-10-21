#include "platform/windows/capture_worker_client.h"

#include "platform/windows/error_diagnostics.h"

#include <windows.h>
#include <winrt/base.h>

#include <algorithm>
#include <array>
#include <optional>
#include <chrono>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace clipss2::platform::windows
{
namespace
{
using Clock = std::chrono::steady_clock;

class ProcessAttributeList final
{
public:
    ProcessAttributeList()
    {
        SIZE_T bytes{};
        if (InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes) ||
            GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0)
            throw std::runtime_error("Could not size worker process attributes.");
        storage_.resize(bytes);
        list_ = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
        if (!InitializeProcThreadAttributeList(list_, 1, 0, &bytes)) winrt::throw_last_error();
    }
    ~ProcessAttributeList() { DeleteProcThreadAttributeList(list_); }
    ProcessAttributeList(const ProcessAttributeList&) = delete;
    ProcessAttributeList& operator=(const ProcessAttributeList&) = delete;
    LPPROC_THREAD_ATTRIBUTE_LIST get() const noexcept { return list_; }
private:
    std::vector<std::byte> storage_;
    LPPROC_THREAD_ATTRIBUTE_LIST list_{};
};

std::wstring widen_ascii(const std::string_view value)
{
    return {value.begin(), value.end()};
}

CaptureWorkerClientStatus status_from_exit_code(const DWORD exit_code) noexcept
{
    switch (static_cast<CaptureWorkerExit>(exit_code))
    {
    case CaptureWorkerExit::success: return CaptureWorkerClientStatus::success;
    case CaptureWorkerExit::stale_target: return CaptureWorkerClientStatus::stale_target;
    case CaptureWorkerExit::capture_failure:
        return CaptureWorkerClientStatus::capture_failure;
    case CaptureWorkerExit::file_failure: return CaptureWorkerClientStatus::file_failure;
    case CaptureWorkerExit::clipboard_failure:
        return CaptureWorkerClientStatus::clipboard_failure;
    case CaptureWorkerExit::partial_success:
        return CaptureWorkerClientStatus::partial_success;
    case CaptureWorkerExit::timeout: return CaptureWorkerClientStatus::timeout;
    case CaptureWorkerExit::invalid_argument:
        return CaptureWorkerClientStatus::invalid_request;
    }
    return CaptureWorkerClientStatus::unexpected_exit;
}

bool terminate_and_reap(const HANDLE process, const UINT exit_code) noexcept
{
    if (!TerminateProcess(process, exit_code))
    {
        if (WaitForSingleObject(process, 0) != WAIT_OBJECT_0) return false;
    }
    return WaitForSingleObject(process, INFINITE) == WAIT_OBJECT_0;
}

CaptureWorkerClientResult failure(
    const CaptureWorkerClientStatus status,
    const Clock::time_point started,
    std::string diagnostic,
    const bool forced = false)
{
    return {
        status,
        0,
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started),
        forced,
        std::move(diagnostic)};
}
}

const char* capture_worker_client_status_name(
    const CaptureWorkerClientStatus status) noexcept
{
    switch (status)
    {
    case CaptureWorkerClientStatus::success: return "success";
    case CaptureWorkerClientStatus::stale_target: return "stale target";
    case CaptureWorkerClientStatus::invalid_request: return "invalid request";
    case CaptureWorkerClientStatus::capture_failure: return "capture failure";
    case CaptureWorkerClientStatus::file_failure: return "file failure";
    case CaptureWorkerClientStatus::clipboard_failure: return "clipboard failure";
    case CaptureWorkerClientStatus::partial_success: return "partial success";
    case CaptureWorkerClientStatus::timeout: return "timeout";
    case CaptureWorkerClientStatus::launch_failure: return "launch failure";
    case CaptureWorkerClientStatus::wait_failure: return "wait failure";
    case CaptureWorkerClientStatus::unexpected_exit: return "unexpected worker exit";
    }
    return "unknown";
}

std::filesystem::path current_executable_path()
{
    constexpr DWORD maximum_length = 32768;
    DWORD capacity = MAX_PATH;
    for (;;)
    {
        std::vector<wchar_t> buffer(capacity);
        SetLastError(ERROR_SUCCESS);
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), capacity);
        if (length == 0)
            throw std::runtime_error(format_win32_error(
                "GetModuleFileNameW failed", GetLastError()));
        if (length < capacity)
            return std::filesystem::path{std::wstring{buffer.data(), length}};
        if (capacity == maximum_length)
            throw std::runtime_error(
                "GetModuleFileNameW returned a truncated executable path.");
        capacity = std::min(maximum_length, capacity * 2);
    }
}

std::wstring quote_windows_command_line_argument(const std::wstring_view argument)
{
    if (!argument.empty() &&
        argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos)
        return std::wstring{argument};

    std::wstring quoted{L'\"'};
    std::size_t backslashes{};
    for (const wchar_t character : argument)
    {
        if (character == L'\\')
        {
            ++backslashes;
            continue;
        }
        if (character == L'\"')
        {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(character);
        }
        else
        {
            quoted.append(backslashes, L'\\');
            quoted.push_back(character);
        }
        backslashes = 0;
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

std::wstring build_capture_worker_command_line(
    const std::filesystem::path& executable,
    const CaptureWorkerRequest& request)
{
    if (executable.empty())
        throw std::invalid_argument("Capture worker executable path is empty.");
    std::wstring command = quote_windows_command_line_argument(executable.wstring());
    command += L" --capture-worker";
    for (const auto& argument : serialize_capture_worker_request(request))
    {
        command.push_back(L' ');
        command += quote_windows_command_line_argument(widen_ascii(argument));
    }
    // CreateProcessW includes the terminating NUL in its 32,767-character limit.
    if (command.size() >= 32767)
        throw std::length_error("Capture worker command line is too long.");
    return command;
}

CaptureWorkerClientResult launch_capture_worker_at(
    const std::filesystem::path& executable,
    const CaptureWorkerRequest& request,
    const std::chrono::milliseconds timeout) noexcept
{
    const auto started = Clock::now();
    std::wstring command;
    try
    {
        command = build_capture_worker_command_line(executable, request);
    }
    catch (...)
    {
        return failure(
            CaptureWorkerClientStatus::invalid_request, started,
            describe_exception(std::current_exception()));
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(STARTUPINFOW);
    std::array<winrt::handle, 3> duplicated;
    std::array<HANDLE, 3> inheritance{};
    std::optional<ProcessAttributeList> attributes;
    std::size_t handle_count{};
    try
    {
        if (!request.quiet)
        {
            const std::array<DWORD, 3> ids{STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
            std::array<HANDLE*, 3> streams{&startup.StartupInfo.hStdInput,
                &startup.StartupInfo.hStdOutput, &startup.StartupInfo.hStdError};
            for (std::size_t i = 0; i < ids.size(); ++i)
            {
                const auto original = GetStdHandle(ids[i]);
                *streams[i] = original;
                if (!original || original == INVALID_HANDLE_VALUE) continue;
                const auto type = GetFileType(original);
                if (type != FILE_TYPE_PIPE && type != FILE_TYPE_DISK) continue;
                HANDLE copy{};
                if (!DuplicateHandle(GetCurrentProcess(), original, GetCurrentProcess(),
                    &copy, 0, TRUE, DUPLICATE_SAME_ACCESS)) winrt::throw_last_error();
                duplicated[i].attach(copy);
                *streams[i] = copy;
                inheritance[handle_count++] = copy;
            }
            if (handle_count != 0)
            {
                attributes.emplace();
                if (!UpdateProcThreadAttribute(attributes->get(), 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                    inheritance.data(), handle_count * sizeof(HANDLE), nullptr, nullptr)) winrt::throw_last_error();
                startup.lpAttributeList = attributes->get();
                startup.StartupInfo.cb = sizeof(startup);
                startup.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
            }
        }
    }
    catch (...)
    {
        return failure(CaptureWorkerClientStatus::launch_failure, started,
            describe_exception(std::current_exception()));
    }
    PROCESS_INFORMATION information{};
    const DWORD flags = (request.quiet ? CREATE_NO_WINDOW : 0) |
        (attributes ? EXTENDED_STARTUPINFO_PRESENT : 0);
    // Preserve the real parent -> selector teardown -> child ordering in redirected
    // diagnostic logs as well as at runtime.
    std::cout.flush();
    std::cerr.flush();
    if (!CreateProcessW(
            executable.c_str(), command.data(), nullptr, nullptr, attributes ? TRUE : FALSE, flags,
            nullptr, nullptr, &startup.StartupInfo, &information))
        return failure(
            CaptureWorkerClientStatus::launch_failure, started,
            format_win32_error("CreateProcessW capture worker failed", GetLastError()));

    winrt::handle process{information.hProcess};
    attributes.reset();
    for (auto& copy : duplicated) copy.close();
    winrt::handle thread{information.hThread};
    thread.close();
    const auto bounded_timeout = std::max(timeout, std::chrono::milliseconds{1});
    const auto deadline = Clock::now() + bounded_timeout;
    for (;;)
    {
        const auto now = Clock::now();
        if (now >= deadline)
        {
            const bool reaped = terminate_and_reap(
                process.get(), static_cast<UINT>(CaptureWorkerExit::timeout));
            auto result = failure(
                reaped ? CaptureWorkerClientStatus::timeout :
                         CaptureWorkerClientStatus::wait_failure,
                started,
                reaped ? "Capture worker exceeded its timeout and was terminated." :
                         "Capture worker timed out and could not be terminated/reaped.",
                true);
            result.exit_code = static_cast<std::uint32_t>(CaptureWorkerExit::timeout);
            return result;
        }

        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - now);
        const auto wait_milliseconds = static_cast<DWORD>(std::min<std::int64_t>(
            std::numeric_limits<DWORD>::max() - 1,
            std::max<std::int64_t>(1, remaining.count())));
        const HANDLE process_handle = process.get();
        const DWORD wait = MsgWaitForMultipleObjectsEx(
            1, &process_handle, wait_milliseconds, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wait == WAIT_OBJECT_0) break;
        if (wait == WAIT_OBJECT_0 + 1)
        {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            continue;
        }
        if (wait == WAIT_TIMEOUT) continue;

        const DWORD wait_error = GetLastError();
        const bool reaped = terminate_and_reap(
            process.get(), static_cast<UINT>(CaptureWorkerExit::capture_failure));
        return failure(
            CaptureWorkerClientStatus::wait_failure, started,
            format_win32_error(
                reaped ? "Wait for capture worker failed; worker terminated" :
                         "Wait for capture worker failed; termination/reap also failed",
                wait_error),
            true);
    }

    DWORD exit_code{};
    if (!GetExitCodeProcess(process.get(), &exit_code))
        return failure(
            CaptureWorkerClientStatus::wait_failure, started,
            format_win32_error(
                "GetExitCodeProcess capture worker failed", GetLastError()));
    return {
        status_from_exit_code(exit_code),
        exit_code,
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started),
        false,
        {}};
}

CaptureWorkerClientResult launch_capture_worker(
    const CaptureWorkerRequest& request,
    const std::chrono::milliseconds timeout) noexcept
{
    const auto started = Clock::now();
    try
    {
        return launch_capture_worker_at(current_executable_path(), request, timeout);
    }
    catch (...)
    {
        return failure(
            CaptureWorkerClientStatus::launch_failure, started,
            describe_exception(std::current_exception()));
    }
}
}

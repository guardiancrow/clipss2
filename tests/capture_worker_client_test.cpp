#include "platform/windows/capture_worker_client.h"

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

using namespace clipss2::platform::windows;

namespace
{
class LocalArguments final
{
public:
    explicit LocalArguments(wchar_t** value) noexcept : value_(value) {}
    ~LocalArguments() { if (value_) (void)LocalFree(value_); }
    LocalArguments(const LocalArguments&) = delete;
    LocalArguments& operator=(const LocalArguments&) = delete;
private:
    wchar_t** value_{};
};
}

int main(const int argc, char**)
{
    // launch_capture_worker_at invokes this binary with the private worker
    // arguments during the bounded-timeout test below.
    if (argc > 1)
    {
        std::this_thread::sleep_for(std::chrono::seconds{30});
        return 0;
    }
    const std::filesystem::path executable =
        L"C:\\Program Files\\clipss2 \u30c6\u30b9\u30c8\\clipss2.exe";
    const CaptureWorkerRequest request{
        static_cast<std::uintptr_t>(0x12345678), 42,
        L"clipss2. Class \\ \" \u30c6\u30b9\u30c8", true,
        CaptureBorderPolicy::system_required,
        clipss2::capture::CaptureDestination::both, clipss2::image::ImageFileFormat::jpeg, false};
    auto command = build_capture_worker_command_line(executable, request);
    int argument_count{};
    wchar_t** raw_arguments = CommandLineToArgvW(command.c_str(), &argument_count);
    if (!raw_arguments) { std::cerr << "CommandLineToArgvW failed\n"; return 1; }
    const LocalArguments arguments{raw_arguments};
    if (argument_count != 13 || raw_arguments[0] != executable.wstring() ||
        std::wstring_view{raw_arguments[1]} != L"--capture-worker")
    {
        std::cerr << "argv prefix mismatch; argc=" << argument_count << '\n';
        return 1;
    }

    const auto serialized = serialize_capture_worker_request(request);
    for (std::size_t index = 0; index < serialized.size(); ++index)
    {
        const std::wstring expected{serialized[index].begin(), serialized[index].end()};
        if (raw_arguments[index + 2] != expected)
        {
            std::cerr << "serialized argv mismatch at " << index << '\n';
            return 1;
        }
    }

    if (quote_windows_command_line_argument(L"") != L"\"\"" ||
        quote_windows_command_line_argument(LR"(a b\)") != LR"("a b\\")")
    {
        std::cerr << "quoting edge-case mismatch\n";
        return 1;
    }

    const auto current = current_executable_path();
    if (!current.is_absolute() || !std::filesystem::exists(current))
    {
        std::cerr << "current executable path mismatch\n";
        return 1;
    }

    const auto launch = launch_capture_worker_at(
        current.parent_path() / L"does not exist" / L"clipss2.exe", request);
    if (launch.status != CaptureWorkerClientStatus::launch_failure ||
        launch.forced_termination || launch.diagnostic.empty())
    {
        std::cerr << "launch-failure mapping mismatch\n";
        return 1;
    }

    const auto timeout = launch_capture_worker_at(
        current, request, std::chrono::milliseconds{20});
    if (timeout.status != CaptureWorkerClientStatus::timeout ||
        !timeout.forced_termination ||
        timeout.exit_code != static_cast<std::uint32_t>(CaptureWorkerExit::timeout))
    {
        std::cerr << "timeout mapping/reap mismatch\n";
        return 1;
    }
}

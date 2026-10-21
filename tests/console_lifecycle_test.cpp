#include "platform/windows/unicode_diagnostics.h"
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace clipss2::platform::windows;
namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
struct Handle
{
    HANDLE value{};
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct StandardHandles
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE), err = GetStdHandle(STD_ERROR_HANDLE);
    ~StandardHandles()
    {
        SetStdHandle(STD_OUTPUT_HANDLE, out); SetStdHandle(STD_ERROR_HANDLE, err);
        std::cout.clear(); std::cerr.clear();
    }
};
void diagnostics()
{
    StandardHandles restore;
    const std::string text = utf8_from_utf16(L"診断 日本語 😀\n");
    for (HANDLE missing : {HANDLE{}, INVALID_HANDLE_VALUE})
    {
        SetStdHandle(STD_OUTPUT_HANDLE, missing); SetStdHandle(STD_ERROR_HANDLE, missing);
        write_stdout_utf8(text); write_stderr_utf8(text);
    }
    Handle reader, writer;
    require(CreatePipe(&reader.value, &writer.value, nullptr, 0), "CreatePipe");
    SetStdHandle(STD_OUTPUT_HANDLE, writer.value);
    SetStdHandle(STD_ERROR_HANDLE, writer.value);
    // A stale CRT stream must not prevent a valid current Win32 sink working.
    std::cout.setstate(std::ios::badbit); std::cerr.setstate(std::ios::badbit);
    write_stdout_utf8(text); write_stderr_utf8(text);
    DWORD available{};
    require(PeekNamedPipe(reader.value, nullptr, 0, nullptr, &available, nullptr), "pipe availability");
    require(available == text.size() * 2, "pipe UTF-8 length");
    std::string observed(available, '\0'); DWORD read{};
    require(ReadFile(reader.value, observed.data(), available, &read, nullptr), "pipe read");
    require(observed == text + text, "pipe Japanese bytes");
    CloseHandle(reader.value); reader.value = nullptr;
    write_stdout_utf8(text); write_stderr_utf8(text); // Broken pipe is nonfatal.
    const HANDLE closed = writer.value;
    CloseHandle(writer.value); writer.value = nullptr;
    SetStdHandle(STD_OUTPUT_HANDLE, closed); SetStdHandle(STD_ERROR_HANDLE, closed);
    write_stdout_utf8(text); write_stderr_utf8(text); // Closed handle is nonfatal.

    // Explicitly owned temporary file, removed on close. Never production logs.
    wchar_t cwd[32768]{};
    require(GetCurrentDirectoryW(32768, cwd) != 0, "working directory");
    const std::wstring path = std::wstring(cwd) + L"/console-test-" + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    Handle file{CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr)};
    require(file.value != INVALID_HANDLE_VALUE, "owned temporary file");
    SetStdHandle(STD_OUTPUT_HANDLE, file.value);
    write_stdout_utf8(text);
    require(SetFilePointer(file.value, 0, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER, "seek");
    observed.assign(text.size(), '\0');
    require(ReadFile(file.value, observed.data(), static_cast<DWORD>(observed.size()), &read, nullptr), "file read");
    require(observed == text, "file UTF-8 bytes");
}
void cli(const std::wstring& exe, const wchar_t* arguments, DWORD expected, int sink)
{
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    Handle reader, writer;
    if (sink >= 2) require(CreatePipe(&reader.value, &writer.value, &security, 0), "CLI pipe");
    if (sink == 3) { CloseHandle(reader.value); reader.value = nullptr; }
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = startup.hStdError = sink == 0 ? nullptr : sink == 1 ? INVALID_HANDLE_VALUE : writer.value;
    PROCESS_INFORMATION child{};
    std::wstring command = L"\"" + exe + L"\" " + arguments;
    // sink 2 uses flags=0: policy, not CREATE_NO_WINDOW, suppresses allocation.
    require(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE,
        sink == 2 ? 0 : CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child), "CLI launch");
    Handle process{child.hProcess}, thread{child.hThread};
    CloseHandle(writer.value); writer.value = nullptr;
    if (WaitForSingleObject(process.value, 15000) != WAIT_OBJECT_0)
    {
        TerminateProcess(process.value, 99); WaitForSingleObject(process.value, 5000);
        throw std::runtime_error("CLI timeout/reaped");
    }
    DWORD exit{}; require(GetExitCodeProcess(process.value, &exit), "CLI exit");
    require(exit == expected, "CLI exit changed with diagnostic sink");
    if (sink == 2)
    {
        char buffer[4096]{}; DWORD read{};
        require(ReadFile(reader.value, buffer, sizeof(buffer), &read, nullptr) && read > 0, "CLI output absent");
        const std::string output(buffer, read);
        require(output.find(expected == 0 ? "Usage: clipss2" : expected == 1 ? "Error:" : "worker") != std::string::npos,
            "CLI output contract");
    }
}
}
int wmain(int argc, wchar_t** argv)
{
    try
    {
        require(argc == 2, "shipping executable argument required");
        diagnostics();
        for (int sink = 0; sink < 4; ++sink)
        {
            cli(argv[1], L"--help", 0, sink);
            cli(argv[1], L"--unknown", 1, sink);
            cli(argv[1], L"--capture-worker", 64, sink);
        }
        std::cout << "Console diagnostics/CLI: 12 launches plus Japanese pipe/file/missing/closed/broken handles PASS\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

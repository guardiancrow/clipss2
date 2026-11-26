#include "platform/windows/product_settings.h"
#include "platform/windows/settings_io_lock.h"
#include <Windows.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string_view>

namespace fs = std::filesystem;
using namespace clipss2::platform::windows;

namespace
{
constexpr std::string_view future_bytes = "schema_version=8\nfuture_setting=keep\n";
constexpr DWORD child_timeout = 10000;
HANDLE ready_to_publish{};
HANDLE allow_publish{};

void require(bool condition, const std::source_location at = std::source_location::current())
{
    if (!condition) throw std::runtime_error("Settings process test failed at line " + std::to_string(at.line()));
}

struct CloseHandleOnExit
{
    void operator()(HANDLE value) const noexcept { (void)CloseHandle(value); }
};
using Handle = std::unique_ptr<void, CloseHandleOnExit>;

struct TempDirectory
{
    fs::path path = fs::temp_directory_path() /
        (L"clipss2-settings-process-" + std::to_wstring(GetCurrentProcessId()) +
            L"-" + std::to_wstring(GetTickCount64()));

    TempDirectory() { require(fs::create_directory(path)); }
    ~TempDirectory() { std::error_code ignored; fs::remove_all(path, ignored); }
};

Handle create_event()
{
    SECURITY_ATTRIBUTES share{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle handle{CreateEventW(&share, TRUE, FALSE, nullptr)};
    require(static_cast<bool>(handle));
    return handle;
}

HANDLE argument_handle(const wchar_t* value)
{
    return reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(std::stoull(value)));
}

std::wstring handle_argument(HANDLE value)
{
    return std::to_wstring(reinterpret_cast<UINT_PTR>(value));
}

struct Child
{
    Handle process;
    bool returned{};

    Child(const fs::path& exe, std::wstring_view mode, const fs::path& path,
        HANDLE start = nullptr, HANDLE ready = nullptr, HANDLE release = nullptr)
    {
        auto command = L"\"" + exe.native() + L"\" " + std::wstring{mode} + L" \"" + path.native() +
            L"\" " + handle_argument(start) + L" " + handle_argument(ready) + L" " + handle_argument(release);
        STARTUPINFOW startup{sizeof(STARTUPINFOW)};
        PROCESS_INFORMATION info{};
        require(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE, 0,
            nullptr, nullptr, &startup, &info) != FALSE);
        process.reset(info.hProcess);
        (void)CloseHandle(info.hThread);
    }

    ~Child()
    {
        if (!returned && WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT)
        {
            (void)TerminateProcess(process.get(), 97);
            (void)WaitForSingleObject(process.get(), child_timeout);
        }
    }

    void finish(DWORD expected = 0)
    {
        require(WaitForSingleObject(process.get(), child_timeout) == WAIT_OBJECT_0);
        returned = true;
        DWORD actual{};
        require(GetExitCodeProcess(process.get(), &actual) != FALSE);
        if (actual != expected)
            std::cerr << "Settings child exit " << actual << ", expected " << expected << '\n';
        require(actual == expected);
    }
};

std::string read_bytes(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    require(file.is_open());
    return {std::istreambuf_iterator<char>{file}, {}};
}

void write_bytes(const fs::path& path, std::string_view bytes)
{
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    require(file.good());
}

ProductPreferences preferences(std::wstring_view mode)
{
    ProductPreferences value;
    if (mode == L"first" || mode == L"paused")
        value.ui_language = UiLanguagePreference::japanese;
    if (mode == L"second")
        value.filename_policy = FilenamePolicy::guid;
    return value;
}

void assert_no_temps(const fs::path& path)
{
    for (const auto& item : fs::directory_iterator(path.parent_path()))
        require(item.path().filename() == path.filename());
}

int worker(std::wstring_view mode, const fs::path& path, HANDLE start, HANDLE ready, HANDLE release)
{
    if (start) require(WaitForSingleObject(start, child_timeout) == WAIT_OBJECT_0);
    if (mode == L"future")
    {
        if (ready) require(SetEvent(ready) != FALSE);
        SettingsIoLock lock;
        lock.acquire();
        auto temp = path;
        temp += L".future-diagnostic";
        write_bytes(temp, future_bytes);
        require(MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE);
        return 0;
    }
    if (mode == L"read")
    {
        for (int read = 0; read < 18; ++read)
        {
            const auto loaded = load_product_settings(path);
            require(loaded.status == SettingsStatus::loaded);
            require(loaded.preferences == preferences(L"default") || loaded.preferences == preferences(L"first") ||
                loaded.preferences == preferences(L"second"));
        }
        return 0;
    }
    if (mode == L"paused")
    {
        ready_to_publish = ready;
        allow_publish = release;
    }
    const auto saved = write_product_settings(path, preferences(mode));
    if (saved.persisted) return 0;
    std::cerr << saved.diagnostic << '\n';
    if (saved.diagnostic.find("lock/wait: Win32 error 1460") != std::string::npos) return 2;
    if (saved.diagnostic.find("UnsupportedSchema") != std::string::npos) return 3;
    return 4;
}

void repetitions(const fs::path& exe, const fs::path& copy, const fs::path& path)
{
    const auto first = serialize_product_settings(preferences(L"first"));
    const auto second = serialize_product_settings(preferences(L"second"));
    for (int i = 0; i < 100; ++i)
    {
        const auto start = create_event();
        Child writer1{exe, L"first", path, start.get()};
        Child writer2{copy, L"second", path, start.get()};
        require(SetEvent(start.get()) != FALSE);
        writer1.finish();
        writer2.finish();
        const auto current = read_bytes(path);
        require(current == first || current == second);
        assert_no_temps(path);
    }
    for (int i = 0; i < 100; ++i)
    {
        const auto start = create_event();
        Child reader{copy, L"read", path, start.get()};
        Child writer{exe, i % 2 ? L"first" : L"second", path, start.get()};
        require(SetEvent(start.get()) != FALSE);
        reader.finish();
        writer.finish();
        assert_no_temps(path);
    }
    std::cout << "Cooperating writer pairs=100; reader observations=1800; concurrent writes=100\n";
}

void future_schema(const fs::path& exe, const fs::path& copy, const fs::path& path)
{
    write_bytes(path, future_bytes);
    Child existing{exe, L"first", path};
    existing.finish(3);
    require(read_bytes(path) == future_bytes);
    write_bytes(path, serialize_product_settings(preferences(L"default")));
    Child publisher{copy, L"future", path};
    publisher.finish();
    Child stale{exe, L"first", path};
    stale.finish(3);
    require(read_bytes(path) == future_bytes);

    const auto before = serialize_product_settings(preferences(L"default"));
    write_bytes(path, before);
    const auto before_move = create_event();
    const auto after_move_allowed = create_event();
    const auto future_ready = create_event();
    Child paused{exe, L"paused", path, nullptr, before_move.get(), after_move_allowed.get()};
    require(WaitForSingleObject(before_move.get(), child_timeout) == WAIT_OBJECT_0);
    Child next{copy, L"future", path, nullptr, future_ready.get()};
    require(WaitForSingleObject(future_ready.get(), child_timeout) == WAIT_OBJECT_0);
    require(WaitForSingleObject(next.process.get(), 30) == WAIT_TIMEOUT);
    require(read_bytes(path) == before);
    require(SetEvent(after_move_allowed.get()) != FALSE);
    paused.finish();
    next.finish();
    require(read_bytes(path) == future_bytes);
    assert_no_temps(path);
    std::cout << "Future schema: pre-existing, cooperative-first, current-first passed\n";
}

void different_store(const fs::path& exe, const fs::path& copy, const fs::path& path, const fs::path& other)
{
    require(load_product_settings(other).status == SettingsStatus::no_settings);
    const auto before_move = create_event();
    const auto allow_move = create_event();
    const auto start = std::chrono::steady_clock::now();
    {
        Child paused{exe, L"paused", path, nullptr, before_move.get(), allow_move.get()};
        require(WaitForSingleObject(before_move.get(), child_timeout) == WAIT_OBJECT_0);
        // A reader constructed during contention must not save its safe defaults.
        ProductSettings reader{other};
        require(reader.load_result().status == SettingsStatus::read_failure);
        require(!reader.save_from_settings(preferences(L"second")).persisted);
        Child blocked{copy, L"second", other};
        blocked.finish(2);
        require(!fs::exists(other.parent_path()));
        require(SetEvent(allow_move.get()) != FALSE);
        paused.finish();
    }
    std::cout << "Two intentional timeouts including process overhead (ms): " <<
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() << '\n';
    ProductSettings recovered{other};
    require(recovered.load_result().status == SettingsStatus::no_settings);
    require(recovered.save_from_settings(preferences(L"second")).persisted);
    require(read_bytes(other) == serialize_product_settings(preferences(L"second")));
    assert_no_temps(path);
    assert_no_temps(other);
}
}

namespace clipss2::platform::windows
{
void settings_process_test_before_publish()
{
    if (!ready_to_publish) return;
    require(SetEvent(ready_to_publish) != FALSE);
    require(WaitForSingleObject(allow_publish, child_timeout) == WAIT_OBJECT_0);
}
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        if (argc == 6)
            return worker(argv[1], argv[2], argument_handle(argv[3]), argument_handle(argv[4]), argument_handle(argv[5]));
        require(argc == 1);
        TempDirectory root;
        const auto parent = root.path / L"parent";
        const auto path = parent / L"settings.conf";
        require(load_product_settings(path).status == SettingsStatus::no_settings);
        require(!fs::exists(parent));
        require(write_product_settings(path, preferences(L"default")).persisted);
        std::array<wchar_t, 32768> image{};
        const DWORD length = GetModuleFileNameW(nullptr, image.data(), static_cast<DWORD>(image.size()));
        require(length != 0 && length < image.size());
        const fs::path exe{image.data()};
        const auto copy_dir = root.path / L"copy";
        require(fs::create_directory(copy_dir));
        const auto copy = copy_dir / exe.filename();
        require(fs::copy_file(exe, copy));
        for (const auto& entry : fs::directory_iterator(exe.parent_path()))
            if (entry.path().extension() == L".dll")
                require(fs::copy_file(entry.path(), copy_dir / entry.path().filename()));
        repetitions(exe, copy, path);
        future_schema(exe, copy, path);
        write_bytes(path, serialize_product_settings(preferences(L"default")));
        different_store(exe, copy, path, root.path / L"other" / L"settings.conf");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

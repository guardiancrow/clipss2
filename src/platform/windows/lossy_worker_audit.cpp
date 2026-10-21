#include "platform/windows/lossy_worker_audit.h"
#include "platform/windows/capture_worker_client.h"
#include "platform/windows/monitor_capture_source.h"
#include "platform/windows/product_settings.h"
#include "platform/windows/unicode_diagnostics.h"
#include "codec/avif/avif_codec.h"
#include "codec/jxl/jxl_internal.h"
#include "codec/png/png_codec.h"
#include <Windows.h>
#include <winrt/base.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace clipss2::platform::windows
{
namespace
{
void require(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
std::vector<std::byte> read(const std::filesystem::path& path)
{
    const auto size = std::filesystem::file_size(path);
    require(size > 0 && size <= 256U * 1024U * 1024U, "Audit file size bounds");
    std::vector<std::byte> result(static_cast<std::size_t>(size));
    std::ifstream input{path, std::ios::binary};
    input.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()));
    require(!!input, "Audit file read"); return result;
}
void seed(const std::filesystem::path& path)
{
    require(!std::filesystem::exists(path), "Seed already exists");
    std::ofstream output{path, std::ios::binary}; output << "owned audit no-overwrite sentinel\n";
    output.close(); require(!!output, "Audit seed write");
}
constexpr auto fixture_class = L"clipss2.LossyWorkerAudit";
LRESULT CALLBACK paint(HWND window, UINT message, WPARAM word, LPARAM parameter) noexcept
{
    if (message == WM_PAINT)
    {
        PAINTSTRUCT p{}; const auto dc = BeginPaint(window, &p);
        FillRect(dc, &p.rcPaint, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(20, 30, 55));
        RECT text{24, 24, 900, 250};
        DrawTextW(dc, L"clipss2 owned Lossy worker fixture\n日本語テスト / Latin Test 123\n"
            L"Small text, colored edges, SDR still capture\nFile / Both / Clipboard; one frame only", -1, &text, DT_LEFT);
        for (int i = 0; i < 8; ++i)
        {
            SetDCBrushColor(dc, RGB(30 + i * 25, 180 - i * 15, 40 + i * 20));
            RECT band{24 + i * 100, 300, 124 + i * 100, 420};
            FillRect(dc, &band, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        }
        EndPaint(window, &p); return 0;
    }
    return DefWindowProcW(window, message, word, parameter);
}
class Fixture
{
public:
    Fixture()
    {
        WNDCLASSW type{}; type.lpfnWndProc = paint; type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = fixture_class;
        atom_ = RegisterClassW(&type); if (!atom_) winrt::throw_last_error();
        window_ = CreateWindowExW(0, fixture_class, L"clipss2 owned integration fixture", WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 960, 640, nullptr, nullptr, type.hInstance, nullptr);
        if (!window_)
        {
            const auto error = GetLastError(); UnregisterClassW(fixture_class, type.hInstance);
            winrt::throw_hresult(HRESULT_FROM_WIN32(error));
        }
        ShowWindow(window_, SW_SHOWNOACTIVATE); UpdateWindow(window_);
    }
    ~Fixture() { if (window_) DestroyWindow(window_); if (atom_) UnregisterClassW(fixture_class, GetModuleHandleW(nullptr)); }
    Fixture(const Fixture&) = delete; Fixture& operator=(const Fixture&) = delete;
    HWND window() const { return window_; }
    CaptureWorkerRequest request() const
    {
        CaptureWorkerRequest r;
        r.hwnd_value = reinterpret_cast<std::uintptr_t>(window_); r.expected_process_id = GetCurrentProcessId();
        r.expected_class = fixture_class; r.file_format = image::ImageFileFormat::avif; return r;
    }
private: ATOM atom_{}; HWND window_{};
};
class AuditEnvironment
{
public:
    AuditEnvironment(const wchar_t* name, const wchar_t* value) : name_(name)
    {
        wchar_t previous[32768]{}; const auto count = GetEnvironmentVariableW(name_, previous, 32768);
        require(count < 32768, "Existing audit environment too long");
        if (count) previous_ = std::wstring{previous, count};
        if (!SetEnvironmentVariableW(name_, value)) winrt::throw_last_error();
    }
    ~AuditEnvironment() { SetEnvironmentVariableW(name_, previous_ ? previous_->c_str() : nullptr); }
    AuditEnvironment(const AuditEnvironment&) = delete; AuditEnvironment& operator=(const AuditEnvironment&) = delete;
private:
    const wchar_t* name_; // Borrowed static diagnostic seam name; never escapes this scope.
    std::optional<std::wstring> previous_;
};
}

int run_lossy_worker_audit(const std::filesystem::path& requested_output)
{
    using capture::CaptureDestination;
    using image::ImageFileFormat;
    const auto root = std::filesystem::absolute(requested_output);
    require(std::filesystem::create_directory(root), "Use a NEW audit output directory with an existing parent");
    Fixture fixture;
    const AuditEnvironment timestamp{L"_CLIPSS2_AUDIT_FIXED_TIMESTAMP", L"1"};
    const AuditEnvironment no_delay{L"_CLIPSS2_AUDIT_LOSSY_ENCODE_DELAY", nullptr};
    MONITORINFOEXW info{}; info.cbSize = sizeof(info);
    const auto monitor = MonitorFromWindow(fixture.window(), MONITOR_DEFAULTTONULL);
    if (!monitor || !GetMonitorInfoW(monitor, &info)) winrt::throw_last_error();
    MonitorSource source{info.szDevice, {info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom}, {}, display_topology_snapshot()};
    // Isolated normal settings: exercise load, normal preference commit, audit captures, fresh reload.
    const auto settings_path = root / "settings.conf";
    ProductPreferences preferences; preferences.file_format = ImageFileFormat::jxl;
    require(write_product_settings(settings_path, preferences).persisted, "Audit settings seed");
    ProductSettings settings{settings_path}; settings.commit(preferences);
    const auto settings_before = read(settings_path);

    const auto run = [&](const char* label, const char* kind, ImageFileFormat format,
        capture::FileCompression compression, CaptureDestination destination, FilenamePolicy policy) {
        const auto directory = root / label;
        require(std::filesystem::create_directory(directory), "Audit case directory exists");
        auto request = fixture.request(); request.file_format = format; request.destination = destination;
        request.file_compression = compression;
        const auto extension = image::image_file_extension(format);
        const auto sentinel = directory / (std::string{"clipss2_"} +
            (policy == FilenamePolicy::timestamp ? "20260913_201512_184" : "000001") + extension);
        seed(sentinel); const auto before = read(sentinel);
        if (destination != CaptureDestination::clipboard) request.file_output = FileOutputOptions{directory, policy, false};
        if (std::string_view{kind} != "window")
        {
            request.hwnd_value = 0; request.expected_process_id = 0; request.expected_class.clear(); request.monitor_source = source;
            if (std::string_view{kind} == "region") request.monitor_source->crop = PixelRectangle{0, 0,
                static_cast<std::int32_t>(std::min<std::int64_t>(1200, source.expected_bounds.width())),
                static_cast<std::int32_t>(std::min<std::int64_t>(800, source.expected_bounds.height()))};
        }
        const auto clipboard_before = GetClipboardSequenceNumber();
        std::cout << "AUDIT_CASE " << label << " source=" << kind << " format=" << image::image_file_format_token(format)
                  << " compression=" << (compression.mode == capture::FileCompressionMode::lossy ? "lossy" :
                      (format == ImageFileFormat::avif || format == ImageFileFormat::jxl) ? "lossless" : "baseline")
                  << " profile=" << codec::lossy_profile_token(compression.profile) << " destination="
                  << capture::capture_destination_name(destination) << "\n";
        const auto result = launch_capture_worker(request, std::string_view{kind} == "monitor" ? std::chrono::seconds{60} : std::chrono::seconds{10});
        require(result.status == CaptureWorkerClientStatus::success && !result.forced_termination, "Lossy audit worker failed");
        std::cout << "Audit parent total ms: " << result.duration.count() / 1000.0 << '\n';
        require(read(sentinel) == before, "Existing file overwritten");
        unsigned files{};
        for (const auto& entry : std::filesystem::directory_iterator(directory))
        {
            if (entry.path() == sentinel) continue;
            require(entry.path().extension() == extension, "Unexpected final/temp artifact"); ++files;
            const auto bytes = read(entry.path());
            if (format == ImageFileFormat::avif || format == ImageFileFormat::jxl)
            {
                const auto decoded = format == ImageFileFormat::avif ? codec::avif::decode_bgra8_avif(bytes) :
                    codec::jxl::detail::decode_and_inspect(bytes, compression.mode == capture::FileCompressionMode::lossy ?
                        codec::jxl::detail::DecodePolicy::lossy : codec::jxl::detail::DecodePolicy::lossless).pixels;
                require(decoded.width > 0 && decoded.height > 0, "Decoded dimensions");
                if (std::string_view{label}.ends_with("-high-file"))
                    (void)codec::png::encode_srgb_png(decoded, root / (std::string{label} + "-decoded.png"));
                // Worker already performed both trusted-source checks. Keep final bytes for human review.
                std::cout << "Audit retained file dimensions: " << decoded.width << 'x' << decoded.height << '\n';
            }
        }
        require(files == (destination == CaptureDestination::clipboard ? 0U : 1U), "Unexpected output file count");
        if (destination == CaptureDestination::file) require(GetClipboardSequenceNumber() == clipboard_before, "File changed Clipboard");
        else require(IsClipboardFormatAvailable(CF_DIBV5) && IsClipboardFormatAvailable(RegisterClipboardFormatW(L"PNG")), "Clipboard formats unavailable");
        require(read(settings_path) == settings_before, "Audit override persisted to settings");
        const ProductSettings restarted{settings_path};
        require(restarted.preferences() == preferences, "Normal restart retained private override");
        std::cout << "AUDIT_RESULT " << label << " PASS; no overwrite; settings unchanged; normal reload lossless\n";
    };
    const capture::FileCompression balanced{capture::FileCompressionMode::lossy, codec::LossyProfile::balanced};
    const capture::FileCompression high{capture::FileCompressionMode::lossy, codec::LossyProfile::high};
    for (const auto format : {ImageFileFormat::avif, ImageFileFormat::jxl})
    {
        const std::string prefix = image::image_file_format_token(format);
        run((prefix + "-window-file").c_str(), "window", format, balanced, CaptureDestination::file, FilenamePolicy::timestamp);
        run((prefix + "-window-both").c_str(), "window", format, balanced, CaptureDestination::both, FilenamePolicy::sequence);
        run((prefix + "-region-file").c_str(), "region", format, balanced, CaptureDestination::file, FilenamePolicy::guid);
        run((prefix + "-monitor-file").c_str(), "monitor", format, balanced, CaptureDestination::file, FilenamePolicy::timestamp);
        run((prefix + "-high-file").c_str(), "window", format, high, CaptureDestination::file, FilenamePolicy::guid);
        run((prefix + "-lossless-file").c_str(), "window", format, {}, CaptureDestination::file, FilenamePolicy::sequence);
        run((prefix + "-clipboard-only").c_str(), "window", format, balanced, CaptureDestination::clipboard, FilenamePolicy::sequence);
    }
    for (const auto format : {ImageFileFormat::png, ImageFileFormat::jpeg, ImageFileFormat::webp})
        run((std::string{image::image_file_format_token(format)} + "-smoke").c_str(), "window", format, {}, CaptureDestination::file, FilenamePolicy::sequence);
    // Real publication failure, without injected OS/file stalls. Both must leave Clipboard unchanged.
    auto failure = fixture.request(); failure.destination = CaptureDestination::both;
    failure.file_compression = balanced; failure.file_output = FileOutputOptions{root / "does-not-exist", FilenamePolicy::timestamp, false};
    const auto clipboard_before = GetClipboardSequenceNumber();
    const auto failed = launch_capture_worker(failure);
    require(failed.status == CaptureWorkerClientStatus::file_failure && !failed.forced_termination &&
        clipboard_before == GetClipboardSequenceNumber(), "Both failure ordering");
    std::cout << "AUDIT_RESULT both-file-failure PASS; Clipboard unchanged\n";
    // One bounded delay, no OS/file/driver stall. Use the unchanged parent 10-second budget.
    auto timeout = fixture.request(); timeout.file_compression = balanced;
    const auto timeout_directory = root / "timeout";
    require(std::filesystem::create_directory(timeout_directory), "Timeout directory exists");
    timeout.file_output = FileOutputOptions{timeout_directory, FilenamePolicy::sequence, false};
    CaptureWorkerClientResult timed;
    {
        const AuditEnvironment delay{L"_CLIPSS2_AUDIT_LOSSY_ENCODE_DELAY", L"1"};
        timed = launch_capture_worker(timeout);
    }
    require(timed.status == CaptureWorkerClientStatus::timeout && timed.forced_termination &&
        timed.exit_code == static_cast<unsigned>(CaptureWorkerExit::timeout) && std::filesystem::is_empty(timeout_directory),
        "Injected timeout did not terminate/reap cleanly");
    std::cout << "AUDIT_RESULT timeout PASS; TerminateProcess + reap completed; parent ms=" << timed.duration.count() / 1000.0 << '\n';
    require(read(settings_path) == settings_before, "Settings changed at audit exit");
    std::cout << "Lossy worker audit completed; artifacts retained; no synthetic input\n";
    return 0;
}
}

#include "audit/audit_policy.h"
#if CLIPSS2_AUDIT_FEATURES
#include "audit/artifact_directory.h"
#endif
#include "platform/windows/capture_window.h"
#include "capture/capture_destination.h"
#include "capture/capture_image_output.h"
#include "platform/windows/capture_window_policy.h"
#include "platform/windows/capture_worker_client.h"
#include "platform/windows/capture_worker_product.h"
#include "platform/windows/window_target_validation.h"
#include "platform/windows/window_selector_ui.h"
#include "platform/windows/region_selector_ui.h"
#include "platform/windows/monitor_capture_source.h"
#include "platform/windows/display_color.h"
#include "platform/windows/d3d_device.h"
#include "platform/windows/one_frame_capture.h"
#include "platform/windows/clipboard_image.h"
#include "platform/windows/error_diagnostics.h"
#include "platform/windows/file_publication.h"
#include "platform/windows/unicode_diagnostics.h"
#include "codec/png/png_validator.h"
#include "codec/jpeg/jpeg_validator.h"
#include "codec/webp/webp_validator.h"
#include "codec/avif/avif_validator.h"
#include "codec/jxl/jxl_validator.h"

#include <windows.h>
#include <winrt/base.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace clipss2::platform::windows
{
namespace
{
DisplayColorInfo read_color(const WindowCandidate& selected, ColorCheck check)
{
    // Exceptions propagate: unknown display state must never produce SDR output.
    const auto color = display_color_info_for_window(
        {selected.hwnd_value, selected.title}, check);
    std::cout << "Color provider: " << color.acquisition_method << '\n'
              << "Advanced Color kind: " << color.advanced_color_kind << '\n';
    return color;
}

DisplayColorInfo read_color_for_capture(
    const WindowCandidate& selected, const bool diagnostic_output, ColorCheck check)
{
    if (diagnostic_output) return read_color(selected, check);
    return display_color_info_for_window(
        {selected.hwnd_value, selected.title}, check);
}

void require_capture_sdr(std::string_view kind)
{
    // This specific policy message contains only the provider's enum name.
    // Do not promote arbitrary exception::what() to trusted UTF-8 elsewhere.
    try { require_sdr_capture_color(kind); }
    catch (const std::runtime_error& error) { throw DiagnosticError(error.what()); }
}

#if CLIPSS2_AUDIT_FEATURES
class PendingOutput final
{
public:
    explicit PendingOutput(image::ImageFileFormat format)
    {
        const auto root = clipss2::audit::artifact_directory() / "window-captures";
        std::filesystem::create_directories(root);
        // create_directory is an atomic reservation. Existing directories and
        // their files are never reused, even across concurrent processes.
        for (unsigned int index = 1; index <= 100000; ++index)
        {
            directory_ = root / ("capture-" + std::to_string(index));
            pending_ = directory_ / "capture.pending";
            final_ = directory_ / (std::string{"capture"} + image::image_file_extension(format));
            if (std::filesystem::create_directory(directory_)) return;
        }
        throw std::runtime_error("Could not reserve a new capture output directory.");
    }
    ~PendingOutput()
    {
        if (published_) return;
        std::error_code error;
        std::filesystem::remove(pending_, error);
        if (error) std::cerr << "Failed output cleanup: " << pending_.string()
                             << ": " << error.message() << '\n';
        error.clear();
        std::filesystem::remove(directory_, error); // never recursive
        if (error) std::cerr << "Output directory cleanup: " << error.message() << '\n';
    }
    PendingOutput(const PendingOutput&) = delete;
    PendingOutput& operator=(const PendingOutput&) = delete;
    const std::filesystem::path& pending() const noexcept { return pending_; }
    const std::filesystem::path& final() const noexcept { return final_; }
    void publish()
    {
        // Same-directory rename, with no replace-existing flag. Until validation
        // succeeds there is no final image success artifact.
        if (!MoveFileW(pending_.c_str(), final_.c_str())) winrt::throw_last_error();
        published_ = true;
    }
private:
    std::filesystem::path directory_, pending_, final_;
    bool published_{};
};
#endif

capture::CaptureOutputExecution deliver_capture(
    const image::CpuImage& image,
    capture::CaptureDestination destination,
    const image::ImageFileFormat file_format,
    const bool diagnostic_output, codec::jpeg::JpegProductSelection jpeg_selection,
    const std::optional<FileOutputOptions>& file_output, capture::FileCompression compression)
{
    return capture_stage({"delivery", "Prepare / encode / publish capture outputs"}, [&] {
    using Clock = std::chrono::steady_clock;
    const auto ms = [](auto elapsed) { return std::chrono::duration<double, std::milli>(elapsed).count(); };
    double publish_ms{}, validate2_ms{}, clipboard_ms{};
    const auto preparation_started = Clock::now();
    const auto plan = capture::capture_output_plan(destination);
    if (compression.mode == capture::FileCompressionMode::lossy && plan.file)
    {
        if (!file_output) throw std::invalid_argument("Private lossy file output requires atomic storage.");
        // Bounded test-only delay before encode. Never active for ordinary/lossless requests.
        wchar_t delay[2]{};
        if (clipss2::audit::environment_variable(L"_CLIPSS2_AUDIT_LOSSY_ENCODE_DELAY", delay, 2) == 1 && delay[0] == L'1')
        {
            std::cout << "Audit lossy pre-encode delay injection: 20000 ms\n" << std::flush;
            Sleep(20000);
        }
    }
    // WGC BGRA8 was audited as premultiplied. PNG requires unassociated color;
    // keep CF_DIBV5 byte-preserving and convert only the PNG representation.
    auto prepared = capture::prepare_capture_image_output(image, destination, file_format, jpeg_selection, compression);
    if (diagnostic_output && (file_format == image::ImageFileFormat::avif || file_format == image::ImageFileFormat::jxl))
        std::cout << "File compression: " << capture::file_compression_token(compression) << '\n';
    std::optional<PreparedClipboardImage> clipboard_payload;
    if (plan.clipboard)
        clipboard_payload.emplace(
            prepare_image_for_clipboard(image, *prepared.lossless_source, std::move(prepared.png_bytes)));
    const auto preparation_ms = ms(Clock::now() - preparation_started);

#if CLIPSS2_AUDIT_FEATURES
    std::optional<PendingOutput> output;
#endif
    std::filesystem::path final_path;
    std::exception_ptr file_error;
    std::exception_ptr clipboard_error;
    ClipboardPublishResult published{};
    const auto& png_bytes = clipboard_payload ? clipboard_payload->png : prepared.png_bytes;
    const auto& file_bytes = file_format == image::ImageFileFormat::png ? png_bytes :
        file_format == image::ImageFileFormat::jpeg ? prepared.jpeg_bytes :
        file_format == image::ImageFileFormat::webp ? prepared.webp_bytes :
        file_format == image::ImageFileFormat::avif ? prepared.avif_bytes : prepared.jxl_bytes;

    const auto execution = capture::execute_capture_output_plan(
        destination,
        [&]() noexcept {
            try
            {
                return capture_stage({"delivery", "File publication / validation"}, [&] {
                if (file_output)
                {
                    const auto publish_started = Clock::now();
                    PublicationHooks audit_hooks;
                    wchar_t fixed[2]{};
                    if (clipss2::audit::environment_variable(L"_CLIPSS2_AUDIT_FIXED_TIMESTAMP", fixed, 2) == 1 && fixed[0] == L'1')
                        audit_hooks.timestamp = [] { return L"20260913_201512_184"; };
                    final_path = publish_file_payload(*file_output, image::image_file_extension(file_format), file_bytes,
                        [&](std::span<const std::byte> persisted) {
                            const auto validate_started = Clock::now();
                            capture::validate_capture_file_payload(file_format, compression, persisted,
                                prepared.lossless_source ? *prepared.lossless_source : image,
                                prepared.jpeg_effective ? &*prepared.jpeg_effective : nullptr);
                            validate2_ms += ms(Clock::now() - validate_started);
                        }, audit_hooks);
                    publish_ms = ms(Clock::now() - publish_started) - validate2_ms;
                }
                else
                {
#if CLIPSS2_AUDIT_FEATURES
                    // Legacy private worker/audit isolation, never normal product.
                    output.emplace(file_format);
                    capture::write_capture_file_image(file_format, file_bytes, output->pending(),
                        file_format == image::ImageFileFormat::jpeg ? image : *prepared.lossless_source,
                        prepared.jpeg_effective ? &*prepared.jpeg_effective : nullptr);
                    output->publish(); final_path = output->final();
#else
                    throw std::invalid_argument("File/Both requires resolved output storage.");
#endif
                }
                return true;
                });
            }
            catch (...)
            {
                file_error = std::current_exception();
                return false;
            }
        },
        [&]() noexcept {
            try
            {
                return capture_stage({"delivery", "Clipboard publication / verification"}, [&] {
                const auto clipboard_started = Clock::now();
                published = publish_prepared_image_to_clipboard(*clipboard_payload);
                if (!published.dibv5_available || !published.png_available)
                    throw std::runtime_error(
                        "Clipboard format availability verification failed.");
                clipboard_ms = ms(Clock::now() - clipboard_started);
                return true;
                });
            }
            catch (...)
            {
                clipboard_error = std::current_exception();
                return false;
            }
        });

    if (diagnostic_output && execution.file_succeeded)
    {
        std::string diagnostic = file_format == image::ImageFileFormat::png ?
            "PNG privacy/color and full-pixel validation: succeeded\n" :
            file_format == image::ImageFileFormat::jpeg ?
            "JPEG marker/privacy and decode validation: succeeded\n" :
            file_format == image::ImageFileFormat::webp ?
            "WebP structure/privacy and full-pixel validation: succeeded\n" :
            file_format == image::ImageFileFormat::avif ?
            "AVIF structure/privacy and full-pixel validation: succeeded\n" :
            "JPEG XL naked codestream/privacy and full-pixel validation: succeeded\n";
        if (compression.mode == capture::FileCompressionMode::lossy)
            diagnostic = "Lossy immediate + persisted structure/color/decode/alpha validation: succeeded\n";
        diagnostic += "File output: succeeded\n";
#if CLIPSS2_AUDIT_FEATURES
        diagnostic += "Output: " + path_utf8(final_path) + '\n';
#endif
        write_stdout_utf8(diagnostic);
    }
    else if (diagnostic_output && execution.file_attempted)
        write_stdout_utf8("File output: failed (" + describe_exception(file_error) + ")\n");
    else if (diagnostic_output)
        std::cout << "File output: not requested\n";

    if (diagnostic_output && execution.clipboard_succeeded)
        std::cout << "PNG memory privacy/color and full-pixel validation: succeeded\n"
                  << "Clipboard publish: succeeded\n"
                  << "Clipboard output: succeeded\nFormats:\n  CF_DIBV5\n  PNG\n"
                  << "CF_DIBV5 payload bytes: " << published.dibv5_bytes << '\n'
                  << "PNG bytes: " << published.png_bytes << '\n'
                  << "Clipboard format availability: verified\n";
    else if (diagnostic_output && execution.clipboard_attempted)
        write_stdout_utf8("Clipboard output: failed (" + describe_exception(clipboard_error) + ")\n");
    else if (diagnostic_output && plan.clipboard)
        std::cout << "Clipboard output: not attempted\n";
    else if (diagnostic_output)
        std::cout << "Clipboard: untouched\n";

    if (diagnostic_output &&
        execution.outcome == capture::CaptureOutputOutcome::partial_success)
        std::cout << "Capture delivery: partial success; file retained\n";
    if (diagnostic_output && plan.file && file_format == image::ImageFileFormat::jpeg)
    {
        if (prepared.jpeg_effective)
            std::cout << "JPEG managed backend: " << codec::jpeg::jpeg_backend_name(prepared.jpeg_effective->backend)
                      << "; " << prepared.jpeg_effective->diagnostic << '\n';
        std::cout << "JPEG preparation/encode/validation ms: " << prepared.jpeg_encode_milliseconds
                  << "\nJPEG bytes: " << prepared.jpeg_bytes.size() << '\n';
    }
    if (diagnostic_output && plan.file && file_format == image::ImageFileFormat::webp)
        std::cout << "WebP lossless preparation/encode/validation ms: "
                  << prepared.webp_encode_milliseconds << "\nWebP bytes: "
                  << prepared.webp_bytes.size() << '\n';
    if (diagnostic_output && plan.file && file_format == image::ImageFileFormat::avif && compression.mode == capture::FileCompressionMode::lossless)
        std::cout << "AVIF lossless preparation/encode/validation ms: "
                  << prepared.avif_encode_milliseconds << "\nAVIF bytes: "
                  << prepared.avif_bytes.size() << '\n';
    if (diagnostic_output && plan.file && file_format == image::ImageFileFormat::jxl && compression.mode == capture::FileCompressionMode::lossless)
        std::cout << "JPEG XL lossless preparation/encode/validation ms: "
                  << prepared.jxl_encode_milliseconds << "\nJPEG XL bytes: "
                  << prepared.jxl_bytes.size() << '\n';
    if (diagnostic_output && compression.mode == capture::FileCompressionMode::lossy)
        std::cout << "Private compression: lossy; profile=" << codec::lossy_profile_token(compression.profile)
                  << "\nAudit output dimensions: " << image.width << 'x' << image.height
                  << "\nAudit encode ms: " << prepared.lossy_encode_milliseconds
                  << "\nAudit validate1 ms: " << prepared.lossy_validate_milliseconds
                  << "\nAudit publication excluding validate2 ms: " << publish_ms
                  << "\nAudit validate2 ms: " << validate2_ms
                  << "\nAudit preparation total ms: " << preparation_ms
                  << "\nAudit clipboard publication ms: " << clipboard_ms << '\n';
    return execution;
    });
}

capture::CaptureOutputExecution capture_window_target_impl(
    const WindowCandidate& candidate,
    const bool include_cursor,
    const capture::CaptureDestination destination,
    const image::ImageFileFormat file_format,
    const bool diagnostic_output, codec::jpeg::JpegProductSelection jpeg_selection,
    const std::optional<FileOutputOptions>& file_output, capture::FileCompression compression)
{
    (void)image::image_file_format_name(file_format);
    capture_stage({"wgc.support", "GraphicsCaptureSession IsSupported"}, [] {
        if (!winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported())
            throw DiagnosticError("reason=wgc-not-supported; capture/output not permitted");
    });
    require_live_window_capture_target(candidate, "before-color");
    const auto color_before = capture_stage({"color.precheck.query", "Require SDR capture color"}, [&] {
        auto color = read_color_for_capture(candidate, diagnostic_output, ColorCheck::precheck);
        require_capture_sdr(color.advanced_color_kind);
        return color;
    });
    require_live_window_capture_target(candidate, "before-wgc");

    const auto device = D3dDevice::create();
    const WindowCaptureTarget target{candidate.hwnd_value, candidate.title};
    const CaptureOptions options{
        include_cursor, CaptureBorderPolicy::system_required, diagnostic_output};
    if (diagnostic_output)
        std::cout << "Capture destination: "
                  << capture::capture_destination_name(destination) << '\n'
                  << "Cursor capture: " << (include_cursor ? "on" : "off") << '\n'
                  << "File format: " << image::image_file_format_name(file_format)
                  << (destination == capture::CaptureDestination::clipboard ? " (file only)" : "") << '\n';
    const auto capture_started = std::chrono::steady_clock::now();
    const auto frame = capture_one_frame(
        device, target, CapturePixelFormat::bgra8_unorm, options);
    require_window_capture_geometry(frame.content_width, frame.content_height,
        frame.texture_width, frame.texture_height, frame.cpu_image.width, frame.cpu_image.height, frame.cpu_image.pixels.size());
    if (diagnostic_output)
        std::cout << "WGC capture: succeeded\nCaptured size: "
                  << frame.content_width << 'x' << frame.content_height
                  << "\nTexture size: " << frame.texture_width << 'x'
                  << frame.texture_height << "\nCPU readback: succeeded\n";
    require_live_window_capture_target(candidate, "after-frame");
    capture_stage({"color.postcheck", "Require current SDR capture color"}, [&] {
        const auto color_after = read_color_for_capture(candidate, diagnostic_output, ColorCheck::postcheck);
        require_capture_sdr(color_after.advanced_color_kind);
        require_unchanged_capture_color(color_before, color_after);
    });
    if (diagnostic_output) std::cout << "Capture: succeeded\n";
    if (diagnostic_output && compression.mode == capture::FileCompressionMode::lossy)
        std::cout << "Capture setup/frame/readback/check ms: " << std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - capture_started).count() << '\n';
    return deliver_capture(frame.cpu_image, destination, file_format, diagnostic_output, jpeg_selection, file_output, compression);
}

int run_capture_window_to(bool fixed_clipboard)
{
    const auto execution = run_window_capture_product_flow(fixed_clipboard);
    return report_product_capture_execution(execution);
}
}

ProductCaptureExecution run_window_capture_product_flow(
    const bool fixed_clipboard,
    const std::function<void()>& before_worker,
    ProductSettings* settings)
{
    std::optional<ProductSettings> one_shot;
    if (!fixed_clipboard && settings == nullptr)
    {
        one_shot.emplace();
        settings = &*one_shot;
    }
    const auto action = fixed_clipboard
        ? WindowSelectionAction::copy : WindowSelectionAction::capture;
    const auto selected = select_window(action,
        !fixed_clipboard && settings ? settings->preferences() : ProductPreferences{});
    // select_window owns the selector/HUD/shields in a nested scope and returns
    // only after all of them are destroyed. WGC belongs exclusively to the child.
    auto accepted = settings ? settings->preferences() : ProductPreferences{};
    std::optional<FileOutputOptions> file_output;
    if (selected && !fixed_clipboard && settings)
    {
        accepted.file_format = selected->file_format;
        accepted.jpeg_selection = selected->jpeg_selection;
        accepted.avif_profile = selected->avif_profile;
        accepted.jxl_profile = selected->jxl_profile;
        // Private legacy audit layout keeps established regression fixtures
        // isolated. It is never set by the application or exposed as public CLI.
        wchar_t legacy[2]{};
        const bool legacy_audit = clipss2::audit::environment_variable(L"_CLIPSS2_AUDIT_LEGACY_OUTPUT", legacy, 2) == 1 && legacy[0] == L'1';
        if (capture::capture_output_plan(selected->destination).file && !legacy_audit)
        {
            file_output = resolve_file_output(settings->store(), accepted.output_directory,
                accepted.filename_policy, pick_output_folder, resolved_pictures_directory);
            if (!file_output)
            {
                std::cout << "Output folder picker: canceled; preferences unchanged\n";
                return {};
            }
        }
    }
    if (selected && before_worker) before_worker();
    return execute_product_capture(
        selected, fixed_clipboard,
        [settings, fixed_clipboard, &accepted, &file_output](CaptureWorkerRequest request) {
            // The same immutable request is validated, committed and launched.
            // UI Confirm froze the identity; resolution succeeded before this
            // acceptance point. Later worker failure cannot roll back intent.
            request.file_output = file_output;
            request.file_compression = selected_file_compression(accepted);
            (void)serialize_capture_worker_request(request);
            if (!fixed_clipboard && settings) settings->commit(accepted);
            return launch_capture_worker(request);
        });
}

capture::CaptureOutputExecution capture_monitor_target(const CaptureWorkerRequest& request)
{
    if (!request.monitor_source) throw std::invalid_argument("Monitor source missing.");
    const auto& source=*request.monitor_source;
    const auto require_current=[&](ColorCheck check) {
        const auto monitor=resolve_monitor_source(source);
        return capture_stage({check == ColorCheck::precheck ? "color.precheck.query" : "color.postcheck",
            "Require current monitor SDR capture color"}, [&] {
            const auto color = display_color_info_for_monitor(monitor, check);
            if (!request.quiet) std::cout << "Color provider: " << color.acquisition_method
                << "\nAdvanced Color kind: " << color.advanced_color_kind << '\n';
            require_capture_sdr(color.advanced_color_kind);
            return color;
        });
    };
    const auto color_before = require_current(ColorCheck::precheck);
    if (!winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported())
        throw std::runtime_error("Windows.Graphics.Capture is not supported.");
    const auto device=D3dDevice::create();
    const auto capture_started = std::chrono::steady_clock::now();
    const auto frame=capture_one_frame(device,request.capture_source(),CapturePixelFormat::bgra8_unorm,
        CaptureOptions{request.include_cursor,request.border_policy,!request.quiet});
    capture_stage({"color.postcheck", "Require unchanged monitor color provider/state"}, [&] {
        require_unchanged_capture_color(color_before, require_current(ColorCheck::postcheck));
    });
    const auto width=source.crop?source.crop->width():source.expected_bounds.width();
    const auto height=source.crop?source.crop->height():source.expected_bounds.height();
    if (frame.cpu_image.width!=width || frame.cpu_image.height!=height)
        throw std::runtime_error("Region CPU image geometry mismatch; no output published.");
    if (!request.quiet)
        std::cout << "WGC capture: succeeded\nCapture source: monitor\nCaptured size: " << frame.content_width << 'x' << frame.content_height
                  << "\nCPU region size: " << width << 'x' << height << "\nFrame wait us: " << frame.frame_wait_duration.count()
                  << "\nGPU readback wait us: " << frame.gpu_readback_duration.count() << '\n';
    if (!request.quiet && request.file_compression.mode == capture::FileCompressionMode::lossy)
        std::cout << "Capture setup/frame/readback/check ms: " << std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - capture_started).count() << '\n';
    return deliver_capture(frame.cpu_image,request.destination,request.file_format,!request.quiet,request.jpeg_selection,request.file_output,request.file_compression);
}

static ProductCaptureExecution run_monitor_source_product_flow(const std::function<void()>& before_worker,ProductSettings* settings,bool full_monitor)
{
    std::optional<ProductSettings> one_shot;
    if (!settings) { one_shot.emplace(); settings=&*one_shot; }
    const auto selected=full_monitor?select_monitor(settings->preferences()):select_region(settings->preferences());
    if (!selected) return {};
    auto accepted=selected->preferences;
    CaptureWorkerRequest request;
    request.monitor_source=selected->source;
    request.include_cursor=selected->include_cursor;
    request.destination=selected->destination;
    request.file_format=accepted.file_format;
    request.jpeg_selection=accepted.jpeg_selection;
    request.file_compression=selected_file_compression(accepted);
    // The shared Phase 15A resolver is the only directory/picker policy.
    if (capture::capture_output_plan(request.destination).file)
    {
        request.file_output=resolve_file_output(settings->store(),accepted.output_directory,
            accepted.filename_policy,pick_output_folder,resolved_pictures_directory);
        if (!request.file_output) { std::cout << "Output folder picker: canceled; preferences unchanged\n"; return {}; }
    }
    (void)resolve_monitor_source(*request.monitor_source);
    (void)serialize_capture_worker_request(request);
    if (before_worker) before_worker();
    settings->commit(accepted);
    // Full-screen Debug jpegli + Both exceeded the legacy 10-second output budget.
    // Frame and Map waits remain independently bounded; only this full-monitor
    // coordinator permits a larger, still finite, end-to-end encoding budget.
    const auto result=launch_capture_worker(request,full_monitor?std::chrono::seconds{60}:std::chrono::seconds{10});
    return {true,result,result.status==CaptureWorkerClientStatus::success?0:1};
}

ProductCaptureExecution run_region_capture_product_flow(const std::function<void()>& before_worker,ProductSettings* settings)
{ return run_monitor_source_product_flow(before_worker,settings,false); }
ProductCaptureExecution run_monitor_capture_product_flow(const std::function<void()>& before_worker,ProductSettings* settings)
{ return run_monitor_source_product_flow(before_worker,settings,true); }
int run_capture_region() { return report_product_capture_execution(run_region_capture_product_flow()); }
int run_capture_monitor() { return report_product_capture_execution(run_monitor_capture_product_flow()); }

int report_product_capture_execution(const ProductCaptureExecution& execution)
{
    if (!execution.worker_launched)
    {
        std::cout << "Capture: not started; no file or clipboard output\n";
        return 0;
    }

    const auto& result = *execution.worker_result;
    std::cout << "Capture worker: "
              << capture_worker_client_status_name(result.status)
              << "\nCapture worker latency ms: "
              << result.duration.count() / 1000.0 << '\n';
    if (result.status != CaptureWorkerClientStatus::success)
    {
        std::string diagnostic = "Capture worker failed: " +
            std::string{capture_worker_client_status_name(result.status)};
        if (!result.diagnostic.empty()) diagnostic += " (" + result.diagnostic + ')';
        write_stderr_utf8(diagnostic + '\n');
    }
    return execution.process_exit_code;
}

capture::CaptureOutputExecution capture_window_target(
    const WindowCandidate& candidate,
    const bool include_cursor,
    const capture::CaptureDestination destination,
    const image::ImageFileFormat file_format,
    const bool diagnostic_output, codec::jpeg::JpegProductSelection jpeg_selection,
    std::optional<FileOutputOptions> file_output, capture::FileCompression file_compression)
{
    return capture_window_target_impl(
        candidate, include_cursor, destination, file_format, diagnostic_output, jpeg_selection, file_output, file_compression);
}

int run_capture_window()
{
    return run_capture_window_to(false);
}

int run_capture_window_clipboard()
{
    return run_capture_window_to(true);
}
}

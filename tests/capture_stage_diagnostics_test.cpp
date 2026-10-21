#include "platform/windows/error_diagnostics.h"
#include "platform/windows/display_color.h"
#include "platform/windows/display_color_note.h"
#include "platform/windows/display_color_policy.h"
#include "platform/windows/window_target_validation.h"
#include "platform/windows/unicode_diagnostics.h"
#include "platform/windows/capture_worker_diagnostics.h"
#include "platform/windows/resource_counters.h"

#include <winrt/base.h>
#include <windows.h>
#include <array>
#include <iostream>
#include <system_error>
#include <thread>

using namespace clipss2::platform::windows;
namespace
{
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
bool contains(std::string_view value, std::string_view part) { return value.find(part) != value.npos; }
template<class Function>
std::string failed(Function action, HRESULT expected)
{
    try { action(); }
    catch (const CaptureStageError& error)
    {
        require(error.result() == expected, "original HRESULT lost");
        return describe_exception(std::current_exception());
    }
    throw std::runtime_error("expected stage failure");
}
void units()
{
    // Real delivery model plus the worker's actual classifier: diagnostics must
    // neither cancel a saved file nor turn a publication failure into success.
    for (const auto destination : {clipss2::capture::CaptureDestination::file,
        clipss2::capture::CaptureDestination::clipboard, clipss2::capture::CaptureDestination::both})
    for (const bool file : {false, true})
    for (const bool clipboard : {false, true})
    {
        const auto outcome = clipss2::capture::execute_capture_output_plan(destination,
            [=] { return file; }, [=] { return clipboard; });
        const auto plan = clipss2::capture::capture_output_plan(destination);
        const auto expected = plan.file && !file ? CaptureWorkerExit::file_failure :
            plan.clipboard && !clipboard ? (plan.file ? CaptureWorkerExit::partial_success :
                CaptureWorkerExit::clipboard_failure) : CaptureWorkerExit::success;
        require(!worker_diagnostic({"pre.capture.diagnostics", "owned failure"}, false,
            [] { throw std::runtime_error("diagnostic-only failure"); }), "diagnostic exception escaped");
        require(worker_delivery_exit(outcome) == expected, "delivery classification changed");
        require(!worker_diagnostic({"post.delivery.diagnostics", "owned failure"}, false,
            [] { throw winrt::hresult_error(E_ACCESSDENIED); }), "diagnostic HRESULT escaped");
        require(worker_delivery_exit(outcome) == expected, "post-diagnostic changed delivery");
    }
    worker_diagnostic_output([] { throw std::runtime_error("broken diagnostic sink"); });

    reset_pss_counters_for_audit();
    SetEnvironmentVariableW(L"_CLIPSS2_AUDIT_CAPTURE_FAIL_HRESULT", L"0x80070005");
    for (const auto stage : {L"resource.pss.after-snapshot", L"resource.pss.after-marker", L"resource.pss.after-walk"})
    {
        SetEnvironmentVariableW(L"_CLIPSS2_AUDIT_CAPTURE_FAIL_STAGE", stage);
        const auto before = pss_counters_for_audit().calls;
        require(!worker_diagnostic({"pre.capture.diagnostics", "Owned PSS cleanup"}, false,
            [] { (void)collect_process_handles(); }), "PSS failure injection missed");
        const auto counts = pss_counters_for_audit();
        require(counts.calls == before + 1 && counts.snapshots == 0 && counts.markers == 0 &&
            counts.cleanup_failures == 0, "PSS exception leaked resource");
    }
    SetEnvironmentVariableW(L"_CLIPSS2_AUDIT_CAPTURE_FAIL_STAGE", nullptr);
    SetEnvironmentVariableW(L"_CLIPSS2_AUDIT_CAPTURE_FAIL_HRESULT", nullptr);
    const auto handles = collect_process_handles();
    require(!handles.empty() && !count_handle_types(handles).empty(), "explicit PSS capability lost");
    require(pss_counters_for_audit().snapshots == 0 && pss_counters_for_audit().markers == 0 &&
        pss_counters_for_audit().cleanup_failures == 0, "successful PSS leaked resource");
    const auto main_calls = pss_counters_for_audit().calls;
    std::array<bool, 2> thread_success{};
    {
        std::array<std::jthread, 2> threads;
        for (std::size_t i = 0; i < threads.size(); ++i)
            threads[i] = std::jthread([&, i] {
                try {
                    (void)collect_process_handles();
                    const auto counts = pss_counters_for_audit();
                    thread_success[i] = counts.calls == 1 && counts.snapshots == 0 && counts.markers == 0 &&
                        counts.cleanup_failures == 0;
                } catch (...) {}
            });
    }
    require(thread_success[0] && thread_success[1] && pss_counters_for_audit().calls == main_calls,
        "fixture PSS counters must not race between threads");
    require(detail::window_color_fallback_note(E_FAIL) ==
        "GetForWindow failed with HRESULT 0x80004005; used the target HWND's nearest monitor.",
        "fixture fallback note changed");
    require(detail::window_color_fallback_note(E_ACCESSDENIED) ==
        "GetForWindow failed with HRESULT 0x80070005; used the target HWND's nearest monitor.",
        "fixture fallback HRESULT note changed");
    const char* stages[]{"target.validate", "color.precheck.factory", "color.precheck.query",
        "d3d.device", "d3d.winrt.bridge", "wgc.item", "wgc.framepool", "wgc.session",
        "wgc.frame", "texture.readback", "color.postcheck", "delivery", "post.delivery.diagnostics"};
    for (const auto stage : stages)
    {
        const auto text = failed([&] {
            capture_stage({stage, "Owned operation", "Owned interface"}, [] { throw winrt::hresult_error(E_NOINTERFACE); });
        }, E_NOINTERFACE);
        require(contains(text, "stage=" + std::string{stage}) && contains(text, "E_NOINTERFACE") &&
            contains(text, "0x80004002") && contains(text, "Owned interface"), "stage identity absent");
    }
    const auto nested = failed([] {
        capture_stage({"target.validate", "outer"}, [] {
            capture_stage({"wgc.item", "CreateForWindow", "IGraphicsCaptureItemInterop"}, [] {
                throw_hresult_failure("Native item operation", E_ACCESSDENIED);
            });
        });
    }, E_ACCESSDENIED);
    require(contains(nested, "stage=wgc.item") && !contains(nested, "stage=target.validate") &&
        contains(nested, "Native item operation"), "innermost operation overwritten");

    // Real provider call, factory injection before any DisplayInformation query/pixel capture.
    const HWND owned = CreateWindowExW(0, L"STATIC", L"owned diagnostic fixture", 0,
        0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(owned != nullptr, "owned target creation");
    struct Cleanup
    {
        HWND window;
        ~Cleanup() {
            DestroyWindow(window);
            SetEnvironmentVariableW(L"_CLIPSS2_AUDIT_CAPTURE_FAIL_STAGE", nullptr);
            SetEnvironmentVariableW(L"_CLIPSS2_AUDIT_CAPTURE_FAIL_HRESULT", nullptr);
        }
    } cleanup{owned};
    SetEnvironmentVariableW(L"_CLIPSS2_AUDIT_CAPTURE_FAIL_HRESULT", L"0x80070005");
    for (const auto check : {ColorCheck::precheck, ColorCheck::postcheck})
    {
        const auto stage = check == ColorCheck::precheck ? L"color.precheck.factory" : L"color.postcheck.factory";
        SetEnvironmentVariableW(L"_CLIPSS2_AUDIT_CAPTURE_FAIL_STAGE", stage);
        for (const bool window : {false, true})
        {
            const auto text = failed([&] {
                if (window) (void)display_color_info_for_window({reinterpret_cast<std::uintptr_t>(owned), {}}, check);
                else (void)display_color_info_for_monitor(1, check); // No OS call uses this sentinel.
            }, E_ACCESSDENIED);
            require(contains(text, "capability=query-failed") && contains(text, "22621") &&
                contains(text, "7449121C-382B-4705-8DA7-A795BA482013"), "color capability details absent");
        }
    }
    SetEnvironmentVariableW(L"_CLIPSS2_AUDIT_CAPTURE_FAIL_STAGE", L"color.precheck.factory");
    SetEnvironmentVariableW(L"_CLIPSS2_AUDIT_CAPTURE_FAIL_HRESULT", L"0x80070005");
    const auto denied = failed([] { (void)display_color_info_for_monitor(1); }, E_ACCESSDENIED);
    require(contains(denied, "capability=query-failed") && !contains(denied, "capability=unavailable"),
        "activation failure incorrectly called missing interface");
    SetEnvironmentVariableW(L"_CLIPSS2_AUDIT_CAPTURE_FAIL_STAGE", nullptr);
    SetEnvironmentVariableW(L"_CLIPSS2_AUDIT_CAPTURE_FAIL_HRESULT", nullptr);

    WindowCandidate identity; identity.hwnd_value = reinterpret_cast<std::uintptr_t>(owned);
    identity.process_id = GetCurrentProcessId();
    std::array<wchar_t, 256> identity_class{};
    const auto identity_class_size = GetClassNameW(owned, identity_class.data(), static_cast<int>(identity_class.size()));
    require(identity_class_size > 0, "owned class read failed");
    identity.class_name.assign(identity_class.data(), identity_class_size);
    require(is_window_candidate_current(identity), "owned HWND identity unexpectedly rejected");
    auto stale = identity; ++stale.process_id;
    require(!is_window_candidate_current(stale) && contains(window_identity_failure_reason(stale), "reason=pid-mismatch"),
        "PID mismatch identity allowed");
    stale = identity; stale.class_name = L"different owned class";
    require(!is_window_candidate_current(stale) && contains(window_identity_failure_reason(stale), "reason=class-mismatch"),
        "HWND reuse equivalent class mismatch allowed");
    stale = identity; stale.hwnd_value = 0;
    require(!is_window_candidate_current(stale) && contains(window_identity_failure_reason(stale), "reason=hwnd-invalid"),
        "destroyed/null HWND identity allowed");
    auto snapshot = identity;
    snapshot.valid_window = true; snapshot.visible = true; snapshot.cloaked_known = true;
    snapshot.bounds = {10, 10, 100, 100}; classify_window_candidate(snapshot);
    auto current = snapshot;
    // Focus/owner/child-layout changes are not target identity conditions.
    current.has_owner = true; current.tool_window = true; current.topmost = true;
    current.bounds = {50, 60, 700, 600}; current.title = L"private title changed";
    classify_window_candidate(current);
    require(window_snapshot_failure_reason(snapshot, std::array{current}).empty(), "move/resize/owner/title false rejection");
    require(contains(window_snapshot_failure_reason(snapshot, {}), "snapshot-target-missing"), "missing snapshot allowed");
    auto other = current; ++other.hwnd_value;
    require(window_snapshot_failure_reason(snapshot, std::array{other, current}).empty(), "multiple windows wrong target");
    for (const auto condition : {0, 1, 2, 3, 4, 5, 6, 7})
    {
        auto rejected = current;
        if (condition == 0) rejected.visible = false;
        if (condition == 1) rejected.minimized = true;
        if (condition == 2) rejected.cloaked = true;
        if (condition == 3) { rejected.cloaked_known = false; rejected.cloaked_query_failure = E_ACCESSDENIED; }
        if (condition == 4) { rejected.bounds = {}; rejected.bounds_query_failure = E_FAIL; rejected.bounds_win32_failure = ERROR_INVALID_WINDOW_HANDLE; }
        if (condition == 5) rejected.own_process = true;
        if (condition == 6) ++rejected.process_id;
        if (condition == 7) rejected.class_name = L"private changed class";
        rejected.diagnostic = "C:\\Users\\private\\secret";
        classify_window_candidate(rejected);
        const auto reason = window_snapshot_failure_reason(snapshot, std::array{rejected});
        require(contains(reason, "reason=") && !contains(reason, "private"), "rejection classification/privacy failed");
        if (condition == 3) require(contains(reason, "E_ACCESSDENIED"), "DWM HRESULT lost");
        if (condition == 4) require(contains(reason, "0x80004005") && contains(reason, "1400"), "bounds native errors lost");
    }
    require_window_capture_geometry(100, 60, 100, 60, 100, 60, 24000);
    require_window_capture_geometry(100, 60, 104, 64, 100, 60, 24000);
    for (const auto condition : {2, 3})
    {
        bool rejected{};
        try { require_window_capture_geometry(100, 60, condition == 0 ? 104 : 100,
            condition == 1 ? 64 : 60, condition == 2 ? 96 : 100, condition == 3 ? 56 : 60, 24000); }
        catch (const CaptureStageError& error) {
            const std::string_view text{error.what()};
            rejected = contains(text, "stage=capture.geometry") && contains(text, "reason=window-frame-size-mismatch") &&
                contains(text, "content=100x60") && !contains(text, "target.validate");
        }
        require(rejected, "geometry rejection mislabeled/disabled");
    }

    // Pure native-provider decision tests: default DXGI SDR never overrides CCD.
    const FallbackColorEvidence sdr{1, 1, true, true, true, 0, 0, 8, 8, 0};
    require(fallback_color_kind(sdr) == "StandardDynamicRange", "verified SDR rejected");
    auto check = [&](auto change, std::string_view expected = "Unknown") {
        auto evidence = sdr; change(evidence);
        require(fallback_color_kind(evidence) == expected, "fallback color evidence policy failed");
    };
    check([](auto& e) { e.advanced_flags = 1; }, "StandardDynamicRange"); // HDR capable, off.
    check([](auto& e) { e.advanced_flags = 9; }, "StandardDynamicRange");
    check([](auto& e) { e.advanced_flags = 3; e.dxgi_color_space = 12; }, "HighDynamicRange");
    check([](auto& e) { e.advanced_flags = 7; }, "WideColorGamut");
    check([](auto& e) { e.advanced_flags = 5; }, "WideColorGamut");
    check([](auto& e) { e.advanced_flags = 3; }); // WCG/ACM or contradictory SDR default.
    check([](auto& e) { e.advanced_flags = 2; });
    check([](auto& e) { e.advanced_flags = 16; });
    check([](auto& e) { e.edid_ids_valid = false; });
    check([](auto& e) { e.matching_paths = 2; });
    check([](auto& e) { e.matching_paths = 0; });
    check([](auto& e) { e.matching_outputs = 2; });
    check([](auto& e) { e.matching_outputs = 0; });
    check([](auto& e) { e.identities_match = false; });
    check([](auto& e) { e.stable = false; });
    check([](auto& e) { e.display_bits = 0; });
    check([](auto& e) { e.dxgi_bits = 10; });
    check([](auto& e) { e.color_encoding = 1; });
    check([](auto& e) { e.dxgi_color_space = 12; });
    check([](auto& e) { e.dxgi_color_space = 999; });
    DisplayColorInfo before; before.acquisition_method = "DisplayConfig+DXGI";
    before.advanced_color_kind = "StandardDynamicRange"; before.stability_token = "owned topology";
    require_unchanged_capture_color(before, before);
    for (const auto change : {0, 1, 2, 3, 4})
    {
        auto after = before;
        if (change == 0) after.stability_token += "changed";
        if (change == 1) after.acquisition_method = "DisplayInformation";
        if (change == 2) after.advanced_color_kind = "HighDynamicRange";
        if (change == 3) after.hdr_available = true;
        if (change == 4) after.max_luminance_in_nits = 400;
        bool rejected{};
        try { require_unchanged_capture_color(before, after); } catch (const DiagnosticError&) { rejected = true; }
        require(rejected, "pre/post provider/monitor/color change accepted");
    }

    std::string unsafe;
    try { throw std::runtime_error(std::string{"\x93\xFA\x96\x7B", 4}); }
    catch (...) { unsafe = describe_exception(std::current_exception()); }
    require(contains(unsafe, "encoding/privacy not established"), "unknown ANSI bytes treated as UTF-8");
    try { capture_stage({"delivery", "Owned file operation"}, [] {
        throw std::system_error(ERROR_ACCESS_DENIED, std::system_category(), "C:\\Users\\private\\secret");
    }); }
    catch (const CaptureStageError& error) {
        const std::string text = error.what();
        require(contains(text, "ERROR_ACCESS_DENIED") && !contains(text, "private"), "Win32 identity/privacy lost");
    }
    const auto japanese = failed([] {
        capture_stage({"wgc.item", "Owned Japanese diagnostic"}, [] {
            throw DiagnosticError(utf8_from_utf16(L"診断 日本語 😀"), E_FAIL);
        });
    }, E_FAIL);
    require(contains(japanese, utf8_from_utf16(L"診断 日本語 😀")), "trusted Japanese UTF-8 lost");
}
int owned_console(UINT code_page)
{
    // Only invoked in our newly allocated child console, never the calling shell.
    require(SetConsoleOutputCP(code_page) != FALSE, "owned console code page");
    HANDLE screen = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, CONSOLE_TEXTMODE_BUFFER, nullptr);
    require(screen != INVALID_HANDLE_VALUE, "owned screen buffer");
    const HANDLE original = GetStdHandle(STD_ERROR_HANDLE);
    struct Restore { HANDLE original, screen; ~Restore(){ SetStdHandle(STD_ERROR_HANDLE, original); CloseHandle(screen); } } restore{original, screen};
    require(SetStdHandle(STD_ERROR_HANDLE, screen) != FALSE, "stderr screen sink");
    const std::wstring wide = L"診断 日本語";
    write_stderr_utf8(utf8_from_utf16(wide));
    CONSOLE_SCREEN_BUFFER_INFO info{};
    require(GetConsoleScreenBufferInfo(screen, &info) != FALSE, "console cursor read");
    require(info.dwCursorPosition.Y == 0, "unexpected console wrap");
    std::wstring observed(info.dwCursorPosition.X, L'\0'); DWORD read{};
    require(ReadConsoleOutputCharacterW(screen, observed.data(), static_cast<DWORD>(observed.size()), {}, &read) != FALSE,
        "console UTF-16 readback");
    observed.resize(read);
    std::erase(observed, L'\0'); // Wide glyph continuation cells.
    require(observed == wide && GetConsoleOutputCP() == code_page, "console encoding / shared setting changed");
    return 0;
}
void console_children()
{
    std::array<wchar_t, 32768> module{};
    require(GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size())) != 0, "test executable identity");
    for (const UINT cp : {932u, 65001u})
    {
        std::wstring command = L"\"" + std::wstring{module.data()} + L"\" --owned-console " + std::to_wstring(cp);
        STARTUPINFOW startup{sizeof(startup)}; startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION child{};
        require(CreateProcessW(module.data(), command.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE,
            nullptr, nullptr, &startup, &child) != FALSE, "owned console child");
        CloseHandle(child.hThread);
        const auto wait = WaitForSingleObject(child.hProcess, 15000);
        if (wait != WAIT_OBJECT_0) { TerminateProcess(child.hProcess, 99); WaitForSingleObject(child.hProcess, 5000); }
        DWORD exit{}; const bool got_exit = GetExitCodeProcess(child.hProcess, &exit) != FALSE;
        CloseHandle(child.hProcess);
        require(wait == WAIT_OBJECT_0 && got_exit && exit == 0, "owned console child failed / reaped");
    }
}
}
int wmain(int argc, wchar_t** argv)
{
    try {
        if (argc == 3 && std::wstring_view{argv[1]} == L"--owned-console")
            return owned_console(std::wstring_view{argv[2]} == L"932" ? 932 : 65001);
        units(); console_children();
        std::cout << "Capture stages / color capability / HRESULT / privacy / CP932 and UTF-8 console: PASS\n";
        return 0;
    } catch (...) { write_stderr_utf8(describe_exception(std::current_exception()) + '\n'); return 1; }
}

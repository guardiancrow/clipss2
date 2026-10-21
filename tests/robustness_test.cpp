#include "platform/windows/error_diagnostics.h"
#include "platform/windows/readback_wait.h"
#include "platform/windows/resource_lifetime_model.h"
#include "platform/windows/handle_isolation_model.h"
#include "platform/windows/capture_reuse_model.h"

#include <dxgi.h>
#include <windows.h>
#include <winrt/base.h>

#include <stdexcept>
#include <string>
#include <system_error>
#include <limits>
#include <vector>

namespace
{
bool contains(const std::string& text, const std::string& expected)
{
    return text.find(expected) != std::string::npos;
}

void throw_unknown() { throw 7; }
}

int main()
{
    using namespace clipss2::platform::windows;
    if (decide_readback_map_result(S_OK, false) != ReadbackWaitAction::succeeded ||
        decide_readback_map_result(DXGI_ERROR_WAS_STILL_DRAWING, false) !=
            ReadbackWaitAction::retry ||
        decide_readback_map_result(DXGI_ERROR_WAS_STILL_DRAWING, true) !=
            ReadbackWaitAction::timed_out ||
        decide_readback_map_result(E_FAIL, false) != ReadbackWaitAction::failed ||
        !is_device_lost_hresult(DXGI_ERROR_DEVICE_REMOVED) ||
        is_device_lost_hresult(E_FAIL))
        return 1;

    const auto known = format_hresult("Map", DXGI_ERROR_DEVICE_REMOVED);
    const auto arbitrary = format_hresult("Operation", static_cast<HRESULT>(0xA1234567));
    const auto win32 = format_win32_error("Open", ERROR_ACCESS_DENIED);
    if (!contains(known, "Map") || !contains(known, "0x887A0005") ||
        !contains(known, "DXGI_ERROR_DEVICE_REMOVED") ||
        !contains(arbitrary, "0xA1234567") ||
        !contains(arbitrary, "No system message is available") ||
        !contains(win32, "0x00000005") ||
        !contains(win32, "ERROR_ACCESS_DENIED") ||
        !contains(format_hresult("Win32 HRESULT", HRESULT_FROM_WIN32(
            ERROR_ACCESS_DENIED)), "E_ACCESSDENIED"))
        return 1;
    require_win32_success(true, ERROR_SUCCESS, "counter");
    try
    {
        require_win32_success(false, ERROR_INVALID_HANDLE,
                              "GetProcessHandleCount failed");
        return 1;
    }
    catch (const std::runtime_error& exception)
    {
        if (!contains(exception.what(), "GetProcessHandleCount failed") ||
            !contains(exception.what(), "ERROR_INVALID_HANDLE"))
            return 1;
    }

    std::exception_ptr error;
    try { throw winrt::hresult_error(E_ACCESSDENIED); }
    catch (...) { error = std::current_exception(); }
    if (!contains(describe_exception(error), "E_ACCESSDENIED")) return 1;
    try { throw std::system_error(std::make_error_code(std::errc::permission_denied)); }
    catch (...) { error = std::current_exception(); }
    if (!contains(describe_exception(error), "system_error")) return 1;
    try { throw std::runtime_error("plain failure"); }
    catch (...) { error = std::current_exception(); }
    if (describe_exception(error) != "plain failure") return 1;
    try { throw_unknown(); }
    catch (...) { error = std::current_exception(); }
    if (describe_exception(error) != "unknown non-standard exception") return 1;

    const ResourceSnapshot baseline{100, 10, 10, 1000, 1000};
    const std::vector<ResourceSnapshot> stable{
        {101, 10, 10, 1200, 1300}, {100, 10, 10, 1100, 1200}};
    if (!evaluate_resource_samples(baseline, stable).resource_counts_passed)
        return 1;
    const std::vector<ResourceSnapshot> leaking{
        {101, 11, 10, 1000, 1000}, {102, 12, 10, 1000, 1000},
        {103, 13, 10, 1000, 1000}};
    const auto leak = evaluate_resource_samples(baseline, leaking);
    if (leak.resource_counts_passed || !leak.handles_monotonically_growing ||
        !leak.user_monotonically_growing)
        return 1;
    try
    {
        (void)evaluate_resource_samples(baseline, {});
        return 1;
    }
    catch (const std::invalid_argument&)
    {
    }

    const std::vector<std::int64_t> stages{0, 0, 1, 1};
    if (first_persistent_growth_stage(stages) != 2 ||
        first_persistent_growth_stage(std::vector<std::int64_t>{0, 0}) !=
            std::numeric_limits<std::size_t>::max())
        return 1;
    const std::vector<std::int64_t> linear{1, 2, 3, 4, 5, 6};
    const std::vector<std::int64_t> plateau{1, 2, 2, 2, 2, 2};
    if (classify_handle_growth(1, {1, 1, 1, 1}, linear) !=
            HandleCleanupClassification::measurement_artifact ||
        classify_handle_growth(0, {1, 1, 0, 0}, {}) !=
            HandleCleanupClassification::delayed_cleanup ||
        classify_handle_growth(0, {1, 1, 1, 1}, plateau) !=
            HandleCleanupClassification::finite_process_cache ||
        classify_handle_growth(0, {1, 1, 1, 1}, linear) !=
            HandleCleanupClassification::persistent_growth ||
        classify_handle_growth(0, {0, 0, 0, 0}, {}) !=
            HandleCleanupClassification::no_growth)
        return 1;

    const auto current = current_lifecycle_counts(100);
    const auto reused = same_item_lifecycle_counts(100, false);
    const auto closed = same_item_lifecycle_counts(100, true);
    if (current.item_creations != 100 || current.session_creations != 100 ||
        reused.item_creations != 1 || reused.session_creations != 100 ||
        closed.item_creations != 0 || closed.session_creations != 0 ||
        classify_capture_reuse(0, 100, 20) !=
            CaptureReuseStrategy::same_target_only ||
        classify_capture_reuse(100, 100, 20) !=
            CaptureReuseStrategy::ineffective ||
        classify_capture_reuse(0, 0, 0) !=
            CaptureReuseStrategy::item_reuse_stops_growth ||
        classify_capture_reuse(0, 10, 0) != CaptureReuseStrategy::unresolved)
        return 1;
}

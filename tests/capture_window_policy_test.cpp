#include "platform/windows/capture_window_policy.h"
#include <iostream>
#include <vector>

int main()
{
    using namespace clipss2::platform::windows;
    int failures = 0;
    auto test = [&](bool selected, bool include_cursor, int stale_check,
                    const char* color, bool color_failure,
                    bool expected_capture, bool expected_error) {
        std::optional<WindowSelectionResult> target;
        if (selected) target.emplace(
            WindowCandidate{}, include_cursor,
            clipss2::capture::CaptureDestination::both);
        int validations = 0, colors = 0, captures = 0;
        bool error = false;
        try
        {
            const bool result = capture_confirmed_selection(target,
                [&](const auto&) { return ++validations != stale_check; },
                [&](const auto&) -> std::string {
                    ++colors;
                    if (color_failure) throw std::runtime_error("Color API unavailable");
                    return color;
                },
                [&](const auto& selection) {
                    if (selection.include_cursor != include_cursor ||
                        selection.destination !=
                            clipss2::capture::CaptureDestination::both)
                        ++failures;
                    ++captures;
                });
            if (result != expected_capture) ++failures;
        }
        catch (const std::runtime_error&) { error = true; }
        if (error != expected_error || captures != int(expected_capture)) ++failures;
        if (!selected && (validations || colors || captures)) ++failures;
        if (stale_check == 1 && colors) ++failures;
    };
    test(true, false, 0, "StandardDynamicRange", false, true, false);
    test(true, true, 0, "StandardDynamicRange", false, true, false);
    test(false, true, 0, "StandardDynamicRange", false, false, false);
    test(true, false, 1, "StandardDynamicRange", false, false, true);
    test(true, false, 2, "StandardDynamicRange", false, false, true);
    test(true, false, 0, "WideColorGamut", false, false, true);
    test(true, false, 0, "HighDynamicRange", false, false, true);
    test(true, false, 0, "Unknown", false, false, true);
    test(true, false, 0, "", true, false, true);
    std::cout << "Capture orchestration failures: " << failures << '\n';
    return failures ? 1 : 0;
}

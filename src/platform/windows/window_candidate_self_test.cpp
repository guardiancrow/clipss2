#include "platform/windows/window_candidate_self_test.h"

#include "platform/windows/unicode_diagnostics.h"
#include "platform/windows/window_candidates.h"

#include <windows.h>

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace clipss2::platform::windows
{
namespace
{
std::string reasons(const WindowCandidate& candidate)
{
    if (candidate.rejection_reasons.empty()) return "-";
    std::string result;
    for (const auto reason : candidate.rejection_reasons)
    {
        if (!result.empty()) result += ',';
        result += rejection_reason_name(reason);
    }
    return result;
}

void print_candidate(const WindowCandidate& candidate)
{
    std::cout << '[' << candidate.enumeration_order << "] selectable="
              << std::boolalpha << candidate.selectable << " HWND=0x" << std::hex
              << candidate.hwnd_value << std::dec << " pid=" << candidate.process_id
              << " bounds=(" << candidate.bounds.left << ',' << candidate.bounds.top
              << ")-(" << candidate.bounds.right << ',' << candidate.bounds.bottom << ')'
              << " valid=" << candidate.valid_window
              << " visible=" << candidate.visible << " cloaked=";
    if (candidate.cloaked_known) std::cout << candidate.cloaked;
    else std::cout << "unknown";
    std::cout << " minimized=" << candidate.minimized
              << " topmost=" << candidate.topmost
              << " tool=" << candidate.tool_window
              << " owned=" << candidate.has_owner
              << " own-process=" << candidate.own_process
              << " bounds-source="
              << (candidate.used_extended_frame_bounds ? "DWM" : "GetWindowRect")
              << " rejected=" << reasons(candidate) << '\n';
    write_stdout_utf8(
        "    title=\"" + utf8_from_utf16(candidate.title) + "\" class=\"" +
        utf8_from_utf16(candidate.class_name) + "\"\n");
    if (!candidate.diagnostic.empty())
        std::cout << "    diagnostic=" << candidate.diagnostic << '\n';
}

void print_order(
    const char* label,
    WindowSelectionSession& session,
    bool forward)
{
    const auto initial = session.selected_index();
    std::vector<std::size_t> order;
    if (initial) order.push_back(*initial);
    const std::size_t count = session.selectable_count();
    for (std::size_t step = 0; step < count; ++step)
    {
        if (!(forward ? session.next() : session.previous()))
            throw std::runtime_error("Candidate cycling stopped unexpectedly.");
        order.push_back(*session.selected_index());
    }
    const bool wrapped = initial && session.selected_index() == initial;
    std::cout << label << " snapshot indices: ";
    constexpr std::size_t display_limit = 20;
    for (std::size_t i = 0; i < std::min(order.size(), display_limit); ++i)
        std::cout << (i == 0 ? "" : " -> ") << order[i];
    if (order.size() > display_limit)
        std::cout << " -> ... (" << order.size() << " positions)";
    std::cout << "; wrap-around=" << std::boolalpha << wrapped << '\n';
    if (!wrapped) throw std::runtime_error("Candidate cycling did not wrap to its start.");
}
}

int run_window_candidate_self_test()
{
    const HWND foreground = GetForegroundWindow();
    const auto foreground_value = reinterpret_cast<std::uintptr_t>(foreground);
    WindowSelectionSession session{enumerate_window_candidates()};
    session.start(foreground_value);

    std::cout << "Window candidate self-test\nSelection state: "
              << selection_state_name(session.state())
              << "\nSnapshot candidate count: " << session.candidates().size()
              << "\nSelectable candidate count: " << session.selectable_count()
              << "\nForeground HWND: 0x" << std::hex << foreground_value << std::dec
              << '\n';
    const auto count_property = [&session](auto projection)
    {
        return std::count_if(
            session.candidates().begin(), session.candidates().end(), projection);
    };
    std::cout << "Observed classifications: visible="
              << count_property([](const auto& c) { return c.visible; })
              << " cloaked=" << count_property([](const auto& c) { return c.cloaked; })
              << " minimized=" << count_property([](const auto& c) { return c.minimized; })
              << " topmost=" << count_property([](const auto& c) { return c.topmost; })
              << " tool=" << count_property([](const auto& c) { return c.tool_window; })
              << " owned=" << count_property([](const auto& c) { return c.has_owner; })
              << " own-process="
              << count_property([](const auto& c) { return c.own_process; }) << '\n';
    if (const auto initial = session.selected_index())
        std::cout << "Initial candidate snapshot index: " << *initial
                  << (session.candidates()[*initial].hwnd_value == foreground_value
                          ? " (foreground)\n" : " (first selectable fallback)\n");
    else
        std::cout << "Initial candidate: none\n";

    constexpr std::size_t list_limit = 40;
    const auto& candidates = session.candidates();
    for (std::size_t i = 0; i < std::min(candidates.size(), list_limit); ++i)
        print_candidate(candidates[i]);
    if (candidates.size() > list_limit)
        std::cout << "Candidate listing truncated after " << list_limit << " entries.\n";

    if (session.selectable_count() == 0)
    {
        std::cout << "Cycling/confirmation: unavailable (no selectable candidate).\n";
        return 1;
    }

    const auto initial = *session.selected_index();
    print_order("Tab forward", session, true);
    if (!session.set_candidate(initial))
        throw std::runtime_error("Could not restore initial candidate.");
    print_order("Shift+Tab reverse", session, false);
    if (!session.set_candidate(initial))
        throw std::runtime_error("Could not restore initial candidate.");

    const WindowCandidate* selected = session.selected_candidate();
    if (selected == nullptr)
        throw std::runtime_error("Could not restore a candidate for confirmation.");
    const auto confirmation = session.confirm(selected->hwnd_value,
        [selected](std::uintptr_t value)
    {
        return selected != nullptr && selected->hwnd_value == value &&
               is_window_candidate_current(*selected);
    });
    std::cout << "Confirm-time HWND/PID/class validation: "
              << (confirmation == ConfirmationStatus::confirmed ? "valid" : "stale/failed")
              << "\nSelection state after explicit confirm: "
              << selection_state_name(session.state())
              << "\nCapture side effect: none\n";
    return confirmation == ConfirmationStatus::confirmed ? 0 : 1;
}
}

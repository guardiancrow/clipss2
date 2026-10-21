#include "platform/windows/window_candidates.h"

#include <stdexcept>
#include <utility>

namespace clipss2::platform::windows
{
bool WindowBounds::empty() const noexcept
{
    return right <= left || bottom <= top;
}

const char* rejection_reason_name(CandidateRejectionReason reason) noexcept
{
    switch (reason)
    {
    case CandidateRejectionReason::invalid_window: return "invalid-window";
    case CandidateRejectionReason::not_visible: return "not-visible";
    case CandidateRejectionReason::cloaked: return "cloaked";
    case CandidateRejectionReason::empty_bounds: return "empty-bounds";
    case CandidateRejectionReason::minimized: return "minimized";
    case CandidateRejectionReason::own_process: return "own-process";
    case CandidateRejectionReason::state_unavailable: return "state-unavailable";
    case CandidateRejectionReason::shell_ui: return "shell-ui";
    }
    return "unknown";
}

void classify_window_candidate(WindowCandidate& candidate)
{
    candidate.rejection_reasons.clear();
    if (!candidate.valid_window)
        candidate.rejection_reasons.push_back(CandidateRejectionReason::invalid_window);
    if (!candidate.visible)
        candidate.rejection_reasons.push_back(CandidateRejectionReason::not_visible);
    if (!candidate.cloaked_known)
        candidate.rejection_reasons.push_back(CandidateRejectionReason::state_unavailable);
    else if (candidate.cloaked)
        candidate.rejection_reasons.push_back(CandidateRejectionReason::cloaked);
    if (candidate.bounds.empty())
        candidate.rejection_reasons.push_back(CandidateRejectionReason::empty_bounds);
    if (candidate.minimized)
        candidate.rejection_reasons.push_back(CandidateRejectionReason::minimized);
    if (candidate.own_process)
        candidate.rejection_reasons.push_back(CandidateRejectionReason::own_process);
    if (candidate.class_name == L"Shell_TrayWnd" ||
        candidate.class_name == L"Shell_SecondaryTrayWnd")
        candidate.rejection_reasons.push_back(CandidateRejectionReason::shell_ui);
    candidate.selectable = candidate.rejection_reasons.empty();
}

const char* selection_state_name(SelectionSessionState state) noexcept
{
    switch (state)
    {
    case SelectionSessionState::inactive: return "Inactive";
    case SelectionSessionState::window_selecting: return "WindowSelecting";
    case SelectionSessionState::confirmed: return "Confirmed";
    case SelectionSessionState::cancelled: return "Cancelled";
    }
    return "Unknown";
}

WindowSelectionSession::WindowSelectionSession(std::vector<WindowCandidate> snapshot)
    : snapshot_(std::move(snapshot)) {}

void WindowSelectionSession::start(std::uintptr_t foreground_hwnd)
{
    if (state_ != SelectionSessionState::inactive)
        throw std::logic_error("Selection session has already started.");
    state_ = SelectionSessionState::window_selecting;
    for (std::size_t i = 0; i < snapshot_.size(); ++i)
        if (snapshot_[i].selectable && snapshot_[i].hwnd_value == foreground_hwnd)
        {
            selected_index_ = i;
            return;
        }
    for (std::size_t i = 0; i < snapshot_.size(); ++i)
        if (snapshot_[i].selectable)
        {
            selected_index_ = i;
            return;
        }
}

bool WindowSelectionSession::cycle(bool forward)
{
    if (state_ != SelectionSessionState::window_selecting || !selected_index_)
        return false;
    const auto count = snapshot_.size();
    for (std::size_t distance = 1; distance <= count; ++distance)
    {
        const std::size_t index = forward
            ? (*selected_index_ + distance) % count
            : (*selected_index_ + count - (distance % count)) % count;
        if (snapshot_[index].selectable)
        {
            selected_index_ = index;
            return true;
        }
    }
    return false;
}

bool WindowSelectionSession::next() { return cycle(true); }
bool WindowSelectionSession::previous() { return cycle(false); }

bool WindowSelectionSession::set_candidate(std::size_t snapshot_index)
{
    if (state_ != SelectionSessionState::window_selecting ||
        snapshot_index >= snapshot_.size() || !snapshot_[snapshot_index].selectable)
        return false;
    selected_index_ = snapshot_index;
    return true;
}

bool WindowSelectionSession::set_candidate_by_hwnd(std::uintptr_t hwnd_value)
{
    if (state_ != SelectionSessionState::window_selecting) return false;
    for (std::size_t index = 0; index < snapshot_.size(); ++index)
        if (snapshot_[index].selectable && snapshot_[index].hwnd_value == hwnd_value)
        {
            selected_index_ = index;
            return true;
        }
    return false;
}

ConfirmationStatus WindowSelectionSession::confirm(
    std::uintptr_t expected_hwnd,
    const std::function<bool(std::uintptr_t)>& is_window)
{
    if (state_ != SelectionSessionState::window_selecting)
        return ConfirmationStatus::not_selecting;
    const auto* candidate = selected_candidate();
    if (!candidate) return ConfirmationStatus::no_candidate;
    if (candidate->hwnd_value != expected_hwnd)
        return ConfirmationStatus::stale_window;
    if (!is_window(candidate->hwnd_value)) return ConfirmationStatus::stale_window;
    state_ = SelectionSessionState::confirmed;
    return ConfirmationStatus::confirmed;
}

void WindowSelectionSession::cancel() noexcept
{
    if (state_ == SelectionSessionState::window_selecting)
        state_ = SelectionSessionState::cancelled;
}

SelectionSessionState WindowSelectionSession::state() const noexcept { return state_; }
const std::vector<WindowCandidate>& WindowSelectionSession::candidates() const noexcept { return snapshot_; }
std::optional<std::size_t> WindowSelectionSession::selected_index() const noexcept { return selected_index_; }
const WindowCandidate* WindowSelectionSession::selected_candidate() const noexcept
{
    return selected_index_ ? &snapshot_[*selected_index_] : nullptr;
}
std::size_t WindowSelectionSession::selectable_count() const noexcept
{
    std::size_t result{};
    for (const auto& candidate : snapshot_) if (candidate.selectable) ++result;
    return result;
}
}

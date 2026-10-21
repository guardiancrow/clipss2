#include "platform/windows/resident_state.h"

#include <stdexcept>

namespace clipss2::platform::windows
{
bool ResidentStateMachine::try_begin_capture() noexcept
{
    if (state_ != ResidentState::idle || shutdown_requested_) return false;
    state_ = ResidentState::selecting;
    return true;
}

bool ResidentStateMachine::try_begin_settings() noexcept
{
    if (state_ != ResidentState::idle || shutdown_requested_) return false;
    state_ = ResidentState::configuring;
    return true;
}

void ResidentStateMachine::settings_finished()
{
    if (state_ != ResidentState::configuring)
        throw std::logic_error("Resident settings completion in an invalid state.");
    finish_operation();
}

void ResidentStateMachine::selection_cancelled()
{
    if (state_ != ResidentState::selecting)
        throw std::logic_error("Resident selection cancel in an invalid state.");
    finish_operation();
}

void ResidentStateMachine::selection_confirmed()
{
    if (state_ != ResidentState::selecting)
        throw std::logic_error("Resident selection confirm in an invalid state.");
    state_ = ResidentState::capturing;
}

void ResidentStateMachine::capture_finished()
{
    if (state_ != ResidentState::capturing)
        throw std::logic_error("Resident capture completion in an invalid state.");
    finish_operation();
}

bool ResidentStateMachine::request_shutdown() noexcept
{
    shutdown_requested_ = true;
    if (state_ != ResidentState::idle) return state_ == ResidentState::shutting_down;
    state_ = ResidentState::shutting_down;
    return true;
}

void ResidentStateMachine::finish_operation()
{
    state_ = shutdown_requested_ ? ResidentState::shutting_down : ResidentState::idle;
}
}

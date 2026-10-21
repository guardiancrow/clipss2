#include "platform/windows/resident_state.h"

#include <cstdlib>
#include <iostream>

namespace
{
using clipss2::platform::windows::ResidentState;
using clipss2::platform::windows::ResidentStateMachine;

void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    ResidentStateMachine state;
    require(state.state() == ResidentState::idle, "initial state is not Idle");
    require(state.try_begin_capture(), "Idle hotkey was not accepted");
    require(state.state() == ResidentState::selecting, "Idle -> Selecting failed");
    for (int index = 0; index < 10; ++index)
        require(!state.try_begin_capture(), "Selecting hotkey was accepted");
    state.selection_cancelled();
    require(state.state() == ResidentState::idle, "Cancel -> Idle failed");

    require(state.try_begin_capture(), "second Idle hotkey was not accepted");
    state.selection_confirmed();
    require(state.state() == ResidentState::capturing, "Confirm -> Capturing failed");
    for (int index = 0; index < 10; ++index)
        require(!state.try_begin_capture(), "Capturing hotkey was accepted");
    state.capture_finished();
    require(state.state() == ResidentState::idle, "Success -> Idle failed");
    require(state.try_begin_capture(), "no-deferred-action Idle hotkey failed");
    state.selection_confirmed();
    state.capture_finished();
    require(state.state() == ResidentState::idle, "Failure -> Idle model failed");

    require(state.request_shutdown(), "Idle shutdown was deferred");
    require(state.state() == ResidentState::shutting_down,
            "Idle -> ShuttingDown failed");

    ResidentStateMachine busy_shutdown;
    require(busy_shutdown.try_begin_capture(), "busy-shutdown setup failed");
    require(!busy_shutdown.request_shutdown(), "Selecting shutdown was immediate");
    require(!busy_shutdown.try_begin_capture(), "shutdown-pending hotkey accepted");

    ResidentStateMachine tray_parity;
    require(tray_parity.try_begin_capture(), "Idle tray request was not accepted");
    for (int index = 0; index < 10; ++index)
        require(!tray_parity.try_begin_capture(), "busy tray request was accepted");
    tray_parity.selection_cancelled();
    require(tray_parity.state() == ResidentState::idle,
            "busy tray requests were deferred after cancellation");

    ResidentStateMachine settings;
    require(settings.try_begin_settings(), "Idle settings request was not accepted");
    require(settings.state() == ResidentState::configuring,
        "Idle -> Configuring failed");
    require(!settings.try_begin_capture(), "Configuring capture request was accepted");
    require(!settings.try_begin_settings(), "second Settings dialog was accepted");
    settings.settings_finished();
    require(settings.state() == ResidentState::idle, "Configuring -> Idle failed");
    require(settings.try_begin_capture(), "capture unavailable after Settings close");
    settings.selection_cancelled();
    busy_shutdown.selection_confirmed();
    busy_shutdown.capture_finished();
    require(busy_shutdown.state() == ResidentState::shutting_down,
            "busy completion did not enter ShuttingDown");

    std::cout << "Resident state transitions and no-deferred policy: passed\n";
    return 0;
}

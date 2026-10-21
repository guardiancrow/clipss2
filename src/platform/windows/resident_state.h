#pragma once

namespace clipss2::platform::windows
{
enum class ResidentState
{
    idle,
    selecting,
    capturing,
    configuring,
    shutting_down,
};

class ResidentStateMachine final
{
public:
    [[nodiscard]] ResidentState state() const noexcept { return state_; }
    [[nodiscard]] bool shutdown_requested() const noexcept
    {
        return shutdown_requested_;
    }

    // Returns true only when a hotkey or tray request starts a new selection.
    // Busy requests are deliberately discarded rather than queued.
    [[nodiscard]] bool try_begin_capture() noexcept;
    [[nodiscard]] bool try_begin_settings() noexcept;
    void settings_finished();
    void selection_cancelled();
    void selection_confirmed();
    void capture_finished();

    // Returns true when shutdown can begin immediately. A busy operation is
    // allowed to reach its normal safe boundary first.
    [[nodiscard]] bool request_shutdown() noexcept;

private:
    void finish_operation();

    ResidentState state_{ResidentState::idle};
    bool shutdown_requested_{};
};
}

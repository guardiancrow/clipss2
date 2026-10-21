#include "platform/windows/window_selection_input.h"
#include <array>
#include <stdexcept>

namespace clipss2::platform::windows
{
const char* selection_input_source_name(SelectionInputSource source) noexcept
{
    switch (source)
    {
    case SelectionInputSource::initial: return "initial";
    case SelectionInputSource::keyboard: return "keyboard";
    case SelectionInputSource::mouse: return "mouse";
    }
    return "unknown";
}

SelectionInputArbitrator::SelectionInputArbitrator(
    ScreenPosition initial_cursor) noexcept
    : cursor_baseline_(initial_cursor) {}

bool SelectionInputArbitrator::observe_cursor(ScreenPosition cursor) noexcept
{
    const bool moved = cursor != cursor_baseline_;
    cursor_baseline_ = cursor;
    return moved;
}

void SelectionInputArbitrator::record_keyboard_input(
    ScreenPosition cursor) noexcept
{
    cursor_baseline_ = cursor;
    source_ = SelectionInputSource::keyboard;
}

void SelectionInputArbitrator::record_mouse_selection() noexcept
{
    source_ = SelectionInputSource::mouse;
}

ScreenPosition SelectionInputArbitrator::cursor_baseline() const noexcept
{
    return cursor_baseline_;
}

SelectionInputSource SelectionInputArbitrator::source() const noexcept
{
    return source_;
}

bool MouseSelectionGesture::press(std::uintptr_t candidate) noexcept
{
    if (active() || candidate == 0) return false;
    candidate_ = candidate;
    inside_ = true;
    cancel_pending_ = false;
    return true;
}

bool MouseSelectionGesture::move_over(
    std::optional<std::uintptr_t> candidate) noexcept
{
    if (!active()) return false;
    const bool inside = candidate && candidate == candidate_;
    const bool changed = inside_ != inside;
    inside_ = inside;
    return changed;
}

MouseReleaseAction MouseSelectionGesture::release(
    std::optional<std::uintptr_t> candidate,
    bool pressed_candidate_is_live) noexcept
{
    if (!active()) return MouseReleaseAction::none;
    const bool confirm = !cancel_pending_ && inside_ && candidate &&
        candidate == candidate_ && pressed_candidate_is_live;
    const bool cancel = cancel_pending_;
    candidate_.reset();
    inside_ = false;
    cancel_pending_ = false;
    if (cancel) return MouseReleaseAction::cancel_selection;
    return confirm ? MouseReleaseAction::confirm : MouseReleaseAction::none;
}

MouseRecoveryAction MouseSelectionGesture::recover_lost_release(
    bool physical_button_is_down) noexcept
{
    if (!active() || physical_button_is_down) return MouseRecoveryAction::none;
    const bool cancel = cancel_pending_;
    candidate_.reset();
    inside_ = false;
    cancel_pending_ = false;
    return cancel ? MouseRecoveryAction::cancel_selection
                  : MouseRecoveryAction::resume_selection;
}

bool MouseSelectionGesture::request_cancel() noexcept
{
    if (!active()) return false;
    cancel_pending_ = true;
    inside_ = false;
    return true;
}

bool MouseSelectionGesture::active() const noexcept { return candidate_.has_value(); }
bool MouseSelectionGesture::inside() const noexcept { return inside_; }
bool MouseSelectionGesture::cancel_pending() const noexcept { return cancel_pending_; }
std::optional<std::uintptr_t> MouseSelectionGesture::candidate() const noexcept
{
    return candidate_;
}

bool CursorCapturePolicy::include_cursor() const noexcept
{
    return include_cursor_;
}

bool CursorCapturePolicy::toggle(bool mouse_gesture_active) noexcept
{
    if (mouse_gesture_active) return false;
    include_cursor_ = !include_cursor_;
    return true;
}

bool JpegSessionPolicy::cycle_profile(bool enabled, bool pressed, bool repeated, bool modified)
{
    using namespace codec::jpeg;
    if (!enabled || pressed || repeated || modified) return false;
    const auto* explicit_selection = std::get_if<JpegProfileSelection>(&selection_);
    if (!explicit_selection) selection_ = JpegProfileSelection{JpegProfile::fast};
    else switch (explicit_selection->profile)
    {
    case JpegProfile::fast: selection_ = JpegProfileSelection{JpegProfile::balanced}; break;
    case JpegProfile::balanced: selection_ = JpegProfileSelection{JpegProfile::smallest}; break;
    case JpegProfile::smallest: selection_ = JpegCurrentDefault{}; break;
    default: throw std::logic_error("Invalid session JPEG profile.");
    }
    return true;
}

bool JpegSessionPolicy::cycle_encoder(bool enabled, bool pressed, bool repeated, bool modified,
    std::span<const codec::jpeg::JpegBackendCapabilities> catalog)
{
    using namespace codec::jpeg;
    auto* explicit_selection = std::get_if<JpegProfileSelection>(&selection_);
    if (!enabled || pressed || repeated || modified || !explicit_selection) return false;
    constexpr std::array choices{JpegBackendId::automatic, JpegBackendId::turbo,
        JpegBackendId::jpegli, JpegBackendId::mozjpeg, JpegBackendId::wic};
    std::size_t current{};
    while (current < choices.size() && choices[current] != explicit_selection->backend) ++current;
    if (current == choices.size()) throw std::logic_error("Invalid session JPEG encoder.");
    for (std::size_t step = 1; step <= choices.size(); ++step)
    {
        const auto candidate = choices[(current + step) % choices.size()];
        if (candidate != JpegBackendId::automatic)
        {
            try
            {
                (void)resolve_jpeg_product_selection(
                    JpegProfileSelection{explicit_selection->profile, candidate}, catalog);
            }
            catch (const std::invalid_argument&) { continue; }
        }
        if (candidate == explicit_selection->backend) return false;
        explicit_selection->backend = candidate;
        return true;
    }
    return false;
}

std::string JpegSessionPolicy::hud_text() const
{
    using namespace codec::jpeg;
    const auto short_name = [](JpegBackendId id) -> std::string
    { return id == JpegBackendId::turbo ? "Turbo" : jpeg_backend_name(id); };
    const auto* explicit_selection = std::get_if<JpegProfileSelection>(&selection_);
    if (!explicit_selection) return "P: JPEG: Default (Turbo)";
    std::string profile;
    switch (explicit_selection->profile)
    {
    case JpegProfile::fast: profile = "Fast"; break;
    case JpegProfile::balanced: profile = "Balanced"; break;
    case JpegProfile::smallest: profile = "Smallest"; break;
    default: throw std::logic_error("Invalid session JPEG profile.");
    }
    std::string encoder;
    if (explicit_selection->backend == JpegBackendId::automatic)
    {
        try { encoder = "Auto (" + short_name(resolve_jpeg_product_selection(selection_).selection.backend) + ")"; }
        catch (const std::invalid_argument&) { encoder = "Auto (unavailable)"; }
    }
    else encoder = short_name(explicit_selection->backend);
    return "P: JPEG: " + profile + "   E: Encoder: " + encoder;
}
}

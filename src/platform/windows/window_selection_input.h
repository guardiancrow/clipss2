#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include "image/image_file_format.h"
#include "capture/file_compression.h"
#include "codec/jpeg/jpeg_product_selection.h"

namespace clipss2::platform::windows
{
// AVIF and JXL have independent state; JPEG's established policy remains separate.
[[nodiscard]] inline bool cycle_codec_profile(image::ImageFileFormat format,
    capture::FileCompression& avif, capture::FileCompression& jxl,
    bool pressed, bool repeated, bool modified)
{
    if (pressed || repeated || modified) return false;
    if (format == image::ImageFileFormat::avif) capture::cycle_file_compression(avif);
    else if (format == image::ImageFileFormat::jxl) capture::cycle_file_compression(jxl);
    else return false;
    return true;
}
struct ScreenPosition
{
    std::int32_t x{};
    std::int32_t y{};

    [[nodiscard]] bool operator==(const ScreenPosition&) const noexcept = default;
};

enum class SelectionInputSource
{
    initial,
    keyboard,
    mouse,
};

[[nodiscard]] const char* selection_input_source_name(
    SelectionInputSource source) noexcept;

// Pure input-arbitration state. Polling alone is not input: a cursor position
// must differ from the baseline before mouse hit resolution is allowed.
class SelectionInputArbitrator final
{
public:
    explicit SelectionInputArbitrator(ScreenPosition initial_cursor) noexcept;

    [[nodiscard]] bool observe_cursor(ScreenPosition cursor) noexcept;
    void record_keyboard_input(ScreenPosition cursor) noexcept;
    void record_mouse_selection() noexcept;

    [[nodiscard]] ScreenPosition cursor_baseline() const noexcept;
    [[nodiscard]] SelectionInputSource source() const noexcept;

private:
    ScreenPosition cursor_baseline_;
    SelectionInputSource source_{SelectionInputSource::initial};
};

enum class MouseReleaseAction
{
    none,
    confirm,
    cancel_selection,
};

enum class MouseRecoveryAction
{
    none,
    resume_selection,
    cancel_selection,
};

// Pure state for an exclusive click gesture. The candidate is locked at down;
// movement only changes whether release would confirm it.
class MouseSelectionGesture final
{
public:
    [[nodiscard]] bool press(std::uintptr_t candidate) noexcept;
    [[nodiscard]] bool move_over(
        std::optional<std::uintptr_t> candidate) noexcept;
    [[nodiscard]] MouseReleaseAction release(
        std::optional<std::uintptr_t> candidate,
        bool pressed_candidate_is_live) noexcept;
    // Clears a gesture only after the physical button is known to be up. This
    // recovery path can never produce a Confirm action.
    [[nodiscard]] MouseRecoveryAction recover_lost_release(
        bool physical_button_is_down) noexcept;
    [[nodiscard]] bool request_cancel() noexcept;

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] bool inside() const noexcept;
    [[nodiscard]] bool cancel_pending() const noexcept;
    [[nodiscard]] std::optional<std::uintptr_t> candidate() const noexcept;

private:
    std::optional<std::uintptr_t> candidate_;
    bool inside_{};
    bool cancel_pending_{};
};

// Per-selector, non-persistent product policy. A new instance always starts
// with cursor capture disabled.
class CursorCapturePolicy final
{
public:
    [[nodiscard]] bool include_cursor() const noexcept;
    [[nodiscard]] bool toggle(bool mouse_gesture_active) noexcept;

private:
    bool include_cursor_{};
};

class FileFormatPolicy final
{
public:
    explicit FileFormatPolicy(image::ImageFileFormat initial = image::ImageFileFormat::png) noexcept
        : format_(initial) {}
    [[nodiscard]] image::ImageFileFormat format() const noexcept { return format_; }
    [[nodiscard]] bool toggle(bool gesture_active, bool repeated, bool modified) noexcept
    {
        if (gesture_active || repeated || modified) return false;
        switch (format_)
        {
        case image::ImageFileFormat::png: format_ = image::ImageFileFormat::jpeg; break;
        case image::ImageFileFormat::jpeg: format_ = image::ImageFileFormat::webp; break;
        case image::ImageFileFormat::webp: format_ = image::ImageFileFormat::avif; break;
        case image::ImageFileFormat::avif: format_ = image::ImageFileFormat::jxl; break;
        case image::ImageFileFormat::jxl: format_ = image::ImageFileFormat::png; break;
        default: return false;
        }
        return true;
    }
private:
    image::ImageFileFormat format_{image::ImageFileFormat::png};
};

// Mutable session intent, initialized from an already validated preference.
// No format/destination transition owns or resets this value.
class JpegSessionPolicy final
{
public:
    explicit JpegSessionPolicy(codec::jpeg::JpegProductSelection initial = codec::jpeg::JpegCurrentDefault{})
        : selection_(std::move(initial)) { codec::jpeg::validate_jpeg_product_selection(selection_); }
    [[nodiscard]] const codec::jpeg::JpegProductSelection& selection() const noexcept
    { return selection_; }
    [[nodiscard]] bool cycle_profile(bool enabled, bool pressed, bool repeated, bool modified);
    [[nodiscard]] bool cycle_encoder(bool enabled, bool pressed, bool repeated, bool modified,
        std::span<const codec::jpeg::JpegBackendCapabilities> catalog =
            codec::jpeg::current_product_jpeg_backends());
    [[nodiscard]] std::string hud_text() const;
private:
    codec::jpeg::JpegProductSelection selection_{codec::jpeg::JpegCurrentDefault{}};
};
}

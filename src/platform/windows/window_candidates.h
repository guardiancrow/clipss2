#pragma once
#include "codec/jpeg/jpeg_product_selection.h"

#include "capture/capture_destination.h"
#include "capture/file_compression.h"
#include "image/image_file_format.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace clipss2::platform::windows
{
struct WindowBounds
{
    std::int32_t left{};
    std::int32_t top{};
    std::int32_t right{};
    std::int32_t bottom{};

    [[nodiscard]] bool empty() const noexcept;
};

enum class CandidateRejectionReason
{
    invalid_window,
    not_visible,
    cloaked,
    empty_bounds,
    minimized,
    own_process,
    state_unavailable,
    shell_ui,
};

[[nodiscard]] const char* rejection_reason_name(
    CandidateRejectionReason reason) noexcept;

struct WindowCandidate
{
    std::uintptr_t hwnd_value{};
    std::wstring title;
    std::wstring class_name;
    std::uint32_t process_id{};
    WindowBounds bounds;
    bool valid_window{};
    bool visible{};
    bool cloaked{};
    bool cloaked_known{};
    bool minimized{};
    bool has_owner{};
    bool tool_window{};
    bool topmost{};
    bool own_process{};
    bool used_extended_frame_bounds{};
    std::size_t enumeration_order{};
    bool selectable{};
    std::vector<CandidateRejectionReason> rejection_reasons;
    std::string diagnostic;
    // Native failure identity for bounded diagnostics; no title/process dump.
    std::optional<std::int32_t> cloaked_query_failure;
    std::optional<std::int32_t> bounds_query_failure;
    std::optional<std::uint32_t> bounds_win32_failure;
};

struct WindowSelectionResult
{
    WindowCandidate candidate;
    bool include_cursor{};
    capture::CaptureDestination destination{capture::CaptureDestination::file};
    // Value snapshot returned by Confirm; no shared mutable settings reference.
    image::ImageFileFormat file_format{image::ImageFileFormat::png};
    codec::jpeg::JpegProductSelection jpeg_selection{codec::jpeg::JpegCurrentDefault{}};
    capture::FileCompression avif_profile;
    capture::FileCompression jxl_profile;
};

// Pure provisional policy. Tool/owned/untitled windows remain selectable diagnostics.
void classify_window_candidate(WindowCandidate& candidate);

[[nodiscard]] std::vector<WindowCandidate> enumerate_window_candidates();
// Revalidates identity immediately before use; no Win32 check can eliminate a
// subsequent destruction race.
[[nodiscard]] bool is_window_candidate_current(const WindowCandidate& candidate);

enum class SelectionSessionState
{
    inactive,
    window_selecting,
    confirmed,
    cancelled,
};

[[nodiscard]] const char* selection_state_name(SelectionSessionState state) noexcept;

enum class ConfirmationStatus
{
    confirmed,
    not_selecting,
    no_candidate,
    stale_window,
};

class WindowSelectionSession final
{
public:
    explicit WindowSelectionSession(std::vector<WindowCandidate> snapshot);

    void start(std::uintptr_t foreground_hwnd);
    bool next();
    bool previous();
    bool set_candidate(std::size_t snapshot_index);
    bool set_candidate_by_hwnd(std::uintptr_t hwnd_value);
    [[nodiscard]] ConfirmationStatus confirm(
        std::uintptr_t expected_hwnd,
        const std::function<bool(std::uintptr_t)>& is_window);
    void cancel() noexcept;

    [[nodiscard]] SelectionSessionState state() const noexcept;
    [[nodiscard]] const std::vector<WindowCandidate>& candidates() const noexcept;
    [[nodiscard]] std::optional<std::size_t> selected_index() const noexcept;
    [[nodiscard]] const WindowCandidate* selected_candidate() const noexcept;
    [[nodiscard]] std::size_t selectable_count() const noexcept;

private:
    [[nodiscard]] bool cycle(bool forward);

    std::vector<WindowCandidate> snapshot_;
    SelectionSessionState state_{SelectionSessionState::inactive};
    std::optional<std::size_t> selected_index_;
};
}

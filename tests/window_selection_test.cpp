#include "platform/windows/window_candidates.h"
#include "platform/windows/window_selection_input.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <utility>
#include <vector>

namespace
{
clipss2::platform::windows::WindowCandidate candidate(
    std::uintptr_t id,
    bool selectable)
{
    clipss2::platform::windows::WindowCandidate value;
    value.hwnd_value = id;
    value.valid_window = selectable;
    value.visible = selectable;
    value.cloaked_known = true;
    value.bounds = {0, 0, selectable ? 10 : 0, selectable ? 10 : 0};
    clipss2::platform::windows::classify_window_candidate(value);
    return value;
}

bool has_reason(
    const clipss2::platform::windows::WindowCandidate& candidate,
    clipss2::platform::windows::CandidateRejectionReason reason)
{
    return std::find(candidate.rejection_reasons.begin(),
                     candidate.rejection_reasons.end(), reason) !=
           candidate.rejection_reasons.end();
}
}

int main()
{
    using namespace clipss2::platform::windows;

    WindowSelectionSession session{{candidate(10, true), candidate(20, true), candidate(30, true)}};
    session.start(20);
    if (!session.selected_candidate() || session.selected_candidate()->hwnd_value != 20 ||
        !session.next() || session.selected_candidate()->hwnd_value != 30 ||
        !session.next() || session.selected_candidate()->hwnd_value != 10 ||
        !session.previous() || session.selected_candidate()->hwnd_value != 30)
        return 1;

    WindowSelectionSession fallback{{candidate(10, true), candidate(20, false), candidate(30, true)}};
    fallback.start(20);
    if (!fallback.selected_candidate() || fallback.selected_candidate()->hwnd_value != 10 ||
        !fallback.previous() || fallback.selected_candidate()->hwnd_value != 30 ||
        !fallback.next() || fallback.selected_candidate()->hwnd_value != 10)
        return 1;

    WindowSelectionSession absent{{candidate(10, true), candidate(30, true)}};
    absent.start(999);
    if (!absent.selected_candidate() || absent.selected_candidate()->hwnd_value != 10)
        return 1;
    if (fallback.confirm(10, [](std::uintptr_t) { return false; }) !=
            ConfirmationStatus::stale_window ||
        fallback.state() != SelectionSessionState::window_selecting)
        return 1;
    if (fallback.confirm(10, [](std::uintptr_t) { return true; }) !=
            ConfirmationStatus::confirmed ||
        fallback.state() != SelectionSessionState::confirmed)
        return 1;

    int confirm_validation_calls = 0;
    WindowSelectionSession keyboard{{candidate(10, true), candidate(20, true)}};
    keyboard.start(10);
    if (!keyboard.next() || !keyboard.previous() || confirm_validation_calls != 0 ||
        keyboard.state() != SelectionSessionState::window_selecting)
        return 1;
    if (keyboard.confirm(10, [&confirm_validation_calls](std::uintptr_t)
        {
            ++confirm_validation_calls;
            return true;
        }) != ConfirmationStatus::confirmed || confirm_validation_calls != 1)
        return 1;

    WindowSelectionSession cancelled{{candidate(10, true)}};
    cancelled.start(10);
    // WM_DISPLAYCHANGE is translated into this same model cancellation before
    // selector windows are torn down. Confirmation must then be impossible.
    cancelled.cancel();
    if (cancelled.state() != SelectionSessionState::cancelled ||
        cancelled.confirm(10, [](std::uintptr_t) { return true; }) !=
            ConfirmationStatus::not_selecting)
        return 1;

    SelectionInputArbitrator input{{100, 200}};
    WindowSelectionSession mixed{{candidate(10, true), candidate(20, true), candidate(30, true)}};
    mixed.start(10);

    if (input.observe_cursor({100, 200}) || input.source() != SelectionInputSource::initial)
        return 1;

    if (!mixed.next()) return 1;
    input.record_keyboard_input({100, 200});
    if (mixed.selected_candidate()->hwnd_value != 20 ||
        input.source() != SelectionInputSource::keyboard ||
        input.observe_cursor({100, 200}))
        return 1;

    if (!input.observe_cursor({150, -25})) return 1;
    const auto keyboard_candidate = mixed.selected_candidate()->hwnd_value;
    if (mixed.set_candidate_by_hwnd(999) ||
        mixed.selected_candidate()->hwnd_value != keyboard_candidate ||
        input.source() != SelectionInputSource::keyboard)
        return 1;

    if (!mixed.set_candidate_by_hwnd(30)) return 1;
    input.record_mouse_selection();
    if (mixed.selected_candidate()->hwnd_value != 30 ||
        input.source() != SelectionInputSource::mouse)
        return 1;

    if (!mixed.previous()) return 1;
    input.record_keyboard_input({150, -25});
    if (mixed.selected_candidate()->hwnd_value != 20 ||
        input.observe_cursor({150, -25}) ||
        input.source() != SelectionInputSource::keyboard)
        return 1;

    WindowCandidate rejected = candidate(40, true);
    rejected.visible = false;
    rejected.cloaked = true;
    rejected.minimized = true;
    rejected.own_process = true;
    rejected.bounds = {};
    classify_window_candidate(rejected);
    if (rejected.selectable || rejected.rejection_reasons.size() != 5 ||
        !has_reason(rejected, CandidateRejectionReason::not_visible) ||
        !has_reason(rejected, CandidateRejectionReason::cloaked) ||
        !has_reason(rejected, CandidateRejectionReason::minimized) ||
        !has_reason(rejected, CandidateRejectionReason::own_process) ||
        !has_reason(rejected, CandidateRejectionReason::empty_bounds))
        return 1;

    WindowCandidate unknown = candidate(45, true);
    unknown.cloaked_known = false;
    classify_window_candidate(unknown);
    if (unknown.selectable ||
        !has_reason(unknown, CandidateRejectionReason::state_unavailable))
        return 1;

    WindowCandidate diagnostic = candidate(50, true);
    diagnostic.tool_window = true;
    diagnostic.has_owner = true;
    diagnostic.title = L"<untitled>";
    classify_window_candidate(diagnostic);
    if (!diagnostic.selectable)
        return 1;

    WindowCandidate taskbar = candidate(55, true);
    taskbar.class_name = L"Shell_TrayWnd";
    classify_window_candidate(taskbar);
    if (taskbar.selectable ||
        !has_reason(taskbar, CandidateRejectionReason::shell_ui))
        return 1;

    MouseSelectionGesture click;
    if (!click.press(10) ||
        click.release(10, true) != MouseReleaseAction::confirm)
        return 1;

    MouseSelectionGesture drag_out;
    if (!drag_out.press(10) || !drag_out.move_over(std::nullopt) ||
        drag_out.release(std::nullopt, true) != MouseReleaseAction::none)
        return 1;

    MouseSelectionGesture leave_return;
    if (!leave_return.press(10) || !leave_return.move_over(std::nullopt) ||
        !leave_return.move_over(10) ||
        leave_return.release(10, true) != MouseReleaseAction::confirm)
        return 1;

    MouseSelectionGesture different;
    if (!different.press(10) || !different.move_over(20) ||
        different.release(20, true) != MouseReleaseAction::none)
        return 1;

    MouseSelectionGesture stale;
    if (!stale.press(10) ||
        stale.release(10, false) != MouseReleaseAction::none)
        return 1;

    WindowSelectionSession stale_keyboard{{candidate(10, true), candidate(20, true)}};
    stale_keyboard.start(10);
    if (stale_keyboard.confirm(10, [](std::uintptr_t) { return false; }) !=
            ConfirmationStatus::stale_window ||
        stale_keyboard.state() != SelectionSessionState::window_selecting ||
        !stale_keyboard.next() ||
        stale_keyboard.selected_candidate()->hwnd_value != 20 ||
        stale_keyboard.state() != SelectionSessionState::window_selecting ||
        stale_keyboard.confirm(20, [](std::uintptr_t) { return true; }) !=
            ConfirmationStatus::confirmed)
        return 1;

    WindowSelectionSession changed_during_confirm{
        {candidate(10, true), candidate(20, true)}};
    changed_during_confirm.start(10);
    if (!changed_during_confirm.next() ||
        changed_during_confirm.confirm(10, [](std::uintptr_t) { return true; }) !=
            ConfirmationStatus::stale_window ||
        changed_during_confirm.state() != SelectionSessionState::window_selecting)
        return 1;

    MouseSelectionGesture escape_pressed;
    if (!escape_pressed.press(10) || !escape_pressed.request_cancel() ||
        !escape_pressed.cancel_pending() || escape_pressed.inside() ||
        escape_pressed.release(10, true) != MouseReleaseAction::cancel_selection)
        return 1;

    MouseSelectionGesture lost_release;
    if (!lost_release.press(10) ||
        lost_release.recover_lost_release(true) != MouseRecoveryAction::none ||
        !lost_release.active() ||
        lost_release.recover_lost_release(false) !=
            MouseRecoveryAction::resume_selection ||
        lost_release.active())
        return 1;

    MouseSelectionGesture lost_release_after_escape;
    if (!lost_release_after_escape.press(10) ||
        !lost_release_after_escape.request_cancel() ||
        lost_release_after_escape.recover_lost_release(false) !=
            MouseRecoveryAction::cancel_selection ||
        lost_release_after_escape.active())
        return 1;

    MouseSelectionGesture idle;
    if (idle.request_cancel() || idle.release(10, true) != MouseReleaseAction::none)
        return 1;

    CursorCapturePolicy cursor_policy;
    if (cursor_policy.include_cursor()) return 1;
    const auto candidate_before_toggle = mixed.selected_candidate()->hwnd_value;
    const auto source_before_toggle = input.source();
    if (!cursor_policy.toggle(false) || !cursor_policy.include_cursor() ||
        mixed.selected_candidate()->hwnd_value != candidate_before_toggle ||
        input.source() != source_before_toggle ||
        !cursor_policy.toggle(false) || cursor_policy.include_cursor())
        return 1;

    MouseSelectionGesture cursor_press;
    if (!cursor_press.press(10) || cursor_policy.include_cursor() ||
        cursor_policy.toggle(cursor_press.active()) || cursor_policy.include_cursor())
        return 1;

    if (!cursor_policy.toggle(false)) return 1;
    const WindowSelectionResult propagated{
        candidate(10, true), cursor_policy.include_cursor(),
        clipss2::capture::CaptureDestination::both};
    if (!propagated.include_cursor || propagated.candidate.hwnd_value != 10)
        return 1;
    if (propagated.destination != clipss2::capture::CaptureDestination::both)
        return 1;

    FileFormatPolicy format;
    using clipss2::image::ImageFileFormat;
    if (format.format() != ImageFileFormat::png ||
        !format.toggle(false, false, false) || format.format() != ImageFileFormat::jpeg ||
        !format.toggle(false, false, false) || format.format() != ImageFileFormat::webp ||
        !format.toggle(false, false, false) || format.format() != ImageFileFormat::avif ||
        !format.toggle(false, false, false) || format.format() != ImageFileFormat::jxl ||
        !format.toggle(false, false, false) || format.format() != ImageFileFormat::png ||
        format.toggle(false, true, false) || format.toggle(false, false, true)) return 1;
    for (const auto expected : {ImageFileFormat::png, ImageFileFormat::jpeg, ImageFileFormat::webp,
                                ImageFileFormat::avif, ImageFileFormat::jxl})
    {
        MouseSelectionGesture press;
        if (!press.press(10)) return 1;
        const auto locked = format.format();
        if (locked != expected || format.toggle(press.active(), false, false) ||
            format.format() != locked || press.release(10, true) != MouseReleaseAction::confirm) return 1;
        const WindowSelectionResult request{candidate(10, true), false,
            clipss2::capture::CaptureDestination::both, locked};
        if (request.file_format != expected || !format.toggle(false, false, false)) return 1;
    }
    if (FileFormatPolicy{}.format() != ImageFileFormat::png ||
        WindowSelectionResult{}.file_format != ImageFileFormat::png) return 1;
    using namespace clipss2::codec::jpeg;
    JpegSessionPolicy jpeg;
    const JpegProductSelection restored = JpegProfileSelection{JpegProfile::balanced, JpegBackendId::jpegli};
    if (JpegSessionPolicy{restored}.selection() != restored ||
        FileFormatPolicy{ImageFileFormat::jpeg}.format() != ImageFileFormat::jpeg) return 1;
    if (jpeg.hud_text() != "P: JPEG: Default (Turbo)" ||
        jpeg.cycle_encoder(true, false, false, false)) return 1;
    for (int cycle = 0; cycle != 2; ++cycle)
    {
        for (const auto profile : {JpegProfile::fast, JpegProfile::balanced, JpegProfile::smallest})
        {
            if (!jpeg.cycle_profile(true, false, false, false) ||
                jpeg.selection() != JpegProductSelection{JpegProfileSelection{profile}}) return 1;
            const auto expected = profile == JpegProfile::fast ? JpegBackendId::turbo : JpegBackendId::jpegli;
            if (!jpeg.cycle_encoder(true, false, false, false) ||
                jpeg.selection() != JpegProductSelection{JpegProfileSelection{profile, expected}} ||
                !jpeg.cycle_encoder(true, false, false, false) ||
                jpeg.selection() != JpegProductSelection{JpegProfileSelection{profile}} ||
                !jpeg.cycle_encoder(true, false, false, false)) return 1;
            // Manual preference never follows a Profile transition.
        }
        if (!jpeg.cycle_profile(true, false, false, false) ||
            !std::holds_alternative<JpegCurrentDefault>(jpeg.selection())) return 1;
    }
    if (!jpeg.cycle_profile(true, false, false, false)) return 1;
    const auto frozen = jpeg.selection();
    for (const auto flags : {std::array{false, false, false, false},
        std::array{true, true, false, false}, std::array{true, false, true, false},
        std::array{true, false, false, true}})
    {
        if (jpeg.cycle_profile(flags[0], flags[1], flags[2], flags[3]) ||
            jpeg.cycle_encoder(flags[0], flags[1], flags[2], flags[3]) ||
            jpeg.selection() != frozen) return 1;
    }
    // Unavailable/unsupported entries never become manual alternatives.
    auto unavailable = std::vector<JpegBackendCapabilities>(current_product_jpeg_backends().begin(),
        current_product_jpeg_backends().end());
    for (auto& backend : unavailable) backend.available = false;
    if (jpeg.cycle_encoder(true, false, false, false, unavailable) || jpeg.selection() != frozen) return 1;
    const WindowSelectionResult frozen_request{candidate(10, true), true,
        clipss2::capture::CaptureDestination::both, ImageFileFormat::jpeg, jpeg.selection()};
    if (!jpeg.cycle_profile(true, false, false, false) || frozen_request.jpeg_selection != frozen ||
        jpeg.hud_text() != "P: JPEG: Balanced   E: Encoder: Auto (jpegli)") return 1;
    FileFormatPolicy retained_format;
    clipss2::capture::CaptureDestinationPolicy retained_destination;
    const auto retained = jpeg.selection();
    for (int index = 0; index != 6; ++index)
    {
        if (!retained_format.toggle(false, false, false) ||
            !retained_destination.cycle(true) || jpeg.selection() != retained) return 1;
    }
    if (!std::holds_alternative<JpegCurrentDefault>(JpegSessionPolicy{}.selection())) return 1;
    clipss2::capture::FileCompression avif, jxl;
    for (const auto codec : {ImageFileFormat::avif, ImageFileFormat::jxl})
    {
        auto& value = codec == ImageFileFormat::avif ? avif : jxl;
        for (const auto token : {"lossy-high", "lossy-balanced", "lossy-small", "lossless"})
        {
            if (!cycle_codec_profile(codec,avif,jxl,false,false,false) ||
                std::string_view{clipss2::capture::file_compression_token(value)} != token) return 1;
            const auto locked = value;
            if (cycle_codec_profile(codec,avif,jxl,true,false,false) ||
                cycle_codec_profile(codec,avif,jxl,false,true,false) ||
                cycle_codec_profile(codec,avif,jxl,false,false,true) || value != locked ||
                jpeg.cycle_encoder(false,false,false,false)) return 1;
        }
        value = clipss2::capture::parse_file_compression("lossy-balanced");
    }
    const auto old_avif = avif, old_jxl = jxl;
    for (int index = 0; index != 5; ++index)
    {
        if (!retained_format.toggle(false,false,false) || avif != old_avif || jxl != old_jxl) return 1;
    }
    for (const auto codec : {ImageFileFormat::png,ImageFileFormat::webp})
        if (cycle_codec_profile(codec,avif,jxl,false,false,false) || avif != old_avif || jxl != old_jxl) return 1;
    std::cout << "Window selection/filter/input-arbitration/format/JPEG/AVIF/JXL profile tests: passed\n";
}

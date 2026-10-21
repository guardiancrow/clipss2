#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "image/cpu_image.h"
#include "platform/windows/capture_border_policy.h"
#include "platform/windows/capture_source.h"

namespace clipss2::platform::windows
{
class D3dDevice;

enum class CapturePixelFormat
{
    bgra8_unorm,
    rgba16_float,
};

struct WindowCaptureTarget
{
    std::uintptr_t hwnd_value{};
    std::wstring title;
};

struct CaptureOptions
{
    bool include_cursor{};
    CaptureBorderPolicy border_policy{CaptureBorderPolicy::system_required};
    bool diagnostic_output{true};
};

struct OneFrameCaptureResult
{
    std::int32_t content_width{};
    std::int32_t content_height{};
    std::uint32_t texture_width{};
    std::uint32_t texture_height{};
    std::string texture_format;
    image::CpuImage cpu_image;
    std::uint32_t mapped_row_pitch{};
    std::size_t logical_row_bytes{};
    std::chrono::microseconds frame_wait_duration{};
    std::chrono::microseconds gpu_readback_duration{};
    std::uint32_t gpu_busy_retry_count{};
};

[[nodiscard]] OneFrameCaptureResult capture_one_frame(
    const D3dDevice& device,
    const CaptureSource& source,
    CapturePixelFormat pixel_format,
    CaptureOptions options,
    std::chrono::milliseconds timeout = std::chrono::seconds{5});

[[nodiscard]] OneFrameCaptureResult capture_one_frame(
    const D3dDevice& device,
    const WindowCaptureTarget& target,
    CapturePixelFormat pixel_format,
    CaptureOptions options,
    std::chrono::milliseconds timeout = std::chrono::seconds{5});
}

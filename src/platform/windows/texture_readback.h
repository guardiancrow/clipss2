#pragma once

#include <cstddef>
#include <cstdint>
#include <chrono>

#include <d3d11.h>
#include <winrt/base.h>

#include "image/cpu_image.h"
#include "platform/windows/capture_source.h"

namespace clipss2::platform::windows
{
class D3dDevice;

inline constexpr auto gpu_readback_timeout = std::chrono::seconds{2};
inline constexpr auto gpu_readback_retry_delay = std::chrono::milliseconds{1};

struct TextureReadbackResult
{
    image::CpuImage image;
    std::uint32_t mapped_row_pitch{};
    std::size_t logical_row_bytes{};
    std::chrono::microseconds wait_duration{};
    std::uint32_t busy_retry_count{};
};

[[nodiscard]] TextureReadbackResult readback_texture(
    const D3dDevice& device,
    const winrt::com_ptr<ID3D11Texture2D>& source_texture,
    std::optional<PixelRectangle> crop = {});
}

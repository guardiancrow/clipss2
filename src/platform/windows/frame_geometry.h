#pragma once

#include "platform/windows/capture_source.h"
#include "platform/windows/error_diagnostics.h"
#include <cstddef>
#include <limits>
#include <optional>

namespace clipss2::platform::windows
{
inline std::size_t geometry_product(std::size_t a, std::size_t b)
{
    if (a && b > std::numeric_limits<std::size_t>::max() / a)
        throw DiagnosticError("reason=size-overflow; capture/output not permitted");
    return a * b;
}

// WGC content occupies the top-left of its pool surface. Surplus is undefined.
inline std::optional<PixelRectangle> window_frame_crop(std::int32_t cw, std::int32_t ch,
    std::uint32_t sw, std::uint32_t sh)
{
    return capture_stage({"capture.geometry", "Window ContentSize / surface bounds"}, [&]() -> std::optional<PixelRectangle> {
        const auto dimensions = [&] {
            return "; content=" + std::to_string(cw) + 'x' + std::to_string(ch) +
                "; texture=" + std::to_string(sw) + 'x' + std::to_string(sh);
        };
        if (cw <= 0 || ch <= 0 || sw == 0 || sh == 0)
            throw DiagnosticError("reason=invalid-content-size" + dimensions());
        // D3D11's maximum Texture2D dimension, independent of signed casts.
        if (sw > 16384 || sh > 16384 || cw > 16384 || ch > 16384)
            throw DiagnosticError("reason=unsupported-texture-size" + dimensions());
        if (static_cast<std::uint32_t>(cw) > sw || static_cast<std::uint32_t>(ch) > sh)
            throw DiagnosticError("reason=content-exceeds-surface; framepool-resize-required" + dimensions());
        if (static_cast<std::uint32_t>(cw) == sw && static_cast<std::uint32_t>(ch) == sh) return {};
        return PixelRectangle{0, 0, cw, ch};
    });
}

inline void require_stable_frame_pool(std::int32_t iw, std::int32_t ih,
    std::int32_t current_w, std::int32_t current_h, std::int32_t pool_w, std::int32_t pool_h,
    std::uint32_t surface_w, std::uint32_t surface_h)
{
    capture_stage({"capture.geometry", "Window frame pool stability"}, [&] {
        const auto dimensions = [&] {
            return "; initial=" + std::to_string(iw) + 'x' + std::to_string(ih) +
                "; current=" + std::to_string(current_w) + 'x' + std::to_string(current_h) +
                "; pool=" + std::to_string(pool_w) + 'x' + std::to_string(pool_h) +
                "; texture=" + std::to_string(surface_w) + 'x' + std::to_string(surface_h);
        };
        if (iw != current_w || ih != current_h)
            throw DiagnosticError("reason=framepool-resize-required; item-size-changed; retry capture" + dimensions());
        if (pool_w <= 0 || pool_h <= 0 || surface_w != static_cast<std::uint32_t>(pool_w) ||
            surface_h != static_cast<std::uint32_t>(pool_h))
            throw DiagnosticError("reason=unexpected-surface-size; capture/output not permitted" + dimensions());
    });
}

struct ReadbackLayout { std::size_t row_bytes, tight_bytes, mapped_bytes; };
inline ReadbackLayout readback_layout(std::size_t width, std::size_t height,
    std::size_t bytes_per_pixel, std::size_t row_pitch)
{
    return capture_stage({"capture.geometry", "Readback row pitch / buffer arithmetic"}, [&] {
        if (!width || !height || (bytes_per_pixel != 4 && bytes_per_pixel != 8))
            throw DiagnosticError("reason=invalid-readback-layout");
        const auto row = geometry_product(width, bytes_per_pixel);
        if (row_pitch < row) throw DiagnosticError("reason=invalid-row-pitch");
        return ReadbackLayout{row, geometry_product(row, height), geometry_product(row_pitch, height)};
    });
}
inline void require_readback_buffer(std::size_t actual, std::size_t required)
{
    capture_stage({"capture.geometry", "CPU buffer bounds"}, [&] {
        if (actual != required) throw DiagnosticError("reason=buffer-size-mismatch; actual-bytes=" +
            std::to_string(actual) + "; required-bytes=" + std::to_string(required));
    });
}
}

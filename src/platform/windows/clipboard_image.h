#pragma once

#include "image/cpu_image.h"

#include <cstddef>
#include <vector>

namespace clipss2::platform::windows
{
struct DibV5Payload
{
    std::vector<std::byte> bytes;
    std::size_t row_bytes{};
    std::size_t pixel_bytes{};
};

struct ClipboardPublishResult
{
    std::size_t dibv5_bytes{};
    std::size_t png_bytes{};
    bool dibv5_available{};
    bool png_available{};
};

struct PreparedClipboardImage
{
    DibV5Payload dibv5;
    std::vector<std::byte> png;
};

// Pure serialization seam used by tests. Rows stay top-down BGRA and alpha
// bytes are copied unchanged.
[[nodiscard]] DibV5Payload make_dibv5_payload(const image::CpuImage& image);

// The DIB and PNG references are separate because WGC BGRA8 remains
// premultiplied for CF_DIBV5 while PNG is explicitly made unassociated.
[[nodiscard]] PreparedClipboardImage prepare_image_for_clipboard(
    const image::CpuImage& dibv5_image,
    const image::CpuImage& png_reference,
    std::vector<std::byte> png);

[[nodiscard]] ClipboardPublishResult publish_prepared_image_to_clipboard(
    const PreparedClipboardImage& prepared);

// Treats input as audited WGC premultiplied BGRA8, prepares and validates both
// immediate-rendering payloads, then publishes CF_DIBV5 and unassociated PNG.
[[nodiscard]] ClipboardPublishResult publish_image_to_clipboard(
    const image::CpuImage& image);
}

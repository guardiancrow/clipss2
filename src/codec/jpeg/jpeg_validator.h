#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <optional>
#include <vector>

namespace clipss2::codec::jpeg
{
enum class JpegBackendId;
struct JpegMarkerInspection
{
    unsigned int code{};
    std::size_t payload_offset{}, payload_size{};
};
struct JfifInspection
{
    unsigned int major{}, minor{}, density_unit{}, x_density{}, y_density{}, thumbnail_width{}, thumbnail_height{};
};
// Immutable parser-created diagnostic representation. Borrows read-only bytes;
// caller must retain them unchanged during synchronous validation/decode.
class JpegInspection
{
public:
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::span<const JpegMarkerInspection> markers() const noexcept { return markers_; }
    [[nodiscard]] const std::optional<JfifInspection>& jfif() const noexcept { return jfif_; }
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] unsigned int frame_marker() const noexcept { return frame_marker_; }
    [[nodiscard]] bool turbo_components() const noexcept { return turbo_components_; }
    [[nodiscard]] const std::string& summary() const noexcept { return summary_; }
private:
    JpegInspection() = default;
    friend JpegInspection inspect_jpeg_structure(std::span<const std::byte>);
    std::span<const std::byte> bytes_;
    std::vector<JpegMarkerInspection> markers_;
    std::optional<JfifInspection> jfif_;
    std::uint32_t width_{}, height_{};
    unsigned int frame_marker_{};
    bool turbo_components_{true};
    std::string summary_;
};
[[nodiscard]] JpegInspection inspect_jpeg_structure(std::span<const std::byte> bytes);
void validate_jpeg_common_privacy(const JpegInspection& inspection);
void validate_jpeg_backend_technical(const JpegInspection& inspection, JpegBackendId selected_backend);
struct JpegValidationResult
{
    std::uint32_t width{};
    std::uint32_t height{};
    std::size_t encoded_bytes{};
    std::string marker_summary;
};

// Narrow allowlist for our baseline sequential, 8-bit, 3-component 4:4:4
// JFIF stream. Not a general-purpose JPEG parser or metadata stripper.
[[nodiscard]] JpegValidationResult validate_jpeg_markers(
    std::span<const std::byte> bytes);

// Also performs a strict decode, checks RGB structure and expected dimensions.
// It deliberately does not compute lossy pixel-quality metrics.
[[nodiscard]] JpegValidationResult validate_screenshot_jpeg(
    std::span<const std::byte> bytes,
    std::uint32_t expected_width, std::uint32_t expected_height);
[[nodiscard]] JpegValidationResult validate_managed_screenshot_jpeg(
    std::span<const std::byte> bytes, JpegBackendId backend,
    std::uint32_t expected_width, std::uint32_t expected_height);
}

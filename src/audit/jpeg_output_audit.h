#pragma once

#include <cstdint>
#include <filesystem>

#include "codec/jpeg/jpeg_codec.h"

namespace clipss2::audit
{
struct JpegFixtureMetrics
{
    double mean_absolute_error{};
    unsigned int maximum_error{};
    unsigned int patch_maximum_error{};
};

[[nodiscard]] image::CpuImage make_jpeg_sdr_fixture();
[[nodiscard]] JpegFixtureMetrics measure_jpeg_fixture(
    const codec::jpeg::OpaqueRgb8Image& reference,
    const codec::jpeg::OpaqueRgb8Image& decoded);
[[nodiscard]] bool jpeg_fixture_quality_passes(const JpegFixtureMetrics& metrics) noexcept;
int run_jpeg_output_audit(const std::filesystem::path& artifact_root);
}

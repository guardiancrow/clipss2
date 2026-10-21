#pragma once
#include "codec/jpeg/jpeg_backend_policy.h"
#include "codec/jpeg/jpeg_validator.h"

namespace clipss2::codec::jpeg::detail
{
[[nodiscard]] std::vector<std::byte> encode_jpegli_bytes(CanonicalJpegRgb8View source, float distance,
    std::size_t output_limit = maximum_jpeg_bytes, long pool_limit = 1024L * 1024L * 1024L);
// Internal raw codec operations, never publication APIs. Validation orchestrator
// is responsible for privacy/technical checks before decode and returning bytes.
std::vector<std::byte> encode_turbo_jpeg_bytes(CanonicalJpegRgb8View source, int quality);
OpaqueRgb8Image decode_validated_jpeg_bytes(const JpegInspection& inspection);
using ManagedJpegDispatch = std::vector<std::byte> (*)(CanonicalJpegRgb8View, const JpegEffectiveSettings&, void*);
// Synchronous internal test seam; context lifetime spans this call, never retained.
JpegBackendResult encode_managed_jpeg_with_dispatch(CanonicalJpegRgb8View source,
    const NormalizedJpegRequest& request, JpegBackendId override_backend,
    std::span<const JpegBackendCapabilities> catalog, ManagedJpegDispatch dispatch, void* context);
}

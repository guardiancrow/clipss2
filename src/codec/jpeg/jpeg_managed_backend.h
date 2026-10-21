#pragma once
#include "codec/jpeg/jpeg_backend_policy.h"

namespace clipss2::codec::jpeg
{
// Select before encode; one admitted concrete dispatch, then common validation.
// Returns owning validated bytes, never publishes. No retry after any failure.
[[nodiscard]] JpegBackendResult encode_managed_srgb_jpeg(CanonicalJpegRgb8View source,
    const NormalizedJpegRequest& request = current_product_jpeg_request(),
    JpegBackendId override_backend = JpegBackendId::automatic);
}

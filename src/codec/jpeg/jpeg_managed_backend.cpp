#include "codec/jpeg/jpeg_managed_backend.h"
#include "codec/jpeg/jpeg_codec_internal.h"
#include <stdexcept>
#include <type_traits>

namespace clipss2::codec::jpeg
{
namespace
{
std::vector<std::byte> dispatch_product_backend(CanonicalJpegRgb8View source,
    const JpegEffectiveSettings& settings, void*)
{
    switch (settings.backend)
    {
    case JpegBackendId::turbo:
        if (settings.subsampling != JpegSubsamplingIntent::yuv444 || settings.progressive || settings.optimized_entropy)
            throw std::invalid_argument("Unsupported managed Turbo effective settings.");
        return detail::encode_turbo_jpeg_bytes(source, std::get<TurboJpegParameters>(settings.parameters).quality);
    case JpegBackendId::jpegli:
        if (settings.subsampling != JpegSubsamplingIntent::yuv444 || settings.progressive || settings.optimized_entropy)
            throw std::invalid_argument("Unsupported managed jpegli effective settings.");
        return detail::encode_jpegli_bytes(source, std::get<JpegliParameters>(settings.parameters).distance);
    default: throw std::invalid_argument("Managed JPEG dispatch: backend is not product-admitted.");
    }
}
}
JpegBackendResult detail::encode_managed_jpeg_with_dispatch(CanonicalJpegRgb8View source,
    const NormalizedJpegRequest& request, JpegBackendId override_backend,
    std::span<const JpegBackendCapabilities> catalog, ManagedJpegDispatch dispatch, void* context)
{
    validate_canonical_jpeg_rgb8(source);
    const auto selection = select_jpeg_backend(override_backend, request, catalog);
    const JpegBackendCapabilities* capability{};
    for (const auto& entry : catalog) if (entry.id == selection.backend) capability = &entry;
    if (!capability || !dispatch) throw std::invalid_argument("Managed JPEG dispatch missing selected adapter.");
    auto effective = translate_jpeg_request(request, *capability);
    effective.diagnostic += std::visit([](const auto& raw) -> std::string {
        using Raw = std::decay_t<decltype(raw)>;
        if constexpr (std::is_same_v<Raw, TurboJpegParameters> || std::is_same_v<Raw, MozJpegParameters>)
            return " Effective quality: " + std::to_string(raw.quality);
        else if constexpr (std::is_same_v<Raw, JpegliParameters>) return " Effective distance: " + std::to_string(raw.distance);
        else return " Effective ImageQuality: " + std::to_string(raw.image_quality);
    }, effective.parameters);
    std::vector<std::byte> bytes;
    try { bytes = dispatch(source, effective, context); } // Exactly one invocation, no fallback loop.
    catch (const std::exception& error)
    { throw std::runtime_error(std::string{"Managed JPEG backend "} + jpeg_backend_name(selection.backend) + " encode stage: " + error.what()); }
    try { (void)validate_managed_screenshot_jpeg(bytes, selection.backend, source.width, source.height); }
    catch (const std::exception& error)
    { throw std::runtime_error(std::string{"Managed JPEG backend "} + jpeg_backend_name(selection.backend) + " validation stage: " + error.what()); }
    return {std::move(bytes), selection.backend, std::move(effective),
        "One managed encode; common privacy/structure, admitted technical profile and common decode passed."};
}
JpegBackendResult encode_managed_srgb_jpeg(CanonicalJpegRgb8View source,
    const NormalizedJpegRequest& request, JpegBackendId override_backend)
{
    return detail::encode_managed_jpeg_with_dispatch(source, request, override_backend,
        current_product_jpeg_backends(), dispatch_product_backend, nullptr);
}
}

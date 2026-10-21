#include "platform/windows/capture_border_policy.h"

namespace clipss2::platform::windows
{
const char* capture_border_policy_name(CaptureBorderPolicy policy) noexcept
{
    switch (policy)
    {
    case CaptureBorderPolicy::system_required: return "system-required";
    }
    return "unknown";
}

BorderlessDeploymentReadiness assess_borderless_readiness(
    const BorderlessPrerequisites& prerequisites) noexcept
{
    if (!prerequisites.border_property_available ||
        !prerequisites.access_type_available ||
        !prerequisites.request_method_available ||
        !prerequisites.borderless_kind_available ||
        !prerequisites.package_identity_present)
        return BorderlessDeploymentReadiness::unavailable;
    return BorderlessDeploymentReadiness::manifest_capability_and_consent_required;
}

const char* borderless_readiness_name(
    BorderlessDeploymentReadiness readiness) noexcept
{
    switch (readiness)
    {
    case BorderlessDeploymentReadiness::unavailable: return "unavailable";
    case BorderlessDeploymentReadiness::manifest_capability_and_consent_required:
        return "package capability and user consent still required";
    }
    return "unknown";
}
}

#pragma once

namespace clipss2::platform::windows
{
enum class CaptureBorderPolicy
{
    system_required,
};

[[nodiscard]] const char* capture_border_policy_name(
    CaptureBorderPolicy policy) noexcept;

struct BorderlessPrerequisites
{
    bool border_property_available{};
    bool access_type_available{};
    bool request_method_available{};
    bool borderless_kind_available{};
    bool package_identity_present{};
};

enum class BorderlessDeploymentReadiness
{
    unavailable,
    manifest_capability_and_consent_required,
};

[[nodiscard]] BorderlessDeploymentReadiness assess_borderless_readiness(
    const BorderlessPrerequisites& prerequisites) noexcept;
[[nodiscard]] const char* borderless_readiness_name(
    BorderlessDeploymentReadiness readiness) noexcept;
}

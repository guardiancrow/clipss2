#include "platform/windows/capture_border_policy.h"

#include <iostream>
#include <string_view>

int main()
{
    using namespace clipss2::platform::windows;
    const BorderlessPrerequisites all_apis{true, true, true, true, false};
    if (assess_borderless_readiness(all_apis) !=
        BorderlessDeploymentReadiness::unavailable)
        return 1;

    BorderlessPrerequisites missing_api{true, true, false, true, true};
    if (assess_borderless_readiness(missing_api) !=
        BorderlessDeploymentReadiness::unavailable)
        return 1;

    BorderlessPrerequisites future_ready{true, true, true, true, true};
    if (assess_borderless_readiness(future_ready) !=
            BorderlessDeploymentReadiness::manifest_capability_and_consent_required ||
        std::string_view{capture_border_policy_name(
            CaptureBorderPolicy::system_required)} != "system-required")
        return 1;

    std::cout << "Capture border deployment policy tests: passed\n";
}

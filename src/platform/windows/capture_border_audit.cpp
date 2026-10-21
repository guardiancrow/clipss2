#include "platform/windows/capture_border_audit.h"

#include "platform/windows/capture_border_policy.h"

#include <windows.h>
#include <appmodel.h>

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.Metadata.h>

namespace clipss2::platform::windows
{
namespace
{
using winrt::Windows::Foundation::Metadata::ApiInformation;

struct PackageIdentity
{
    bool present{};
    std::wstring full_name;
};

PackageIdentity current_package_identity()
{
    UINT32 length{};
    const LONG probe = GetCurrentPackageFullName(&length, nullptr);
    if (probe == APPMODEL_ERROR_NO_PACKAGE) return {};
    if (probe != ERROR_INSUFFICIENT_BUFFER)
        throw std::runtime_error(
            "GetCurrentPackageFullName size query failed with Win32 " +
            std::to_string(probe) + '.');

    std::vector<wchar_t> buffer(length);
    const LONG result = GetCurrentPackageFullName(&length, buffer.data());
    if (result != ERROR_SUCCESS)
        throw std::runtime_error(
            "GetCurrentPackageFullName failed with Win32 " +
            std::to_string(result) + '.');
    if (length == 0 || buffer.empty() || buffer.front() == L'\0')
        throw std::runtime_error("Package identity returned an empty full name.");
    return {true, std::wstring{buffer.data()}};
}

BorderlessPrerequisites query_prerequisites(bool package_identity_present)
{
    constexpr std::wstring_view session_type =
        L"Windows.Graphics.Capture.GraphicsCaptureSession";
    constexpr std::wstring_view access_type =
        L"Windows.Graphics.Capture.GraphicsCaptureAccess";
    constexpr std::wstring_view access_kind =
        L"Windows.Graphics.Capture.GraphicsCaptureAccessKind";
    return {
        ApiInformation::IsPropertyPresent(session_type, L"IsBorderRequired"),
        ApiInformation::IsTypePresent(access_type),
        ApiInformation::IsMethodPresent(access_type, L"RequestAccessAsync", 1),
        ApiInformation::IsEnumNamedValuePresent(access_kind, L"Borderless"),
        package_identity_present};
}
}

int run_capture_border_audit()
{
    const auto package = current_package_identity();
    const auto prerequisites = query_prerequisites(package.present);
    const auto readiness = assess_borderless_readiness(prerequisites);
    const bool border_api_present = prerequisites.border_property_available &&
        prerequisites.access_type_available &&
        prerequisites.request_method_available &&
        prerequisites.borderless_kind_available;

    std::cout << "Capture border audit\n"
              << "Border API present: " << std::boolalpha
              << border_api_present << '\n'
              << "OS/API IsBorderRequired property available: " << std::boolalpha
              << prerequisites.border_property_available << '\n'
              << "GraphicsCaptureAccess type available: "
              << prerequisites.access_type_available << '\n'
              << "GraphicsCaptureAccess.RequestAccessAsync available: "
              << prerequisites.request_method_available << '\n'
              << "GraphicsCaptureAccessKind.Borderless available: "
              << prerequisites.borderless_kind_available << '\n'
              << "Package identity: " << (package.present ? "present" : "absent")
              << '\n';
    if (package.present)
        std::wcout << L"Package full name: " << package.full_name << L'\n';
    std::cout << "Current deployment model: "
              << (package.present ? "packaged" : "unpackaged") << '\n'
              << "Borderless package capability context: "
              << borderless_readiness_name(readiness) << '\n'
              << "Borderless capability usable by current deployment: "
              << (readiness == BorderlessDeploymentReadiness::unavailable
                      ? "no" : "not yet; manifest capability and consent required")
              << '\n'
              << "Current product border policy: "
              << capture_border_policy_name(CaptureBorderPolicy::system_required)
              << '\n'
              << "WGC IsBorderRequired handling: property not touched\n"
              << "Borderless request: not attempted\n";
    if (!package.present)
        std::cout << "Reason: package identity/capability not configured\n";
    else
        std::cout << "Reason: Phase 10D audit is read-only; manifest capability and "
                     "user consent have not been established\n";
    return 0;
}
}

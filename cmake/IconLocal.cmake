# Copyright (c) 2026 guardiancrow. MIT.
# Developer validation only: never admits changed sources as historical shipping inputs.
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC"
   OR NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64"
   OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8
   OR NOT CMAKE_GENERATOR STREQUAL "Ninja" OR CMAKE_CONFIGURATION_TYPES
   OR NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|Release)$"
   OR NOT CLIPSS2_SHIPPING_PRIVACY STREQUAL "ON"
   OR NOT CLIPSS2_SHIPPING_PROFILE STREQUAL "v2"
   OR NOT CLIPSS2_PUBLICATION_SCOPE STREQUAL "icon-local")
    message(FATAL_ERROR "ICON-LOCAL:Configuration")
endif()
file(REAL_PATH "${CMAKE_BINARY_DIR}" icon_binary)
file(REAL_PATH "${CMAKE_SOURCE_DIR}/build/icon-phase2" icon_build_root)
cmake_path(IS_PREFIX icon_build_root "${icon_binary}" NORMALIZE icon_under_build)
if(NOT icon_under_build OR icon_binary STREQUAL icon_build_root)
    message(FATAL_ERROR "ICON-LOCAL:UseAnIndependentBuildRootUnderBuildIconPhase2")
endif()
if(NOT IS_DIRECTORY "${CLIPSS2_ICON_DEPENDENCY_ROOT}")
    message(FATAL_ERROR "ICON-LOCAL:SupplyOriginalValidatedDependencyCheckout")
endif()
file(REAL_PATH "${CLIPSS2_ICON_DEPENDENCY_ROOT}" icon_dependency_root)
file(TO_CMAKE_PATH "${icon_dependency_root}" icon_dependency_root)
foreach(pair IN ITEMS
    "CMAKE_TOOLCHAIN_FILE|build/sec-br1-r1/vcpkg/scripts/buildsystems/vcpkg.cmake"
    "VCPKG_INSTALLED_DIR|build/sec-br1-r1/installed"
    "VCPKG_MANIFEST_DIR|cmake/security/sec-br1"
    "CLIPSS2_JPEGLI_SOURCE_DIR|build/managed-jpegli-src"
    "CLIPSS2_DERIVED_JPEGLI_DIR|build/sec-br1-r1/managed-jpegli")
    string(REPLACE "|" ";" fields "${pair}")
    list(GET fields 0 variable)
    list(GET fields 1 relative)
    if(NOT "${${variable}}" STREQUAL "${icon_dependency_root}/${relative}")
        message(FATAL_ERROR "ICON-LOCAL:DependencyPath:${variable}")
    endif()
endforeach()
set(icon_expected_overlays "${icon_dependency_root}/vcpkg-overlays/security-sec-br1;${icon_dependency_root}/vcpkg-overlays/shipping-privacy-v2")
if(NOT VCPKG_OVERLAY_PORTS STREQUAL icon_expected_overlays
   OR NOT VCPKG_OVERLAY_TRIPLETS STREQUAL "${icon_dependency_root}/triplets"
   OR NOT VCPKG_TARGET_TRIPLET STREQUAL "x64-windows-shipping-privacy"
   OR NOT VCPKG_HOST_TRIPLET STREQUAL "x64-windows"
   OR NOT VCPKG_MANIFEST_INSTALL STREQUAL "OFF"
   OR NOT CMAKE_FIND_USE_PACKAGE_REGISTRY STREQUAL "OFF"
   OR NOT CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY STREQUAL "OFF")
    message(FATAL_ERROR "ICON-LOCAL:DependencyPolicy")
endif()
find_program(CLIPSS2_ICON_PWSH NAMES pwsh REQUIRED)
execute_process(COMMAND "${CLIPSS2_ICON_PWSH}" -NoProfile -File
    "${CMAKE_SOURCE_DIR}/tools/build_security_release_sec_br2.ps1"
    -Root "${icon_dependency_root}" -Mode ValidateDependencies
    RESULT_VARIABLE icon_dependencies
    OUTPUT_FILE "${CMAKE_BINARY_DIR}/icon-dependency-validation.json")
if(NOT icon_dependencies EQUAL 0)
    message(FATAL_ERROR "ICON-LOCAL:OriginalDependencyValidationFailed")
endif()
add_compile_definitions(WINRT_NO_SOURCE_LOCATION)
function(clipss2_validate_privacy_derivative original derived)
    execute_process(COMMAND "${CLIPSS2_ICON_PWSH}" -NoProfile -File
        "${CMAKE_SOURCE_DIR}/tools/build_security_release_sec_br2.ps1"
        -Root "${CLIPSS2_ICON_DEPENDENCY_ROOT}" -Mode VerifyManaged
        RESULT_VARIABLE icon_managed
        OUTPUT_FILE "${CMAKE_BINARY_DIR}/icon-managed-validation.json")
    if(NOT icon_managed EQUAL 0)
        message(FATAL_ERROR "ICON-LOCAL:OriginalManagedValidationFailed")
    endif()
endfunction()
message(STATUS "ICON-LOCAL: developer validation only; no shipping source/binary identity or release approval")

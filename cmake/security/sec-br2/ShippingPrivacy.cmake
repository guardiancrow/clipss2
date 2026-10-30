# Copyright (c) 2026 guardiancrow. MIT.
# BR1's input/installed identities remain immutable; BR2 changes product identity.
if(NOT CLIPSS2_SHIPPING_PRIVACY STREQUAL "ON"
   OR NOT CLIPSS2_SHIPPING_PROFILE STREQUAL "v2"
   OR NOT CLIPSS2_PUBLICATION_SCOPE STREQUAL "sec-br2"
   OR NOT PROJECT_VERSION STREQUAL "0.1.1"
   OR NOT CMAKE_GENERATOR STREQUAL "Ninja" OR CMAKE_CONFIGURATION_TYPES
   OR NOT CMAKE_BUILD_TYPE STREQUAL "Release"
   OR NOT MSVC OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC"
   OR NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64"
   OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "SEC-BR2:Configuration")
endif()
foreach(pair IN ITEMS
    "CMAKE_BINARY_DIR|build/sec-br2/product"
    "VCPKG_INSTALLED_DIR|build/sec-br1-r1/installed"
    "CLIPSS2_DERIVED_JPEGLI_DIR|build/sec-br1-r1/managed-jpegli"
    "CLIPSS2_JPEGLI_SOURCE_DIR|build/managed-jpegli-src"
    "VCPKG_MANIFEST_DIR|cmake/security/sec-br1"
    "VCPKG_OVERLAY_TRIPLETS|triplets"
    "CMAKE_TOOLCHAIN_FILE|build/sec-br1-r1/vcpkg/scripts/buildsystems/vcpkg.cmake")
    string(REPLACE "|" ";" fields "${pair}")
    list(GET fields 0 variable)
    list(GET fields 1 relative)
    if(NOT DEFINED ${variable} OR NOT "${${variable}}" STREQUAL "${CMAKE_SOURCE_DIR}/${relative}")
        message(FATAL_ERROR "SEC-BR2:OutputRoot:${variable}")
    endif()
endforeach()
set(expected_overlays "${CMAKE_SOURCE_DIR}/vcpkg-overlays/security-sec-br1;${CMAKE_SOURCE_DIR}/vcpkg-overlays/shipping-privacy-v2")
if(NOT VCPKG_OVERLAY_PORTS STREQUAL expected_overlays
   OR NOT VCPKG_TARGET_TRIPLET STREQUAL "x64-windows-shipping-privacy"
   OR NOT VCPKG_HOST_TRIPLET STREQUAL "x64-windows"
   OR NOT VCPKG_MANIFEST_INSTALL STREQUAL "OFF"
   OR NOT CMAKE_FIND_USE_PACKAGE_REGISTRY STREQUAL "OFF"
   OR NOT CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY STREQUAL "OFF")
    message(FATAL_ERROR "SEC-BR2:DependencyPolicy")
endif()
find_program(CLIPSS2_BR2_PWSH NAMES pwsh REQUIRED)
execute_process(COMMAND "${CLIPSS2_BR2_PWSH}" -NoProfile -File
    "${CMAKE_SOURCE_DIR}/tools/build_security_release_sec_br2.ps1"
    -Root "${CMAKE_SOURCE_DIR}" -Mode ValidateDependencies
    RESULT_VARIABLE admitted OUTPUT_QUIET)
if(NOT admitted EQUAL 0)
    message(FATAL_ERROR "SEC-BR2:Dependencies")
endif()
add_compile_definitions(WINRT_NO_SOURCE_LOCATION)
function(clipss2_validate_privacy_derivative original derived)
    execute_process(COMMAND "${CLIPSS2_BR2_PWSH}" -NoProfile -File
        "${CMAKE_SOURCE_DIR}/tools/build_security_release_sec_br2.ps1"
        -Root "${CMAKE_SOURCE_DIR}" -Mode VerifyManaged
        RESULT_VARIABLE checked OUTPUT_QUIET)
    if(NOT checked EQUAL 0)
        message(FATAL_ERROR "SEC-BR2:Managed")
    endif()
endfunction()

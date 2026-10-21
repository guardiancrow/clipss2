# Copyright (c) 2026 guardiancrow. MIT.
# Input ledger binds this policy. Its SHA is pinned only in independent reviewed
# PowerShell code, never in this bound file (avoids a cyclic hash contract).
function(clipss2_validate_s1_paths)
    foreach(pair IN ITEMS
        "CMAKE_BINARY_DIR|build/windows-x64-s1-v2"
        "VCPKG_INSTALLED_DIR|build/vcpkg_installed-s1-v2"
        "CLIPSS2_DERIVED_JPEGLI_DIR|build/managed-jpegli-s1-v2"
        "CLIPSS2_JPEGLI_SOURCE_DIR|build/managed-jpegli-src"
        "VCPKG_OVERLAY_PORTS|vcpkg-overlays/shipping-privacy-v2"
        "VCPKG_OVERLAY_TRIPLETS|triplets"
        "CMAKE_TOOLCHAIN_FILE|build/shipping-vcpkg-s1-v2/scripts/buildsystems/vcpkg.cmake")
        string(REPLACE "|" ";" fields "${pair}")
        list(GET fields 0 variable)
        list(GET fields 1 relative)
        # Reject aliases/relative spellings rather than silently normalizing them.
        set(expected "${CMAKE_SOURCE_DIR}/${relative}")
        if(NOT DEFINED ${variable} OR NOT "${${variable}}" STREQUAL "${expected}")
            message(FATAL_ERROR "S1:OutputRoot:${variable}")
        endif()
    endforeach()
    if(NOT VCPKG_TARGET_TRIPLET STREQUAL "x64-windows-shipping-privacy"
       OR NOT VCPKG_HOST_TRIPLET STREQUAL "x64-windows"
       OR NOT VCPKG_MANIFEST_INSTALL STREQUAL "OFF"
       OR NOT CMAKE_FIND_USE_PACKAGE_REGISTRY STREQUAL "OFF"
       OR NOT CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY STREQUAL "OFF")
        message(FATAL_ERROR "S1:DependencyPolicy")
    endif()
endfunction()
function(clipss2_validate_shipping_profile_s1)
    find_program(CLIPSS2_PLAN_PWSH NAMES pwsh REQUIRED)
    execute_process(COMMAND "${CLIPSS2_PLAN_PWSH}" -NoProfile -File
        "${CMAKE_SOURCE_DIR}/tools/build_shipping_dependencies_s1.ps1"
        -Root "${CMAKE_SOURCE_DIR}" -Mode ValidateInstalled -Profile v2 -Scope s1-v1
        -Privacy ON -Configuration Release -Architecture x64 -InstalledDir "${VCPKG_INSTALLED_DIR}"
        RESULT_VARIABLE admitted OUTPUT_QUIET ERROR_QUIET)
    if(NOT admitted EQUAL 0)
        message(FATAL_ERROR "S1:SourceInputsOrInstalledMarker")
    endif()
endfunction()

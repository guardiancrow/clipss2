# Copyright (c) 2026 guardiancrow. MIT.
set(maint_phase "3a")
set(maint_manifest "${CMAKE_CURRENT_LIST_DIR}")
if(CLIPSS2_PUBLICATION_SCOPE MATCHES "^sec-maint-v013-(3[bc])$")
    set(maint_phase "${CMAKE_MATCH_1}")
    string(APPEND maint_manifest "/${maint_phase}")
endif()
string(TOUPPER "SEC-MAINT-${maint_phase}" maint_label)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC"
   OR NOT CMAKE_CXX_COMPILER_VERSION STREQUAL "19.44.35230.0"
   OR NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64"
   OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8
   OR NOT CMAKE_GENERATOR STREQUAL "Ninja" OR CMAKE_CONFIGURATION_TYPES
   OR NOT CMAKE_VERSION VERSION_EQUAL 4.4.4
   OR NOT CMAKE_BUILD_TYPE STREQUAL "Release"
   OR NOT CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL "MultiThreadedDLL"
   OR NOT CLIPSS2_SHIPPING_PRIVACY STREQUAL "ON"
   OR NOT CLIPSS2_SHIPPING_PROFILE STREQUAL "v2"
   OR NOT PROJECT_VERSION STREQUAL "0.1.3")
    message(FATAL_ERROR "${maint_label}: unsupported build configuration")
endif()

string(REPLACE "\\" "/" sdk "$ENV{WindowsSDKVersion}")
string(REGEX REPLACE "/+$" "" sdk "${sdk}")
execute_process(COMMAND "${CMAKE_MAKE_PROGRAM}" --version
    OUTPUT_VARIABLE ninja_version OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE ninja_result)
if(NOT sdk STREQUAL "10.0.26100.0" OR NOT "$ENV{VCToolsVersion}" STREQUAL "14.44.35207"
   OR NOT ninja_result EQUAL 0 OR NOT ninja_version STREQUAL "1.13.2")
    message(FATAL_ERROR "${maint_label}: unapproved SDK, MSVC toolset or Ninja")
endif()

file(REAL_PATH "${CLIPSS2_MAINT_RUN_ROOT}" run)
file(REAL_PATH "${CMAKE_SOURCE_DIR}/build/sec-maint-v013/${maint_phase}" allowed)
cmake_path(IS_PREFIX allowed "${run}" NORMALIZE owned)
if(NOT owned OR run STREQUAL allowed
   OR NOT CMAKE_BINARY_DIR STREQUAL "${run}/build"
   OR NOT VCPKG_INSTALLED_DIR STREQUAL "${run}/installed"
   OR NOT CMAKE_TOOLCHAIN_FILE STREQUAL "${run}/vcpkg/scripts/buildsystems/vcpkg.cmake"
   OR NOT VCPKG_MANIFEST_DIR STREQUAL maint_manifest
   OR NOT VCPKG_OVERLAY_TRIPLETS STREQUAL "${CMAKE_SOURCE_DIR}/triplets"
   OR NOT VCPKG_TARGET_TRIPLET STREQUAL "x64-windows-shipping-privacy"
   OR NOT VCPKG_HOST_TRIPLET STREQUAL "x64-windows"
   OR NOT VCPKG_MANIFEST_INSTALL STREQUAL "OFF"
   OR NOT CMAKE_FIND_USE_PACKAGE_REGISTRY STREQUAL "OFF"
   OR NOT CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY STREQUAL "OFF")
    message(FATAL_ERROR "${maint_label}: unapproved dependency or output roots")
endif()

set(ledger "${maint_manifest}/source-inputs-v1.json")
set(marker_id "clipss2-sec-maint-${maint_phase}-installed-v1")
file(SHA256 "${ledger}" inputs_hash)
file(READ "${run}/installed/share/${marker_id}.json" marker)
set(receipt_id "clipss2-sec-maint-${maint_phase}-install-receipt-v1")
file(READ "${run}/installed/share/${receipt_id}.json" receipt)
file(SHA256 "${run}/installed/share/${receipt_id}.json" receipt_hash)
string(JSON id GET "${marker}" markerId)
string(JSON bound_inputs GET "${marker}" sourceInputsSha256)
string(JSON bound_receipt GET "${marker}" receiptSha256)
string(JSON installed_id GET "${receipt}" receiptId)
string(JSON installed_inputs GET "${receipt}" sourceInputsSha256)
if(NOT id STREQUAL marker_id
   OR NOT installed_id STREQUAL receipt_id OR NOT installed_inputs STREQUAL inputs_hash
   OR NOT bound_inputs STREQUAL inputs_hash OR NOT bound_receipt STREQUAL receipt_hash)
    message(FATAL_ERROR "${maint_label}: dependency receipt mismatch")
endif()

add_compile_definitions(WINRT_NO_SOURCE_LOCATION)

function(clipss2_validate_privacy_derivative original derived)
    # Both trees were checked with the existing Shipping Privacy validator
    # before configuration. Check their source and derivative bytes again.
    file(STRINGS "${CMAKE_SOURCE_DIR}/cmake/privacy/managed-sources-v2.tsv" rows ENCODING UTF-8)
    foreach(row IN LISTS rows)
        string(REGEX MATCH "^([^\t]+)\t([^\t]+)\t[^\t]+\t[^\t]+\t(.+)$" entry "${row}")
        if(NOT entry)
            message(FATAL_ERROR "${maint_label}: malformed managed source ledger")
        endif()
        set(path "${CMAKE_MATCH_3}")
        string(TOLOWER "${CMAKE_MATCH_1}" expected_original)
        string(TOLOWER "${CMAKE_MATCH_2}" expected_derived)
        file(SHA256 "${original}/${path}" original_hash)
        file(SHA256 "${derived}/${path}" derived_hash)
        if(NOT original_hash STREQUAL expected_original OR NOT derived_hash STREQUAL expected_derived)
            message(FATAL_ERROR "${maint_label}: managed source identity mismatch: ${path}")
        endif()
    endforeach()
endfunction()

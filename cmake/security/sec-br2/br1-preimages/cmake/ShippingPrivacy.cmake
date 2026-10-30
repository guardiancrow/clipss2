# Copyright (c) 2026 guardiancrow. MIT.
# No default profile, normal-OFF path or legacy dispatcher in S1.
if(CLIPSS2_PUBLICATION_SCOPE STREQUAL "sec-br1")
    include("${CMAKE_CURRENT_LIST_DIR}/security/sec-br1/ShippingPrivacy.cmake")
    return()
endif()
include("${CMAKE_CURRENT_LIST_DIR}/privacy/DependencyProfile-s1-v1.cmake")
function(clipss2_validate_s1_configuration)
    if(NOT DEFINED CLIPSS2_SHIPPING_PRIVACY OR NOT CLIPSS2_SHIPPING_PRIVACY STREQUAL "ON")
        message(FATAL_ERROR "S1:Privacy")
    endif()
    if(NOT DEFINED CLIPSS2_SHIPPING_PROFILE OR NOT CLIPSS2_SHIPPING_PROFILE STREQUAL "v2")
        message(FATAL_ERROR "S1:Profile")
    endif()
    if(NOT DEFINED CLIPSS2_PUBLICATION_SCOPE OR NOT CLIPSS2_PUBLICATION_SCOPE STREQUAL "s1-v1")
        message(FATAL_ERROR "S1:Scope")
    endif()
    # The supported public generator is Ninja single-config. Reject multi-config
    # entirely; CMAKE_BUILD_TYPE alone cannot constrain those generators.
    if(NOT CMAKE_GENERATOR STREQUAL "Ninja" OR CMAKE_CONFIGURATION_TYPES)
        message(FATAL_ERROR "S1:Generator")
    endif()
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        message(FATAL_ERROR "S1:Configuration")
    endif()
    if(NOT MSVC OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC"
       OR NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64" OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "S1:Architecture")
    endif()
    clipss2_validate_s1_paths()
endfunction()
if(NOT CMAKE_SCRIPT_MODE_FILE)
    clipss2_validate_s1_configuration()
    clipss2_validate_shipping_profile_s1()
    add_compile_definitions(WINRT_NO_SOURCE_LOCATION)
endif()
# Preserve the callback expected by unchanged ManagedJpegli.cmake.
function(clipss2_validate_privacy_derivative original derived)
    find_program(CLIPSS2_PWSH_EXECUTABLE NAMES pwsh REQUIRED)
    execute_process(COMMAND "${CLIPSS2_PWSH_EXECUTABLE}" -NoProfile -File
        "${CMAKE_SOURCE_DIR}/tools/derive_shipping_privacy_s1.ps1"
        -Root "${CMAKE_SOURCE_DIR}" -Original "${original}" -Destination "${derived}"
        -Mode VerifyOnly -Profile v2 -Scope s1-v1 -Privacy ON -Configuration Release -Architecture x64
        RESULT_VARIABLE checked OUTPUT_QUIET ERROR_QUIET)
    if(NOT checked EQUAL 0)
        message(FATAL_ERROR "S1:DerivativeValidation")
    endif()
endfunction()

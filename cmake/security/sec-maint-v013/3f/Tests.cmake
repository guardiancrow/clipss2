# Copyright (c) 2026 guardiancrow. MIT.
set(webp_app_local "${VCPKG_APPLOCAL_DEPS}")
set(VCPKG_APPLOCAL_DEPS OFF)
set(webp_reference "${CLIPSS2_MAINT_PREVIOUS_RUN}")
if(maint_phase MATCHES "^(3g-b|4a)$")
    set(webp_reference "${CLIPSS2_MAINT_WEBP_REFERENCE_RUN}")
endif()
set(webp_previous "${webp_reference}/installed/${VCPKG_TARGET_TRIPLET}")
foreach(variant IN ITEMS reference candidate)
    set(probe "clipss2_maint_webp_${variant}")
    add_executable(${probe} tests/sec_maint_3f_webp_probe.cpp)
    set_target_properties(${probe} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/webp-probes/${variant}")
    clipss2_configure_cpp_target(${probe})
    target_compile_definitions(${probe} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(${probe} PRIVATE psapi)
    if(variant STREQUAL "reference")
        set(dll_source "${webp_reference}/product")
        target_sources(${probe} PRIVATE src/codec/webp/webp_codec.cpp src/codec/webp/webp_validator.cpp)
        target_include_directories(${probe} BEFORE PRIVATE "${webp_previous}/include")
        target_include_directories(${probe} PRIVATE src)
        target_link_libraries(${probe} PRIVATE clipss2_image "${webp_previous}/lib/libwebp.lib")
    else()
        set(dll_source "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
        target_link_libraries(${probe} PRIVATE clipss2_webp_codec WebP::webp)
    endif()
    foreach(dll IN ITEMS libwebp libsharpyuv)
        add_custom_command(TARGET ${probe} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${dll_source}/${dll}.dll" "$<TARGET_FILE_DIR:${probe}>"
            VERBATIM)
    endforeach()
endforeach()
set(VCPKG_APPLOCAL_DEPS "${webp_app_local}")

file(READ "${CMAKE_CURRENT_LIST_DIR}/source-inputs-v1.json" webp_inputs)
string(JSON expected_writer GET "${webp_inputs}" libwebp patchedWriterSha256)
set(webp_writer "${CLIPSS2_MAINT_WEBP_SOURCE}/src/utils/bit_writer_utils.c")
file(SHA256 "${webp_writer}" actual_writer)
string(TOLOWER "${expected_writer}" expected_writer)
if(NOT actual_writer STREQUAL expected_writer)
    message(FATAL_ERROR "SEC-MAINT-3F: patched WebP probe input digest mismatch")
endif()
add_executable(clipss2_maint_bitwriter tests/sec_maint_3f_bitwriter.c "${webp_writer}"
    "${CLIPSS2_MAINT_WEBP_SOURCE}/src/utils/utils.c" "${CLIPSS2_MAINT_WEBP_SOURCE}/src/utils/palette.c")
target_include_directories(clipss2_maint_bitwriter PRIVATE "${CLIPSS2_MAINT_WEBP_SOURCE}")
add_test(NAME sec_maint_3f_bitwriter COMMAND clipss2_maint_bitwriter)

add_test(NAME sec_maint_3f_webp_cross_decode
    COMMAND "${CMAKE_COMMAND}"
        "-Dreference=$<TARGET_FILE:clipss2_maint_webp_reference>"
        "-Dcandidate=$<TARGET_FILE:clipss2_maint_webp_candidate>"
        "-Devidence=${CMAKE_BINARY_DIR}/webp-probes/evidence"
        -P "${CMAKE_CURRENT_LIST_DIR}/WebpCrossRead.cmake")

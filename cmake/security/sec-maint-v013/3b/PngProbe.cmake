# Copyright (c) 2026 guardiancrow. MIT.
set(_app_local "${VCPKG_APPLOCAL_DEPS}")
set(VCPKG_APPLOCAL_DEPS OFF)
set(_png_reference "${CLIPSS2_MAINT_PNG_REFERENCE_INSTALLED}/${VCPKG_TARGET_TRIPLET}")
set(_png_candidate "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")

add_library(clipss2_maint_png_reference_codec STATIC
    src/codec/png/png_codec.cpp src/codec/png/png_validator.cpp)
target_include_directories(clipss2_maint_png_reference_codec PUBLIC src "${_png_reference}/include")
target_link_libraries(clipss2_maint_png_reference_codec PUBLIC "${_png_reference}/lib/libpng16.lib")
clipss2_configure_cpp_target(clipss2_maint_png_reference_codec)

foreach(variant IN ITEMS reference candidate)
    set(probe "clipss2_maint_png_${variant}")
    add_executable(${probe} tests/sec_maint_3b_png_probe.cpp)
    target_compile_definitions(${probe} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    clipss2_configure_cpp_target(${probe})
    set_target_properties(${probe} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/png-probes/${variant}")
    if(variant STREQUAL "reference")
        set(dll_source "${CLIPSS2_MAINT_PNG_REFERENCE_PRODUCT}")
        target_compile_definitions(${probe} PRIVATE SEC_MAINT_PNG_VERSION=10658)
        target_link_libraries(${probe} PRIVATE clipss2_maint_png_reference_codec)
    else()
        set(dll_source "${_png_candidate}/bin")
        target_compile_definitions(${probe} PRIVATE SEC_MAINT_PNG_VERSION=10659)
        target_link_libraries(${probe} PRIVATE clipss2_png_codec)
    endif()
    foreach(dll IN ITEMS libpng16 z)
        add_custom_command(TARGET ${probe} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${dll_source}/${dll}.dll" "$<TARGET_FILE_DIR:${probe}>"
            VERBATIM)
    endforeach()
endforeach()
set(VCPKG_APPLOCAL_DEPS "${_app_local}")

foreach(fixture IN ITEMS odd large transparent detail flat)
    add_test(NAME "sec_maint_3b_png_${fixture}"
        COMMAND "${CMAKE_COMMAND}"
            "-Dreference=$<TARGET_FILE:clipss2_maint_png_reference>"
            "-Dcandidate=$<TARGET_FILE:clipss2_maint_png_candidate>"
            "-Dfixture=${fixture}"
            "-Devidence_root=${CMAKE_BINARY_DIR}/png-probes/evidence"
            -P "${CMAKE_CURRENT_LIST_DIR}/PngCrossRead.cmake")
endforeach()

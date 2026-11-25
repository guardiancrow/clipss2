# Copyright (c) 2026 guardiancrow. MIT.
set(_app_local "${VCPKG_APPLOCAL_DEPS}")
set(VCPKG_APPLOCAL_DEPS OFF)
set(jpeg_current "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
set(jpeg_previous "${CLIPSS2_MAINT_PREVIOUS_RUN}/installed/${VCPKG_TARGET_TRIPLET}")
foreach(variant IN ITEMS reference candidate)
    set(probe "clipss2_maint_turbo_${variant}")
    add_executable(${probe}
        tests/sec_maint_3d_turbo_probe.cpp
        src/audit/jpeg_output_audit.cpp
        src/codec/jpeg/jpeg_backend_policy.cpp
        src/codec/jpeg/jpeg_codec.cpp
        src/codec/jpeg/jpeg_flatten.cpp
        src/codec/jpeg/jpeg_validator.cpp)
    set_target_properties(${probe} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/turbo-probes/${variant}")
    clipss2_configure_cpp_target(${probe})
    target_include_directories(${probe} PRIVATE src)
    target_link_libraries(${probe} PRIVATE clipss2_png_codec version)
    set(dlls jpeg62 turbojpeg libpng16 libz)
    if(variant STREQUAL "reference")
        set(bin "${CLIPSS2_MAINT_PREVIOUS_RUN}/product")
        target_include_directories(${probe} BEFORE PRIVATE "${jpeg_previous}/include")
        target_link_libraries(${probe} PRIVATE "${jpeg_previous}/lib/turbojpeg.lib" "${jpeg_previous}/lib/jpeg.lib")
        target_compile_definitions(${probe} PRIVATE SEC_MAINT_TURBO_VERSION=3002000 CLIPSS2_JPEG_BACKEND_VERSION="3.2.0")
    else()
        set(bin "${jpeg_current}/bin")
        target_link_libraries(${probe} PRIVATE libjpeg-turbo::turbojpeg JPEG::JPEG)
        target_compile_definitions(${probe} PRIVATE SEC_MAINT_TURBO_VERSION=3002001 CLIPSS2_JPEG_BACKEND_VERSION="3.2.1")
        list(APPEND dlls spng)
    endif()
    target_compile_definitions(${probe} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    foreach(dll IN LISTS dlls)
        add_custom_command(TARGET ${probe} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${bin}/${dll}.dll" "$<TARGET_FILE_DIR:${probe}>"
            VERBATIM)
    endforeach()
endforeach()
set(VCPKG_APPLOCAL_DEPS "${_app_local}")

foreach(check IN ITEMS jpeg png-loader)
    add_test(NAME "sec_maint_3d_${check}"
        COMMAND "${CMAKE_COMMAND}"
            "-Dreference=$<TARGET_FILE:clipss2_maint_turbo_reference>"
            "-Dcandidate=$<TARGET_FILE:clipss2_maint_turbo_candidate>"
            "-Dcheck=${check}" "-Devidence_root=${CMAKE_BINARY_DIR}/turbo-probes/evidence"
            -P "${CMAKE_CURRENT_LIST_DIR}/TurboTests.cmake")
endforeach()

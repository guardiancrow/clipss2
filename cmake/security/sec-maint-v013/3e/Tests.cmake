# Copyright (c) 2026 guardiancrow. MIT.
set(avif_app_local "${VCPKG_APPLOCAL_DEPS}")
set(VCPKG_APPLOCAL_DEPS OFF)
set(avif_reference "${CLIPSS2_MAINT_PREVIOUS_RUN}")
if(CLIPSS2_PUBLICATION_SCOPE STREQUAL "sec-maint-v013-3f")
    set(avif_reference "${CLIPSS2_MAINT_AVIF_REFERENCE_RUN}")
endif()
foreach(variant IN ITEMS reference candidate)
    set(probe "clipss2_maint_avif_${variant}")
    add_executable(${probe} tests/sec_maint_3e_avif_probe.cpp)
    set_target_properties(${probe} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/avif-probes/${variant}")
    clipss2_configure_cpp_target(${probe})
    target_compile_definitions(${probe} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(${probe} PRIVATE psapi)
    if(variant STREQUAL "reference")
        set(previous "${avif_reference}/installed/${VCPKG_TARGET_TRIPLET}")
        set(dll_source "${avif_reference}/product")
        target_sources(${probe} PRIVATE src/codec/avif/avif_codec.cpp
            src/codec/avif/avif_validator.cpp src/codec/avif/avif_lossy_validator.cpp)
        target_include_directories(${probe} BEFORE PRIVATE "${previous}/include")
        target_include_directories(${probe} PRIVATE src)
        target_link_libraries(${probe} PRIVATE clipss2_image
            "${previous}/lib/avif.lib" "${previous}/lib/libyuv.lib" "${previous}/lib/aom.lib")
        target_compile_definitions(${probe} PRIVATE SEC_MAINT_AOM_PATCH=0 SEC_MAINT_LIBYUV_VERSION=1916)
    else()
        set(dll_source "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
        target_link_libraries(${probe} PRIVATE clipss2_avif_codec AOM::aom yuv)
        target_compile_definitions(${probe} PRIVATE SEC_MAINT_AOM_PATCH=3 SEC_MAINT_LIBYUV_VERSION=1943)
    endif()
    foreach(dll IN ITEMS avif aom libyuv jpeg62)
        add_custom_command(TARGET ${probe} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${dll_source}/${dll}.dll" "$<TARGET_FILE_DIR:${probe}>"
            VERBATIM)
    endforeach()
endforeach()
set(VCPKG_APPLOCAL_DEPS "${avif_app_local}")

add_test(NAME sec_maint_3e_yuv
    COMMAND "${CMAKE_COMMAND}"
        "-Dreference=$<TARGET_FILE:clipss2_maint_avif_reference>"
        "-Dcandidate=$<TARGET_FILE:clipss2_maint_avif_candidate>" -Dcheck=convert
        -P "${CMAKE_CURRENT_LIST_DIR}/AvifCrossRead.cmake")
add_test(NAME sec_maint_3e_avif_cross_decode
    COMMAND "${CMAKE_COMMAND}"
        "-Dreference=$<TARGET_FILE:clipss2_maint_avif_reference>"
        "-Dcandidate=$<TARGET_FILE:clipss2_maint_avif_candidate>"
        "-Devidence=${CMAKE_BINARY_DIR}/avif-probes/evidence" -Dcheck=avif
        -P "${CMAKE_CURRENT_LIST_DIR}/AvifCrossRead.cmake")

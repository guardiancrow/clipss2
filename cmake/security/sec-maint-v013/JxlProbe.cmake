# Copyright (c) 2026 guardiancrow. MIT.
# Compare independent links to the exact v0.1.2 and candidate DLLs.
set(_app_local "${VCPKG_APPLOCAL_DEPS}")
set(VCPKG_APPLOCAL_DEPS OFF)
set(_reference "${CLIPSS2_MAINT_REFERENCE_INSTALLED}/x64-windows-shipping-privacy")
set(_current "${VCPKG_INSTALLED_DIR}/x64-windows-shipping-privacy")

add_library(clipss2_maint_reference_codec STATIC
    src/codec/jxl/jxl_codec.cpp src/codec/jxl/jxl_validator.cpp
    src/codec/jxl/jxl_lossy_validator.cpp)
target_include_directories(clipss2_maint_reference_codec PUBLIC src PRIVATE "${_reference}/include")
target_link_libraries(clipss2_maint_reference_codec PUBLIC clipss2_image
    PRIVATE "${_reference}/lib/jxl.lib" "${_reference}/lib/jxl_threads.lib")
clipss2_configure_cpp_target(clipss2_maint_reference_codec)

foreach(_variant IN ITEMS reference candidate)
    set(_probe "clipss2_maint_jxl_${_variant}")
    add_executable(${_probe} tests/sec_maint_3a_jxl_probe.cpp)
    target_compile_definitions(${_probe} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    clipss2_configure_cpp_target(${_probe})
    set_target_properties(${_probe} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/jxl-probes/${_variant}")
    if(_variant STREQUAL "reference")
        set(_deps "${_reference}")
        set(_dll_source "${CLIPSS2_MAINT_REFERENCE_PRODUCT}")
        target_compile_definitions(${_probe} PRIVATE SEC_MAINT_JXL_VERSION=12000)
        target_link_libraries(${_probe} PRIVATE clipss2_maint_reference_codec)
    else()
        set(_deps "${_current}")
        set(_dll_source "${_current}/bin")
        target_compile_definitions(${_probe} PRIVATE SEC_MAINT_JXL_VERSION=13000)
        target_link_libraries(${_probe} PRIVATE clipss2_jxl_codec)
    endif()
    target_include_directories(${_probe} PRIVATE "${_deps}/include")
    target_link_libraries(${_probe} PRIVATE psapi "${_deps}/lib/jxl.lib" "${_deps}/lib/jxl_threads.lib")
    foreach(_dll IN ITEMS jxl jxl_cms jxl_threads hwy brotlidec brotlienc brotlicommon lcms2-2)
        add_custom_command(TARGET ${_probe} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_dll_source}/${_dll}.dll" "$<TARGET_FILE_DIR:${_probe}>"
            VERBATIM)
    endforeach()
endforeach()
set(VCPKG_APPLOCAL_DEPS "${_app_local}")

foreach(_fixture IN ITEMS odd large texture)
    add_test(NAME "sec_maint_3a_jxl_${_fixture}"
        COMMAND clipss2_maint_jxl_candidate test "${_fixture}")
endforeach()

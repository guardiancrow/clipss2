# Copyright (c) 2026 guardiancrow. MIT.
set(_app_local "${VCPKG_APPLOCAL_DEPS}")
set(VCPKG_APPLOCAL_DEPS OFF)
set(_png_candidate "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
set(variants reference candidate)
if(CLIPSS2_PUBLICATION_SCOPE STREQUAL "sec-maint-v013-3c")
    list(APPEND variants reference_3b)
endif()
foreach(variant IN LISTS variants)
    set(probe "clipss2_maint_png_${variant}")
    add_executable(${probe} tests/sec_maint_3b_png_probe.cpp)
    target_compile_definitions(${probe} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    clipss2_configure_cpp_target(${probe})
    set_target_properties(${probe} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/png-probes/${variant}")
    set(png_version 10659)
    set(z_dll z)
    set(z_version 1.3.2)
    if(variant STREQUAL "candidate")
        set(dll_source "${_png_candidate}/bin")
        target_include_directories(${probe} PRIVATE "${_png_candidate}/include")
        if(CLIPSS2_PUBLICATION_SCOPE STREQUAL "sec-maint-v013-3c")
            set(z_dll libz)
            set(z_version 1.4)
        endif()
        target_link_libraries(${probe} PRIVATE clipss2_png_codec)
    else()
        if(variant STREQUAL "reference")
            set(dll_source "${CLIPSS2_MAINT_PNG_REFERENCE_PRODUCT}")
            set(png_reference "${CLIPSS2_MAINT_PNG_REFERENCE_INSTALLED}/${VCPKG_TARGET_TRIPLET}")
            set(png_version 10658)
        else()
            set(dll_source "${CLIPSS2_MAINT_PNG_PREVIOUS_PRODUCT}")
            set(png_reference "${CLIPSS2_MAINT_PNG_PREVIOUS_INSTALLED}/${VCPKG_TARGET_TRIPLET}")
        endif()
        set(codec "${probe}_codec")
        add_library(${codec} STATIC src/codec/png/png_codec.cpp src/codec/png/png_validator.cpp)
        target_include_directories(${codec} PUBLIC src "${png_reference}/include")
        target_link_libraries(${codec} PUBLIC "${png_reference}/lib/libpng16.lib")
        clipss2_configure_cpp_target(${codec})
        target_link_libraries(${probe} PRIVATE ${codec})
    endif()
    target_compile_definitions(${probe} PRIVATE SEC_MAINT_PNG_VERSION=${png_version}
        SEC_MAINT_ZLIB_VERSION="${z_version}" SEC_MAINT_ZLIB_DLL=L"${z_dll}.dll")
    foreach(dll IN ITEMS libpng16 ${z_dll})
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
    if(CLIPSS2_PUBLICATION_SCOPE STREQUAL "sec-maint-v013-3c")
        add_test(NAME "sec_maint_3c_png_${fixture}"
            COMMAND "${CMAKE_COMMAND}"
                "-Dreference=$<TARGET_FILE:clipss2_maint_png_reference_3b>"
                "-Dcandidate=$<TARGET_FILE:clipss2_maint_png_candidate>"
                "-Dfixture=${fixture}" "-Dsplit_idat=ON"
                "-Devidence_root=${CMAKE_BINARY_DIR}/png-probes/evidence-3c"
                -P "${CMAKE_CURRENT_LIST_DIR}/PngCrossRead.cmake")
    endif()
endforeach()

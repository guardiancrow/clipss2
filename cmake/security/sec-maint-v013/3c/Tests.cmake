# Copyright (c) 2026 guardiancrow. MIT.
add_executable(clipss2_maint_zlib_test tests/sec_maint_3c_zlib_test.cpp)
target_compile_definitions(clipss2_maint_zlib_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(clipss2_maint_zlib_test PRIVATE ZLIB::ZLIB)
clipss2_configure_cpp_target(clipss2_maint_zlib_test)
add_test(NAME sec_maint_3c_zlib_stream COMMAND clipss2_maint_zlib_test)

find_program(maint_pwsh pwsh.exe REQUIRED)
set(packaging_args)
if(CLIPSS2_PUBLICATION_SCOPE STREQUAL "sec-maint-v013-3d")
    list(APPEND packaging_args -Phase 3d -LegacyRun "${CLIPSS2_MAINT_PNG_PREVIOUS_RUN}")
elseif(clipss2_maint_codec_scope STREQUAL "sec-maint-v013-3f")
    list(APPEND packaging_args -Phase "${maint_phase}" -LegacyRun "${CLIPSS2_MAINT_PNG_PREVIOUS_RUN}"
        -TurboReferenceRun "${CLIPSS2_MAINT_TURBO_REFERENCE_RUN}" -AvifReferenceRun "${CLIPSS2_MAINT_AVIF_REFERENCE_RUN}")
    if(maint_phase STREQUAL "3g-b")
        list(APPEND packaging_args -WebpReferenceRun "${CLIPSS2_MAINT_WEBP_REFERENCE_RUN}")
    endif()
elseif(CLIPSS2_PUBLICATION_SCOPE STREQUAL "sec-maint-v013-3e")
    list(APPEND packaging_args -Phase 3e -LegacyRun "${CLIPSS2_MAINT_PNG_PREVIOUS_RUN}"
        -TurboReferenceRun "${CLIPSS2_MAINT_TURBO_REFERENCE_RUN}")
endif()
add_test(NAME sec_maint_3c_packaging
    COMMAND "${maint_pwsh}" -NoProfile -File "${CMAKE_SOURCE_DIR}/tools/test_sec_maint_3c_package.ps1"
        -Mode SelfTest -RunRoot "${CLIPSS2_MAINT_RUN_ROOT}"
        -ProductDirectory "$<TARGET_FILE_DIR:clipss2>"
        -PreviousRun "${CLIPSS2_MAINT_PREVIOUS_RUN}" ${packaging_args})

# Copyright (c) 2026 guardiancrow. MIT.
add_executable(clipss2_maint_zlib_test tests/sec_maint_3c_zlib_test.cpp)
target_compile_definitions(clipss2_maint_zlib_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(clipss2_maint_zlib_test PRIVATE ZLIB::ZLIB)
clipss2_configure_cpp_target(clipss2_maint_zlib_test)
add_test(NAME sec_maint_3c_zlib_stream COMMAND clipss2_maint_zlib_test)

find_program(maint_pwsh pwsh.exe REQUIRED)
add_test(NAME sec_maint_3c_packaging
    COMMAND "${maint_pwsh}" -NoProfile -File "${CMAKE_SOURCE_DIR}/tools/test_sec_maint_3c_package.ps1"
        -Mode SelfTest -RunRoot "${CLIPSS2_MAINT_RUN_ROOT}"
        -ProductDirectory "$<TARGET_FILE_DIR:clipss2>"
        -PreviousRun "${CLIPSS2_MAINT_PREVIOUS_RUN}")

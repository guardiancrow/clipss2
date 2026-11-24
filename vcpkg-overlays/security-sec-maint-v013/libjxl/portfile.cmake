# Based on the Microsoft vcpkg libjxl port at 8235a7aece54307d71f711944d6339dd6dccd50c (MIT).
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO libjxl/libjxl
    REF v0.13.0
    SHA512 f5a6cc4c875c9cdd217001096d34e5912afffdf3a58bae816c1c28c7e241eb0909ea37af0bef6d4b2fb8fae20ae94e29b2ef30fb65f778d4a7d62e07e20e3f85
    PATCHES
        fix-dependencies.patch
        modified-file-notice.patch
        diagnostic-file-id.patch
)

# CMake's Windows tar extraction skips symlinks. Restore the exact pinned
# archive entries before allowing even a core-only build.
set(metrics_dir "${SOURCE_PATH}/tools/benchmark/metrics")
if(NOT EXISTS "${metrics_dir}/iqa_wrapper.sh")
    message(FATAL_ERROR "SEC-MAINT-3A: missing libjxl benchmark link target")
endif()
foreach(metric IN ITEMS dists-rgb fsim-rgb fsim-y gmsd-rgb lpips-rgb msssim-rgb
        msssim-y nlpd-y ssim-rgb ssim-y vif-rgb vsi-rgb)
    set(link "${metrics_dir}/${metric}.sh")
    if(NOT IS_SYMLINK "${link}" AND NOT EXISTS "${link}")
        execute_process(COMMAND cmd.exe /d /c mklink "${metric}.sh" iqa_wrapper.sh
            WORKING_DIRECTORY "${metrics_dir}" RESULT_VARIABLE made
            OUTPUT_QUIET ERROR_QUIET)
        if(NOT made EQUAL 0)
            message(FATAL_ERROR "SEC-MAINT-3A: Windows cannot restore the archived symlink ${metric}.sh")
        endif()
    endif()
    if(NOT IS_SYMLINK "${link}")
        message(FATAL_ERROR "SEC-MAINT-3A: archive entry is not a symlink: ${metric}.sh")
    endif()
    file(READ_SYMLINK "${link}" target)
    if(NOT target STREQUAL "iqa_wrapper.sh" OR NOT EXISTS "${link}")
        message(FATAL_ERROR "SEC-MAINT-3A: unexpected archived symlink target: ${metric}.sh")
    endif()
endforeach()

vcpkg_find_acquire_program(PYTHON3)
vcpkg_execute_required_process(
    COMMAND "${PYTHON3}" -I -B "${CMAKE_CURRENT_LIST_DIR}/verify-source.py"
        "${DOWNLOADS}/libjxl-libjxl-v0.13.0.tar.gz" "${SOURCE_PATH}"
    WORKING_DIRECTORY "${CURRENT_BUILDTREES_DIR}"
    LOGNAME verify-source
)

foreach(file IN ITEMS lib/jxl/base/status.h lib/jxl/decode.cc lib/jxl/encode.cc)
    file(STRINGS "${SOURCE_PATH}/${file}" absolute_diagnostics REGEX "__FILE__")
    file(STRINGS "${SOURCE_PATH}/${file}" local_diagnostics REGEX "\"libjxl\"")
    if(absolute_diagnostics OR NOT local_diagnostics)
        message(FATAL_ERROR "SEC-MAINT-3A: unapproved libjxl diagnostic source: ${file}")
    endif()
endforeach()

if(NOT VCPKG_TARGET_IS_WINDOWS OR NOT VCPKG_TARGET_ARCHITECTURE STREQUAL "x64"
   OR NOT VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic"
   OR NOT VCPKG_CRT_LINKAGE STREQUAL "dynamic")
    message(FATAL_ERROR "SEC-MAINT-3A: unsupported libjxl configuration")
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DJPEGXL_FORCE_SYSTEM_HWY=ON
        -DJPEGXL_FORCE_SYSTEM_BROTLI=ON
        -DJPEGXL_FORCE_SYSTEM_LCMS2=ON
        -DJPEGXL_ENABLE_BOXES=ON
        -DJPEGXL_ENABLE_TOOLS=OFF
        -DJPEGXL_ENABLE_BENCHMARK=OFF
        -DJPEGXL_ENABLE_DOXYGEN=OFF
        -DJPEGXL_ENABLE_EXAMPLES=OFF
        -DJPEGXL_ENABLE_FUZZERS=OFF
        -DJPEGXL_ENABLE_JNI=OFF
        -DJPEGXL_ENABLE_MANPAGES=OFF
        -DJPEGXL_ENABLE_OPENEXR=OFF
        -DJPEGXL_ENABLE_PLUGINS=OFF
        -DJPEGXL_ENABLE_SJPEG=OFF
        -DJPEGXL_ENABLE_SKCMS=OFF
        -DJPEGXL_ENABLE_TCMALLOC=OFF
        -DJPEGXL_BUNDLE_LIBPNG=OFF
        -DBUILD_TESTING=OFF
        -DCMAKE_FIND_PACKAGE_TARGETS_GLOBAL=ON
)
vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
file(INSTALL "${SOURCE_PATH}/PATENTS" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")

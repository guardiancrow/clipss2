# The baseline helper trusts an existing filename. Require the verified archive.
set(source_ref ef08f21f6d94addd69e5ae521d0f75f14cb644ed)
set(source_archive "${DOWNLOADS}/${PORT}-${source_ref}.tar.gz")
if(VCPKG_USE_HEAD_VERSION OR NOT EXISTS "${source_archive}")
    message(FATAL_ERROR "SEC-MAINT-3E requires the verified libyuv Git archive")
endif()
file(SHA512 "${source_archive}" source_sha512)
if(NOT source_sha512 STREQUAL "63f0a4c700ee02a7a33ac9e8918ef08d0c86bda92ed7f9403e7e63eef287028e24165793200e93c85fafe3866d5070c4c4b96a2d83810e40d717fde2fe068dd8")
    message(FATAL_ERROR "SEC-MAINT-3E libyuv archive SHA-512 mismatch")
endif()

vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://chromium.googlesource.com/libyuv/libyuv
    REF "${source_ref}"
    PATCHES
        cmake.diff
        libyuv-msvc-826ab02.diff
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        tools BUILD_TOOLS
)

# Query first: get-vars reconfigures and clears the same build directories.
vcpkg_cmake_get_vars(cmake_vars_file)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        -DUNIT_TEST=OFF
    OPTIONS_DEBUG
        -DBUILD_TOOLS=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup()
if("tools" IN_LIST FEATURES)
    vcpkg_copy_tools(TOOL_NAMES yuvconvert yuvconstants AUTO_CLEAN)
endif()

if(VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/libyuv/basic_types.h" "defined(LIBYUV_USING_SHARED_LIBRARY)" "1")
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

file(COPY "${CMAKE_CURRENT_LIST_DIR}/libyuv-config.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")

include("${cmake_vars_file}")
if(VCPKG_DETECTED_CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    file(APPEND "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage" [[

Attention:
You are using MSVC to compile libyuv. This build won't compile any
of the acceleration codes, which results in a very slow library.
See workarounds: https://github.com/microsoft/vcpkg/issues/28446
]])
endif()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")

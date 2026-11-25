# The baseline helper trusts an existing filename. Require the verified archive.
set(source_ref b141d9c45db236335351c8a4b2a14b3ade27c937)
set(source_archive "${DOWNLOADS}/${PORT}-${source_ref}.tar.gz")
if(VCPKG_USE_HEAD_VERSION OR NOT EXISTS "${source_archive}")
    message(FATAL_ERROR "SEC-MAINT-3E requires the verified AOM Git archive")
endif()
file(SHA512 "${source_archive}" source_sha512)
if(NOT source_sha512 STREQUAL "68b090de2d8b049ccd8f91e9801241cef5c8047a65383d7ed6bfe578953a411a377369e4fc86c3457ed700cde82aecf48e091aafcda5a3cb808138ad56b7bc02")
    message(FATAL_ERROR "SEC-MAINT-3E AOM archive SHA-512 mismatch")
endif()

vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL "https://aomedia.googlesource.com/aom"
    REF "${source_ref}"
    FETCH_REF "v${VERSION}"
    PATCHES
        aom-rename-static.diff
)

vcpkg_find_acquire_program(PERL)

if(VCPKG_TARGET_ARCHITECTURE MATCHES "^(x86|x64)$")
    # Upstream AOM only uses NASM on x86-family targets. Non-x86 targets such as
    # Apple Silicon configure with ENABLE_NASM=OFF and should not require nasm.
    vcpkg_find_acquire_program(NASM)
    set(aom_nasm_compiler "-DCMAKE_ASM_NASM_COMPILER=${NASM}")
endif()

set(aom_target_cpu "")
if(VCPKG_TARGET_IS_UWP OR (VCPKG_TARGET_IS_WINDOWS AND VCPKG_TARGET_ARCHITECTURE MATCHES "^arm"))
    # UWP + aom's assembler files result in weirdness and build failures
    # Also, disable assembly on ARM and ARM64 Windows to fix compilation issues.
    set(aom_target_cpu "-DAOM_TARGET_CPU=generic")
endif()

if(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm" AND VCPKG_TARGET_IS_LINUX)
    set(aom_target_cpu "-DENABLE_NEON=OFF")
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${aom_target_cpu}
        -DENABLE_APPS=OFF
        -DENABLE_DOCS=OFF
        -DENABLE_EXAMPLES=OFF
        -DENABLE_TESTDATA=OFF
        -DENABLE_TESTS=OFF
        -DENABLE_TOOLS=OFF
        -DTHREADS_PREFER_PTHREAD_FLAG=ON
        ${aom_nasm_compiler}
        "-DPERL_EXECUTABLE=${PERL}"
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/AOM)
vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

vcpkg_install_copyright(FILE_LIST
    "${SOURCE_PATH}/LICENSE"
    "${SOURCE_PATH}/PATENTS"
    "${SOURCE_PATH}/third_party/fastfeat/LICENSE"
    "${SOURCE_PATH}/third_party/vector/LICENSE"
    "${SOURCE_PATH}/third_party/x86inc/LICENSE"
)

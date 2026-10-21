# Upstream libjxl 0.12 exports pkg-config modules, not CMake config targets.
find_program(PKG_CONFIG_EXECUTABLE NAMES pkgconf pkg-config REQUIRED
    PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/pkgconf"
    NO_DEFAULT_PATH)
find_package(PkgConfig REQUIRED)
set(_jxl_saved_pkg_config_path "$ENV{PKG_CONFIG_PATH}")
set(_jxl_saved_pkg_config_libdir "$ENV{PKG_CONFIG_LIBDIR}")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_jxl_pkgconfig "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib/pkgconfig")
else()
    set(_jxl_pkgconfig "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/pkgconfig")
endif()
set(ENV{PKG_CONFIG_PATH} "${_jxl_pkgconfig}")
set(ENV{PKG_CONFIG_LIBDIR} "${_jxl_pkgconfig}")
pkg_check_modules(LIBJXL REQUIRED IMPORTED_TARGET libjxl=0.12.0)
pkg_check_modules(LIBJXL_THREADS REQUIRED IMPORTED_TARGET libjxl_threads=0.12.0)
set(ENV{PKG_CONFIG_PATH} "${_jxl_saved_pkg_config_path}")
set(ENV{PKG_CONFIG_LIBDIR} "${_jxl_saved_pkg_config_libdir}")

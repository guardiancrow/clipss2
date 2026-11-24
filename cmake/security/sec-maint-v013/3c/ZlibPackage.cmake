# Copyright (c) 2026 guardiancrow. MIT.
find_package(ZLIB 1.4 EXACT CONFIG REQUIRED COMPONENTS shared)
file(REAL_PATH "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}" zlib_root)
get_target_property(zlib_import ZLIB::ZLIB IMPORTED_IMPLIB_RELEASE)
get_target_property(zlib_library ZLIB::ZLIB IMPORTED_LOCATION_RELEASE)
get_target_property(zlib_includes ZLIB::ZLIB INTERFACE_INCLUDE_DIRECTORIES)
file(REAL_PATH "${zlib_import}" zlib_import)
file(REAL_PATH "${zlib_library}" zlib_library)
if(NOT zlib_import STREQUAL "${zlib_root}/lib/libz.lib"
   OR NOT zlib_library STREQUAL "${zlib_root}/bin/libz.dll"
   OR NOT "${zlib_root}/include" IN_LIST zlib_includes
   OR TARGET ZLIB::ZLIBSTATIC)
    message(FATAL_ERROR "SEC-MAINT-3C: ZLIB::ZLIB does not bind the candidate shared package")
endif()

# Copyright (c) 2026 guardiancrow. MIT.
# Fixed x64-windows linkage, separate ABI/cache identity. Keep vcpkg's normal VS selection.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_PROVIDED_FORTRAN ON)
set(VCPKG_LINKER_FLAGS_RELEASE "/PDBALTPATH:%_PDB%")

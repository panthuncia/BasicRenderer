# vcpkg custom triplet for cross-compiling Windows x64 binaries with clang-cl on Linux.
#
# Usage: set VCPKG_OVERLAY_TRIPLETS=<repo>/cmake/vcpkg-triplets
#        and use --triplet x64-windows-clang-cl

set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_CMAKE_SYSTEM_NAME Windows)

# Chain-load the cross-compilation toolchain so that vcpkg's internal builds
# use clang-cl + lld-link and can find the Windows SDK via xwin.
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../linux-to-windows-clang-cl.cmake")

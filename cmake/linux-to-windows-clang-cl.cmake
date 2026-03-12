# Cross-compilation toolchain: Linux host → Windows x64 target via clang-cl + lld-link
#
# Requires:
#   - LLVM/Clang installed (clang-cl, lld-link, llvm-lib, llvm-rc)
#   - Windows SDK + MSVC CRT unpacked by xwin into XWIN_DIR (default: ~/.xwin)
#   - Wine installed (used as CMAKE_CROSSCOMPILING_EMULATOR for build-time tools)
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/linux-to-windows-clang-cl.cmake ...
#   (or via CMakePresets.json / VCPKG_CHAINLOAD_TOOLCHAIN_FILE)

set(CMAKE_SYSTEM_NAME    Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

# --- Compilers ---
set(CMAKE_C_COMPILER   clang-cl   CACHE STRING "")
set(CMAKE_CXX_COMPILER clang-cl   CACHE STRING "")
set(CMAKE_LINKER        lld-link  CACHE STRING "")
set(CMAKE_AR            llvm-lib  CACHE STRING "")
set(CMAKE_RC_COMPILER   llvm-rc   CACHE STRING "")

# clang-cl needs to know the MSVC target triple
set(_clang_target "--target=x86_64-pc-windows-msvc")
set(CMAKE_C_FLAGS_INIT   "${_clang_target}" CACHE STRING "")
set(CMAKE_CXX_FLAGS_INIT "${_clang_target}" CACHE STRING "")

# Use lld-link as the linker driver
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-fuse-ld=lld-link" CACHE STRING "")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld-link" CACHE STRING "")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-fuse-ld=lld-link" CACHE STRING "")

# --- xwin paths (Windows SDK + MSVC CRT) ---
# Override XWIN_DIR at configure time if your xwin output lives elsewhere.
if(NOT DEFINED XWIN_DIR)
    set(XWIN_DIR "$ENV{HOME}/.xwin" CACHE PATH "Root of the xwin splat directory")
endif()

set(XWIN_CRT_INCLUDE  "${XWIN_DIR}/crt/include")
set(XWIN_SDK_INCLUDE  "${XWIN_DIR}/sdk/include")
set(XWIN_CRT_LIB      "${XWIN_DIR}/crt/lib/x86_64")
set(XWIN_SDK_LIB      "${XWIN_DIR}/sdk/lib/um/x86_64")
set(XWIN_UCRT_LIB     "${XWIN_DIR}/sdk/lib/ucrt/x86_64")

# Tell clang-cl where to find MSVC CRT and Windows SDK headers.
# /imsvc is the clang-cl flag for "system include directory in MSVC mode".
string(CONCAT _xwin_includes
    " /imsvc${XWIN_CRT_INCLUDE}"
    " /imsvc${XWIN_SDK_INCLUDE}/ucrt"
    " /imsvc${XWIN_SDK_INCLUDE}/um"
    " /imsvc${XWIN_SDK_INCLUDE}/shared"
)
set(CMAKE_C_FLAGS_INIT   "${CMAKE_C_FLAGS_INIT}${_xwin_includes}"   CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT}${_xwin_includes}" CACHE STRING "" FORCE)

# Tell lld-link where to find import libraries.
string(CONCAT _xwin_libpaths
    " /LIBPATH:${XWIN_CRT_LIB}"
    " /LIBPATH:${XWIN_SDK_LIB}"
    " /LIBPATH:${XWIN_UCRT_LIB}"
)
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${CMAKE_EXE_LINKER_FLAGS_INIT}${_xwin_libpaths}"    CACHE STRING "" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${CMAKE_SHARED_LINKER_FLAGS_INIT}${_xwin_libpaths}" CACHE STRING "" FORCE)
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${CMAKE_MODULE_LINKER_FLAGS_INIT}${_xwin_libpaths}" CACHE STRING "" FORCE)

# --- Build-time tool execution ---
# resource_codegen.exe (and potentially other host tools) are built as Windows
# executables but must run during the build on the Linux host.
# Wine is used transparently for that purpose.
find_program(_wine_exe NAMES wine REQUIRED)
set(CMAKE_CROSSCOMPILING_EMULATOR "${_wine_exe}" CACHE STRING "")

cmake_minimum_required(VERSION 3.16)
project(BasicRHI LANGUAGES CXX)

# Require C++17
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

find_package(DirectX-Headers CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# Build a static library for now.
add_library(BasicRHI STATIC 
    rhi_dx12.cpp
 "rhi_interop.h" "rhi_dx12.h"  "rhi_interop_dx12.h" "resource_states.h" "rhi_helpers.h" "rhi_interop_dx12.cpp" "rhi_conversions_dx12.h" "rhi_debug.h" "rhi_colors.h")

target_compile_options(BasicRHI PRIVATE /WX)
target_compile_options(BasicRHI PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(BasicRHI PUBLIC Streamline::headers Pix::headers)

# Public header(s)
set_target_properties(BasicRHI PROPERTIES
    PUBLIC_HEADER "rhi.h"
)

# Set sources
target_sources(BasicRHI PRIVATE
    rhi_dx12.cpp
    rhi_interop_dx12.cpp
)

# Public include paths:
# - During build (add_subdirectory consumers): this source dir
# - After install (find_package consumers): ${prefix}/include
target_include_directories(BasicRHI
    PUBLIC
        ${DIRECTX_HEADERS_INCLUDE_DIR}
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# --- Installation ---

# Install the library and its public headers to standard dirs
install(TARGETS BasicRHI
    EXPORT BasicRHITargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}   # .dll on Windows
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}   # .so on Linux / .dylib on macOS
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}   # import lib / static lib if ever built as such
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/BasicRHI
)

# Export a targets file so dependents can do find_package(BasicRHI CONFIG)
install(EXPORT BasicRHITargets
    NAMESPACE BasicRHI::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/BasicRHI
)

# Optionally generate a very simple package config (so find_package(BasicRHI CONFIG) works)
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/BasicRHIConfigVersion.cmake"
    VERSION 1.0.0
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BasicRHIConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/BasicRHIConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/BasicRHI"
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/BasicRHIConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/BasicRHIConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/BasicRHI"
)

target_link_libraries(BasicRHI
PRIVATE 
    Microsoft::DirectX-Headers
    spdlog::spdlog_header_only
)
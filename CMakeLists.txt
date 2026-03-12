# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required (VERSION 3.8)

set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")

# Enable Zi for debug builds on MSVC
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)

  if (MSVC AND NOT CMAKE_CROSSCOMPILING)
    # Native MSVC: use PDB (Program Database) debug info
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
      "$<IF:$<CONFIG:Debug,RelWithDebInfo>,ProgramDatabase,>"
    )
  elseif(MSVC AND CMAKE_CROSSCOMPILING)
    # clang-cl cross-compile: use embedded CodeView (/Z7) since
    # lld-link has limited PDB generation support
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
      "$<IF:$<CONFIG:Debug,RelWithDebInfo>,Embedded,>"
    )
  endif()
endif()

if (POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

project ("BasicRenderer")

if (WIN32)
  add_compile_definitions(NOMINMAX)
endif()

if (MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
endif()

#set(CMAKE_VS_JUST_MY_CODE_DEBUGGING ON) # Skip external code in the debugger.

# Gather precompiled dependencies
set(USD_BASE_URL
  "https://github.com/panthuncia/OpenUSDArtifacts/releases/download/25.05.01"
)
set(USD_DBG_ZIP  "${CMAKE_SOURCE_DIR}/ThirdParty/OpenUSD_dbg.zip")
set(USD_REL_ZIP  "${CMAKE_SOURCE_DIR}/ThirdParty/OpenUSD_rel.zip")
set(USD_DBG_DIR  "${CMAKE_SOURCE_DIR}/ThirdParty/usd_dbg")
set(USD_REL_DIR  "${CMAKE_SOURCE_DIR}/ThirdParty/usd_rel")
set(BASICRENDERER_USD_VARIANT "" CACHE STRING "USD package variant to use: dbg or rel. Leave empty for automatic selection.")

# Download if the folder isn't already there
if(NOT EXISTS "${USD_DBG_DIR}")
  file(DOWNLOAD
    "${USD_BASE_URL}/OpenUSD_dbg.zip"
    "${USD_DBG_ZIP}"
    SHOW_PROGRESS
  )
  file(ARCHIVE_EXTRACT
    INPUT  "${USD_DBG_ZIP}"
    DESTINATION "${USD_DBG_DIR}"
  )
  file(REMOVE "${USD_DBG_ZIP}")
endif()

if(NOT EXISTS "${USD_REL_DIR}")
  file(DOWNLOAD
    "${USD_BASE_URL}/OpenUSD_rel.zip"
    "${USD_REL_ZIP}"
    SHOW_PROGRESS
  )
  file(ARCHIVE_EXTRACT
    INPUT  "${USD_REL_ZIP}"
    DESTINATION "${USD_REL_DIR}"
  )
  file(REMOVE "${USD_REL_ZIP}")
endif()

# Include sub-projects.
if(NOT BASICRENDERER_USD_VARIANT)
  if(CMAKE_CONFIGURATION_TYPES)
    set(BASICRENDERER_USD_VARIANT "rel")
  elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(BASICRENDERER_USD_VARIANT "dbg")
  else()
    set(BASICRENDERER_USD_VARIANT "rel")
  endif()
endif()

if(NOT BASICRENDERER_USD_VARIANT STREQUAL "dbg" AND NOT BASICRENDERER_USD_VARIANT STREQUAL "rel")
  message(FATAL_ERROR "BASICRENDERER_USD_VARIANT must be 'dbg' or 'rel'.")
endif()

set(_usd_selected_dir "${CMAKE_SOURCE_DIR}/ThirdParty/usd_${BASICRENDERER_USD_VARIANT}")
list(APPEND CMAKE_PREFIX_PATH "${_usd_selected_dir}")
set(PXR_DIR "${_usd_selected_dir}/lib/cmake/pxr")

add_subdirectory("ThirdParty")
add_subdirectory("BasicRHI")
add_subdirectory("OpenRenderGraph")
add_subdirectory ("BasicRenderer")
add_subdirectory("CLodCacheTool")
add_subdirectory("UsdPlugins")
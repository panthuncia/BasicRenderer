# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required (VERSION 3.8)

set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  if (NOT ENABLE_ADDRESS_SANITIZER)
	set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
  endif()
endif()

project ("BasicRenderer")

#set(CMAKE_VS_JUST_MY_CODE_DEBUGGING ON) # Skip external code in the debugger.

if (MSVC)
  add_compile_options(/Zc:preprocessor)
endif()

# Include sub-projects.
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/ThirdParty/usd_dbg")
set(PXR_DIR "${CMAKE_SOURCE_DIR}/ThirdParty/usd_dbg/lib/cmake/pxr")
else()
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/ThirdParty/usd_rel")
set(PXR_DIR "${CMAKE_SOURCE_DIR}/ThirdParty/usd_rel/lib/cmake/pxr")
endif()

add_subdirectory ("BasicRenderer")

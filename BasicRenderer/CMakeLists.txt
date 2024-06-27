# CMakeList.txt : CMake project for BasicRenderer, include source and define
# project specific logic here.
#

# Add source to this project's executable.
add_definitions(-D_UNICODE -DUNICODE)


file(GLOB_RECURSE SOURCES "src/*.cpp")
add_executable (BasicRenderer WIN32  ${SOURCES})

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET BasicRenderer PROPERTY CXX_STANDARD 20)
endif()

# Define where we want to copy the shaders directory in the build output
set(SHADERS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/BasicRenderer/shaders")
set(SHADERS_DEST_DIR "${CMAKE_BINARY_DIR}/BasicRenderer/")

file(COPY ${SHADERS_SOURCE_DIR} DESTINATION ${SHADERS_DEST_DIR})

# Include directories
include_directories(include/)

# Find and link DirectX 12
target_link_libraries(${PROJECT_NAME} PRIVATE d3d12.lib d3dcompiler.lib dxgi.lib)

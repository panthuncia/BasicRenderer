# CMakeList.txt : CMake project for BasicRenderer, include source and define
# project specific logic here.
#

# Ensure project include paths take precedence over system paths.
set(CMAKE_INCLUDE_DIRECTORIES_BEFORE ON)


# Add source to this project's executable.
add_definitions(-D_UNICODE -DUNICODE)

find_package(spdlog CONFIG REQUIRED)
find_package(directxtex CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(meshoptimizer REQUIRED)
find_package(DirectX-Headers CONFIG REQUIRED)
find_package(DirectX12-Agility CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(flecs CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
# Build tool for code generation
add_executable(codegen
  "generators/resource_codegen.cpp"
)

target_link_libraries(codegen
  PRIVATE
    nlohmann_json::nlohmann_json
    fmt::fmt
)

target_include_directories(codegen
    PRIVATE
    refl-cpp::refl-cpp
)

set(GENERATED_DIR  "${CMAKE_SOURCE_DIR}/BasicRenderer/generated")
set(GENERATED_HDR  "${GENERATED_DIR}/BuiltinResources.h")

# ensure the output directory exists
file(MAKE_DIRECTORY "${GENERATED_DIR}")
set(CMAKE_VERBOSE_MAKEFILE ON)
add_custom_command(
  OUTPUT "${GENERATED_HDR}"
  COMMAND codegen
          "${CMAKE_SOURCE_DIR}/BasicRenderer/Generators/Templates/BuiltinResources.h.json"
          "${GENERATED_HDR}"
  DEPENDS codegen
          "${CMAKE_SOURCE_DIR}/BasicRenderer/Generators/Templates/BuiltinResources.h.json"
  COMMENT "Regenerating BuiltinResources.h from BuiltinResources.h.json"
  VERBATIM
  USES_TERMINAL
)

set_target_properties(codegen PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)

add_custom_target(GenerateBuiltinResources
  DEPENDS "${GENERATED_HDR}"
)

file(GLOB_RECURSE SOURCES "src/*.cpp")
add_executable (${PROJECT_NAME} WIN32  ${SOURCES})

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 23)
endif()

# pend on the generated header in the BasicRenderer target
add_dependencies(${PROJECT_NAME} GenerateBuiltinResources)

target_include_directories(${PROJECT_NAME}
  PRIVATE
    "${GENERATED_DIR}"
)

#set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi") # For MSVC
#set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")

function(add_symlink_or_copy_per_config source destination IS_DIRECTORY target_name)
    cmake_parse_arguments(_args "" "" "IS_DIRECTORY;TARGET_NAME" ${ARGN})

    if (NOT target_name)
        get_filename_component(target_name "${destination}" NAME)
    endif()

    if (CMAKE_BUILD_TYPE STREQUAL "Release")
            if (IS_DIRECTORY)
            add_custom_target(
                "${target_name}_sync" ALL
                COMMENT "Release: Copying directory ${destination} ← ${source}"
                VERBATIM
                COMMAND   ${CMAKE_COMMAND} -E remove_directory   "${destination}"
                COMMAND   ${CMAKE_COMMAND} -E copy_directory     "${source}" "${destination}"
            )
        else()
            add_custom_target(
                "${target_name}_sync" ALL
                COMMENT "Release: Copying file ${destination} ← ${source}"
                VERBATIM
                COMMAND   ${CMAKE_COMMAND} -E remove            "${destination}"
                COMMAND   ${CMAKE_COMMAND} -E copy              "${source}" "${destination}"
            )
        endif()
    else()
        if (IS_DIRECTORY)
        add_custom_target(
            "${target_name}_sync" ALL
            COMMENT "Debug: Symlinking directory ${destination} ← ${source}"
            VERBATIM
            COMMAND   ${CMAKE_COMMAND} -E remove_directory   "${destination}"
            COMMAND   ${CMAKE_COMMAND} -E create_symlink     "${source}" "${destination}"
        )
        else()
            add_custom_target(
                "${target_name}_sync" ALL
                COMMENT "Debug: Symlinking file ${destination} ← ${source}"
                VERBATIM
                COMMAND   ${CMAKE_COMMAND} -E remove            "${destination}"
                COMMAND   ${CMAKE_COMMAND} -E create_symlink    "${source}" "${destination}"
            )
        endif()
    endif()

    add_dependencies(BasicRenderer "${target_name}_sync")
endfunction()

set(SHADERS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/BasicRenderer/shaders")
set(SHADERS_DEST_DIR "${CMAKE_BINARY_DIR}/BasicRenderer/shaders")

add_symlink_or_copy_per_config(
    "${SHADERS_SOURCE_DIR}"
    "${SHADERS_DEST_DIR}"
    TRUE  # IS_DIRECTORY
    "shaders"
)

set(MODELS_SRC_DIR "${CMAKE_SOURCE_DIR}/models")
set(MODELS_DEST_PATH "${CMAKE_BINARY_DIR}/BasicRenderer/models")

add_symlink_or_copy_per_config(
    "${MODELS_SRC_DIR}"
    "${MODELS_DEST_PATH}"
    TRUE  # IS_DIRECTORY
    "models"
)

set(TEXTURES_SRC_DIR "${CMAKE_SOURCE_DIR}/textures")
set(TEXTURES_DEST_PATH "${CMAKE_BINARY_DIR}/BasicRenderer/textures")

add_symlink_or_copy_per_config(
    "${TEXTURES_SRC_DIR}"
    "${TEXTURES_DEST_PATH}"
    TRUE  # IS_DIRECTORY
    "textures"
)

set(COMPILER_BINARY_DIR "${CMAKE_SOURCE_DIR}/BasicRenderer/dxcompiler.dll")
set(COMPILER_DEST_FILE "${CMAKE_BINARY_DIR}/BasicRenderer/dxcompiler.dll")

add_symlink_or_copy_per_config(
    "${COMPILER_BINARY_DIR}"
    "${COMPILER_DEST_FILE}"
    FALSE  # IS_DIRECTORY
    "dxcompiler_dll"
)

set(SIGNING_BINARY_DIR "${CMAKE_SOURCE_DIR}/BasicRenderer/dxil.dll")
set(SIGNING_DEST_FILE "${CMAKE_BINARY_DIR}/BasicRenderer/dxil.dll")

add_symlink_or_copy_per_config(
    "${SIGNING_BINARY_DIR}"
    "${SIGNING_DEST_FILE}"
    FALSE  # IS_DIRECTORY
    "dxil_dll"
)

set(PIX_BINARY_DIR "${CMAKE_SOURCE_DIR}/BasicRenderer/WinPixEventRuntime.dll")
set(PIX_DEST_FILE "${CMAKE_BINARY_DIR}/BasicRenderer/WinPixEventRuntime.dll")

add_symlink_or_copy_per_config(
    "${PIX_BINARY_DIR}"
    "${PIX_DEST_FILE}"
    FALSE  # IS_DIRECTORY
    "winpix_dll"
)

set(D3D_SRC_DIR "${CMAKE_SOURCE_DIR}/BasicRenderer/D3D")
set(D3D_DEST_PATH "${CMAKE_BINARY_DIR}/BasicRenderer/D3D")

add_symlink_or_copy_per_config(
    "${D3D_SRC_DIR}"
    "${D3D_DEST_PATH}"
    TRUE  # IS_DIRECTORY
    "d3d"
)

set(AFTERMATH_BINARY_DIR "${CMAKE_SOURCE_DIR}/BasicRenderer/GFSDK_Aftermath_Lib.x64.dll")
set(AFTERMATH_DEST_FILE "${CMAKE_BINARY_DIR}/BasicRenderer/GFSDK_Aftermath_Lib.x64.dll")

add_symlink_or_copy_per_config(
    "${AFTERMATH_BINARY_DIR}"
    "${AFTERMATH_DEST_FILE}"
    FALSE  # IS_DIRECTORY
    "aftermath_dll"
)

set(SL_BINARY_DIR "${CMAKE_SOURCE_DIR}/BasicRenderer/sl.interposer.dll")
set(SL_DEST_FILE "${CMAKE_BINARY_DIR}/BasicRenderer/sl.interposer.dll")

add_symlink_or_copy_per_config(
    "${SL_BINARY_DIR}"
    "${SL_DEST_FILE}"
    FALSE  # IS_DIRECTORY
    "sl_interposer_dll"
)

set(NVSL_DIR "${CMAKE_SOURCE_DIR}/BasicRenderer/NVSL")
set(NVSL_FILE "${CMAKE_BINARY_DIR}/BasicRenderer/NVSL")

add_symlink_or_copy_per_config(
    "${NVSL_DIR}"
    "${NVSL_FILE}"
    TRUE  # IS_DIRECTORY
    "NVSL_DIR"
)

set(FFX_BIN_DIR "${CMAKE_SOURCE_DIR}/BasicRenderer/FFX/amd_fidelityfx_dx12.dll")
set(FFX_BIN_FILE "${CMAKE_BINARY_DIR}/BasicRenderer/amd_fidelityfx_dx12.dll")

add_symlink_or_copy_per_config(
    "${FFX_BIN_DIR}"
    "${FFX_BIN_FILE}"
    FALSE  # IS_DIRECTORY
    "FFX_dx12"
)

set(FFX_BACKEND_DIR "${CMAKE_SOURCE_DIR}/BasicRenderer/FFX/ffx_backend_dx12_x64.dll")
set(FFX_BACKEND_FILE "${CMAKE_BINARY_DIR}/BasicRenderer/ffx_backend_dx12_x64.dll")

add_symlink_or_copy_per_config(
    "${FFX_BACKEND_DIR}"
    "${FFX_BACKEND_FILE}"
    FALSE  # IS_DIRECTORY
    "FFX_backend_dx12"
)

set(FFX_SSSR_DIR "${CMAKE_SOURCE_DIR}/BasicRenderer/FFX/ffx_sssr_x64.dll")
set(FFX_SSSR_FILE "${CMAKE_BINARY_DIR}/BasicRenderer/ffx_sssr_x64.dll")

add_symlink_or_copy_per_config(
    "${FFX_SSSR_DIR}"
    "${FFX_SSSR_FILE}"
    FALSE  # IS_DIRECTORY
    "FFX_sssr_dx12"
)

set(FFX_DENOISER_DIR "${CMAKE_SOURCE_DIR}/BasicRenderer/FFX/ffx_denoiser_x64.dll")
set(FFX_DENOISER_FILE "${CMAKE_BINARY_DIR}/BasicRenderer/ffx_denoiser_x64.dll")

add_symlink_or_copy_per_config(
    "${FFX_DENOISER_DIR}"
    "${FFX_DENOISER_FILE}"
    FALSE  # IS_DIRECTORY
    "FFX_denoiser_dx12"
)

target_include_directories(${PROJECT_NAME} BEFORE PUBLIC ${DIRECTX_HEADERS_INCLUDE_DIR} include/ refl-cpp::refl-cpp)

# Heck off Microsoft
add_compile_definitions(NOMINMAX)

# Disable warnings for signed/unsigned mismatch
add_compile_options("/wd4267")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj")

# Compile‐only: /Zi on RelWithDebInfo
target_compile_options(${PROJECT_NAME} PRIVATE
  $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:RelWithDebInfo>>:/Zi>
)

# Link‐only: /DEBUG on RelWithDebInfo
target_link_options(${PROJECT_NAME} PRIVATE
  $<$<AND:$<CONFIG:RelWithDebInfo>>:/DEBUG>
)

if (ENABLE_NSIGHT_AFTERMATH)
  # This will pass -DENABLE_NSIGHT_AFTERMATH to the compiler
  target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_NSIGHT_AFTERMATH)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
  # first, define integer codes for each build flavor
  BUILD_TYPE_DEBUG=0
  BUILD_TYPE_RELEASE=1
  BUILD_TYPE_RELEASE_DEBUG=2

  # then set BUILD_TYPE to one of those codes based on CMAKE_BUILD_TYPE:
  $<$<CONFIG:Debug>:BUILD_TYPE=BUILD_TYPE_DEBUG>
  $<$<CONFIG:Release>:BUILD_TYPE=BUILD_TYPE_RELEASE>
  $<$<CONFIG:RelWithDebInfo>:BUILD_TYPE=BUILD_TYPE_RELEASE_DEBUG>
)

# Find and link DirectX 12
target_link_libraries(${PROJECT_NAME} 
PRIVATE 
    d3dcompiler.lib 
    dxgi.lib 
    spdlog::spdlog 
    Microsoft::DirectXTex 
    imgui::imgui 
    meshoptimizer::meshoptimizer 
    Microsoft::DirectX-Headers 
    Microsoft::DirectX12-Agility 
    assimp::assimp 
    flecs::flecs 
    flecs::flecs_static 
    "${CMAKE_SOURCE_DIR}/BasicRenderer/WinPixEventRuntime.lib" 
    "${CMAKE_SOURCE_DIR}/BasicRenderer/include/Aftermath/lib/x64/GFSDK_Aftermath_Lib.x64.lib" 
    "${CMAKE_SOURCE_DIR}/BasicRenderer/sl.interposer.lib" 
    "${CMAKE_SOURCE_DIR}/BasicRenderer/amd_fidelityfx_dx12.lib" 
    "${CMAKE_SOURCE_DIR}/BasicRenderer/ffx_backend_dx12_x64.lib"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/ffx_sssr_x64.lib"
)
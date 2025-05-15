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

file(GLOB_RECURSE SOURCES "src/*.cpp")
add_executable (BasicRenderer WIN32  ${SOURCES})

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET BasicRenderer PROPERTY CXX_STANDARD 20)
endif()

#set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi") # For MSVC
#set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")

function(add_symlink_or_copy_per_config source destination IS_DIRECTORY target_name)
    # Default target name if not provided
    if(NOT target_name)
        get_filename_component(target_name "${destination}" NAME)
    endif()

    # Ensure IS_DIRECTORY is uppercase (TRUE or FALSE)
    #string(TOUPPER "${IS_DIRECTORY}" IS_DIRECTORY)

    string(TOUPPER "${CMAKE_BUILD_TYPE}" BUILD_TYPE_UPPER)

    if(BUILD_TYPE_UPPER STREQUAL "DEBUG")
        # Debug Configuration - Create Symbolic Link
        if(${IS_DIRECTORY})
            add_custom_command(
                OUTPUT "${destination}"
                COMMAND ${CMAKE_COMMAND} -E remove_directory "${destination}"
                COMMAND ${CMAKE_COMMAND} -E create_symlink "${source}" "${destination}"
                DEPENDS "${source}"
                COMMENT "Linking directory ${source} to ${destination}"
                VERBATIM
            )
        else()
            add_custom_command(
                OUTPUT "${destination}"
                COMMAND ${CMAKE_COMMAND} -E remove "${destination}"
                COMMAND ${CMAKE_COMMAND} -E create_symlink "${source}" "${destination}"
                DEPENDS "${source}"
                COMMENT "Linking file ${source} to ${destination}"
                VERBATIM
            )
        endif()

        set(symlink_target_name "${target_name}_symlink_target")
        add_custom_target(${symlink_target_name} ALL DEPENDS "${destination}")
        set_target_properties(${symlink_target_name} PROPERTIES CONFIGURATIONS Debug)
        add_dependencies(BasicRenderer ${symlink_target_name})
    else()
    # Release Configuration - Copy
        if(${IS_DIRECTORY})
            add_custom_command(
                OUTPUT "${destination}"
                COMMAND ${CMAKE_COMMAND} -E remove_directory "${destination}"
                COMMAND ${CMAKE_COMMAND} -E copy_directory "${source}" "${destination}"
                DEPENDS "${source}"
                COMMENT "Copying directory ${source} to ${destination}"
                VERBATIM
            )
        else()
            add_custom_command(
                OUTPUT "${destination}"
                COMMAND ${CMAKE_COMMAND} -E remove "${destination}"
                COMMAND ${CMAKE_COMMAND} -E copy "${source}" "${destination}"
                DEPENDS "${source}"
                COMMENT "Copying file ${source} to ${destination}"
                VERBATIM
            )
        endif()

        set(copy_target_name "${target_name}_copy_target")
        add_custom_target(${copy_target_name} ALL DEPENDS "${destination}")
        set_target_properties(${copy_target_name} PROPERTIES CONFIGURATIONS Release)
        add_dependencies(BasicRenderer ${copy_target_name})
    endif()
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

target_include_directories(${PROJECT_NAME} BEFORE PUBLIC ${DIRECTX_HEADERS_INCLUDE_DIR} include/)

# Heck off Microsoft
add_compile_definitions(NOMINMAX)

# Disable warnings for signed/unsigned mismatch
add_compile_options("/wd4267")

if (MSVC)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj")
#  # Tell the compiler to generate Program Database debug info
#  set(CMAKE_CXX_FLAGS_RELEASE
#      "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
#  # Tell the linker to embed or emit the .pdb file
#  set(CMAKE_SHARED_LINKER_FLAGS_RELEASE
#      "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG")
#  set(CMAKE_EXE_LINKER_FLAGS_RELEASE
#      "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG")
endif()

# Find and link DirectX 12
target_link_libraries(${PROJECT_NAME} PRIVATE d3dcompiler.lib dxgi.lib spdlog::spdlog Microsoft::DirectXTex imgui::imgui meshoptimizer::meshoptimizer Microsoft::DirectX-Headers Microsoft::DirectX12-Agility assimp::assimp flecs::flecs flecs::flecs_static "${CMAKE_SOURCE_DIR}/BasicRenderer/WinPixEventRuntime.lib" "${CMAKE_SOURCE_DIR}/BasicRenderer/include/Aftermath/lib/x64/GFSDK_Aftermath_Lib.x64.lib")
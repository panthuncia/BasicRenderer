# Require C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find OpenUSD (must provide USD_INCLUDE_DIRS and USD_LIBRARIES)
find_package(pxr REQUIRED)
find_package(spdlog REQUIRED)

# Find CURL for HTTP fetching
find_package(curl REQUIRED)

# Our plugin target
add_library(usd_httpResolver MODULE
    HttpResolver.cpp
    HttpResolver.h
)

# Link against USD and CURL
target_link_libraries(usd_httpResolver PRIVATE
    spdlog::spdlog
    arch
    plug
    ar
    ${CURL_LIBRARIES}
)

# On Windows, ensure it's called usd_httpResolver.dll (no lib prefix)
set_target_properties(usd_httpResolver PROPERTIES
    PREFIX ""
    SUFFIX ".dll"
)

install(TARGETS usd_httpResolver
    LIBRARY DESTINATION ${CMAKE_BINARY_DIR}
)

set_target_properties(usd_httpResolver PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/BasicRenderer"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/BasicRenderer"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/BasicRenderer"
)

install(FILES plugInfo.json
    DESTINATION plugins/pxr
)


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

# Endure folder exists
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/BasicRenderer/thirdPartyUSDPlugins")

add_symlink_or_copy_per_config(
    "${CMAKE_SOURCE_DIR}/UsdPlugins/HttpResolver/httpResolver"
    "${CMAKE_BINARY_DIR}/BasicRenderer/thirdPartyUSDPlugins/httpResolver"
    TRUE  # IS_DIRECTORY
    "httpResolver"
)
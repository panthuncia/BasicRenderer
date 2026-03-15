cmake_minimum_required(VERSION 3.16)

project(BasicScene VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)

find_package(flecs CONFIG REQUIRED)

add_library(BasicScene STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/SceneWorldManager.cpp"
)

add_library(BasicScene::BasicScene ALIAS BasicScene)

set_target_properties(BasicScene PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(BasicScene
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(BasicScene
    PUBLIC
        flecs::flecs
)

if (MSVC)
    target_compile_options(BasicScene PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
endif()

install(TARGETS BasicScene
    EXPORT BasicSceneTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(EXPORT BasicSceneTargets
    FILE BasicSceneTargets.cmake
    NAMESPACE BasicScene::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/BasicScene"
)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/BasicSceneConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BasicSceneConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/BasicSceneConfig.cmake"
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/BasicScene"
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/BasicSceneConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/BasicSceneConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/BasicScene"
)
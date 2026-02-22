cmake_minimum_required(VERSION 3.16)

project(OpenRenderGraph VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)

option(OPENRENDERGRAPH_ENABLE_SUBMODULE_FALLBACK "Allow fallback to in-tree BasicRHI when package is unavailable" ON)
option(OPENRENDERGRAPH_FORWARD_BASICRHI_DEP_OPTIONS "Forward BasicRHI PIX/Streamline options when adding BasicRHI subdirectory" ON)

set(OPENRENDERGRAPH_BASICRHI_ENABLE_STREAMLINE "" CACHE STRING "Override BASICRHI_ENABLE_STREAMLINE for submodule fallback (ON/OFF). Empty keeps BasicRHI default.")
set(OPENRENDERGRAPH_BASICRHI_ENABLE_PIX "" CACHE STRING "Override BASICRHI_ENABLE_PIX for submodule fallback (ON/OFF). Empty keeps BasicRHI default.")
set(OPENRENDERGRAPH_BASICRHI_STREAMLINE_HEADERS_DIR "" CACHE PATH "Override BASICRHI_STREAMLINE_HEADERS_DIR for submodule fallback.")
set(OPENRENDERGRAPH_BASICRHI_PIX_HEADERS_DIR "" CACHE PATH "Override BASICRHI_PIX_HEADERS_DIR for submodule fallback.")

find_package(BasicRHI CONFIG QUIET)
if(TARGET BasicRHI AND NOT TARGET BasicRHI::BasicRHI)
    add_library(BasicRHI::BasicRHI ALIAS BasicRHI)
endif()

if(NOT TARGET BasicRHI::BasicRHI AND OPENRENDERGRAPH_ENABLE_SUBMODULE_FALLBACK)
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../BasicRHI/CMakeLists.txt")
        if(OPENRENDERGRAPH_FORWARD_BASICRHI_DEP_OPTIONS)
            if(NOT OPENRENDERGRAPH_BASICRHI_ENABLE_STREAMLINE STREQUAL "")
                set(BASICRHI_ENABLE_STREAMLINE "${OPENRENDERGRAPH_BASICRHI_ENABLE_STREAMLINE}" CACHE BOOL "" FORCE)
            endif()
            if(NOT OPENRENDERGRAPH_BASICRHI_ENABLE_PIX STREQUAL "")
                set(BASICRHI_ENABLE_PIX "${OPENRENDERGRAPH_BASICRHI_ENABLE_PIX}" CACHE BOOL "" FORCE)
            endif()
            if(NOT OPENRENDERGRAPH_BASICRHI_STREAMLINE_HEADERS_DIR STREQUAL "")
                set(BASICRHI_STREAMLINE_HEADERS_DIR "${OPENRENDERGRAPH_BASICRHI_STREAMLINE_HEADERS_DIR}" CACHE PATH "" FORCE)
            endif()
            if(NOT OPENRENDERGRAPH_BASICRHI_PIX_HEADERS_DIR STREQUAL "")
                set(BASICRHI_PIX_HEADERS_DIR "${OPENRENDERGRAPH_BASICRHI_PIX_HEADERS_DIR}" CACHE PATH "" FORCE)
            endif()
        endif()
        add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../BasicRHI" "${CMAKE_BINARY_DIR}/_deps/BasicRHI")
    endif()
endif()

if(TARGET BasicRHI AND NOT TARGET BasicRHI::BasicRHI)
    add_library(BasicRHI::BasicRHI ALIAS BasicRHI)
endif()

if(NOT TARGET BasicRHI::BasicRHI)
    message(FATAL_ERROR "OpenRenderGraph requires BasicRHI::BasicRHI. Install BasicRHI package or provide BasicRHI as submodule.")
endif()

find_package(spdlog CONFIG REQUIRED)
find_package(flecs CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS container_hash)
find_package(imgui CONFIG REQUIRED)
find_package(implot CONFIG REQUIRED)
find_package(slang CONFIG REQUIRED)

add_library(OpenRenderGraph STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/DebugUI/MemoryIntrospectionWidget.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/DebugUI/MemoryViewWidget.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/DebugUI/RenderGraphInspector.cpp"

    "${CMAKE_CURRENT_SOURCE_DIR}/src/Render/DescriptorHeap.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Render/CommandListPool.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Render/ImmediateExecution/ImmediateCommandList.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Render/MemoryMetadataAdapter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Render/MemoryIntrospectionAPI.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Render/MemoryIntrospectionBackend.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Render/Runtime/DefaultStatisticsService.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Render/Runtime/DefaultUploadService.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Render/Runtime/DefaultReadbackService.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Render/Runtime/DefaultDescriptorService.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Render/Runtime/DefaultRenderGraphSettingsService.cpp"
    
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Render/PassBuilders.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Render/RenderGraph/RenderGraph.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Render/RenderGraph/Aliasing/RenderGraphAliasingSubsystem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Render/RenderGraph/Aliasing/RenderGraphAliasingAlgorithms.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Managers/CommandRecordingManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Managers/Singletons/DeviceManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Managers/Singletons/DescriptorHeapManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Managers/Singletons/UploadManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Managers/Singletons/ReadbackManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Managers/Singletons/StatisticsManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Resources/ExternalBackingResource.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Resources/PixelBuffer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Resources/Buffers/Buffer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Resources/Buffers/DynamicBufferBase.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Resources/ResourceStateTracker.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Resources/GPUBacking/GPUBufferBacking.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Resources/GPUBacking/GPUTextureBacking.cpp"
)

add_library(OpenRenderGraph::OpenRenderGraph ALIAS OpenRenderGraph)

set_target_properties(OpenRenderGraph PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(OpenRenderGraph
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/include"

)

target_link_libraries(OpenRenderGraph
    PUBLIC
        BasicRHI::BasicRHI
        flecs::flecs
        Boost::container_hash
        imgui::imgui
        implot::implot
        slang::slang
    PRIVATE
        spdlog::spdlog_header_only
)

target_compile_definitions(OpenRenderGraph
    PUBLIC
        BUILD_TYPE_DEBUG=0
        BUILD_TYPE_RELEASE=1
        BUILD_TYPE_RELEASE_DEBUG=2
        $<$<CONFIG:Debug>:BUILD_TYPE=BUILD_TYPE_DEBUG>
        $<$<CONFIG:Release>:BUILD_TYPE=BUILD_TYPE_RELEASE>
        $<$<CONFIG:RelWithDebInfo>:BUILD_TYPE=BUILD_TYPE_RELEASE_DEBUG>
)

if (MSVC)
    target_compile_options(OpenRenderGraph PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
endif()

install(TARGETS OpenRenderGraph
    EXPORT OpenRenderGraphTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(EXPORT OpenRenderGraphTargets
    FILE OpenRenderGraphTargets.cmake
    NAMESPACE OpenRenderGraph::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/OpenRenderGraph"
)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/OpenRenderGraphConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/OpenRenderGraphConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/OpenRenderGraphConfig.cmake"
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/OpenRenderGraph"
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/OpenRenderGraphConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/OpenRenderGraphConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/OpenRenderGraph"
)

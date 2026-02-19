cmake_minimum_required(VERSION 3.16)

project(OpenRenderGraph LANGUAGES CXX)

find_package(spdlog CONFIG REQUIRED)
find_package(flecs CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS container_hash)
find_package(directxtex CONFIG REQUIRED)

add_library(OpenRenderGraph STATIC
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Render/DescriptorHeap.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Render/CommandListPool.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Render/ImmediateExecution/ImmediateCommandList.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Render/MemoryMetadataAdapter.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Render/MemoryIntrospectionAPI.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Render/MemoryIntrospectionBackend.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Render/Runtime/DefaultStatisticsService.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Render/Runtime/DefaultUploadService.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Render/Runtime/DefaultReadbackService.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Render/Runtime/DefaultRenderGraphSettingsService.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Render/PassBuilders.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Render/RenderGraph/RenderGraph.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Render/RenderGraph/Aliasing/RenderGraphAliasingSubsystem.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Render/RenderGraph/Aliasing/RenderGraphAliasingAlgorithms.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Managers/CommandRecordingManager.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Managers/Singletons/DeviceManager.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Managers/Singletons/ResourceManager.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Managers/Singletons/UploadManager.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Managers/Singletons/ReadbackManager.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Managers/Singletons/StatisticsManager.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Resources/ResourceStateTracker.cpp"
)

set_target_properties(OpenRenderGraph PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(OpenRenderGraph
    PUBLIC
        "${CMAKE_SOURCE_DIR}/BasicRenderer/include"
)

target_link_libraries(OpenRenderGraph
    PUBLIC
        BasicRHI
        flecs::flecs
        Boost::container_hash
    PRIVATE
        spdlog::spdlog_header_only
        Microsoft::DirectXTex
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

cmake_minimum_required(VERSION 3.16)

project(OpenRenderGraph LANGUAGES CXX)

option(ORG_STRICT_HEADERS "Disallow OpenRenderGraph from including BasicRenderer headers" ON)

find_package(spdlog CONFIG REQUIRED)
find_package(flecs CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS container_hash)

add_library(OpenRenderGraph STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Render/DescriptorHeap.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Render/CommandListPool.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Render/ImmediateExecution/ImmediateCommandList.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Render/MemoryMetadataAdapter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Render/MemoryIntrospectionAPI.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Render/MemoryIntrospectionBackend.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Render/Runtime/DefaultStatisticsService.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Render/Runtime/DefaultUploadService.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Render/Runtime/DefaultReadbackService.cpp"
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
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Resources/ResourceStateTracker.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Resources/GPUBacking/GPUBufferBacking.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Resources/GPUBacking/GPUTextureBacking.cpp"
)

set_target_properties(OpenRenderGraph PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(OpenRenderGraph
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if (NOT ORG_STRICT_HEADERS)
    target_include_directories(OpenRenderGraph
        PUBLIC
            "${CMAKE_SOURCE_DIR}/BasicRenderer/include"
    )
    message(WARNING "OpenRenderGraph: ORG_STRICT_HEADERS=OFF enables temporary BasicRenderer header access.")
endif()

target_link_libraries(OpenRenderGraph
    PUBLIC
        BasicRHI
        flecs::flecs
        Boost::container_hash
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

cmake_minimum_required(VERSION 3.16)

project(OpenRenderGraph LANGUAGES CXX)

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

set_target_properties(OpenRenderGraph PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(OpenRenderGraph
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/include"

)

target_link_libraries(OpenRenderGraph
    PUBLIC
        BasicRHI
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

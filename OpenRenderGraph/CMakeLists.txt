cmake_minimum_required(VERSION 3.16)

project(OpenRenderGraph LANGUAGES CXX)

find_package(spdlog CONFIG REQUIRED)
find_package(flecs CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS container_hash)

add_library(OpenRenderGraph STATIC
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Render/DescriptorHeap.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Render/CommandListPool.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Render/ImmediateExecution/ImmediateCommandList.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Render/PassBuilders.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Render/RenderGraph/RenderGraph.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Render/RenderGraph/Aliasing/RenderGraphAliasingSubsystem.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Render/RenderGraph/Aliasing/RenderGraphAliasingAlgorithms.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Managers/CommandRecordingManager.cpp"
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
)

if (MSVC)
    target_compile_options(OpenRenderGraph PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
endif()

cmake_minimum_required(VERSION 3.16)

project(OpenRenderGraph LANGUAGES CXX)

add_library(OpenRenderGraph STATIC
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Render/DescriptorHeap.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Render/CommandListPool.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Render/ImmediateExecution/ImmediateCommandList.cpp"
    "${CMAKE_SOURCE_DIR}/BasicRenderer/src/Managers/CommandRecordingManager.cpp"
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
)

if (MSVC)
    target_compile_options(OpenRenderGraph PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
endif()

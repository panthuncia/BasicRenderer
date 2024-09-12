#pragma once

enum class ResourceState {
    Undefined,
    Common,
    RenderTarget,
    DepthWrite,
    DepthRead,
    ShaderResource,
    CopySource,
    CopyDest,
};
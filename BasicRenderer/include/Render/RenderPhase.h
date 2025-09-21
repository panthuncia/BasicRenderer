#pragma once
enum class RenderPhase : uint16_t {
    PreZ, GBuffer, Lighting, ForwardBase, OIT_Fill, OIT_Resolve, Shadow,
};

#pragma once

#include <cstdint>

#include "Mesh/VertexFlags.h"

namespace MeshVertexLayout {

inline constexpr uint32_t PositionOffset = 0u;
inline constexpr uint32_t PositionSize = 12u;
inline constexpr uint32_t NormalOffset = PositionOffset + PositionSize;
inline constexpr uint32_t NormalSize = 12u;
inline constexpr uint32_t BaseVertexSize = NormalOffset + NormalSize;
inline constexpr uint32_t TexcoordSize = 8u;
inline constexpr uint32_t ColorSize = 12u;
inline constexpr uint32_t MaxVertexSize = BaseVertexSize + TexcoordSize + ColorSize;

inline constexpr bool HasTexcoords(uint32_t flags) noexcept {
    return (flags & VertexFlags::VERTEX_TEXCOORDS) != 0u;
}

inline constexpr bool HasColors(uint32_t flags) noexcept {
    return (flags & VertexFlags::VERTEX_COLORS) != 0u;
}

inline constexpr uint32_t TexcoordOffset(uint32_t) noexcept {
    return BaseVertexSize;
}

inline constexpr uint32_t ColorOffset(uint32_t flags) noexcept {
    return BaseVertexSize + (HasTexcoords(flags) ? TexcoordSize : 0u);
}

inline constexpr uint32_t VertexSize(uint32_t flags) noexcept {
    return BaseVertexSize
        + (HasTexcoords(flags) ? TexcoordSize : 0u)
        + (HasColors(flags) ? ColorSize : 0u);
}

} // namespace MeshVertexLayout

#pragma once

#include <optional>

using namespace DirectX;

#include <DirectXMath.h>
#include <variant>
#include <vector>
#include <cstdint>

using namespace DirectX;

struct VertexNormalMapped {
    XMFLOAT3 position;
    XMFLOAT3 normal;
    XMFLOAT2 texcoord;
    XMFLOAT3 tangent;
    XMFLOAT3 bitangent;
};

enum VertexFlags {
    VERTEX_COLORS = 1 << 0,
    VERTEX_NORMALS = 1 << 1,
	VERTEX_TEXCOORDS = 1 << 2,
	VERTEX_SKINNED = 1 << 3,
};
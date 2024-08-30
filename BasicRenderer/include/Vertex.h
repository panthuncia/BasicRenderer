#pragma once

#include <optional>

using namespace DirectX;

#include <DirectXMath.h>
#include <variant>
#include <vector>
#include <cstdint>

using namespace DirectX;

struct VertexBasic {
    XMFLOAT3 position;
    XMFLOAT3 normal;
};

struct VertexColored {
    XMFLOAT3 position;
    XMFLOAT3 normal;
    XMFLOAT4 color;
};

struct VertexTextured {
    XMFLOAT3 position;
    XMFLOAT3 normal;
    XMFLOAT2 texcoord;
};

struct VertexSkinned {
    XMFLOAT3 position;
    XMFLOAT3 normal;
    XMFLOAT2 texcoord;
    XMUINT4 joints;
    XMFLOAT4 weights;
};

struct VertexNormalMapped {
    XMFLOAT3 position;
    XMFLOAT3 normal;
    XMFLOAT2 texcoord;
    XMFLOAT3 tangent;
    XMFLOAT3 bitangent;
};

struct VertexNormalMappedSkinned {
    XMFLOAT3 position;
    XMFLOAT3 normal;
    XMFLOAT2 texcoord;
    XMFLOAT3 tangent;
    XMFLOAT3 bitangent;
    XMUINT4 joints;
    XMFLOAT4 weights;
};

using Vertex = std::variant<VertexBasic, VertexColored, VertexTextured, VertexSkinned, VertexNormalMapped, VertexNormalMappedSkinned>;
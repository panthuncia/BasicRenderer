#pragma once

#include <DirectXMath.h>
#include <memory>
#include <string>

class Texture;

struct TextureAndConstant {
    std::shared_ptr<Texture> texture;  // null if none
    float factor = 1.0f;
};

struct MaterialDescription {
    std::string name;
    DirectX::XMFLOAT4    diffuseColor = { 1,1,1,1 };
    DirectX::XMFLOAT4    emissiveColor = { 0,0,0,1 };
    TextureAndConstant    baseColor = {};
    TextureAndConstant    metallic = { nullptr, 0.0f };
    TextureAndConstant    roughness = { nullptr, 0.5f };
    TextureAndConstant    emissive = {};
    TextureAndConstant    opacity = { nullptr, 1.0f };
	TextureAndConstant    aoMap = {};
	TextureAndConstant    heightMap = {};
    TextureAndConstant	 normal = {};
};

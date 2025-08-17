#pragma once

#include <DirectXMath.h>
#include <memory>
#include <string>
#include <cctype>

#include "Utilities/DefaultOptional.h"

class Texture;

struct TextureAndConstant {
	TextureAndConstant() = default;
    TextureAndConstant(std::shared_ptr<Texture> tex, float f) : texture(tex), factor(f) {
    }
    TextureAndConstant(std::shared_ptr<Texture> tex, float f, std::vector<uint32_t> ch) : texture(tex), factor(f), channels(std::move(ch)) {
	}
    std::shared_ptr<Texture> texture;  // null if none
    DefaultedOptional<float> factor = DefaultedOptional<float>(1.0f);
	std::vector<uint32_t> channels; // For swizzling texture channels, e.g. R, G, B, A
};

struct MaterialDescription {
    std::string name;
    DirectX::XMFLOAT4   diffuseColor = { 1,1,1,1 };
    DirectX::XMFLOAT4   emissiveColor = { 0,0,0,1 };
	float alphaCutoff = 0.5f;
	bool negateNormals = false; // Some materials may require this
	bool invertNormalGreen = false; // For OpenGL compatibility
	BlendState blendState = BlendState::BLEND_STATE_UNKNOWN; // By default, infer from other properties
    TextureAndConstant  baseColor = {};
    TextureAndConstant  metallic = { nullptr, 0.0f };
    TextureAndConstant  roughness = { nullptr, 0.5f };
    TextureAndConstant  emissive = {};
    TextureAndConstant  opacity = { nullptr, 1.0f };
	TextureAndConstant  aoMap = {};
	TextureAndConstant  heightMap = {};
    TextureAndConstant	normal = {};
};

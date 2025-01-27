#include "ModelLoader.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <spdlog/spdlog.h>
#include <DirectXMath.h>
#include <vector>

#include "Material.h"
#include "MaterialFlags.h"
#include "PSOFlags.h"
#include "Sampler.h"

D3D12_TEXTURE_ADDRESS_MODE aiTextureMapModeToD3D12(aiTextureMapMode mode) {
    switch (mode) {
    case aiTextureMapMode_Wrap:
        return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        break;
    case aiTextureMapMode_Clamp:
        return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        break;
    case aiTextureMapMode_Mirror:
        return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        break;
    default:
        return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        break;
    }
}

// Helper: loads raw image data with stb_image, creates a PixelBuffer, then wraps in a Texture
static std::shared_ptr<Texture> loadAiTexture(
    const aiScene* scene,
    const std::string& texPath,          // Could be an embedded reference "*0" or a real file path
    const std::string& directory,        // Base directory for external textures
    std::shared_ptr<Sampler> sampler,    // Optional sampler
    bool sRGB
)
{
    // 1. Determine if texture is EMBEDDED or EXTERNAL
    if (!texPath.empty() && texPath[0] == '*')
    {
        // Embedded texture
        // Convert the number after '*' into an index
        unsigned int textureIndex = std::atoi(texPath.c_str() + 1); // skip '*'
        if (textureIndex >= scene->mNumTextures) {
            throw std::runtime_error("Embedded texture index out of range: " + texPath);
        }

        // Access the aiTexture
        aiTexture* aiTex = scene->mTextures[textureIndex];
        if (aiTex == nullptr) {
            throw std::runtime_error("Null embedded texture at index: " + std::to_string(textureIndex));
        }

        // If it's compressed (e.g. PNG/JPG), we need to decode from aiTex->pcData as memory
        if (aiTex->mHeight == 0)
        {
            // mHeight == 0 means it's compressed data in a single block of size mWidth
            int width, height, channels;
            stbi_uc* data = stbi_load_from_memory(
                reinterpret_cast<stbi_uc*>(aiTex->pcData),
                aiTex->mWidth, // size of the compressed data block
                &width, &height, &channels, 4
            );

            if (!data) {
                throw std::runtime_error("Failed to load embedded compressed texture: " + texPath);
            }

            // Convert to PixelBuffer
            TextureDescription desc;
            desc.width    = width;
            desc.height   = height;
            desc.channels = 4;
            desc.format   = DXGI_FORMAT_R8G8B8A8_UNORM; // TODO: SRGB?

            auto pBuffer = PixelBuffer::Create(desc, { data });
            stbi_image_free(data);

            return std::make_shared<Texture>(pBuffer, sampler);
        }
        else
        {
            // If mHeight != 0, then it's raw (uncompressed) pixels in BGRA format
            // aiTex->mWidth * aiTex->mHeight is the total resolution
            int width       = aiTex->mWidth;
            int height      = aiTex->mHeight;
            int channels    = 4;

            TextureDescription desc;
            desc.width    = width;
            desc.height   = height;
            desc.channels = channels;
            desc.format   = DXGI_FORMAT_R8G8B8A8_UNORM;

            // Create a container for the raw bytes
            // aiTex->pcData is an array of aiTexel => each aiTexel has b,g,r,a
            std::vector<uint8_t> rawData(width * height * channels);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    int idx = (y * width + x);
                    rawData[idx*4 + 0] = aiTex->pcData[idx].b;
                    rawData[idx*4 + 1] = aiTex->pcData[idx].g;
                    rawData[idx*4 + 2] = aiTex->pcData[idx].r;
                    rawData[idx*4 + 3] = aiTex->pcData[idx].a;
                }
            }

            auto pBuffer = PixelBuffer::Create(desc, { rawData.data() });
            return std::make_shared<Texture>(pBuffer, sampler);
        }
    }
    else
    {
        // EXTERNAL file: load from (directory + texPath)
        std::string fullPath = directory + "/" + texPath; 
        int width, height, channels;
        stbi_uc* data = stbi_load(fullPath.c_str(), &width, &height, &channels, 4);
        if (!data) {
            throw std::runtime_error("Failed to load external texture file: " + fullPath);
        }

        TextureDescription desc;
        desc.width    = width;
        desc.height   = height;
        desc.channels = 4;
        desc.format   = DXGI_FORMAT_R8G8B8A8_UNORM; 

        auto pBuffer = PixelBuffer::Create(desc, { data });
        stbi_image_free(data);

        return std::make_shared<Texture>(pBuffer, sampler);
    }
}

std::vector<std::shared_ptr<Material>> LoadMaterialsFromAssimpScene(
    const aiScene* scene,
    const std::string& directory,                     // folder containing the model
    bool sRGB
)
{
    std::vector<std::shared_ptr<Texture>> textures;
    std::vector<std::shared_ptr<Material>> materials;

    // To avoid reloading the same texture multiple times:
    std::unordered_map<std::string, std::shared_ptr<Texture>> loadedTextures;

    // Iterate over every material, check each texture slot
    for (unsigned int mIndex = 0; mIndex < scene->mNumMaterials; ++mIndex)
    {
        aiMaterial* mat = scene->mMaterials[mIndex];
        if (!mat) continue;

        // Example texture types we might care about:
        static const aiTextureType textureTypes[] = {
            aiTextureType_DIFFUSE,
            //aiTextureType_SPECULAR,
            aiTextureType_NORMALS,
            aiTextureType_METALNESS,       // for PBR extension
            aiTextureType_DIFFUSE_ROUGHNESS, // for PBR extension
            aiTextureType_EMISSIVE,
			aiTextureType_AMBIENT_OCCLUSION,
            aiTextureType_EMISSION_COLOR,
			aiTextureType_HEIGHT,
            aiTextureType_DISPLACEMENT,
        };
		std::unordered_map<aiTextureType, std::shared_ptr<Texture>> materialTextures;

        for (aiTextureType tType : textureTypes)
        {
            unsigned int texCount = mat->GetTextureCount(tType);
            if (texCount > 1) {
                throw std::exception("More than one texture per slot not yet supported");
            }
            for (unsigned int tIndex = 0; tIndex < texCount; ++tIndex)
            {
                aiTextureMapping mapping = {};
                ai_real blend = {};
                aiTextureOp op = {};
                aiTextureMapMode mapmode[2] = {};
                aiString aiTexPath = {};

                if (aiGetMaterialTexture(mat, tType, tIndex, &aiTexPath, &mapping, nullptr, &blend, &op, mapmode, nullptr) == AI_SUCCESS)
                {
                    std::string texPath = aiTexPath.C_Str(); // e.g. "*0" or "myTexture.png"
                    // Check if we already loaded it:
                    auto it = loadedTextures.find(texPath);
                    if (it == loadedTextures.end())
                    {
						D3D12_SAMPLER_DESC samplerDesc = {};
						samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
						samplerDesc.AddressU = aiTextureMapModeToD3D12(mapmode[0]);
						samplerDesc.AddressV = aiTextureMapModeToD3D12(mapmode[1]);
						samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // 3D textures not supported
						samplerDesc.MipLODBias = 0;
						samplerDesc.MaxAnisotropy = 1;
						samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
						samplerDesc.BorderColor[0] = 1.0f;
						samplerDesc.BorderColor[1] = 1.0f;
						samplerDesc.BorderColor[2] = 1.0f;
						samplerDesc.BorderColor[3] = 1.0f;
						samplerDesc.MinLOD = 0;
						samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;

						std::shared_ptr<Sampler> sampler = Sampler::CreateSampler(samplerDesc);

                        // Not loaded yet, load now
                        try {
                            std::shared_ptr<Texture> newTex = loadAiTexture(
                                scene, 
                                texPath, 
                                directory, 
                                sampler, 
                                sRGB
                            );
                            if (newTex) {
                                loadedTextures[texPath] = newTex;
								materialTextures[tType] = newTex;
                            }
                        }
                        catch (const std::exception& e) {
                            // handle or log the error
                            spdlog::error("Failed loading texture {}: {}", texPath, e.what());
                        }
                    }
                    else
                    {
                        // Already loaded, just use the existing one
						materialTextures[tType] = it->second;
                    }
                }
            }
        }

        UINT materialFlags = 0;
        UINT psoFlags = 0;

        // Basic properties: e.g., retrieve diffuse color
        aiColor4D diffuse(1.f, 1.f, 1.f, 1.f);
        mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);

        // Emissive factor (if available)
        aiColor3D emissive(0.f, 0.f, 0.f);
        mat->Get(AI_MATKEY_COLOR_EMISSIVE, emissive);

        float metallicFactor = 0.f;
        mat->Get(AI_MATKEY_METALLIC_FACTOR, metallicFactor);

        float roughnessFactor = 1.f;
        mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughnessFactor);

        std::shared_ptr<Texture> baseColorTexture      = nullptr;
        std::shared_ptr<Texture> normalTexture         = nullptr;
        std::shared_ptr<Texture> metallicTex  = nullptr;
		std::shared_ptr<Texture> roughnessTex = nullptr;
        std::shared_ptr<Texture> aoMap                 = nullptr;
        std::shared_ptr<Texture> emissiveTexture       = nullptr;
		std::shared_ptr<Texture> heightMap = nullptr;

		if (materialTextures.find(aiTextureType_DIFFUSE) != materialTextures.end()) {
			baseColorTexture = materialTextures[aiTextureType_DIFFUSE];
			materialFlags |= MaterialFlags::MATERIAL_BASE_COLOR_TEXTURE | MaterialFlags::MATERIAL_TEXTURED;
		}
		if (materialTextures.find(aiTextureType_BASE_COLOR) != materialTextures.end()) {
            if (baseColorTexture != nullptr) {
				spdlog::warn("Material {} has both BASE_COLOR and DIFFUSE textures. Using BASE_COLOR", mIndex);
            }
			baseColorTexture = materialTextures[aiTextureType_BASE_COLOR];
			materialFlags |= MaterialFlags::MATERIAL_BASE_COLOR_TEXTURE | MaterialFlags::MATERIAL_TEXTURED;
		}
		if (materialTextures.find(aiTextureType_NORMALS) != materialTextures.end()) {
			normalTexture = materialTextures[aiTextureType_NORMALS];
			materialFlags |= MaterialFlags::MATERIAL_NORMAL_MAP | MaterialFlags::MATERIAL_TEXTURED;
		}
		if (materialTextures.find(aiTextureType_METALNESS) != materialTextures.end()) {
            metallicTex = materialTextures[aiTextureType_METALNESS];
			materialFlags |= MaterialFlags::MATERIAL_PBR_MAPS | MaterialFlags::MATERIAL_TEXTURED;
		}
		if (materialTextures.find(aiTextureType_DIFFUSE_ROUGHNESS) != materialTextures.end()) {
			roughnessTex = materialTextures[aiTextureType_DIFFUSE_ROUGHNESS];
			materialFlags |= MaterialFlags::MATERIAL_PBR_MAPS | MaterialFlags::MATERIAL_TEXTURED;
		}
		if (materialTextures.find(aiTextureType_AMBIENT_OCCLUSION) != materialTextures.end()) {
			aoMap = materialTextures[aiTextureType_AMBIENT_OCCLUSION];
			materialFlags |= MaterialFlags::MATERIAL_AO_TEXTURE | MaterialFlags::MATERIAL_TEXTURED;
		}
		if (materialTextures.find(aiTextureType_EMISSION_COLOR) != materialTextures.end()) {
			emissiveTexture = materialTextures[aiTextureType_EMISSION_COLOR];
			materialFlags |= MaterialFlags::MATERIAL_EMISSIVE_TEXTURE | MaterialFlags::MATERIAL_TEXTURED;
		}
		if (materialTextures.find(aiTextureType_HEIGHT) != materialTextures.end()) {
			heightMap = materialTextures[aiTextureType_HEIGHT];
			materialFlags |= MaterialFlags::MATERIAL_PARALLAX | MaterialFlags::MATERIAL_TEXTURED;
		}
		if (materialTextures.find(aiTextureType_DISPLACEMENT) != materialTextures.end()) {
			if (heightMap != nullptr) {
				spdlog::warn("Material {} has both HEIGHT and DISPLACEMENT textures. Using DISPLACEMENT", mIndex);
			}
			heightMap = materialTextures[aiTextureType_DISPLACEMENT];
			materialFlags |= MaterialFlags::MATERIAL_PARALLAX | MaterialFlags::MATERIAL_TEXTURED;
		}


        // For alpha, blending, doubleSided
        float alphaCutoff       = 0.5f;
        BlendState blendMode    = BlendState::BLEND_STATE_OPAQUE;

        // If the material is two-sided
        bool twoSided = false;
        if (mat->Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS && twoSided) {
            materialFlags |= MaterialFlags::MATERIAL_DOUBLE_SIDED;
            psoFlags |= PSOFlags::PSO_DOUBLE_SIDED | PSO_ALPHA_TEST; // All double-sided materials are alpha-tested and vice-versa
            blendMode = BlendState::BLEND_STATE_MASK;
        }

        float opacity = 1.0f;
        if (mat->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS && opacity < 1.0f) {
            blendMode = BlendState::BLEND_STATE_BLEND;
            psoFlags |= PSOFlags::PSO_BLEND;
        }



        // Material name
        aiString matName;
        mat->Get(AI_MATKEY_NAME, matName);
        std::string mName = matName.C_Str();
        if (mName.empty()) mName = "Material_" + std::to_string(mIndex);

        // Convert AI colors to XMFLOAT4
        DirectX::XMFLOAT4 baseColorFactor(diffuse.r, diffuse.g, diffuse.b, diffuse.a);
        DirectX::XMFLOAT4 emissiveFactor(emissive.r, emissive.g, emissive.b, 1.f);

        auto newMaterial = std::make_shared<Material>(
            mName,
            materialFlags,
            psoFlags,
            baseColorTexture,
            normalTexture,
            aoMap,
            nullptr, // No height map here
            metallicTex,
            roughnessTex,
            emissiveTexture,
            metallicFactor,
            roughnessFactor,
            baseColorFactor,
            emissiveFactor,
            blendMode,
            alphaCutoff
        );
		materials.push_back(newMaterial);
    }

    return materials;
}

std::shared_ptr<Scene> LoadModel(std::string filePath) {
	Assimp::Importer importer;
	const aiScene* pScene = importer.ReadFile(filePath, aiProcess_Triangulate);

    if (!pScene || pScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !pScene->mRootNode) {
        spdlog::error("Model loading failed for {}. Error: {}", filePath, importer.GetErrorString());
        return nullptr;
    }

	auto scene = std::make_shared<Scene>();

	// Directory of the model file
	std::string directory = filePath.substr(0, filePath.find_last_of('/'));

    auto materials = LoadMaterialsFromAssimpScene(pScene, directory, false);
    return nullptr;
}
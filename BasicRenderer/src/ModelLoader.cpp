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
#include "Filetypes.h"
#include "Scene.h"
#include "Mesh.h"
#include "Skeleton.h"
#include "Components.h"
#include "AnimationController.h"

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
        unsigned int textureIndex = std::atoi(texPath.c_str() + 1);
        if (textureIndex >= scene->mNumTextures) {
            throw std::runtime_error("Embedded texture index out of range: " + texPath);
        }

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
			ImageDimensions dims;
			dims.width = width;
			dims.height = height;
			dims.rowPitch = width * 4;
			dims.slicePitch = width * height * 4;
			desc.imageDimensions.push_back(dims);
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
			ImageDimensions dims;
			dims.width = width;
			dims.height = height;
			dims.rowPitch = width * 4;
			dims.slicePitch = width * height * 4;
			desc.imageDimensions.push_back(dims);
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
        std::string fullPath = ws2s(GetExePath()) + "\\" + directory + "\\" + texPath;
        auto fileExtension = GetFileExtension(fullPath);
        ImageFiletype format = extensionToFiletype[fileExtension];
		ImageLoader loader = imageFiletypeToLoader[format];

        switch (loader) {
		case ImageLoader::STBImage:
			return loadTextureFromFileSTBI(fullPath, sampler);
		case ImageLoader::DirectXTex:
			return loadTextureFromFileDXT(s2ws(fullPath), sampler);
		default:
			throw std::runtime_error("Unsupported texture format: " + fullPath);
        }
    }
}

std::vector<std::shared_ptr<Material>> LoadMaterialsFromAssimpScene(
    const aiScene* scene,
    const std::string& directory, // folder containing the model
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

        // Ttexture types we might care about:
        static const aiTextureType textureTypes[] = {
            aiTextureType_DIFFUSE,
            //aiTextureType_SPECULAR,
            aiTextureType_NORMALS,
            aiTextureType_METALNESS,       // for PBR extension
            aiTextureType_DIFFUSE_ROUGHNESS, // for PBR extension
            aiTextureType_EMISSIVE,
			aiTextureType_AMBIENT_OCCLUSION,
            aiTextureType_LIGHTMAP,
            aiTextureType_EMISSIVE,
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
                    std::string texPath = aiTexPath.C_Str(); // e.g. "*0" or "texture.png"
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

        // Basic properties
        aiColor4D diffuse(1.f, 1.f, 1.f, 1.f);
        mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);

        // Emissive factor (if available)
        aiColor3D emissive(0.f, 0.f, 0.f);
        mat->Get(AI_MATKEY_COLOR_EMISSIVE, emissive);

        float metallicFactor = 0.f;
        mat->Get(AI_MATKEY_METALLIC_FACTOR, metallicFactor);

        float roughnessFactor = 1.f;
        mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughnessFactor);

		materialFlags |= MaterialFlags::MATERIAL_PBR; // TODO: Non-PBR materials

        // For alpha, blending, doubleSided
        float alphaCutoff       = 0.5f;
        BlendState blendMode    = BlendState::BLEND_STATE_OPAQUE;

        std::shared_ptr<Texture> baseColorTexture      = nullptr;
        std::shared_ptr<Texture> normalTexture         = nullptr;
        std::shared_ptr<Texture> metallicTex  = nullptr;
		std::shared_ptr<Texture> roughnessTex = nullptr;
        std::shared_ptr<Texture> aoMap                 = nullptr;
        std::shared_ptr<Texture> emissiveTexture       = nullptr;
		std::shared_ptr<Texture> heightMap = nullptr;

		if (materialTextures.find(aiTextureType_DIFFUSE) != materialTextures.end()) {
			baseColorTexture = materialTextures[aiTextureType_DIFFUSE];
			if (!baseColorTexture->AlphaIsAllOpaque()) {
                materialFlags |= MaterialFlags::MATERIAL_DOUBLE_SIDED;
				psoFlags |= PSOFlags::PSO_ALPHA_TEST;
                blendMode = BlendState::BLEND_STATE_MASK;
			}
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
            if (normalTexture->GetImageLoader() == ImageLoader::DirectXTex) {
				materialFlags |= MaterialFlags::MATERIAL_INVERT_NORMALS;
            }
		}
		if (materialTextures.find(aiTextureType_METALNESS) != materialTextures.end()) {
            metallicTex = materialTextures[aiTextureType_METALNESS];
			materialFlags |= MaterialFlags::MATERIAL_PBR | MaterialFlags::MATERIAL_PBR_MAPS | MaterialFlags::MATERIAL_TEXTURED;
		}
		if (materialTextures.find(aiTextureType_DIFFUSE_ROUGHNESS) != materialTextures.end()) {
			roughnessTex = materialTextures[aiTextureType_DIFFUSE_ROUGHNESS];
			materialFlags |= MaterialFlags::MATERIAL_PBR | MaterialFlags::MATERIAL_PBR_MAPS | MaterialFlags::MATERIAL_TEXTURED;
		}
		if (materialTextures.find(aiTextureType_AMBIENT_OCCLUSION) != materialTextures.end()) {
			aoMap = materialTextures[aiTextureType_AMBIENT_OCCLUSION];
			materialFlags |= MaterialFlags::MATERIAL_AO_TEXTURE | MaterialFlags::MATERIAL_TEXTURED;
		}
        if (materialTextures.find(aiTextureType_LIGHTMAP) != materialTextures.end()) {
			if (aoMap != nullptr) {
				spdlog::warn("Material {} has both AMBIENT_OCCLUSION and LIGHTMAP textures. Using LIGHTMAP", mIndex);
			}
            aoMap = materialTextures[aiTextureType_LIGHTMAP];
            materialFlags |= MaterialFlags::MATERIAL_AO_TEXTURE | MaterialFlags::MATERIAL_TEXTURED;
        }

        if (materialTextures.find(aiTextureType_EMISSIVE) != materialTextures.end()) {
            emissiveTexture = materialTextures[aiTextureType_EMISSIVE];
            materialFlags |= MaterialFlags::MATERIAL_EMISSIVE_TEXTURE | MaterialFlags::MATERIAL_TEXTURED;
        }
		if (materialTextures.find(aiTextureType_EMISSION_COLOR) != materialTextures.end()) {
            if (emissiveTexture != nullptr) {
                spdlog::warn("Material {} has both EMISSION_COLOR and EMISSIVE textures. Using EMISSION_COLOR", mIndex);
            }
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
			diffuse.a *= opacity;
        }

		float transparencyFactor = 0.0f;
        if (mat->Get(AI_MATKEY_TRANSPARENCYFACTOR, transparencyFactor) == AI_SUCCESS && transparencyFactor > 0.0f) {
			blendMode = BlendState::BLEND_STATE_BLEND;
			psoFlags |= PSOFlags::PSO_BLEND;
        }

        aiString matName;
        mat->Get(AI_MATKEY_NAME, matName);
        std::string mName = matName.C_Str();
        if (mName.empty()) mName = "Material_" + std::to_string(mIndex);

        DirectX::XMFLOAT4 baseColorFactor(diffuse.r, diffuse.g, diffuse.b, diffuse.a);
        DirectX::XMFLOAT4 emissiveFactor(emissive.r, emissive.g, emissive.b, 1.f);

        auto newMaterial = std::make_shared<Material>(
            mName,
            materialFlags,
            psoFlags,
            baseColorTexture,
            normalTexture,
            aoMap,
			nullptr, // TODO: heightMap
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

static std::pair<std::vector<std::shared_ptr<Mesh>>, std::vector<int>> parseAiMeshes(
    const aiScene* pScene,
    const std::vector<std::shared_ptr<Material>>& materials
) 
{
    std::vector<std::shared_ptr<Mesh>> meshes;
    meshes.reserve(pScene->mNumMeshes);
	std::vector<int> meshSkinIndices;
	meshSkinIndices.reserve(pScene->mNumMeshes);

	// Assimp doesn't have a concept of a "skin" like glTF, so we'll just increment a counter
	// each time we encounter a mesh with bones.
	int currentSkinIndex = -1;
    for (unsigned int i = 0; i < pScene->mNumMeshes; ++i) {
        aiMesh* aMesh = pScene->mMeshes[i];

        MeshData geometry;

        if (aMesh->HasBones()) {
            currentSkinIndex++;
            geometry.skinIndex = currentSkinIndex;
			meshSkinIndices.push_back(currentSkinIndex);
		}
		else {
			geometry.skinIndex = -1;
			meshSkinIndices.push_back(-1);
		}

        // Positions
        geometry.positions.reserve(aMesh->mNumVertices * 3);
        for (unsigned int v = 0; v < aMesh->mNumVertices; v++) {
            const auto& vec = aMesh->mVertices[v];
            geometry.positions.push_back(vec.x);
            geometry.positions.push_back(vec.y);
            geometry.positions.push_back(vec.z);
        }

        // Normals (if present)
        if (aMesh->HasNormals()) {
            geometry.normals.reserve(aMesh->mNumVertices * 3);
            for (unsigned int v = 0; v < aMesh->mNumVertices; v++) {
                const auto& n = aMesh->mNormals[v];
                geometry.normals.push_back(n.x);
                geometry.normals.push_back(n.y);
                geometry.normals.push_back(n.z);
            }
            geometry.flags |= VertexFlags::VERTEX_NORMALS;
        }

        // Texture coords (only load the first set)
        if (aMesh->HasTextureCoords(0)) {
            geometry.texcoords.reserve(aMesh->mNumVertices * 2);
            for (unsigned int v = 0; v < aMesh->mNumVertices; v++) {
                const auto& uv = aMesh->mTextureCoords[0][v];
                geometry.texcoords.push_back(uv.x);
                geometry.texcoords.push_back(uv.y);
            }
            geometry.flags |= VertexFlags::VERTEX_TEXCOORDS;
        }

        // Indices
        for (unsigned int f = 0; f < aMesh->mNumFaces; f++) {
            const aiFace& face = aMesh->mFaces[f];
            for (unsigned int idx = 0; idx < face.mNumIndices; idx++) {
                geometry.indices.push_back(face.mIndices[idx]);
            }
        }

        // Material reference
        if (aMesh->mMaterialIndex < materials.size()) {
            geometry.material = materials[aMesh->mMaterialIndex];
        } else {
            // fallback if no valid material index
            geometry.material = nullptr;
        }

        // If the mesh has bones, we fill out geometry.joints + geometry.weights
        if (aMesh->HasBones()) {
            geometry.flags |= VertexFlags::VERTEX_SKINNED;
            geometry.joints.resize(aMesh->mNumVertices * 4, 0);
            geometry.weights.resize(aMesh->mNumVertices * 4, 0.f);

            // We'll accumulate up to 4 influences per vertex
            std::vector<unsigned int> jointCount(aMesh->mNumVertices, 0);

            for (unsigned int b = 0; b < aMesh->mNumBones; b++) {
                aiBone* bone = aMesh->mBones[b];
                // We'll store bone->mOffsetMatrix in an array for skeleton usage
                // We'll handle skeleton creation later.

                for (unsigned int w = 0; w < bone->mNumWeights; w++) {
                    const aiVertexWeight& vw = bone->mWeights[w];
                    unsigned int vertexID = vw.mVertexId;
                    float weight = vw.mWeight;

                    unsigned int& count = jointCount[vertexID];
					if (count < 4 && weight > 0.0f) { // Why does assimp pollute vertex 0 with a bunch of zero-weight influences?
                        geometry.joints[vertexID * 4 + count] = b;  // bone index
                        geometry.weights[vertexID * 4 + count] = weight;
                        count++;
                    } else if (weight > 0.0f){
						throw std::runtime_error("Vertex has more than 4 non-zero bone influences");
                    }
                }
            }
        }

        meshes.push_back(MeshFromData(geometry, s2ws(aMesh->mName.C_Str())));
    }

    return { meshes , meshSkinIndices};
}

static void buildAiNodeHierarchy(
    std::shared_ptr<Scene> scene,
    aiNode* ainode,
    const aiScene* pScene,
    const std::vector<std::shared_ptr<Mesh>>& meshes,
    std::vector<flecs::entity>& outNodes,
    std::unordered_map<std::string, flecs::entity>& nodeMap,
	flecs::entity parent = flecs::entity()) {

    std::string nodeName(ainode->mName.C_Str());

    // For local transform: aiNode->mTransformation is a 4x4 matrix
    // Convert it to an XMMATRIX
    aiMatrix4x4 m = ainode->mTransformation;

	XMMATRIX transform = XMMatrixSet( // Transpose
        m.a1, m.b1, m.c1, m.d1,
        m.a2, m.b2, m.c2, m.d2,
        m.a3, m.b3, m.c3, m.d3,
        m.a4, m.b4, m.c4, m.d4
    );

    flecs::entity entity;
	// handle multiple meshes per node
    if (ainode->mNumMeshes > 0) {
		std::vector<std::shared_ptr<Mesh>> objectMeshes;
        objectMeshes.reserve(ainode->mNumMeshes);
        for (unsigned int i = 0; i < ainode->mNumMeshes; i++) {
            int meshIndex = ainode->mMeshes[i];
            objectMeshes.push_back(meshes[meshIndex]);
        }
		entity = scene->CreateRenderableEntityECS(objectMeshes, s2ws(nodeName));
		//node = scene->CreateRenderableObject(objectMeshes, s2ws(nodeName));
	}
	else {
		entity = scene->CreateNodeECS(s2ws(nodeName));
		//node = scene->CreateNode(s2ws(nodeName));
    }

    // Decompose the transform into T/R/S
    XMVECTOR s, r, t;
    XMMatrixDecompose(&s, &r, &t, transform);

    // Set the local transform
    entity.set<Components::Rotation>({ r });
	entity.set<Components::Position>({ t });
	entity.set<Components::Scale>({ s });

    // Attach to parent
    if (parent) {
		entity.child_of(parent);
    }
    else {
		spdlog::warn("Node {} has no parent", nodeName);
    }
    outNodes.push_back(entity);

	nodeMap[nodeName] = entity;
    // Recursively process children
    for (unsigned int c = 0; c < ainode->mNumChildren; ++c) {
        buildAiNodeHierarchy(scene, ainode->mChildren[c], pScene, meshes, outNodes, nodeMap, entity);
    }
}

static std::vector<std::shared_ptr<Animation>> parseAiAnimations(
    const aiScene* pScene,
    const std::vector<flecs::entity>& nodes,
	const std::unordered_map<std::string, flecs::entity>& nodeMap
)
{
    std::vector<std::shared_ptr<Animation>> animations;
    animations.reserve(pScene->mNumAnimations);

    for (unsigned int i = 0; i < pScene->mNumAnimations; i++) {
        aiAnimation* aiAnim = pScene->mAnimations[i];
        std::string animName = aiAnim->mName.length > 0 ? aiAnim->mName.C_Str() : ("Anim_" + std::to_string(i));

        auto animation = std::make_shared<Animation>(animName);

        // Each aiAnimation has channels -> each channel is for one node
        for (unsigned int c = 0; c < aiAnim->mNumChannels; c++) {
            aiNodeAnim* channel = aiAnim->mChannels[c];
            std::string nodeName = channel->mNodeName.C_Str();

            // Find the node that matches nodeName
			flecs::entity node;
			bool found = false;
			if (nodeMap.find(nodeName) != nodeMap.end()) {
                node = nodeMap.at(nodeName);
				found = true;
            }

            if (!found) {
                // This channel references a node we didn’t find
				spdlog::warn("Animation {} references unknown node: {}", animName, nodeName);
                continue;
            }

            // Ensure we have an AnimationClip for that node in this animation
            if (animation->nodesMap.find(node.name().c_str()) == animation->nodesMap.end()) {
                animation->nodesMap[node.name().c_str()] = std::make_shared<AnimationClip>();
            }
            auto& clip = animation->nodesMap[node.name().c_str()];

            // For position keys
            for (unsigned int k = 0; k < channel->mNumPositionKeys; k++) {
                float time = static_cast<float>(channel->mPositionKeys[k].mTime)/aiAnim->mTicksPerSecond;
                const aiVector3D& v = channel->mPositionKeys[k].mValue;
                clip->addPositionKeyframe(time, XMFLOAT3(v.x, v.y, v.z));
            }

            // For rotation keys
            for (unsigned int k = 0; k < channel->mNumRotationKeys; k++) {
                float time = static_cast<float>(channel->mRotationKeys[k].mTime)/aiAnim->mTicksPerSecond;
                const aiQuaternion& q = channel->mRotationKeys[k].mValue;
                // Convert to XMVECTOR
                XMVECTOR quat = XMVectorSet(q.x, q.y, q.z, q.w);
                clip->addRotationKeyframe(time, quat);
            }

            // For scale keys
            for (unsigned int k = 0; k < channel->mNumScalingKeys; k++) {
                float time = static_cast<float>(channel->mScalingKeys[k].mTime)/aiAnim->mTicksPerSecond;
                const aiVector3D& s = channel->mScalingKeys[k].mValue;
                clip->addScaleKeyframe(time, XMFLOAT3(s.x, s.y, s.z));
            }
        }

        animations.push_back(animation);
    }

    return animations;
}

static std::shared_ptr<Skeleton> parseSkeletonForMesh(
    aiMesh* aMesh,
    const std::unordered_map<std::string, flecs::entity>& nodeMap,
    const std::vector<std::shared_ptr<Animation>>& animations) {
    if (!aMesh->HasBones()) {
		return nullptr;
    }
    // Collect inverse bind matrices for each bone
    std::vector<XMMATRIX> inverseBindMatrices;
    inverseBindMatrices.reserve(aMesh->mNumBones);
    std::vector<flecs::entity> jointNodes;
    jointNodes.reserve(aMesh->mNumBones);

    for (unsigned int b = 0; b < aMesh->mNumBones; b++) {
        aiBone* bone = aMesh->mBones[b];

        // Convert offset matrix
        aiMatrix4x4 o = bone->mOffsetMatrix;
        XMMATRIX offset = XMMatrixSet( // Transpose
            o.a1, o.b1, o.c1, o.d1,
            o.a2, o.b2, o.c2, o.d2,
            o.a3, o.b3, o.c3, o.d3,
            o.a4, o.b4, o.c4, o.d4
        );

        inverseBindMatrices.push_back(offset);

        // Find the node that corresponds to this bone
        std::string boneName = bone->mName.C_Str();
		flecs::entity boneNode;
		bool found = false;
        if (nodeMap.find(boneName) != nodeMap.end()) {
			boneNode = nodeMap.at(boneName);
			found = true;
        }
		if (!found) {
			// This bone doesn't match any node
			spdlog::error("Bone {} doesn't match any node", boneName);
			throw std::runtime_error("Bone doesn't match any node");
		}
        if (!boneNode.has<AnimationController>()) {
			// Create a new AnimationController for this bone
			boneNode.add<AnimationController>();
			boneNode.set<Components::AnimationName>({ boneName });
		}
        jointNodes.push_back(boneNode);
    }

    auto skeleton = std::make_shared<Skeleton>(jointNodes, inverseBindMatrices);

    // Associate animations that reference these bones
    for (unsigned int b = 0; b < aMesh->mNumBones; b++) {
        auto jointNode = jointNodes[b];
        if (!jointNode) continue;
        for (auto& anim : animations) {
            if (anim->nodesMap.find(jointNode.name().c_str()) != anim->nodesMap.end()) {
                skeleton->AddAnimation(anim);
            }
        }
    }

    return skeleton;
}

std::vector<std::shared_ptr<Skeleton>> BuildSkeletons(const aiScene* pScene,     
    const std::unordered_map<std::string, flecs::entity>& nodeMap,
    const std::vector<std::shared_ptr<Animation>>& animations) {
	std::vector<std::shared_ptr<Skeleton>> skeletons;
	// For each mesh, parse the skeleton
	for (unsigned int i = 0; i < pScene->mNumMeshes; i++) {
		aiMesh* aMesh = pScene->mMeshes[i];
		auto skeleton = parseSkeletonForMesh(aMesh, nodeMap, animations);
		if (skeleton) {
			skeletons.push_back(skeleton);
		}
	}
	return skeletons;
}

std::shared_ptr<Scene> LoadModel(std::string filePath) {
	Assimp::Importer importer;
	const aiScene* pScene = importer.ReadFile(filePath, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_OptimizeGraph | aiProcess_OptimizeMeshes);

    if (!pScene || pScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !pScene->mRootNode) {
        spdlog::error("Model loading failed for {}. Error: {}", filePath, importer.GetErrorString());
        return nullptr;
    }

	auto scene = std::make_shared<Scene>();

	// Directory of the model file
	std::string directory = filePath.substr(0, filePath.find_last_of('/'));

    auto materials = LoadMaterialsFromAssimpScene(pScene, directory, false);
	auto [meshes, meshSkinIndices] = parseAiMeshes(pScene, materials);
	std::vector<flecs::entity> nodes;
	std::unordered_map<std::string, flecs::entity> nodeMap;
	buildAiNodeHierarchy(scene, pScene->mRootNode, pScene, meshes, nodes, nodeMap);
    
	auto animations = parseAiAnimations(pScene, nodes, nodeMap);
	auto skeletons = BuildSkeletons(pScene, nodeMap, animations);

	for (auto& skeleton : skeletons) {
		scene->AddSkeleton(skeleton);
	}

    for (int i = 0; i < meshSkinIndices.size(); i++) {
		int skinIndex = meshSkinIndices[i];
        if (skinIndex != -1) {
			meshes[i]->SetBaseSkin(skeletons[skinIndex]);
        }
	}

    scene->ProcessEntitySkins();

    return scene;
}
#pragma once

#include <memory>
#include <string>

#include "ShaderBuffers.h"

class TextureAsset;
class BufferView;
class EnvironmentManager;
class Environment {
public:
	Environment(EnvironmentManager* manager, std::wstring name = L"") : m_currentManager(manager), m_name(name) {
	}

	std::shared_ptr<TextureAsset>& GetEnvironmentCubemap() {
		return m_environmentCubemap;
	}

	std::shared_ptr<TextureAsset>& GetEnvironmentPrefilteredCubemap() {
		return m_environmentPrefilteredCubemap;
	}

	BufferView* GetEnvironmentBufferView() const {
		return m_environmentBufferView.get();
	}

	std::shared_ptr<TextureAsset>& GetHDRITexture() {
		return m_hdriTexture;
	}

	void SetHDRI(std::shared_ptr<TextureAsset> hdriTexture);

	unsigned int GetEnvironmentIndex() const;

	unsigned int GetReflectionCubemapResolution() const {
		return reflectionCubemapResolution;
	}

	const std::wstring& GetName() {
		return m_name;
	}

private:
	EnvironmentInfo m_environmentInfo = {};
	EnvironmentManager* m_currentManager;
	std::wstring m_name;
	std::shared_ptr<TextureAsset> m_hdriTexture; // Optional
	std::shared_ptr<TextureAsset> m_environmentCubemap; // Generated from HDRI or rendered
	std::shared_ptr<TextureAsset> m_environmentPrefilteredCubemap; // Generated from environment cubemap
	std::shared_ptr<BufferView> m_environmentBufferView; // Includes spherical harmonics

	unsigned int reflectionCubemapResolution = 512;

	void SetEnvironmentCubemap(std::shared_ptr<TextureAsset> texture);

	void SetEnvironmentPrefilteredCubemap(std::shared_ptr<TextureAsset> texture);

	void SetEnvironmentBufferView(std::shared_ptr<BufferView> bufferView) {
		m_environmentBufferView = bufferView;
	}

	void SetReflectionCubemapResolution(unsigned int resolution);

	friend class EnvironmentManager;
};
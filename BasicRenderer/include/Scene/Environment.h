#pragma once

#include <memory>

#include "ShaderBuffers.h"

class Texture;
class BufferView;
class EnvironmentManager;
class Environment {
public:
	Environment(EnvironmentManager* manager) : m_currentManager(manager) {
	}

	std::shared_ptr<Texture>& GetEnvironmentCubemap() {
		return m_environmentCubemap;
	}

	std::shared_ptr<Texture>& GetEnvironmentPrefilteredCubemap() {
		return m_environmentPrefilteredCubemap;
	}

	BufferView* GetEnvironmentBufferView() const {
		return m_environmentBufferView.get();
	}

	std::shared_ptr<Texture>& GetHDRITexture() {
		return m_hdriTexture;
	}

	void SetFromHDRI(std::shared_ptr<Texture> hdriTexture);

	unsigned int GetEnvironmentIndex() const;

private:
	EnvironmentManager* m_currentManager;
	std::shared_ptr<Texture> m_hdriTexture; // Optional
	std::shared_ptr<Texture> m_environmentCubemap; // Generated from HDRI or rendered
	std::shared_ptr<Texture> m_environmentPrefilteredCubemap; // Generated from environment cubemap
	std::shared_ptr<BufferView> m_environmentBufferView; // Includes spherical harmonics

	void SetEnvironmentCubemap(std::shared_ptr<Texture> texture) {
		m_environmentCubemap = texture;
	}

	void SetEnvironmentPrefilteredCubemap(std::shared_ptr<Texture> texture) {
		m_environmentPrefilteredCubemap = texture;
	}

	void SetEnvironmentBufferView(std::shared_ptr<BufferView> bufferView) {
		m_environmentBufferView = bufferView;
	}

	friend class EnvironmentManager;
};
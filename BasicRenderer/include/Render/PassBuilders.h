#pragma once

#include "RenderGraph.h"

class RenderPassBuilder {
public:
    // Variadic entry points
    template<typename... Args>
    RenderPassBuilder& WithShaderResource(Args&&... args) {
        (addShaderResource(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    RenderPassBuilder& WithRenderTarget(Args&&... args) {
        (addRenderTarget(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    RenderPassBuilder& WithDepthTarget(Args&&... args) {
        (addDepthTarget(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    RenderPassBuilder& WithConstantBuffer(Args&&... args) {
        (addConstantBuffer(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    RenderPassBuilder& WithUnorderedAccess(Args&&... args) {
        (addUnorderedAccess(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    RenderPassBuilder& WithCopyDest(Args&&... args) {
        (addCopyDest(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    RenderPassBuilder& WithCopySource(Args&&... args) {
        (addCopySource(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    RenderPassBuilder& WithIndirectArguments(Args&&... args) {
        (addIndirectArguments(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename PassT, typename... CtorArgs>
    std::shared_ptr<PassT> Build(CtorArgs&&... args) {
        // construct the pass
        auto pass = std::make_shared<PassT>(std::forward<CtorArgs>(args)...);
        // record into the graph
        graph.AddRenderPass(pass, params, passName);
        return pass;
    }

private:
    RenderPassBuilder(RenderGraph& g, std::string name)
        : graph(g), passName(std::move(name)) {}

    // Single resource overload
    void addShaderResource(const std::shared_ptr<Resource>& r) {
        params.shaderResources.push_back(r);
    }

    // initializer list overload
    void addShaderResource(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list)
            params.shaderResources.push_back(r);
    }

	// Render target
    void addRenderTarget(const std::shared_ptr<Resource>& r) {
        params.renderTargets.push_back(r);
    }

    void addRenderTarget(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list)
            params.renderTargets.push_back(r);
    }

	// Depth target
	void addDepthTarget(const std::shared_ptr<Resource>& r) {
		params.depthTextures.push_back(r);
	}

	void addDepthTarget(std::initializer_list<std::shared_ptr<Resource>> list) {
		for (auto& r : list)
			params.depthTextures.push_back(r);
	}

	// Constant buffer
	void addConstantBuffer(const std::shared_ptr<Resource>& r) {
		params.constantBuffers.push_back(r);
	}

	void addConstantBuffer(std::initializer_list<std::shared_ptr<Resource>> list) {
		for (auto& r : list)
			params.constantBuffers.push_back(r);
	}

	// Unordered access
	void addUnorderedAccess(const std::shared_ptr<Resource>& r) {
		params.unorderedAccessViews.push_back(r);
	}

	void addUnorderedAccess(std::initializer_list<std::shared_ptr<Resource>> list) {
		for (auto& r : list)
			params.unorderedAccessViews.push_back(r);
	}

	// Copy destination
	void addCopyDest(const std::shared_ptr<Resource>& r) {
		params.copyTargets.push_back(r);
	}

	void addCopyDest(std::initializer_list<std::shared_ptr<Resource>> list) {
		for (auto& r : list)
			params.copyTargets.push_back(r);
	}

	// Copy source
	void addCopySource(const std::shared_ptr<Resource>& r) {
		params.copySources.push_back(r);
	}

	void addCopySource(std::initializer_list<std::shared_ptr<Resource>> list) {
		for (auto& r : list)
			params.copySources.push_back(r);
	}

	// Indirect arguments
	void addIndirectArguments(const std::shared_ptr<Resource>& r) {
		params.indirectArgumentBuffers.push_back(r);
	}

	void addIndirectArguments(std::initializer_list<std::shared_ptr<Resource>> list) {
		for (auto& r : list)
			params.indirectArgumentBuffers.push_back(r);
	}

    // storage
    RenderGraph&             graph;
    std::string              passName;
    RenderPassParameters     params;

    friend class RenderGraph;
};

class ComputePassBuilder {
public:
    // Variadic entry points
    template<typename... Args>
    ComputePassBuilder& WithShaderResource(Args&&... args) {
        (addShaderResource(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    ComputePassBuilder& WithConstantBuffer(Args&&... args) {
        (addConstantBuffer(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    ComputePassBuilder& WithUnorderedAccess(Args&&... args) {
        (addUnorderedAccess(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename PassT, typename... CtorArgs>
    std::shared_ptr<PassT> Build(CtorArgs&&... args) {
        // construct the pass
        auto pass = std::make_shared<PassT>(std::forward<CtorArgs>(args)...);
        // record into the graph
        graph.AddComputePass(pass, params, passName);
        return pass;
    }

private:
    ComputePassBuilder(RenderGraph& g, std::string name)
        : graph(g), passName(std::move(name)) {}

    // Single resource overload
    void addShaderResource(const std::shared_ptr<Resource>& r) {
        params.shaderResources.push_back(r);
    }

    // initializer list overload
    void addShaderResource(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list)
            params.shaderResources.push_back(r);
    }

    // Constant buffer
    void addConstantBuffer(const std::shared_ptr<Resource>& r) {
        params.constantBuffers.push_back(r);
    }

    void addConstantBuffer(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list)
            params.constantBuffers.push_back(r);
    }

    // Unordered access
    void addUnorderedAccess(const std::shared_ptr<Resource>& r) {
        params.unorderedAccessViews.push_back(r);
    }

    void addUnorderedAccess(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list)
            params.unorderedAccessViews.push_back(r);
    }

    // storage
    RenderGraph&             graph;
    std::string              passName;
    ComputePassParameters     params;

    friend class RenderGraph;
};
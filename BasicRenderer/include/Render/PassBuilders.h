#pragma once

#include "RenderGraph.h"

class RenderPassBuilder {
public:
    // Variadic entry points

    //First set, callable on Lvalues
    template<typename... Args>
    RenderPassBuilder& WithShaderResource(Args&&... args) & {
        (addShaderResource(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    RenderPassBuilder& WithRenderTarget(Args&&... args) & {
        (addRenderTarget(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    RenderPassBuilder& WithDepthTarget(Args&&... args) & {
        (addDepthTarget(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    RenderPassBuilder& WithConstantBuffer(Args&&... args) & {
        (addConstantBuffer(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    RenderPassBuilder& WithUnorderedAccess(Args&&... args) & {
        (addUnorderedAccess(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    RenderPassBuilder& WithCopyDest(Args&&... args) & {
        (addCopyDest(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    RenderPassBuilder& WithCopySource(Args&&... args) & {
        (addCopySource(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    RenderPassBuilder& WithIndirectArguments(Args&&... args) & {
        (addIndirectArguments(std::forward<Args>(args)), ...);
        return *this;
    }

    // Second set, callable on temporaries
    template<typename... Args>
    RenderPassBuilder WithShaderResource(Args&&... args) && {
        (addShaderResource(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
    RenderPassBuilder WithRenderTarget(Args&&... args) && {
        (addRenderTarget(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
    RenderPassBuilder WithDepthTarget(Args&&... args) && {
        (addDepthTarget(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
    RenderPassBuilder WithConstantBuffer(Args&&... args) && {
        (addConstantBuffer(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
    RenderPassBuilder WithUnorderedAccess(Args&&... args) && {
        (addUnorderedAccess(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
    RenderPassBuilder WithCopyDest(Args&&... args) && {
        (addCopyDest(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
    RenderPassBuilder WithCopySource(Args&&... args) && {
        (addCopySource(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
    RenderPassBuilder WithIndirectArguments(Args&&... args) && {
        (addIndirectArguments(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    // First build, callable on Lvalues
    template<typename PassT, typename... CtorArgs>
    RenderPassBuilder& Build(CtorArgs&&... args) & {
        ensureNotBuilt();
        built_ = true;

        auto pass = std::make_shared<PassT>(std::forward<CtorArgs>(args)...);
        graph.AddRenderPass(pass, params, passName);

        return *this;
    }

	// Second build, callable on temporaries
    template<typename PassT, typename... CtorArgs>
    RenderPassBuilder Build(CtorArgs&&... args) && {
        ensureNotBuilt();
        built_ = true;

        auto pass = std::make_shared<PassT>(std::forward<CtorArgs>(args)...);
        graph.AddRenderPass(pass, params, passName);

        return std::move(*this);
    }

private:
    RenderPassBuilder(RenderGraph& g, std::string name)
        : graph(g), passName(std::move(name)) {}

    // Single resource overload
    void addShaderResource(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        params.shaderResources.push_back(r);
    }

    // initializer list overload
    void addShaderResource(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            params.shaderResources.push_back(r);
        }
    }

	// Render target
    void addRenderTarget(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        params.renderTargets.push_back(r);
    }

    void addRenderTarget(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            params.shaderResources.push_back(r);
        }
    }

	// Depth target
	void addDepthTarget(const std::shared_ptr<Resource>& r) {
        if (!r) return;
		params.depthTextures.push_back(r);
	}

	void addDepthTarget(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            params.shaderResources.push_back(r);
        }
	}

	// Constant buffer
	void addConstantBuffer(const std::shared_ptr<Resource>& r) {
        if (!r) return;
		params.constantBuffers.push_back(r);
	}

	void addConstantBuffer(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            params.shaderResources.push_back(r);
        }
	}

	// Unordered access
	void addUnorderedAccess(const std::shared_ptr<Resource>& r) {
        if (!r) return;
		params.unorderedAccessViews.push_back(r);
	}

	void addUnorderedAccess(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            params.shaderResources.push_back(r);
        }
	}

	// Copy destination
	void addCopyDest(const std::shared_ptr<Resource>& r) {
        if (!r) return;
		params.copyTargets.push_back(r);
	}

	void addCopyDest(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            params.shaderResources.push_back(r);
        }
	}

	// Copy source
	void addCopySource(const std::shared_ptr<Resource>& r) {
        if (!r) return;
		params.copySources.push_back(r);
	}

	void addCopySource(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            params.shaderResources.push_back(r);
        }
	}

	// Indirect arguments
	void addIndirectArguments(const std::shared_ptr<Resource>& r) {
        if (!r) return;
		params.indirectArgumentBuffers.push_back(r);
	}

	void addIndirectArguments(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            params.shaderResources.push_back(r);
        }
	}

    void ensureNotBuilt() const {
        if (built_) throw std::runtime_error("RenderPassBuilder::Build() may only be called once");
    }

    // storage
    RenderGraph&             graph;
    std::string              passName;
    RenderPassParameters     params;
    bool built_ = false;

    friend class RenderGraph;
};

class ComputePassBuilder {
public:
    // Variadic entry points
    
    //First set, callable on Lvalues
    template<typename... Args>
    ComputePassBuilder& WithShaderResource(Args&&... args) & {
        (addShaderResource(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    ComputePassBuilder& WithConstantBuffer(Args&&... args) & {
        (addConstantBuffer(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    ComputePassBuilder& WithUnorderedAccess(Args&&... args) & {
        (addUnorderedAccess(std::forward<Args>(args)), ...);
        return *this;
    }

	// Second set, callable on temporaries
    template<typename... Args>
    ComputePassBuilder WithShaderResource(Args&&... args) && {
        (addShaderResource(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
    ComputePassBuilder WithConstantBuffer(Args&&... args) && {
        (addConstantBuffer(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
    ComputePassBuilder WithUnorderedAccess(Args&&... args) && {
        (addUnorderedAccess(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    // First build, callable on Lvalues
    template<typename PassT, typename... CtorArgs>
    ComputePassBuilder& Build(CtorArgs&&... args) & {
        ensureNotBuilt();
        built_ = true;

        auto pass = std::make_shared<PassT>(std::forward<CtorArgs>(args)...);
        graph.AddComputePass(pass, params, passName);

        return *this;
    }

    // Second build, callable on temporaries
    template<typename PassT, typename... CtorArgs>
    ComputePassBuilder Build(CtorArgs&&... args) && {
        ensureNotBuilt();
        built_ = true;

        auto pass = std::make_shared<PassT>(std::forward<CtorArgs>(args)...);
        graph.AddComputePass(pass, params, passName);

        return std::move(*this);
    }

private:
    ComputePassBuilder(RenderGraph& g, std::string name)
        : graph(g), passName(std::move(name)) {}

    // Single resource overload
    void addShaderResource(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        params.shaderResources.push_back(r);
    }

    // initializer list overload
    void addShaderResource(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            params.shaderResources.push_back(r);
        }
    }

    // Constant buffer
    void addConstantBuffer(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        params.constantBuffers.push_back(r);
    }

    void addConstantBuffer(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            params.shaderResources.push_back(r);
        }
    }

    // Unordered access
    void addUnorderedAccess(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        params.unorderedAccessViews.push_back(r);
    }

    void addUnorderedAccess(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            params.shaderResources.push_back(r);
        }
    }

    void ensureNotBuilt() const {
        if (built_) throw std::runtime_error("ComputePassBuilder::Build() may only be called once");
    }

    // storage
    RenderGraph&             graph;
    std::string              passName;
    ComputePassParameters     params;
    bool built_ = false;

    friend class RenderGraph;
};
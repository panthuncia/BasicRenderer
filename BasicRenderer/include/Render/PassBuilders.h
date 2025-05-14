#pragma once

#include "RenderGraph.h"
#include "ResourceRequirements.h"
#include "Resources/ResourceStateTracker.h"

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
    RenderPassBuilder& WithDepthRead(Args&&... args) & {
        (addDepthRead(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
    RenderPassBuilder& WithDepthReadWrite(Args&&... args) & {
        (addDepthReadWrite(std::forward<Args>(args)), ...);
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
    RenderPassBuilder WithDepthReadWrite(Args&&... args) && {
        (addDepthReadWrite(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
    RenderPassBuilder WithDepthRead(Args&&... args) && {
        (addDepthRead(std::forward<Args>(args)), ...);
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

	RenderPassBuilder& IsGeometryPass()& {
		params.isGeometryPass = true;
		return *this;
	}

    RenderPassBuilder IsGeometryPass() && {
        params.isGeometryPass = true;
		return std::move(*this);
    }

    // First build, callable on Lvalues
    template<typename PassT, typename... CtorArgs>
    RenderPassBuilder& Build(CtorArgs&&... args) & {
        ensureNotBuilt();
        built_ = true;

        params.resourceRequirements = GatherResourceRequirements();
        auto pass = std::make_shared<PassT>(std::forward<CtorArgs>(args)...);
        graph.AddRenderPass(pass, params, passName);

        return *this;
    }

    // Second build, callable on temporaries
    template<typename PassT, typename... CtorArgs>
    RenderPassBuilder Build(CtorArgs&&... args) && {
        ensureNotBuilt();
        built_ = true;

		params.resourceRequirements = GatherResourceRequirements();
        auto pass = std::make_shared<PassT>(std::forward<CtorArgs>(args)...);
        graph.AddRenderPass(pass, params, passName);

        return std::move(*this);
    }

private:
    RenderPassBuilder(RenderGraph& g, std::string name)
        : graph(g), passName(std::move(name)) {}

    // Single resource overload
    void addShaderResource(const ResourceAndRange& r) {
        if (!r.resource) return;
        params.shaderResources.push_back(r);
    }

    // initializer list overload
    void addShaderResource(std::initializer_list<ResourceAndRange> list) {
        for (auto& r : list) {
            if (!r.resource) continue;
            params.shaderResources.push_back(r);
        }
    }

    void addShaderResource(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        ResourceAndRange resourceAndRange(r);
        params.shaderResources.push_back(resourceAndRange);
    }

    void addShaderResource(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            ResourceAndRange resourceAndRange(r);
            params.shaderResources.push_back(resourceAndRange);
        }
    }

    // Render target
    void addRenderTarget(const ResourceAndRange& r) {
        if (!r.resource) return;
        params.renderTargets.push_back(r);
    }

    void addRenderTarget(std::initializer_list<ResourceAndRange> list) {
        for (auto& r : list) {
            if (!r.resource) continue;
            params.renderTargets.push_back(r);
        }
    }

    void addRenderTarget(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        ResourceAndRange resourceAndRange(r);
        params.renderTargets.push_back(resourceAndRange);
    }

    void addRenderTarget(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            ResourceAndRange resourceAndRange(r);
            params.renderTargets.push_back(resourceAndRange);
        }
    }

    // Depth target
    void addDepthReadWrite(const ResourceAndRange& r) {
        if (!r.resource) return;
        params.depthReadWriteResources.push_back(r);
    }

    void addDepthReadWrite(std::initializer_list<ResourceAndRange> list) {
        for (auto& r : list) {
            if (!r.resource) continue;
            params.depthReadWriteResources.push_back(r);
        }
    }

    void addDepthReadWrite(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        ResourceAndRange resourceAndRange(r);
        params.depthReadWriteResources.push_back(resourceAndRange);
    }

    void addDepthReadWrite(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            ResourceAndRange resourceAndRange(r);
            params.depthReadWriteResources.push_back(resourceAndRange);
        }
    }

    void addDepthRead(const ResourceAndRange& r) {
        if (!r.resource) return;
        params.depthReadResources.push_back(r);
    }

    void addDepthRead(std::initializer_list<ResourceAndRange> list) {
        for (auto& r : list) {
            if (!r.resource) continue;
            params.depthReadResources.push_back(r);
        }
    }

    void addDepthRead(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        ResourceAndRange resourceAndRange(r);
        params.depthReadResources.push_back(resourceAndRange);
    }

    void addDepthRead(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            ResourceAndRange resourceAndRange(r);
            params.depthReadResources.push_back(resourceAndRange);
        }
    }

    // Constant buffer
    void addConstantBuffer(const ResourceAndRange& r) {
        if (!r.resource) return;
        params.constantBuffers.push_back(r);
    }

    void addConstantBuffer(std::initializer_list<ResourceAndRange> list) {
        for (auto& r : list) {
            if (!r.resource) continue;
            params.constantBuffers.push_back(r);
        }
    }

    void addConstantBuffer(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        ResourceAndRange resourceAndRange(r);
        params.constantBuffers.push_back(resourceAndRange);
    }

    void addConstantBuffer(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            ResourceAndRange resourceAndRange(r);
            params.constantBuffers.push_back(resourceAndRange);
        }
    }

    // Unordered access
    void addUnorderedAccess(const ResourceAndRange& r) {
        if (!r.resource) return;
        params.unorderedAccessViews.push_back(r);
    }

    void addUnorderedAccess(std::initializer_list<ResourceAndRange> list) {
        for (auto& r : list) {
            if (!r.resource) continue;
            params.unorderedAccessViews.push_back(r);
        }
    }

    void addUnorderedAccess(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        ResourceAndRange resourceAndRange(r);
        params.unorderedAccessViews.push_back(resourceAndRange);
    }

    void addUnorderedAccess(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            ResourceAndRange resourceAndRange(r);
            params.unorderedAccessViews.push_back(resourceAndRange);
        }
    }

    // Copy destination
    void addCopyDest(const ResourceAndRange& r) {
        if (!r.resource) return;
        params.copyTargets.push_back(r);
    }

    void addCopyDest(std::initializer_list<ResourceAndRange> list) {
        for (auto& r : list) {
            if (!r.resource) continue;
            params.copyTargets.push_back(r);
        }
    }

    void addCopyDest(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        ResourceAndRange resourceAndRange(r);
        params.copyTargets.push_back(resourceAndRange);
    }

    void addCopyDest(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            ResourceAndRange resourceAndRange(r);
            params.copyTargets.push_back(resourceAndRange);
        }
    }

    // Copy source
    void addCopySource(const ResourceAndRange& r) {
        if (!r.resource) return;
        params.copySources.push_back(r);
    }

    void addCopySource(std::initializer_list<ResourceAndRange> list) {
        for (auto& r : list) {
            if (!r.resource) continue;
            params.copySources.push_back(r);
        }
    }

    void addCopySource(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        ResourceAndRange resourceAndRange(r);
        params.copySources.push_back(resourceAndRange);
    }

    void addCopySource(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            ResourceAndRange resourceAndRange(r);
            params.copySources.push_back(resourceAndRange);
        }
    }

    // Indirect arguments
    void addIndirectArguments(const ResourceAndRange& r) {
        if (!r.resource) return;
        params.indirectArgumentBuffers.push_back(r);
    }

    void addIndirectArguments(std::initializer_list<ResourceAndRange> list) {
        for (auto& r : list) {
            if (!r.resource) continue;
            params.indirectArgumentBuffers.push_back(r);
        }
    }

    void addIndirectArguments(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        ResourceAndRange resourceAndRange(r);
        params.indirectArgumentBuffers.push_back(resourceAndRange);
    }

    void addIndirectArguments(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            ResourceAndRange resourceAndRange(r);
            params.indirectArgumentBuffers.push_back(resourceAndRange);
        }
    }

    void ensureNotBuilt() const {
        if (built_) throw std::runtime_error("RenderPassBuilder::Build() may only be called once");
    }


    std::vector<ResourceRequirement> GatherResourceRequirements() const {
        std::vector<ResourceRequirement> allReqs;
        allReqs.reserve(
            params.shaderResources.size() +
            params.constantBuffers.size()  +
            params.renderTargets.size()    +
			params.depthReadResources.size() +
			params.depthReadWriteResources.size() +
			params.unorderedAccessViews.size() +
			params.copySources.size() +
			params.copyTargets.size() +
			params.indirectArgumentBuffers.size()
        );

        auto append = [&](std::vector<ResourceAndRange> const& views, ResourceAccessType access){
            for (auto const& view : views) {
                if (!view.resource) continue;
                ResourceRequirement rr(view);
                rr.access = access;
                rr.layout = AccessToLayout(access, /*directQueue*/true);
                rr.sync   = RenderSyncFromAccess(access);

                // validate layout
                if (view.resource->HasLayout() &&
                    !ValidateResourceLayoutAndAccessType(rr.layout, rr.access))
                {
                    throw std::runtime_error("Resource layout and state validation failed");
                }

                allReqs.push_back(std::move(rr));
            }
            };

        append(params.shaderResources,    ResourceAccessType::SHADER_RESOURCE);
        append(params.constantBuffers,    ResourceAccessType::CONSTANT_BUFFER);
        append(params.renderTargets,      ResourceAccessType::RENDER_TARGET);
        append(params.depthReadResources, ResourceAccessType::DEPTH_READ);
        append(params.depthReadWriteResources, ResourceAccessType::DEPTH_READ_WRITE);
        append(params.unorderedAccessViews, ResourceAccessType::UNORDERED_ACCESS);
        append(params.copySources,        ResourceAccessType::COPY_SOURCE);
        append(params.copyTargets,        ResourceAccessType::COPY_DEST);
        append(params.indirectArgumentBuffers, ResourceAccessType::INDIRECT_ARGUMENT);

        return allReqs;
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

    template<typename... Args>
    ComputePassBuilder& WithIndirectArguments(Args&&... args) & {
        (addIndirectArguments(std::forward<Args>(args)), ...);
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

    template<typename... Args>
    ComputePassBuilder WithIndirectArguments(Args&&... args) && {
        (addIndirectArguments(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    // First build, callable on Lvalues
    template<typename PassT, typename... CtorArgs>
    ComputePassBuilder& Build(CtorArgs&&... args) & {
        ensureNotBuilt();
        built_ = true;

        params.resourceRequirements = GatherResourceRequirements();
        auto pass = std::make_shared<PassT>(std::forward<CtorArgs>(args)...);
        graph.AddComputePass(pass, params, passName);

        return *this;
    }

    // Second build, callable on temporaries
    template<typename PassT, typename... CtorArgs>
    ComputePassBuilder Build(CtorArgs&&... args) && {
        ensureNotBuilt();
        built_ = true;

        params.resourceRequirements = GatherResourceRequirements();
        auto pass = std::make_shared<PassT>(std::forward<CtorArgs>(args)...);
        graph.AddComputePass(pass, params, passName);

        return std::move(*this);
    }

private:
    ComputePassBuilder(RenderGraph& g, std::string name)
        : graph(g), passName(std::move(name)) {}

    // Single resource overload
    void addShaderResource(const ResourceAndRange& r) {
        if (!r.resource) return;
        params.shaderResources.push_back(r);
    }

    // initializer list overload
    void addShaderResource(std::initializer_list<ResourceAndRange> list) {
        for (auto& r : list) {
            if (!r.resource) continue;
            params.shaderResources.push_back(r);
        }
    }

    void addShaderResource(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        ResourceAndRange resourceAndRange(r);
        params.shaderResources.push_back(resourceAndRange);
    }

    void addShaderResource(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            ResourceAndRange resourceAndRange(r);
            params.shaderResources.push_back(resourceAndRange);
        }
    }

    // Constant buffer
    void addConstantBuffer(const ResourceAndRange& r) {
        if (!r.resource) return;
        params.constantBuffers.push_back(r);
    }

    void addConstantBuffer(std::initializer_list<ResourceAndRange> list) {
        for (auto& r : list) {
            if (!r.resource) continue;
            params.constantBuffers.push_back(r);
        }
    }

    void addConstantBuffer(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        ResourceAndRange resourceAndRange(r);
        params.constantBuffers.push_back(resourceAndRange);
    }

    void addConstantBuffer(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            ResourceAndRange resourceAndRange(r);
            params.constantBuffers.push_back(resourceAndRange);
        }
    }

    // Unordered access
    void addUnorderedAccess(const ResourceAndRange& r) {
        if (!r.resource) return;
        params.unorderedAccessViews.push_back(r);
    }

    void addUnorderedAccess(std::initializer_list<ResourceAndRange> list) {
        for (auto& r : list) {
            if (!r.resource) continue;
            params.unorderedAccessViews.push_back(r);
        }
    }

    void addUnorderedAccess(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        ResourceAndRange resourceAndRange(r);
        params.unorderedAccessViews.push_back(resourceAndRange);
    }

    void addUnorderedAccess(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            ResourceAndRange resourceAndRange(r);
            params.unorderedAccessViews.push_back(resourceAndRange);
        }
    }

	// Indirect arguments
	void addIndirectArguments(const ResourceAndRange& r) {
		if (!r.resource) return;
		params.indirectArgumentBuffers.push_back(r);
	}
	void addIndirectArguments(std::initializer_list<ResourceAndRange> list) {
		for (auto& r : list) {
			if (!r.resource) continue;
			params.indirectArgumentBuffers.push_back(r);
		}
	}

    void addIndirectArguments(const std::shared_ptr<Resource>& r) {
        if (!r) return;
        ResourceAndRange resourceAndRange(r);
        params.indirectArgumentBuffers.push_back(resourceAndRange);
    }

    void addIndirectArguments(std::initializer_list<std::shared_ptr<Resource>> list) {
        for (auto& r : list) {
            if (!r) continue;
            ResourceAndRange resourceAndRange(r);
            params.indirectArgumentBuffers.push_back(resourceAndRange);
        }
    }

    void ensureNotBuilt() const {
        if (built_) throw std::runtime_error("ComputePassBuilder::Build() may only be called once");
    }

    std::vector<ResourceRequirement> GatherResourceRequirements() const {
        std::vector<ResourceRequirement> allReqs;
        allReqs.reserve(
            params.shaderResources.size() +
            params.constantBuffers.size()  +
            params.unorderedAccessViews.size() +
			params.indirectArgumentBuffers.size()
        );

        auto append = [&](std::vector<ResourceAndRange> const& views, ResourceAccessType access){
            for (auto const& view : views) {
                if (!view.resource) continue;
                ResourceRequirement rr(view);
                rr.access = access;
                rr.layout = AccessToLayout(access, /*directQueue*/false);
                rr.sync   = RenderSyncFromAccess(access);

                // validate layout
                if (view.resource->HasLayout() &&
                    !ValidateResourceLayoutAndAccessType(rr.layout, rr.access))
                {
                    throw std::runtime_error("Resource layout and state validation failed");
                }

                allReqs.push_back(std::move(rr));
            }
            };

        append(params.shaderResources,    ResourceAccessType::SHADER_RESOURCE);
        append(params.constantBuffers,    ResourceAccessType::CONSTANT_BUFFER);
        append(params.unorderedAccessViews, ResourceAccessType::UNORDERED_ACCESS);
        append(params.indirectArgumentBuffers, ResourceAccessType::INDIRECT_ARGUMENT);

        return allReqs;
    }

    // storage
    RenderGraph&             graph;
    std::string              passName;
    ComputePassParameters     params;
    bool built_ = false;

    friend class RenderGraph;
};
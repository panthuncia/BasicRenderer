#pragma once

#include <type_traits>
#include <memory>
#include <rhi.h>

#include "RenderGraph.h"
#include "ResourceRequirements.h"
#include "Resources/ResourceStateTracker.h"
#include "Resources/ResourceIdentifier.h"
#include "Interfaces/IResourceResolver.h"
#include "Interfaces/IPassBuilder.h"
#include "Resources/Resolvers/ECSResourceResolver.h"

// Tag for a contiguous mip-range [first..first+count)
struct Mip {
	Mip(uint32_t first, uint32_t count) : first(first), count(count) {}
    uint32_t first, count;
};

// Tag for a half-open "from" mip-range [first..inf)
struct FromMip {
    uint32_t first;
};

// Tag for a half-open "up to" mip-range [0..last]
struct UpToMip {
    uint32_t last;
};

// Tag for a contiguous slice-range [first..first+count)
struct Slice {
	Slice(uint32_t first, uint32_t count) : first(first), count(count) {}
    uint32_t first, count;
};

// Tag for a half-open "from" slice-range [first..inf)
struct FromSlice {
    uint32_t first;
};

// Tag for a half-open "up to" slice-range [0..last]
struct UpToSlice {
    uint32_t last;
};

//// shared_ptr
//inline ResourceAndRange Subresources(const std::shared_ptr<Resource>& r) {
//    // everything
//    return { r };
//}
//
//inline ResourceAndRange Subresources(const std::shared_ptr<Resource>& r,
//    Mip m)
//{
//    RangeSpec spec;
//    spec.mipLower   = { BoundType::Exact, m.first      };
//    spec.mipUpper   = { BoundType::Exact, m.first + m.count - 1 };
//    return { r, spec };
//}
//
//inline ResourceAndRange Subresources(const std::shared_ptr<Resource>& r,
//    FromMip fm)
//{
//    RangeSpec spec;
//    spec.mipLower   = { BoundType::From, fm.first };
//    return { r, spec };
//}
//
//inline ResourceAndRange Subresources(const std::shared_ptr<Resource>& r,
//    UpToMip um)
//{
//    RangeSpec spec;
//    spec.mipUpper   = { BoundType::UpTo, um.last };
//    return { r, spec };
//}
//
//inline ResourceAndRange Subresources(const std::shared_ptr<Resource>& r,
//    Slice s)
//{
//    RangeSpec spec;
//    spec.sliceLower = { BoundType::Exact, s.first       };
//    spec.sliceUpper = { BoundType::Exact, s.first + s.count - 1 };
//    return { r, spec };
//}
//
//inline ResourceAndRange Subresources(const std::shared_ptr<Resource>& r,
//    FromSlice fs)
//{
//    RangeSpec spec;
//    spec.sliceLower = { BoundType::From, fs.first };
//    return { r, spec };
//}
//
//inline ResourceAndRange Subresources(const std::shared_ptr<Resource>& r,
//    UpToSlice us)
//{
//    RangeSpec spec;
//    spec.sliceUpper = { BoundType::UpTo, us.last };
//    return { r, spec };
//}
//
//inline ResourceAndRange Subresources(const std::shared_ptr<Resource>& r,
//    Mip     m,
//    Slice   s)
//{
//    RangeSpec spec;
//    spec.mipLower   = { BoundType::Exact, m.first      };
//    spec.mipUpper   = { BoundType::Exact, m.first + m.count - 1 };
//    spec.sliceLower = { BoundType::Exact, s.first       };
//    spec.sliceUpper = { BoundType::Exact, s.first + s.count - 1 };
//    return { r, spec };
//}


// ResourceIdentifier
inline ResourceIdentifierAndRange Subresources(const ResourceIdentifier& r) {
    // everything
    return { r };
}

inline ResourceIdentifierAndRange Subresources(const ResourceIdentifier& r,
    Mip m)
{
    RangeSpec spec;
    spec.mipLower   = { BoundType::Exact, m.first      };
    spec.mipUpper   = { BoundType::Exact, m.first + m.count - 1 };
    return { r, spec };
}

inline ResourceIdentifierAndRange Subresources(const ResourceIdentifier& r,
    FromMip fm)
{
    RangeSpec spec;
    spec.mipLower   = { BoundType::From, fm.first };
    return { r, spec };
}

inline ResourceIdentifierAndRange Subresources(const ResourceIdentifier& r,
    UpToMip um)
{
    RangeSpec spec;
    spec.mipUpper   = { BoundType::UpTo, um.last };
    return { r, spec };
}

inline ResourceIdentifierAndRange Subresources(const ResourceIdentifier& r,
    Slice s)
{
    RangeSpec spec;
    spec.sliceLower = { BoundType::Exact, s.first       };
    spec.sliceUpper = { BoundType::Exact, s.first + s.count - 1 };
    return { r, spec };
}

inline ResourceIdentifierAndRange Subresources(const ResourceIdentifier& r,
    FromSlice fs)
{
    RangeSpec spec;
    spec.sliceLower = { BoundType::From, fs.first };
    return { r, spec };
}

inline ResourceIdentifierAndRange Subresources(const ResourceIdentifier& r,
    UpToSlice us)
{
    RangeSpec spec;
    spec.sliceUpper = { BoundType::UpTo, us.last };
    return { r, spec };
}

inline ResourceIdentifierAndRange Subresources(const ResourceIdentifier& r,
    Mip     m,
    Slice   s)
{
    RangeSpec spec;
    spec.mipLower   = { BoundType::Exact, m.first      };
    spec.mipUpper   = { BoundType::Exact, m.first + m.count - 1 };
    spec.sliceLower = { BoundType::Exact, s.first       };
    spec.sliceUpper = { BoundType::Exact, s.first + s.count - 1 };
    return { r, spec };
}

// BuiltinResource
inline ResourceIdentifierAndRange Subresources(const char* r) {
	return Subresources(ResourceIdentifier{ r });
}

inline ResourceIdentifierAndRange Subresources(const char* r,
    Mip m) {
	return Subresources(ResourceIdentifier{ r }, m);
}

inline ResourceIdentifierAndRange Subresources(const char* r,
    FromMip fm) {
	return Subresources(ResourceIdentifier{ r }, fm);
}

inline ResourceIdentifierAndRange Subresources(const char* r,
    UpToMip um) {
	return Subresources(ResourceIdentifier{ r }, um);
}

inline ResourceIdentifierAndRange Subresources(const char* r,
    Slice s) {
	return Subresources(ResourceIdentifier{ r }, s);
}

inline ResourceIdentifierAndRange Subresources(const char* r,
    FromSlice fs) {
	return Subresources(ResourceIdentifier{ r }, fs);
}

inline ResourceIdentifierAndRange Subresources(const char* r,
    UpToSlice us) {
	return Subresources(ResourceIdentifier{ r }, us);
}

inline ResourceIdentifierAndRange Subresources(const char* r,
    Mip     m,
    Slice   s) {
	return Subresources(ResourceIdentifier{ r }, m, s);
}

//
//  expandToRanges(...) is a set of overloads that take one of six
//  input types and return a std::vector<ResourceAndRange> so that we can
//  unify all the add*() calls into a single template.
//

// If we already have a ResourceAndRange, just return it in a vector:
//inline std::vector<ResourceAndRange>
//expandToRanges(ResourceAndRange const & rar, RenderGraph* graph)
//{
//    if (!rar.resource) return {};
//    return { rar };
//}
//
//// If we have a list of ResourceAndRange, return a vector copy of it:
//inline std::vector<ResourceAndRange>
//expandToRanges(std::initializer_list<ResourceAndRange> list, RenderGraph* graph)
//{
//    std::vector<ResourceAndRange> out;
//    out.reserve(list.size());
//    for (auto const & r : list) {
//        if (!r.resource) continue;
//        out.push_back(r);
//    }
//    return out;
//}

// If we have a shared_ptr<Resource>, wrap it in a ResourceAndRange:
//template<typename U>
//inline std::enable_if_t<std::is_base_of_v<Resource, U>,
//    std::vector<ResourceAndRange>>
//    expandToRanges(std::shared_ptr<U> const& r, RenderGraph* graph)
//{
//    if (!r) return {};
//    std::shared_ptr<Resource> basePtr = r;
//    return { ResourceAndRange{ basePtr } };
//}

// If we have an initializer_list of shared_ptr<Resource>:
//inline std::vector<ResourceAndRange>
//expandToRanges(std::initializer_list<std::shared_ptr<Resource>> list, RenderGraph* graph)
//{
//    std::vector<ResourceAndRange> out;
//    out.reserve(list.size());
//    for (auto const & r : list) {
//        if (!r) continue;
//        out.push_back(ResourceAndRange{ r });
//    }
//    return out;
//}

// If we have a ResourceIdentifierAndRange, ask the builder to resolve it into an actual ResourceAndRange:
std::vector<ResourceHandleAndRange> expandToRanges(ResourceIdentifierAndRange const& rir, RenderGraph* graph);

// If we have an initializer_list of ResourceIdentifierAndRange,
inline std::vector<ResourceHandleAndRange>
expandToRanges(std::initializer_list<ResourceIdentifierAndRange> list,
    RenderGraph* graph)
{
    std::vector<ResourceHandleAndRange> out;
    out.reserve(list.size());
    for (auto const & rir : list) {
        if (auto vec = expandToRanges(rir, graph); !vec.empty()) {
            // vec always has exactly one element, but we push it.
            out.push_back(std::move(vec.front()));
        }
    }
    return out;
}

template<typename> 
constexpr bool is_shared_ptr_v = false;

template<typename U> 
constexpr bool is_shared_ptr_v<std::shared_ptr<U>> = true;

// processResourceArguments(...) is a set of overloads that take one of several 
// ways to represent a resource and return a vector of ResourceHandleAndRange

// For a ResourceHandleAndRange, just return it in a vector
inline std::vector<ResourceHandleAndRange>
processResourceArguments(const ResourceHandleAndRange& rar,
    RenderGraph* graph)
{
    //if (!rar.resource) return {};
    return { rar };
}

// For a resource pointer + range spec, wrap it and expand it to actual resource handles + ranges
inline std::vector<ResourceHandleAndRange>
processResourceArguments(const ResourcePtrAndRange& rar,
    RenderGraph* graph)
{
	auto range = rar.range;
    auto handle = graph->RequestResourceHandle(rar.resource.get());
    return { ResourceHandleAndRange{ handle, range } };
}

// For a resource pointer, assume full range
inline std::vector<ResourceHandleAndRange>
processResourceArguments(const std::shared_ptr<Resource>& r,
    RenderGraph* graph)
{
    return processResourceArguments(
        ResourcePtrAndRange{ r },
        graph
    );
}

// For a resource resolver + range spec, resolve it and process the result
inline std::vector<ResourceHandleAndRange>
processResourceArguments(const ResourceResolverAndRange& rrr,
    RenderGraph* graph)
{
    std::vector<ResourceHandleAndRange> out;
    auto resources = rrr.pResolver->Resolve();
    for (auto const& res : resources) {
        const auto rar = ResourcePtrAndRange(res, rrr.range);
        auto vec = processResourceArguments(rar, graph);
        out.insert(out.end(),
            std::make_move_iterator(vec.begin()),
            std::make_move_iterator(vec.end()));
    }
    return out;
}

// For a resource resolver, assume full range
inline std::vector<ResourceHandleAndRange>
processResourceArguments(const IResourceResolver& resolver,
    RenderGraph* graph)
{
    return processResourceArguments(
        ResourceResolverAndRange(resolver),
        graph
    );
}

// For a resource identifier + range spec, expand it to actual resource handles + ranges
inline std::vector<ResourceHandleAndRange>
processResourceArguments(const ResourceIdentifierAndRange& rir,
    RenderGraph* graph)
{
    // This could be a resolver- ask the graph.
    if (auto resolver = graph->RequestResolver(rir.identifier, true)) {
        return processResourceArguments(*resolver, graph);
    }

    return expandToRanges(rir, graph);
}

// For a bare resource identifier, assume full range
inline std::vector<ResourceHandleAndRange>
processResourceArguments(const ResourceIdentifier& rid,
    RenderGraph* graph)
{
    return processResourceArguments(
        ResourceIdentifierAndRange{ rid },
        graph
    );
}

// For a builtin resource name, assume full range
inline std::vector<ResourceHandleAndRange>
processResourceArguments(const char* br,
    RenderGraph* graph)
{
    return processResourceArguments(
        ResourceIdentifierAndRange{ ResourceIdentifier{ br } },
        graph
    );
}

// For an initializer_list, process each element individually
template<typename T>
inline std::enable_if_t<
    std::is_same_v<std::decay_t<T>, std::initializer_list<typename std::decay_t<T>::value_type>>,
    std::vector<ResourceHandleAndRange>
>
processResourceArguments(T&& list, RenderGraph* graph)
{
    std::vector<ResourceHandleAndRange> out;
    out.reserve(list.size());

    for (auto const & elem : list) {
        auto vec = processResourceArguments(elem, graph);
        if (!vec.empty()) 
            out.push_back(std::move(vec.front()));
    }
    return out;
}

namespace detail {
    template<typename U>
    inline void extractId(std::unordered_set<ResourceIdentifier, ResourceIdentifier::Hasher>& out, std::shared_ptr<U> const& resource) {
        out.insert(std::to_string(resource->GetGlobalResourceID()));
    }
    inline void extractId(auto& out, const ResourcePtrAndRange& rar) {
		extractId(out, rar.resource); // empty identifier
    }
    inline void extractId(auto& out, ResourceIdentifierAndRange const& rir) {
        out.insert(rir.identifier);
    }
    inline void extractId(auto& out, ResourceIdentifier const& rid) {
        out.insert(rid);
    }
    inline void extractId(auto& out, char* br) {
        out.insert(ResourceIdentifier{ br });
    }

    template<typename T>
    inline void extractId(auto& out, std::initializer_list<T> list) {
        for (auto const& e : list) extractId(out, e);
    }
}

template<class T>
inline constexpr bool ResourceLike =
std::is_same_v<std::decay_t<T>, std::shared_ptr<Resource>> ||
std::is_same_v<std::decay_t<T>, ResourceIdentifier> ||
std::is_same_v<std::decay_t<T>, ResourcePtrAndRange> ||
std::is_same_v<std::decay_t<T>, ResourceIdentifierAndRange> ||
std::is_same_v<std::decay_t<T>, std::string_view> || // Can be cast to ResourceIdentifier
std::is_same_v<std::decay_t<T>, const char*> || // Can be cast to ResourceIdentifier
std::is_same_v<std::decay_t<T>, IResourceResolver>; // can be cast to ECSResourceResolver 

template<typename T>
concept NotIResourceResolver = !std::derived_from<std::decay_t<T>, IResourceResolver>; // annoying

template<typename T>
concept DerivedRenderPass = std::derived_from<T, RenderPass>;

class RenderPassBuilder : public IPassBuilder {
public:
    PassBuilderKind Kind() const noexcept override { return PassBuilderKind::Render; }
    IResourceProvider* ResourceProvider() noexcept override { return pass.get(); }
    // Variadic entry points

    //First set, callable on Lvalues
    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    RenderPassBuilder& WithShaderResource(Args&&... args) & {
        (addShaderResource(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    RenderPassBuilder& WithRenderTarget(Args&&... args) & {
        (addRenderTarget(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    RenderPassBuilder& WithDepthRead(Args&&... args) & {
        (addDepthRead(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    RenderPassBuilder& WithDepthReadWrite(Args&&... args) & {
        (addDepthReadWrite(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    RenderPassBuilder& WithConstantBuffer(Args&&... args) & {
        (addConstantBuffer(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    RenderPassBuilder& WithUnorderedAccess(Args&&... args) & {
        (addUnorderedAccess(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    RenderPassBuilder& WithCopyDest(Args&&... args) & {
        (addCopyDest(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    RenderPassBuilder& WithCopySource(Args&&... args) & {
        (addCopySource(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    RenderPassBuilder& WithIndirectArguments(Args&&... args) & {
        (addIndirectArguments(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    RenderPassBuilder& WithLegacyInterop(Args&&... args)& {
        (addLegacyInterop(std::forward<Args>(args)), ...);
        return *this;
    }

    RenderPassBuilder& WithInternalTransition(
        ResourceIdentifierAndRange rar,
        ResourceState exitState) &
    {
        addInternalTransition(rar, exitState);
        return *this;
    }

    // Second set, callable on temporaries
    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    RenderPassBuilder WithShaderResource(Args&&... args) && {
        (addShaderResource(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    RenderPassBuilder WithRenderTarget(Args&&... args) && {
        (addRenderTarget(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    RenderPassBuilder WithDepthReadWrite(Args&&... args) && {
        (addDepthReadWrite(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    RenderPassBuilder WithDepthRead(Args&&... args) && {
        (addDepthRead(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    RenderPassBuilder WithConstantBuffer(Args&&... args) && {
        (addConstantBuffer(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
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
        requires ((NotIResourceResolver<Args>) && ...)
    RenderPassBuilder WithCopySource(Args&&... args) && {
        (addCopySource(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    RenderPassBuilder WithIndirectArguments(Args&&... args) && {
        (addIndirectArguments(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    RenderPassBuilder WithLegacyInterop(Args&&... args)&& {
        (addLegacyInterop(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    RenderPassBuilder WithInternalTransition(
        ResourceIdentifierAndRange rar,
        ResourceState exitState) &&
    {
        addInternalTransition(rar, exitState);
        return std::move(*this);
    }

	// LVALUE overloads for IResourceResolver
    RenderPassBuilder& WithShaderResource(const IResourceResolver& r)& {
        auto resources = r.Resolve();
        for (auto& res : resources) {
            graph->AddResource(res);
        }
        addShaderResource(resources);
        return *this;
    }

    RenderPassBuilder& WithRenderTarget(const IResourceResolver& r)& {
		auto resources = r.Resolve();
		for (auto& res : resources) {
			graph->AddResource(res);
		}
		(addRenderTarget(r.Resolve()));
        return *this;
	}

    RenderPassBuilder& WithDepthReadWrite(const IResourceResolver& r)& {
		auto resources = r.Resolve();
        for (auto& res : resources) {
            graph->AddResource(res);
		}
		(addDepthReadWrite(r.Resolve()));
		return *this;
    }

    RenderPassBuilder& WithDepthRead(const IResourceResolver& r)& {
		auto resources = r.Resolve();
        for (auto& res : resources) {
			graph->AddResource(res);
		}
		(addDepthRead(r.Resolve()));
        return *this;
    }

	RenderPassBuilder& WithConstantBuffer(const IResourceResolver& r)& {
		auto resources = r.Resolve();
		for (auto& res : resources) {
			graph->AddResource(res);
		}
        return *this;
	}

    RenderPassBuilder& WithUnorderedAccess(const IResourceResolver& r)& {
		auto resources = r.Resolve();
		for (auto& res : resources) {
			graph->AddResource(res);
		}
		(addUnorderedAccess(r.Resolve()));
        return *this;
	}

    RenderPassBuilder& WithCopyDest(const IResourceResolver& r)& {
		auto resources = r.Resolve();
		for (auto& res : resources) {
			graph->AddResource(res);
		}
		(addCopyDest(r.Resolve()));
        return *this;
    }

    RenderPassBuilder& WithCopySource(const IResourceResolver& r)& {
		auto resources = r.Resolve();
		for (auto& res : resources) {
			graph->AddResource(res);
		}
		(addCopySource(r.Resolve()));
        return *this;
    }

    RenderPassBuilder& WithIndirectArguments(const IResourceResolver& r)& {
		auto resources = r.Resolve();
		for (auto& res : resources) {
			graph->AddResource(res);
		}
		(addIndirectArguments(r.Resolve()));
        return *this;
	}

    RenderPassBuilder& WithLegacyInterop(const IResourceResolver& r)& {
		auto resources = r.Resolve();
        for (auto& res : resources) {
            graph->AddResource(res);
		}
		(addLegacyInterop(r.Resolve()));
		return *this;
	}

	// RVALUE overloads for IResourceResolver

    RenderPassBuilder WithShaderResource(const IResourceResolver& r)&& {
		auto resources = r.Resolve();
		for (auto& res : resources) {
			graph->AddResource(res);
		}
		(addShaderResource(r.Resolve()));
        return std::move(*this);
	}
    RenderPassBuilder WithRenderTarget(const IResourceResolver& r)&& {
		auto resources = r.Resolve();
		for (auto& res : resources) {
			graph->AddResource(res);
		}
		(addRenderTarget(r.Resolve()));
        return std::move(*this);
    }
    RenderPassBuilder WithDepthReadWrite(const IResourceResolver& r)&& {
		auto resources = r.Resolve();
        for (auto& res : resources) {
            graph->AddResource(res);
        }
		(addDepthReadWrite(r.Resolve()));
        return std::move(*this);
    }
    RenderPassBuilder WithDepthRead(const IResourceResolver& r)&& {
		auto resources = r.Resolve();
        for (auto& res : resources) {
            graph->AddResource(res);
		}
		(addDepthRead(r.Resolve()));
        return std::move(*this);
    }
    RenderPassBuilder WithConstantBuffer(const IResourceResolver& r)&& {
		auto resources = r.Resolve();
        for (auto& res : resources) {
			graph->AddResource(res);
		}
		(addConstantBuffer(r.Resolve()));
        return std::move(*this);
    }
    RenderPassBuilder WithUnorderedAccess(const IResourceResolver& r)&& {
		auto resources = r.Resolve();
        for (auto& res : resources) {
            graph->AddResource(res);
        }
		(addUnorderedAccess(r.Resolve()));
        return std::move(*this);
    }
    RenderPassBuilder WithCopyDest(const IResourceResolver& r)&& {
		auto resources = r.Resolve();
        for (auto& res : resources) {
            graph->AddResource(res);
		}
		(addCopyDest(r.Resolve()));
        return std::move(*this);
    }
    RenderPassBuilder WithCopySource(const IResourceResolver& r)&& {
		auto resources = r.Resolve();
        for (auto& res : resources) {
			graph->AddResource(res);
		}
		(addCopySource(r.Resolve()));
        return std::move(*this);
    }
    RenderPassBuilder WithIndirectArguments(const IResourceResolver& r)&& {
		auto resources = r.Resolve();
        for (auto& res : resources) {
			graph->AddResource(res);
		}
        return std::move(*this);
    }
    RenderPassBuilder WithLegacyInterop(const IResourceResolver& r)&& {
		auto resources = r.Resolve();
        for (auto& res : resources) {
			graph->AddResource(res);
		}
		(addLegacyInterop(r.Resolve()));
        return std::move(*this);
	}

	RenderPassBuilder& IsGeometryPass()& {
		m_isGeometryPass = true;
		return *this;
	}

    RenderPassBuilder IsGeometryPass() && {
        m_isGeometryPass = true;
		return std::move(*this);
    }

    // First build, callable on Lvalues
    template<DerivedRenderPass PassT, rg::PassInputs InputsT>
    void Build(InputsT&& inputs) & {
        if (!built_) {
            built_ = true;
            pass = std::make_shared<PassT>();
        }
        pass->SetInputs(std::forward<InputsT>(inputs));
    }

    // Second build, callable on temporaries
    template<DerivedRenderPass PassT, rg::PassInputs InputsT>
    void Build(InputsT&& inputs) && {
        if (!built_) {
            built_ = true;
            pass = std::make_shared<PassT>();
        }
        pass->SetInputs(std::forward<InputsT>(inputs));
    }

    template<DerivedRenderPass PassT, typename... StableCtorArgs>
    void Build(StableCtorArgs&&... ctorArgs)& {
        Build<PassT>(rg::NoInputs{}, std::forward<StableCtorArgs>(ctorArgs)...);
    }

    template<DerivedRenderPass PassT, typename... StableCtorArgs>
    void Build(StableCtorArgs&&... ctorArgs)&& {
        Build<PassT>(rg::NoInputs{}, std::forward<StableCtorArgs>(ctorArgs)...);
    }

    auto const& DeclaredResourceIds() const { return _declaredIds; }

private:
    RenderPassBuilder(RenderGraph* g, std::string name)
        : graph(g), passName(std::move(name)) {}

	// Copy and move constructors are default, but private
	RenderPassBuilder(const RenderPassBuilder&) = default;
	RenderPassBuilder(RenderPassBuilder&&) = default;

    // Same for assignment
	RenderPassBuilder& operator=(const RenderPassBuilder&) = default;
	RenderPassBuilder& operator=(RenderPassBuilder&&) = default;

    void Finalize() {
        if (!built_) return;

        params = {}; // Reset params to clear any resources from previous build

        pass->DeclareResourceUsages(this);

        params.isGeometryPass = m_isGeometryPass;
        params.identifierSet = _declaredIds;
        params.resourceRequirements = GatherResourceRequirements();

        graph->AddRenderPass(pass, params, passName);
    }

    void Reset() {
        built_ = false;
        pass = nullptr;
        params = {};
        _declaredIds.clear();
        m_isGeometryPass = false;
	}

    // Shader Resource
	template<typename T>
        requires ResourceLike<T>
	RenderPassBuilder& addShaderResource(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
		auto ranges = processResourceArguments(std::forward<T>(x), graph);
		for (auto& r : ranges) {
			//if (!r.resource) continue;
			params.shaderResources.push_back(r);
		}
		return *this;
	}
    template<class Range>
        requires (std::ranges::range<Range>&&
    ResourceLike<std::ranges::range_value_t<Range>>)
        RenderPassBuilder& addShaderResource(Range&& xs) {
        for (auto&& e : xs) {
            addShaderResource(std::forward<decltype(e)>(e));
        }
        return *this;
    }

    // Render target
    template<typename T>
        requires ResourceLike<T>
    RenderPassBuilder& addRenderTarget(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
		auto ranges = processResourceArguments(std::forward<T>(x), graph);
		for (auto& r : ranges) {
			//if (!r.resource) continue;
			params.renderTargets.push_back(r);
		}
		return *this;
    }
    template<class Range>
		requires (std::ranges::range<Range>&&
	ResourceLike<std::ranges::range_value_t<Range>>)
		RenderPassBuilder& addRenderTarget(Range&& xs) {
		for (auto&& e : xs) {
			addRenderTarget(std::forward<decltype(e)>(e));
		}
		return *this;
	}

    // Depth target
	template<typename T>
        requires ResourceLike<T>
	RenderPassBuilder& addDepthReadWrite(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
		auto ranges = processResourceArguments(std::forward<T>(x), graph);
		for (auto& r : ranges) {
			//if (!r.resource) continue;
			params.depthReadWriteResources.push_back(r);
		}
		return *this;
	}
	template<class Range>
		requires (std::ranges::range<Range>&&
	ResourceLike<std::ranges::range_value_t<Range>>)
        RenderPassBuilder& addDepthReadWrite(Range&& xs) {
        for (auto&& e : xs) {
            addDepthReadWrite(std::forward<decltype(e)>(e));
        }
        return *this;
	}

	template<typename T>
        requires ResourceLike<T>
	RenderPassBuilder& addDepthRead(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
		auto ranges = processResourceArguments(std::forward<T>(x), graph);
		for (auto& r : ranges) {
			//if (!r.resource) continue;
			params.depthReadResources.push_back(r);
		}
		return *this;
	}
	template<class Range>
		requires (std::ranges::range<Range>&&
    ResourceLike<std::ranges::range_value_t<Range>>)
        RenderPassBuilder& addDepthRead(Range&& xs) {
        for (auto&& e : xs) {
            addDepthRead(std::forward<decltype(e)>(e));
        }
		return *this;
	}

    // Constant buffer
	template<typename T>
        requires ResourceLike<T>
	RenderPassBuilder& addConstantBuffer(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
		auto ranges = processResourceArguments(std::forward<T>(x), graph);
		for (auto& r : ranges) {
			//if (!r.resource) continue;
			params.constantBuffers.push_back(r);
		}
		return *this;
	}
	template<class Range>
		requires (std::ranges::range<Range>&&
    ResourceLike<std::ranges::range_value_t<Range>>)
        RenderPassBuilder& addConstantBuffer(Range&& xs) {
        for (auto&& e : xs) {
            addConstantBuffer(std::forward<decltype(e)>(e));
		}
		return *this;
	}

    // Unordered access
	template<typename T>
        requires ResourceLike<T>
	RenderPassBuilder& addUnorderedAccess(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
		auto ranges = processResourceArguments(std::forward<T>(x), graph);
		for (auto& r : ranges) {
			//if (!r.resource) continue;
			params.unorderedAccessViews.push_back(r);
		}
		return *this;
	}
	template<class Range>
		requires (std::ranges::range<Range>&&
    ResourceLike<std::ranges::range_value_t<Range>>)
        RenderPassBuilder& addUnorderedAccess(Range&& xs) {
        for (auto&& e : xs) {
            addUnorderedAccess(std::forward<decltype(e)>(e));
		}
		return *this;
	}

    // Copy destination
	template<typename T>
        requires ResourceLike<T>
	RenderPassBuilder& addCopyDest(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
		auto ranges = processResourceArguments(std::forward<T>(x), graph);
		for (auto& r : ranges) {
			//if (!r.resource) continue;
			params.copyTargets.push_back(r);
		}
		return *this;
	}
	template<class Range>
		requires (std::ranges::range<Range>&&
    ResourceLike<std::ranges::range_value_t<Range>>)
        RenderPassBuilder& addCopyDest(Range&& xs) {
        for (auto&& e : xs) {
			addCopyDest(std::forward<decltype(e)>(e));
		}
		return *this;
	}

    // Copy source
	template<typename T>
        requires ResourceLike<T>
	RenderPassBuilder& addCopySource(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
		auto ranges = processResourceArguments(std::forward<T>(x), graph);
		for (auto& r : ranges) {
			//if (!r.resource) continue;
			params.copySources.push_back(r);
		}
		return *this;
	}
	template<class Range>
		requires (std::ranges::range<Range>&&
    ResourceLike<std::ranges::range_value_t<Range>>)
        RenderPassBuilder& addCopySource(Range&& xs) {
		for (auto&& e : xs) {
			addCopySource(std::forward<decltype(e)>(e));
		}
		return *this;
	}

    // Indirect arguments
	template<typename T>
        requires ResourceLike<T>
	RenderPassBuilder& addIndirectArguments(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
		auto ranges = processResourceArguments(std::forward<T>(x), graph);
		for (auto& r : ranges) {
			//if (!r.resource) continue;
			params.indirectArgumentBuffers.push_back(r);
		}
		return *this;
	}
	template <class Range>
		requires (std::ranges::range<Range>&&
    ResourceLike<std::ranges::range_value_t<Range>>)
        RenderPassBuilder& addIndirectArguments(Range&& xs) {
        for (auto&& e : xs) {
			addIndirectArguments(std::forward<decltype(e)>(e));
		}
		return *this;
	}

    // Legacy interop resources
    template<typename T>
        requires ResourceLike<T>
    RenderPassBuilder& addLegacyInterop(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
        auto ranges = processResourceArguments(std::forward<T>(x), graph);
        for (auto& r : ranges) {
            //if (!r.resource) continue;
            params.legacyInteropResources.push_back(r);
        }
        return *this;
    }
    template<class Range>
		requires (std::ranges::range<Range>&&
    ResourceLike<std::ranges::range_value_t<Range>>)
        RenderPassBuilder& addLegacyInterop(Range&& xs) {
        for (auto&& e : xs) {
            addLegacyInterop(std::forward<decltype(e)>(e));
        }
        return *this;
	}

    RenderPassBuilder& addInternalTransition(
        ResourceIdentifierAndRange rar,
        ResourceState exitState)&
    {
        auto ranges = processResourceArguments(rar, graph);
        for (auto& r : ranges) {
            //if (!r.resource) continue;
            params.internalTransitions.emplace_back(r, exitState);
        }
        return *this;
    }
	template <class Range>
		requires (std::ranges::range<Range>&&
    std::is_same_v<std::ranges::range_value_t<Range>, ResourceIdentifierAndRange>)
        RenderPassBuilder& addInternalTransition(Range&& xs, ResourceState exitState) {
        for (auto&& e : xs) {
            addInternalTransition(std::forward<decltype(e)>(e), exitState);
        }
        return *this;
	}

    std::vector<ResourceRequirement> GatherResourceRequirements() const {
        // Collect every (ResourceAndRange,AccessFlag) pair from all the With* lists
        std::vector<std::pair<ResourceHandleAndRange, rhi::ResourceAccessType>> entries;
        entries.reserve(
            params.shaderResources.size()
            + params.constantBuffers.size()
            + params.renderTargets.size()
            + params.depthReadResources.size()
            + params.depthReadWriteResources.size()
            + params.unorderedAccessViews.size()
            + params.copySources.size()
            + params.copyTargets.size()
            + params.indirectArgumentBuffers.size()
        );

        auto accumulate = [&](auto const& list, rhi::ResourceAccessType flag){
            for(auto const& rr : list){
                //if(!rr.resource) continue;
                entries.emplace_back(rr, flag);
            }
            };

        accumulate(params.shaderResources, rhi::ResourceAccessType::ShaderResource);
        accumulate(params.constantBuffers, rhi::ResourceAccessType::ConstantBuffer);
        accumulate(params.renderTargets, rhi::ResourceAccessType::RenderTarget);
        accumulate(params.depthReadResources, rhi::ResourceAccessType::DepthRead);
        accumulate(params.depthReadWriteResources, rhi::ResourceAccessType::DepthReadWrite);
        accumulate(params.unorderedAccessViews, rhi::ResourceAccessType::UnorderedAccess);
        accumulate(params.copySources, rhi::ResourceAccessType::CopySource);
        accumulate(params.copyTargets, rhi::ResourceAccessType::CopyDest);
        accumulate(params.indirectArgumentBuffers, rhi::ResourceAccessType::IndirectArgument);
        accumulate(params.legacyInteropResources, rhi::ResourceAccessType::Common);

        // Build a tracker for each resource, applying each (range->state)
        constexpr ResourceState initialState{
            rhi::ResourceAccessType::Common,
            rhi::ResourceLayout::Common,
            rhi::ResourceSyncState::All
        };

        std::unordered_map<uint64_t,SymbolicTracker> trackers;
        std::unordered_map<uint64_t,ResourceRegistry::RegistryHandle> handleMap;

        for(auto& [rar, flag] : entries) {
            uint64_t id = rar.resource.GetGlobalResourceID();
            handleMap[id] = rar.resource;

            // Create a tracker spanning "all" with initial NONE state
            auto [it, inserted] = trackers.try_emplace(
                id,
                /*whole=*/ RangeSpec{},
                /*init=*/  initialState
            );
            auto& tracker = it->second;

            // Find the desired state for this entry
            ResourceState want {
                flag,
                AccessToLayout(flag, /*isRender=*/true),
                RenderSyncFromAccess(flag)
            };

            // Apply- use a dummy vector since we don't need per-pass transitions here
            std::vector<ResourceTransition> dummy;
            tracker.Apply(rar.range, nullptr, want, dummy);
        }

        // Flatten each tracker’s segments into a ResourceRequirement
        std::vector<ResourceRequirement> out;
        out.reserve(trackers.size());  // rough

        for(auto& [id, tracker] : trackers) {
            auto pRes = handleMap[id];
            for(auto const& seg : tracker.GetSegments()) {
                if (seg.state.access == rhi::ResourceAccessType::Common && seg.state.layout == rhi::ResourceLayout::Common) {
                    //continue; // TODO: Will we ever need explicit transitions to common for declared resources?
                }
                // build a ResourceAndRange for this segment
                ResourceHandleAndRange rr(pRes);
                rr.range = seg.rangeSpec;

                ResourceRequirement req(rr);
                req.state = seg.state;
                out.push_back(std::move(req));
            }
        }

        return out;
    }

    // storage
    RenderGraph*             graph;
    std::string              passName;
    RenderPassParameters     params;
	std::shared_ptr<RenderPass> pass;
    bool built_ = false;
    bool m_isGeometryPass = false;
    std::unordered_set<ResourceIdentifier, ResourceIdentifier::Hasher> _declaredIds;

    friend class RenderGraph; // Allow RenderGraph to create instances of this builder
};

template<typename T>
concept DerivedComputePass = std::derived_from<T, ComputePass>;

class ComputePassBuilder : public IPassBuilder {
public:
    PassBuilderKind Kind() const noexcept override { return PassBuilderKind::Compute; }
    IResourceProvider* ResourceProvider() noexcept override { return pass.get(); }
    // Variadic entry points

    //First set, callable on Lvalues
    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    ComputePassBuilder& WithShaderResource(Args&&... args) & {
        (addShaderResource(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    ComputePassBuilder& WithConstantBuffer(Args&&... args) & {
        (addConstantBuffer(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    ComputePassBuilder& WithUnorderedAccess(Args&&... args) & {
        (addUnorderedAccess(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    ComputePassBuilder& WithIndirectArguments(Args&&... args) & {
        (addIndirectArguments(std::forward<Args>(args)), ...);
        return *this;
    }

    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    ComputePassBuilder& WithLegacyInterop(Args&&... args)& {
        (addLegacyInterop(std::forward<Args>(args)), ...);
        return *this;
    }

    ComputePassBuilder& WithInternalTransition(
        ResourceIdentifierAndRange rar,
        ResourceState exitState) &
    {
        addInternalTransition(rar, exitState);
        return *this;
    }

    // Second set, callable on temporaries
    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    ComputePassBuilder WithShaderResource(Args&&... args) && {
        (addShaderResource(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    ComputePassBuilder WithConstantBuffer(Args&&... args) && {
        (addConstantBuffer(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    ComputePassBuilder WithUnorderedAccess(Args&&... args) && {
        (addUnorderedAccess(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    ComputePassBuilder WithIndirectArguments(Args&&... args) && {
        (addIndirectArguments(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    template<typename... Args>
        requires ((NotIResourceResolver<Args>) && ...)
    ComputePassBuilder WithLegacyInterop(Args&&... args)&& {
        (addLegacyInterop(std::forward<Args>(args)), ...);
        return std::move(*this);
    }

    ComputePassBuilder WithInternalTransition(
        ResourceIdentifierAndRange rar,
        ResourceState exitState) &&
    {
		addInternalTransition(rar, exitState);
        return std::move(*this);
    }

    // LVALUE overloads for IResourceResolver
    ComputePassBuilder& WithShaderResource(const IResourceResolver& r)& {
		auto resources = r.Resolve();
		for (auto& res : resources) {
			graph->AddResource(res);
		}
		(addShaderResource(r.Resolve()));
        return *this;
    }

    ComputePassBuilder& WithConstantBuffer(const IResourceResolver& r)& {
		auto resources = r.Resolve();
		for (auto& res : resources) {
			graph->AddResource(res);
		}
		(addConstantBuffer(r.Resolve()));
        return *this;
    }

    ComputePassBuilder& WithUnorderedAccess(const IResourceResolver& r)& {
		auto resources = r.Resolve();
		for (auto& res : resources) {
			graph->AddResource(res);
		}
		(addUnorderedAccess(r.Resolve()));
        return *this;
    }

    ComputePassBuilder& WithIndirectArguments(const IResourceResolver& r)& {
		auto resources = r.Resolve();
		for (auto& res : resources) {
			graph->AddResource(res);
		}
		(addIndirectArguments(r.Resolve()));
        return *this;
    }

    ComputePassBuilder& WithLegacyInterop(const IResourceResolver& r)& {
		auto resources = r.Resolve();
        for (auto& res : resources) {
			graph->AddResource(res);
		}
		(addLegacyInterop(r.Resolve()));
        return *this;
    }

    // RVALUE overloads for IResourceResolver

    ComputePassBuilder WithShaderResource(const IResourceResolver& r)&& {
		auto resources = r.Resolve();
		for (auto& res : resources) {
			graph->AddResource(res);
		}
		(addShaderResource(r.Resolve()));
        return std::move(*this);
    }
    ComputePassBuilder WithConstantBuffer(const IResourceResolver& r)&& {
		auto resources = r.Resolve();
		for (auto& res : resources) {
			graph->AddResource(res);
		}
		(addConstantBuffer(r.Resolve()));
        return std::move(*this);
    }
    ComputePassBuilder WithUnorderedAccess(const IResourceResolver& r)&& {
		auto resources = r.Resolve();
		for (auto& res : resources) {
			graph->AddResource(res);
		}
		(addUnorderedAccess(r.Resolve()));
        return std::move(*this);
    }
    ComputePassBuilder WithIndirectArguments(const IResourceResolver& r)&& {
		auto resources = r.Resolve();
        for (auto& res : resources) {
            graph->AddResource(res);
        }
		(addIndirectArguments(r.Resolve()));
        return std::move(*this);
    }
    ComputePassBuilder WithLegacyInterop(const IResourceResolver& r)&& {
		auto resources = r.Resolve();
        for (auto& res : resources) {
            graph->AddResource(res);
        }
		(addLegacyInterop(r.Resolve()));
        return std::move(*this);
    }

    // First build, callable on Lvalues
    template<DerivedComputePass PassT, rg::PassInputs InputsT>
    void Build(InputsT&& inputs) & {
        if (!built_) {
            built_ = true;
            pass = std::make_shared<PassT>();
        }
        pass->SetInputs(std::forward<InputsT>(inputs));
    }

    // Second build, callable on temporaries
    template<DerivedComputePass PassT, rg::PassInputs InputsT>
    void Build(InputsT&& inputs) && {
        if (!built_) {
            built_ = true;
            pass = std::make_shared<PassT>();
        }
        pass->SetInputs(std::forward<InputsT>(inputs));
    }

    template<DerivedComputePass PassT, typename... StableCtorArgs>
    void Build(StableCtorArgs&&... ctorArgs)& {
        Build<PassT>(rg::NoInputs{}, std::forward<StableCtorArgs>(ctorArgs)...);
    }

    template<DerivedComputePass PassT, typename... StableCtorArgs>
    void Build(StableCtorArgs&&... ctorArgs)&& {
        Build<PassT>(rg::NoInputs{}, std::forward<StableCtorArgs>(ctorArgs)...);
    }

    auto const& DeclaredResourceIds() const { return _declaredIds; }

private:
    ComputePassBuilder(RenderGraph* g, std::string name)
        : graph(g), passName(std::move(name)) {}

    // Copy and move constructors are default, but private
    ComputePassBuilder(const ComputePassBuilder&) = default;
    ComputePassBuilder(ComputePassBuilder&&) = default;

    // Same for assignment
    ComputePassBuilder& operator=(const ComputePassBuilder&) = default;
    ComputePassBuilder& operator=(ComputePassBuilder&&) = default;

    void Finalize() {
        if (!built_) return;

        params = {}; // Reset params to clear any resources from previous build

        pass->DeclareResourceUsages(this);

        params.identifierSet = _declaredIds;
        params.resourceRequirements = GatherResourceRequirements();

        graph->AddComputePass(pass, params, passName);
    }

    void Reset() {
        built_ = false;
        pass = nullptr;
        params = {};
        _declaredIds.clear();
    }

    // Shader resource
	template<typename T>
        requires ResourceLike<T>
	ComputePassBuilder& addShaderResource(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
		auto ranges = processResourceArguments(std::forward<T>(x), graph);
		for (auto& r : ranges) {
			//if (!r.resource) continue;
			params.shaderResources.push_back(r);
		}
		return *this;
	}
    template<class Range>
        requires (std::ranges::range<Range>&&
    ResourceLike<std::ranges::range_value_t<Range>>)
        ComputePassBuilder& addShaderResource(Range&& xs) {
        for (auto&& e : xs) {
            addShaderResource(std::forward<decltype(e)>(e));
        }
        return *this;
    }

    // Constant buffer
	template<typename T>
        requires ResourceLike<T>
	ComputePassBuilder& addConstantBuffer(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
		auto ranges = processResourceArguments(std::forward<T>(x), graph);
		for (auto& r : ranges) {
			//if (!r.resource) continue;
			params.constantBuffers.push_back(r);
		}
		return *this;
	}
    template<class Range>
		requires (std::ranges::range<Range>&&
	ResourceLike<std::ranges::range_value_t<Range>>)
        ComputePassBuilder& addConstantBuffer(Range&& xs) {
		for (auto&& e : xs) {
			addConstantBuffer(std::forward<decltype(e)>(e));
		}
		return *this;
	}

    // Unordered access
	template<typename T>
        requires ResourceLike<T>
	ComputePassBuilder& addUnorderedAccess(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
		auto ranges = processResourceArguments(std::forward<T>(x), graph);
		for (auto& r : ranges) {
			//if (!r.resource) continue;
			params.unorderedAccessViews.push_back(r);
		}
		return *this;
	}
    template<class Range>
		requires (std::ranges::range<Range>&&
	ResourceLike<std::ranges::range_value_t<Range>>)
        ComputePassBuilder& addUnorderedAccess(Range&& xs) {
        for (auto&& e : xs) {
            addUnorderedAccess(std::forward<decltype(e)>(e));
        }
        return *this;
	}

	// Indirect arguments
	template<typename T>
        requires ResourceLike<T>
	ComputePassBuilder& addIndirectArguments(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
		auto ranges = processResourceArguments(std::forward<T>(x), graph);
		for (auto& r : ranges) {
			//if (!r.resource) continue;
			params.indirectArgumentBuffers.push_back(r);
		}
		return *this;
	}
	template<class Range>
		requires (std::ranges::range<Range>&&
	ResourceLike<std::ranges::range_value_t<Range>>)
        ComputePassBuilder& addIndirectArguments(Range&& xs) {
		for (auto&& e : xs) {
			addIndirectArguments(std::forward<decltype(e)>(e));
		}
		return *this;
	}

    // Legacy interop resources
    template<typename T>
        requires ResourceLike<T>
    ComputePassBuilder& addLegacyInterop(T&& x) {
        detail::extractId(_declaredIds, std::forward<T>(x));
        auto ranges = processResourceArguments(std::forward<T>(x), graph);
        for (auto& r : ranges) {
            //if (!r.resource) continue;
            params.legacyInteropResources.push_back(r);
        }
        return *this;
    }
    template<class Range>
		requires (std::ranges::range<Range>&&
    ResourceLike<std::ranges::range_value_t<Range>>)
        ComputePassBuilder& addLegacyInterop(Range&& xs) {
        for (auto&& e : xs) {
            addLegacyInterop(std::forward<decltype(e)>(e));
        }
		return *this;
	}

    ComputePassBuilder& addInternalTransition(
        ResourceIdentifierAndRange rar,
        ResourceState exitState) &
    {
		auto ranges = processResourceArguments(rar, graph);
        for (auto& r : ranges) {
            //if (!r.resource) continue;
            params.internalTransitions.emplace_back(r, exitState);
		}
        return *this;
	}
	template<class Range>
		requires (std::ranges::range<Range>&&
    ResourceLike<std::ranges::range_value_t<Range>>)
        ComputePassBuilder& addInternalTransition(
            Range&& xs,
            ResourceState exitState) {
        for (auto&& e : xs) {
            addInternalTransition(std::forward<decltype(e)>(e), exitState);
        }
        return *this;
	}

    std::vector<ResourceRequirement> GatherResourceRequirements() const {
        // Collect every (ResourceAndRange,AccessFlag) pair from all the With* lists
        std::vector<std::pair<ResourceHandleAndRange, rhi::ResourceAccessType>> entries;
        entries.reserve(
            params.shaderResources.size()
            + params.constantBuffers.size()
            + params.unorderedAccessViews.size()
            + params.indirectArgumentBuffers.size()
        );

        auto accumulate = [&](auto const& list, rhi::ResourceAccessType flag){
            for(auto const& rr : list){
                //if(!rr.resource) continue;
                entries.emplace_back(rr, flag);
            }
            };

        accumulate(params.shaderResources, rhi::ResourceAccessType::ShaderResource);
        accumulate(params.constantBuffers, rhi::ResourceAccessType::ConstantBuffer);
        accumulate(params.unorderedAccessViews, rhi::ResourceAccessType::UnorderedAccess);
        accumulate(params.indirectArgumentBuffers, rhi::ResourceAccessType::IndirectArgument);
        accumulate(params.legacyInteropResources, rhi::ResourceAccessType::Common);

        // Build a tracker for each resource, applying each (range->state)
        constexpr ResourceState initialState{
            rhi::ResourceAccessType::Common,
            rhi::ResourceLayout::Common,
            rhi::ResourceSyncState::All
        };

        std::unordered_map<uint64_t,SymbolicTracker> trackers;
        std::unordered_map<uint64_t,ResourceRegistry::RegistryHandle> handleMap;

        for(auto& [rar, flag] : entries) {
            uint64_t id = rar.resource.GetGlobalResourceID();
            handleMap[id] = rar.resource;

            // Create a tracker spanning "all" with initial NONE state
            auto [it, inserted] = trackers.try_emplace(
                id,
                /*whole=*/ RangeSpec{},
                /*init=*/  initialState
            );
            auto& tracker = it->second;

            // Find the desired state for this entry
            ResourceState want {
                flag,
                AccessToLayout(flag, /*isRender=*/true),
                ComputeSyncFromAccess(flag)
            };

            // Apply- use a dummy vector since we don't need per-pass transitions here
            std::vector<ResourceTransition> dummy;
            tracker.Apply(rar.range, nullptr, want, dummy);
        }

        // Flatten each tracker’s segments into a ResourceRequirement
        std::vector<ResourceRequirement> out;
        out.reserve(trackers.size());  // rough

        for(auto& [id, tracker] : trackers) {
            auto pRes = handleMap[id];
            for(auto const& seg : tracker.GetSegments()) {
                if (seg.state.access == rhi::ResourceAccessType::Common && seg.state.layout == rhi::ResourceLayout::Common) {
                    //continue; // TODO: Will we ever need explicit transitions to common for declared resources?
                }
                // build a ResourceAndRange for this segment
                ResourceHandleAndRange rr(pRes);
                rr.range = seg.rangeSpec;

                ResourceRequirement req(rr);
                req.state = seg.state;
                out.push_back(std::move(req));
            }
        }

        return out;
    }

    // storage
    RenderGraph*             graph;
    std::string              passName;
    ComputePassParameters     params;
    std::shared_ptr<ComputePass> pass;
    bool built_ = false;
    std::unordered_set<ResourceIdentifier, ResourceIdentifier::Hasher> _declaredIds;

	friend class RenderGraph; // Allow RenderGraph to create instances of this builder
};
# OpenRenderGraph

OpenRenderGraph (ORG) is a powerful rendering/GPU-compute framework, built around a render graph.

ORG provides a pass-oriented API for building frame pipelines, tracking resource state/transitions, handling queue synchronization, planning automatic resource aliasing on eligible resources, and executing graphics/compute work with explicit resource usage contracts.

## What ORG does

- Builds and executes frame graphs from render and compute passes.
- Tracks resource access/layout transitions and batch ordering.
- Exposes runtime services for upload, readback, statistics, and graph settings.
- Includes memory introspection and aliasing planning/debug support.
- Includes optional ImGui-based debug UI widgets for graph and memory inspection.

## Include model

### Main API

```cpp
#include "OpenRenderGraph/OpenRenderGraph.h"
```

Use this for graph composition, pass construction, resource APIs, queue/runtime interfaces, and memory introspection APIs.

### Debug UI widgets (ImGui)

```cpp
#include "OpenRenderGraph/DebugUI.h"
```

Provides:

- `ui::MemoryViewWidget`
- `ui::RenderGraphInspector`
- `ui::MemoryIntrospectionWidget`

## Basic usage

### 1) Create a graph instance

```cpp
auto graph = std::make_unique<RenderGraph>();
```

### 2) Resource creation and registration

Create resources (buffer/texture), configure descriptors and metadata, then register them with a `ResourceIdentifier`.

```cpp
auto counterBuffer = Buffer::CreateUnmaterializedStructuredBuffer(
	1,
	sizeof(uint32_t),
	/*unorderedAccess=*/true,
	/*counterBuffer=*/false,
	/*nonShaderVisibleUAV=*/false,
	rhi::HeapType::DeviceLocal);

counterBuffer->SetName("MyCounter");
graph->RegisterResource("Builtin::Counters::MyCounter", counterBuffer);

TextureDescription hdrDesc{};
hdrDesc.format = rhi::Format::R16G16B16A16_Float;
hdrDesc.hasRTV = true;
hdrDesc.hasSRV = true;
hdrDesc.imageDimensions.emplace_back(1920, 1080, 0, 0);

auto hdrTarget = PixelBuffer::CreateSharedUnmaterialized(hdrDesc);
hdrTarget->SetName("HDR Target");
graph->RegisterResource("Builtin::Color::HDR", hdrTarget);
```

#### Materialized vs unmaterialized resources

ORG resource factories commonly have both forms:

- Materialized: `CreateShared(...)`
- Unmaterialized: `CreateSharedUnmaterialized(...)` or `CreateUnmaterializedStructuredBuffer(...)`

Unmaterialized means the resource object exists in the graph, but the underlying GPU allocation is deferred until the graph/runtime needs it.

Use unmaterialized when:

- the resource may not be used every frame,
- the graph can alias/transiently schedule it,
- you want lower upfront allocation pressure.

Use materialized when:

- you need the real GPU object immediately,
- the resource lifetime is long-lived and always resident,
- external code requires the backing allocation right away.

Practically: default to unmaterialized for resources that the GPU will be managing, and to materialized for resources that you will be uploading data to from the CPU.

### 3) Providers: what they are and why they exist

A provider is any object implementing `IResourceProvider` that can supply resources/resolvers on demand for known keys. This keeps the graph decoupled from owning systems (streaming managers, scene systems, asset/runtime caches).

When the graph requests a key, providers can lazily return an existing resource (or construct one), instead of forcing every resource to be eagerly registered up front.

```cpp
class MyProvider final : public IResourceProvider {
public:
	std::shared_ptr<Resource> ProvideResource(ResourceIdentifier const& key) override {
		if (key.ToString() == "Builtin::Color::HDR") {
			return m_hdr;
		}
		return nullptr;
	}

	std::vector<ResourceIdentifier> GetSupportedKeys() override {
		return { ResourceIdentifier("Builtin::Color::HDR") };
	}

private:
	std::shared_ptr<PixelBuffer> m_hdr;
};

graph->RegisterProvider(myProvider);
```

### 4) Minimal pass example

A pass declares resource usages in `DeclareResourceUsages(...)` and provides work in `Execute(...)`.

```cpp
struct ClearCounterInputs {
	ResourceHandleAndRange counter;
};

inline rg::Hash64 HashValue(const ClearCounterInputs& i) {
	return static_cast<rg::Hash64>(i.counter.resource.GetGlobalResourceID());
}

inline bool operator==(const ClearCounterInputs& a, const ClearCounterInputs& b) {
	return a.counter.resource.GetGlobalResourceID() == b.counter.resource.GetGlobalResourceID();
}

class ClearCounterPass final : public ComputePass {
public:
	explicit ClearCounterPass(ClearCounterInputs in) {
		SetInputs(std::move(in));
	}

	void DeclareResourceUsages(ComputePassBuilder* builder) override {
		const auto& in = Inputs<ClearCounterInputs>();
		builder->WithUnorderedAccess(in.counter);
	}

	void Setup() override {}

	void Execute(PassExecutionContext& context) override {
		(void)context;
		// Record compute work here.
	}

	void Cleanup() override {}
};
```
### 5) Build passes

Use `BuildRenderPass(...)` / `BuildComputePass(...)` to add pass instances into the graph.

```cpp
graph->BuildComputePass("ClearCounter")
	.Build<ClearCounterPass>(ClearCounterInputs{
		.counter = graph->RequestResourceHandle("Builtin::Counters::MyCounter")
	});

graph->BuildRenderPass("MainLightingPass")
	.Build<DeferredShadingPass>();
```

### 6) Optional: register graph extensions

Where the basic pass API relies on explicit ordering to determine producers and consumers, Extensions inject structural or frame-local passes in a more flexible manner. 

During render graph compile, any extension that has been registered with the graph will be queried for the passes it wishes to add, and it may inject those at any point in the graph. Beginning and end insertions are handled with explicit Begin and End markers, and pass-relative insertion references "Before" and/or "after" anchor passes by name.

Passes added by extensions observe local ordering when no explicit ordering is declared- passes which do not declare an achor point will be assumed to come "after" the previous pass which was added by that extension, if one exists.

Minimal custom extension example:

```cpp
class MyRenderGraphExtension final : public RenderGraph::IRenderGraphExtension {
public:
	void OnRegistryReset(ResourceRegistry* registry) override {
		// Optional: refresh any resolver/provider context that depends on the registry.
		(void)registry;
	}

	void GatherStructuralPasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& out) override {
		(void)rg;

		RenderGraph::ExternalPassDesc desc{};
		desc.type = RenderGraph::PassType::Compute;
		desc.name = "MyExtension::SetupPass";
		desc.where = RenderGraph::ExternalInsertPoint::Begin(/*priority*/10);
		desc.pass = std::make_shared<MySetupComputePass>();
		out.push_back(std::move(desc));
	}

	void GatherFramePasses(RenderGraph& rg, std::vector<RenderGraph::ExternalPassDesc>& out) override {
		(void)rg;
		(void)out;
		// Optional: emit per-frame ephemeral passes (captures, probes, etc.).
	}
};

// Registration:
graph->RegisterExtension(std::make_unique<MyRenderGraphExtension>());
```

### 7) Per-frame update/execute

```cpp
UpdateExecutionContext updateCtx{};
updateCtx.frameIndex = frameIndex;
updateCtx.frameFenceValue = frameFenceValue;
updateCtx.deltaTime = deltaTime;

graph->Update(updateCtx, device);

PassExecutionContext execCtx{};
// fill queue/lists/timelines as required by your backend integration
graph->Execute(execCtx);
```

## Runtime services

ORG exposes service interfaces through `RenderGraph`:

- `GetUploadService()`
- `GetReadbackService()`
- `GetStatisticsService()`
- `GetRenderGraphSettingsService()`

Integrators typically initialize needed services during startup, then consume them during update/execute and teardown.

## Debug UI usage (ImGui)

Widgets can be used directly if your app already has an ImGui frame:

```cpp
ui::MemoryIntrospectionWidget memWidget;
memWidget.PushFrameSample(timeSeconds, totalBytes);
memWidget.Draw(&open, &memorySnapshot, &frameGraphSnapshot);

RGInspector::Show(graph->GetBatches(), ...);
```

For capture requests from `RGInspector`, pass a callback that forwards to `IReadbackService::RequestReadbackCapture(...)`.

## CMake notes

`OpenRenderGraph` is built as a static library in this workspace and links against:

- `BasicRHI`
- `flecs`
- `Boost::container_hash`
- `spdlog`
- `imgui`
- `implot`
- `slang`

If you consume ORG in another project, ensure equivalent dependencies are available.

## Packaging and standalone usage

`OpenRenderGraph` exports a package target:

- `OpenRenderGraph::OpenRenderGraph`

and expects `BasicRHI::BasicRHI` to be available (installed package preferred).

### Build and install ORG

```powershell
cmake -S OpenRenderGraph -B out/build/org -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="out/install/rhi"
cmake --build out/build/org
cmake --install out/build/org --prefix out/install/org
```

### Consume ORG from another CMake project

```cmake
find_package(OpenRenderGraph CONFIG REQUIRED)
target_link_libraries(MyTarget PRIVATE OpenRenderGraph::OpenRenderGraph)
```

### Submodule fallback mode

If `BasicRHI` is not installed, ORG can fallback to an in-tree sibling `../BasicRHI`:

- `OPENRENDERGRAPH_ENABLE_SUBMODULE_FALLBACK=ON` (default)

When fallback is used, you can forward BasicRHI manual dependency options:

- `OPENRENDERGRAPH_FORWARD_BASICRHI_DEP_OPTIONS=ON` (default)
- `OPENRENDERGRAPH_BASICRHI_ENABLE_STREAMLINE=ON|OFF`
- `OPENRENDERGRAPH_BASICRHI_ENABLE_PIX=ON|OFF`
- `OPENRENDERGRAPH_BASICRHI_STREAMLINE_HEADERS_DIR=<path>`
- `OPENRENDERGRAPH_BASICRHI_PIX_HEADERS_DIR=<path>`


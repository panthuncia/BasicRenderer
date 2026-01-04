#pragma once


#include <directx/d3d12.h>
#include <rhi.h>
#include <rhi_interop_dx12.h>
#include <memory>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>
#include <implot.h>
#include <functional>
#include <spdlog/spdlog.h>
#include <windows.h>
#include <filesystem>
#include <flecs.h>
#include <vector>
#include <algorithm>
#include <chrono>

#include "Render/RenderContext.h"
#include "Utilities/Utilities.h"
#include "Render/OutputTypes.h"
#include "Import/ModelLoader.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Render/TonemapTypes.h"
#include "Managers/Singletons/StatisticsManager.h"
#include "Managers/Singletons/UpscalingManager.h"
#include "Menu/RenderGraphInspector.h"
#include "Menu/MemoryIntrospectionWidget.h"
#include "Resources/MemoryStatisticsComponents.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static inline const char* MajorCategory(rhi::ResourceType t) {
    using RT = rhi::ResourceType;
    switch (t) {
    case RT::Buffer:                return "Buffers";
    case RT::Texture1D:             return "Textures";
    case RT::Texture2D:             return "Textures";
    case RT::Texture3D:             return "Textures";
    case RT::AccelerationStructure: return "AccelStructs";
    default:                        return "Other";
    }
}

struct PerResourceMemInfo {
    uint64_t bytes = 0;
    std::string category; // "Textures/Material", etc.
    std::string name;     // optional
};

using PerResourceMemIndex = std::unordered_map<uint64_t, PerResourceMemInfo>;

static void BuildMemorySnapshotFromFlecs(
    ui::MemorySnapshot& out,
    flecs::query<const MemoryStatisticsComponents::MemSizeBytes>& q,
    PerResourceMemIndex* outIndex /*= nullptr*/)
{
    out.categories.clear();
    out.resources.clear();
    out.totalBytes = 0;

    std::unordered_map<std::string, uint64_t> minorBuckets;
    minorBuckets.reserve(256);

    if (outIndex) { outIndex->clear(); outIndex->reserve(2048); }

    q.each([&](flecs::entity e, const MemoryStatisticsComponents::MemSizeBytes& sz) {
        auto rt = e.try_get<MemoryStatisticsComponents::ResourceType>();
        auto rname = e.try_get<MemoryStatisticsComponents::ResourceName>();
        auto rid = e.try_get<MemoryStatisticsComponents::ResourceID>();
        auto use = e.try_get<MemoryStatisticsComponents::ResourceUsage>();

        const uint64_t bytes = sz.size;
        out.totalBytes += bytes;

        const char* major = rt ? MajorCategory(rt->type)
            : MajorCategory(rhi::ResourceType::Unknown);

        const char* usage = (use && !use->usage.empty()) ? use->usage.c_str()
            : "Unspecified";

        const std::string cat = std::string(major) + "/" + usage;

        minorBuckets[cat] += bytes;

        // Per-resource row (unchanged)
        ui::MemoryResourceRow row{};
        row.bytes = bytes;
        row.uid = rid ? rid->id : 0;

        if (rname && !rname->name.empty()) row.name = rname->name;
        else if (rid) row.name = "Resource " + std::to_string((unsigned long long)rid->id);
        else row.name = "Unknown resource";

        row.type = cat;
        out.resources.push_back(row);

        // NEW: per-resource index entry (this is what the frame-graph builder will use)
        if (outIndex && rid) {
            auto& info = (*outIndex)[rid->id];
            info.bytes = bytes;
            info.category = cat;
            if (rname && !rname->name.empty()) info.name = rname->name;
        }
        });

    out.categories.reserve(minorBuckets.size());
    for (auto& [label, bytes] : minorBuckets) {
        if (bytes) out.categories.push_back({ label, bytes });
    }

    std::sort(out.categories.begin(), out.categories.end(),
        [](auto const& a, auto const& b) { return a.bytes > b.bytes; });
}

struct MemInfo {
    uint64_t bytes = 0;
    std::string name; // from ResourceIdentifier.name (string ID)
};

static void BuildIdToMemInfoIndex(
    std::unordered_map<uint64_t, MemInfo>& out,
    flecs::query<const MemoryStatisticsComponents::MemSizeBytes>& q)
{
    out.clear();
    out.reserve(2048);

    q.each([&](flecs::entity e, const MemoryStatisticsComponents::MemSizeBytes& sz) {
        auto rid = e.try_get<MemoryStatisticsComponents::ResourceID>();
        if (!rid) return; // can't join without numeric id

        MemInfo info;
        info.bytes = sz.size;

        if (auto ident = e.try_get<ResourceIdentifier>()) {
            // ident->name should already be "A::B::C"
            info.name = ident->name;
        }

        out[rid->id] = std::move(info);
        });
}

static void BuildFrameGraphSnapshotFromBatches(
    ui::FrameGraphSnapshot& out,
    const std::vector<RenderGraph::PassBatch>& batches,
    const PerResourceMemIndex& memIndex)
{
    out.batches.clear();
    out.batches.reserve(batches.size());

    std::unordered_set<uint64_t> uniqueIds;
    uniqueIds.reserve(2048);

    std::unordered_map<std::string, uint64_t> catSum;
    catSum.reserve(64);

    for (int bi = 0; bi < (int)batches.size(); ++bi) {
        const auto& b = batches[bi];

        uniqueIds.clear();

        auto scanTransitions = [&](const std::vector<ResourceTransition>& v) {
            for (auto& t : v) {
                if (!t.pResource) continue;
                uniqueIds.insert(t.pResource->GetGlobalResourceID());
            }
            };

        scanTransitions(b.renderTransitions);
        scanTransitions(b.computeTransitions);
        scanTransitions(b.batchEndTransitions);

        for (auto id : b.allResources) uniqueIds.insert(id);
        for (auto id : b.internallyTransitionedResources) uniqueIds.insert(id);

        uint64_t footprint = 0;
        catSum.clear();

        uint64_t missingBytes = 0;

        for (uint64_t id : uniqueIds) {
            auto it = memIndex.find(id);
            if (it == memIndex.end()) {
                // ?
                continue;
            }

            footprint += it->second.bytes;
            catSum[it->second.category] += it->second.bytes;
        }

        ui::FrameGraphBatchRow row{};
        row.label = "Batch " + std::to_string(bi);
        row.footprintBytes = footprint;
        row.hasEndTransitions = !b.batchEndTransitions.empty();

        row.passNames.reserve(b.computePasses.size() + b.renderPasses.size());
        for (auto const& p : b.computePasses) row.passNames.push_back(p.name);
        for (auto const& p : b.renderPasses)  row.passNames.push_back(p.name);

        row.categories.reserve(catSum.size());
        for (auto& [label, bytes] : catSum) {
            row.categories.push_back({ label, bytes });
        }
        std::sort(row.categories.begin(), row.categories.end(),
            [](auto const& a, auto const& b) { return a.bytes > b.bytes; });

        out.batches.push_back(std::move(row));
    }
}


class Menu {
public:
    static Menu& GetInstance();

    void Initialize(HWND hwnd, IDXGISwapChain3* swapChain);
    void Render(RenderContext& context);
    bool HandleInput(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	void SetRenderGraph(RenderGraph* renderGraph) { m_renderGraph = renderGraph; }
    void Cleanup() {
		m_settingSubscriptions.clear();
        m_memQuery = {}; // Destruct query before ECS world dies
		m_sizeQuery = {};
    }

private:
    rhi::DescriptorHeapPtr g_pd3dSrvDescHeap;
    Menu() { 
        ImGui::CreateContext();
		ImPlot::CreateContext();
    };
	IDXGISwapChain3* m_pSwapChain = nullptr;
	
    flecs::query<const MemoryStatisticsComponents::MemSizeBytes> m_memQuery;
	flecs::query<const MemoryStatisticsComponents::MemSizeBytes,
        const MemoryStatisticsComponents::ResourceID> m_sizeQuery =
        ECSManager::GetInstance().GetWorld().query<const MemoryStatisticsComponents::MemSizeBytes,
        const MemoryStatisticsComponents::ResourceID>();

	flecs::entity selectedNode;

	RenderGraph* m_renderGraph;

    int FindFileIndex(const std::vector<std::string>& hdrFiles, const std::string& existingFile);

    void DrawEnvironmentsDropdown();
	void DrawOutputTypeDropdown();
    void DrawUpscalingCombo();
    void DrawUpscalingQualityCombo();
    void DrawTonemapTypeDropdown();
    void DrawBrowseButton(const std::wstring& targetDirectory);
    void DrawLoadModelButton();
    void DisplaySceneNode(flecs::entity node, bool isOnlyChild);
    void DisplaySceneGraph();
    void DisplaySelectedNode();
    void DrawPassTimingWindow();

    std::chrono::steady_clock::time_point m_startTime = std::chrono::steady_clock::now();

	bool m_meshShadersSupported = false;
    
    std::filesystem::path environmentsDir;

    std::string environmentName;
    std::vector<std::string> hdrFiles;

	std::function<std::string()> getEnvironmentName;
	std::function<void(std::string)> setEnvironment;

	bool imageBasedLightingEnabled = false;
    std::function<bool()> getImageBasedLightingEnabled;
    std::function<void(bool)> setImageBasedLightingEnabled;

	bool punctualLightingEnabled = false;
	std::function<bool()> getPunctualLightingEnabled;
	std::function<void(bool)> setPunctualLightingEnabled;

    bool shadowsEnabled = false;
	std::function<bool()> getShadowsEnabled;
	std::function<void(bool)> setShadowsEnabled;

	std::function<void(unsigned int)> setOutputType;
    std::function<void(unsigned int)> setTonemapType;
	std::function<unsigned int()> getTonemapType;

    bool meshShaderEnabled = false;
    bool indirectDrawsWereEnabled = false;
    std::function<bool()> getMeshShaderEnabled;
	std::function<void(bool)> setMeshShaderEnabled;

	bool indirectDrawsEnabled = false;
	std::function<bool()> getIndirectDrawsEnabled;
	std::function<void(bool)> setIndirectDrawsEnabled;

	bool occlusionCulling = true;
	std::function<bool()> getOcclusionCullingEnabled;
	std::function<void(bool)> setOcclusionCullingEnabled;

	bool meshletCulling = true;
	std::function<bool()> getMeshletCullingEnabled;
	std::function<void(bool)> setMeshletCullingEnabled;

    bool wireframeEnabled = false;
	std::function<bool()> getWireframeEnabled;
	std::function<void(bool)> setWireframeEnabled;

    std::function<flecs::entity ()> getSceneRoot;

    bool allowTearing = false;
	std::function<bool()> getAllowTearing;
    std::function<void(bool)> setAllowTearing;

    bool drawBoundingSpheres = false;
	std::function<bool()> getDrawBoundingSpheres;
	std::function<void(bool)> setDrawBoundingSpheres;

    bool clusteredLighting = true;
	std::function<bool()> getClusteredLightingEnabled;
	std::function<void(bool)> setClusteredLightingEnabled;

	bool m_visibilityRenderingEnabled = true;
	std::function<bool()> getVisibilityRenderingEnabled;
	std::function<void(bool)> setVisibilityRenderingEnabled;

	bool m_gtaoEnabled = true;
	std::function<bool()> getGTAOEnabled;
	std::function<void(bool)> setGTAOEnabled;

	bool m_bloomEnabled = true;
	std::function<bool()> getBloomEnabled;
	std::function<void(bool)> setBloomEnabled;

	bool m_screenSpaceReflectionsEnabled = true;
	std::function<bool()> getScreenSpaceReflectionsEnabled;
	std::function<void(bool)> setScreenSpaceReflectionsEnabled;

    bool m_jitterEnabled = true;
    std::function<bool()> getJitterEnabled;
    std::function<void(bool)> setJitterEnabled;

	bool m_collectPipelineStatistics = false;
	std::function<bool()> getCollectPipelineStatistics;
    std::function<void(bool)> setCollectPipelineStatistics;

	UpscalingMode m_currentUpscalingMode = UpscalingMode::None;
	std::function<UpscalingMode()> getUpscalingMode;
	std::function<void(UpscalingMode)> setUpscalingMode;

	UpscaleQualityMode m_currentUpscalingQualityMode = UpscaleQualityMode::Balanced;
	std::function<UpscaleQualityMode()> getUpscalingQualityMode;
    std::function<void(UpscaleQualityMode)> setUpscalingQualityMode;

	bool m_useAsyncCompute = true;
	std::function<bool()> getUseAsyncCompute;
    std::function<void(bool)> setUseAsyncCompute;

	std::function<std::shared_ptr<Scene>(std::shared_ptr<Scene>)> appendScene;
	std::vector<SettingsManager::Subscription> m_settingSubscriptions;
};

inline Menu& Menu::GetInstance() {
    static Menu instance;
    return instance;
}

inline bool Menu::HandleInput(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return static_cast<bool>(ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam));
}

inline void Menu::Initialize(HWND hwnd, IDXGISwapChain3* swapChain) {
	m_pSwapChain = swapChain;
	auto numFramesInFlight = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numFramesInFlight")();

    environmentsDir = std::filesystem::path(GetExePath()) / "textures" / "environment";

	auto device = DeviceManager::GetInstance().GetDevice();
	auto result = device.CreateDescriptorHeap({ rhi::DescriptorHeapType::CbvSrvUav, 1, true }, g_pd3dSrvDescHeap);

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX12_Init(rhi::dx12::get_device(device), 
        numFramesInFlight,
        DXGI_FORMAT_R8G8B8A8_UNORM, 
        rhi::dx12::get_descriptor_heap(g_pd3dSrvDescHeap.Get()),
        rhi::dx12::get_descriptor_heap(g_pd3dSrvDescHeap.Get())->GetCPUDescriptorHandleForHeapStart(),
        rhi::dx12::get_descriptor_heap(g_pd3dSrvDescHeap.Get())->GetGPUDescriptorHandleForHeapStart());
    ImGui_ImplWin32_EnableDpiAwareness();


    IMGUI_CHECKVERSION();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.FontGlobalScale = 1.2f;

	DirectX::XMUINT2 renderResolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
	DirectX::XMUINT2 outputResolution = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("outputResolution")();
	io.DisplaySize = ImVec2(static_cast<float>(outputResolution.x), static_cast<float>(outputResolution.y));
    io.DisplayFramebufferScale = ImVec2(
        2.0, 2.0);

    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    m_memQuery = ECSManager::GetInstance().GetWorld().query_builder<const MemoryStatisticsComponents::MemSizeBytes>().build();

	// Helper to set an observer on a setting which updates local copies of settings
    auto observerSetting = [&](auto& localCopy, const std::string& settingName) {
        m_settingSubscriptions.push_back(SettingsManager::GetInstance().addObserver<std::decay_t<decltype(localCopy)>>(settingName,
            [&localCopy](const std::decay_t<decltype(localCopy)>& newValue) {
                localCopy = newValue;
            }));
		};

	getEnvironmentName = SettingsManager::GetInstance().getSettingGetter<std::string>("environmentName");
	setEnvironment = SettingsManager::GetInstance().getSettingSetter<std::string>("environmentName");

    auto& settingsManager = SettingsManager::GetInstance();
    getImageBasedLightingEnabled = settingsManager.getSettingGetter<bool>("enableImageBasedLighting");
    setImageBasedLightingEnabled = settingsManager.getSettingSetter<bool>("enableImageBasedLighting");
	imageBasedLightingEnabled = getImageBasedLightingEnabled();
	observerSetting(imageBasedLightingEnabled, "enableImageBasedLighting");

	getPunctualLightingEnabled = settingsManager.getSettingGetter<bool>("enablePunctualLighting");
	setPunctualLightingEnabled = settingsManager.getSettingSetter<bool>("enablePunctualLighting");
	punctualLightingEnabled = getPunctualLightingEnabled();
	observerSetting(punctualLightingEnabled, "enablePunctualLighting");

	getShadowsEnabled = settingsManager.getSettingGetter<bool>("enableShadows");
	setShadowsEnabled = settingsManager.getSettingSetter<bool>("enableShadows");
	shadowsEnabled = getShadowsEnabled();
	observerSetting(shadowsEnabled, "enableShadows");

    hdrFiles = GetFilesInDirectoryMatchingExtension(environmentsDir.wstring(), L".hdr");
	environmentName = getEnvironmentName();
    settingsManager.addObserver<std::string>("environmentName", [this](const std::string& newValue) {
        environmentName = getEnvironmentName();
        });

	setOutputType = settingsManager.getSettingSetter<unsigned int>("outputType");
	setTonemapType = settingsManager.getSettingSetter<unsigned int>("tonemapType");
	getTonemapType = settingsManager.getSettingGetter<unsigned int>("tonemapType");

    getSceneRoot = settingsManager.getSettingGetter<std::function<flecs::entity()>>("getSceneRoot")();

	setMeshShaderEnabled = settingsManager.getSettingSetter<bool>("enableMeshShader");
	getMeshShaderEnabled = settingsManager.getSettingGetter<bool>("enableMeshShader");
	meshShaderEnabled = getMeshShaderEnabled();
	observerSetting(meshShaderEnabled, "enableMeshShader");

	setIndirectDrawsEnabled = settingsManager.getSettingSetter<bool>("enableIndirectDraws");
	getIndirectDrawsEnabled = settingsManager.getSettingGetter<bool>("enableIndirectDraws");
	indirectDrawsEnabled = getIndirectDrawsEnabled();
	observerSetting(indirectDrawsEnabled, "enableIndirectDraws");

	getOcclusionCullingEnabled = settingsManager.getSettingGetter<bool>("enableOcclusionCulling");
	setOcclusionCullingEnabled = settingsManager.getSettingSetter<bool>("enableOcclusionCulling");
	occlusionCulling = getOcclusionCullingEnabled();
	observerSetting(occlusionCulling, "enableOcclusionCulling");

	getMeshletCullingEnabled = settingsManager.getSettingGetter<bool>("enableMeshletCulling");
	setMeshletCullingEnabled = settingsManager.getSettingSetter<bool>("enableMeshletCulling");
	meshletCulling = getMeshletCullingEnabled();
	observerSetting(meshletCulling, "enableMeshletCulling");

	setWireframeEnabled = settingsManager.getSettingSetter<bool>("enableWireframe");
	getWireframeEnabled = settingsManager.getSettingGetter<bool>("enableWireframe");
	wireframeEnabled = getWireframeEnabled();
	observerSetting(wireframeEnabled, "enableWireframe");

	setAllowTearing = settingsManager.getSettingSetter<bool>("allowTearing");
	getAllowTearing = settingsManager.getSettingGetter<bool>("allowTearing");
	allowTearing = getAllowTearing();
	observerSetting(allowTearing, "allowTearing");

	setDrawBoundingSpheres = settingsManager.getSettingSetter<bool>("drawBoundingSpheres");
	getDrawBoundingSpheres = settingsManager.getSettingGetter<bool>("drawBoundingSpheres");
	drawBoundingSpheres = getDrawBoundingSpheres();
	observerSetting(drawBoundingSpheres, "drawBoundingSpheres");

	setClusteredLightingEnabled = settingsManager.getSettingSetter<bool>("enableClusteredLighting");
	getClusteredLightingEnabled = settingsManager.getSettingGetter<bool>("enableClusteredLighting");
	clusteredLighting = getClusteredLightingEnabled();
	observerSetting(clusteredLighting, "enableClusteredLighting");

	setVisibilityRenderingEnabled = settingsManager.getSettingSetter<bool>("enableVisibilityRendering");
	getVisibilityRenderingEnabled = settingsManager.getSettingGetter<bool>("enableVisibilityRendering");
	m_visibilityRenderingEnabled = getVisibilityRenderingEnabled();
	observerSetting(m_visibilityRenderingEnabled, "enableVisibilityRendering");

	getGTAOEnabled = settingsManager.getSettingGetter<bool>("enableGTAO");
	setGTAOEnabled = settingsManager.getSettingSetter<bool>("enableGTAO");
	m_gtaoEnabled = getGTAOEnabled();
	observerSetting(m_gtaoEnabled, "enableGTAO");

	setBloomEnabled = settingsManager.getSettingSetter<bool>("enableBloom");
	getBloomEnabled = settingsManager.getSettingGetter<bool>("enableBloom");
	m_bloomEnabled = getBloomEnabled();
	observerSetting(m_bloomEnabled, "enableBloom");

	setScreenSpaceReflectionsEnabled = settingsManager.getSettingSetter<bool>("enableScreenSpaceReflections");
	getScreenSpaceReflectionsEnabled = settingsManager.getSettingGetter<bool>("enableScreenSpaceReflections");
	m_screenSpaceReflectionsEnabled = getScreenSpaceReflectionsEnabled();
	observerSetting(m_screenSpaceReflectionsEnabled, "enableScreenSpaceReflections");

    setJitterEnabled = settingsManager.getSettingSetter<bool>("enableJitter");
    getJitterEnabled = settingsManager.getSettingGetter<bool>("enableJitter");
    m_jitterEnabled = getJitterEnabled();
	observerSetting(m_jitterEnabled, "enableJitter");

	getCollectPipelineStatistics = settingsManager.getSettingGetter<bool>("collectPipelineStatistics");
	setCollectPipelineStatistics = settingsManager.getSettingSetter<bool>("collectPipelineStatistics");
	m_collectPipelineStatistics = getCollectPipelineStatistics();

    getUpscalingMode = settingsManager.getSettingGetter<UpscalingMode>("upscalingMode");
    setUpscalingMode = settingsManager.getSettingSetter<UpscalingMode>("upscalingMode");
    m_currentUpscalingMode = getUpscalingMode();
	observerSetting(m_currentUpscalingMode, "upscalingMode");

	getUpscalingQualityMode = settingsManager.getSettingGetter<UpscaleQualityMode>("upscalingQualityMode");
    setUpscalingQualityMode = settingsManager.getSettingSetter<UpscaleQualityMode>("upscalingQualityMode");
    m_currentUpscalingQualityMode = getUpscalingQualityMode();
	observerSetting(m_currentUpscalingQualityMode, "upscalingQualityMode");

	getUseAsyncCompute = settingsManager.getSettingGetter<bool>("useAsyncCompute");
    setUseAsyncCompute = settingsManager.getSettingSetter<bool>("useAsyncCompute");
    m_useAsyncCompute = getUseAsyncCompute();
	observerSetting(m_useAsyncCompute, "useAsyncCompute");

	appendScene = settingsManager.getSettingGetter<std::function<std::shared_ptr<Scene>(std::shared_ptr<Scene>)>>("appendScene")();

    m_meshShadersSupported = DeviceManager::GetInstance().GetMeshShadersSupported();
}

static bool PassUsesResourceAdapter(const void* passAndRes, uint64_t resourceId, bool isCompute) {
    if (isCompute) {
        auto& pr = *reinterpret_cast<const RenderGraph::ComputePassAndResources*>(passAndRes);
		bool found = false;
        for (const auto& req : pr.resources.resourceRequirements) {
            if (req.resourceHandleAndRange.resource.GetGlobalResourceID() == resourceId) {
				found = true;
            }
        }
		return found;
    }
    else {
        auto& pr = *reinterpret_cast<const RenderGraph::RenderPassAndResources*>(passAndRes);
        bool found = false;
        for (const auto& req : pr.resources.resourceRequirements) {
            if (req.resourceHandleAndRange.resource.GetGlobalResourceID() == resourceId) {
                found = true;
            }
        }
        return found;
    }
}

inline void Menu::Render(RenderContext& context) {
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();

	ImGui::NewFrame();
    static bool showRG = false;
    static bool showMemoryIntrospection = false;

	{
		static float f = 0.0f;
		static int counter = 0;

		ImGui::Begin("Renderer Configuration", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

        if (ImGui::Checkbox("Image-Based Lighting", &imageBasedLightingEnabled)) {
			setImageBasedLightingEnabled(imageBasedLightingEnabled);
        }
		if (ImGui::Checkbox("Punctual Lighting", &punctualLightingEnabled)) {
			setPunctualLightingEnabled(punctualLightingEnabled);
		}
		if (ImGui::Checkbox("Shadows", &shadowsEnabled)) {
			setShadowsEnabled(shadowsEnabled);
		}
        if (m_meshShadersSupported) {
            if (ImGui::Checkbox("Use Mesh Shaders", &meshShaderEnabled)) {
                setMeshShaderEnabled(meshShaderEnabled);
            }
        }
        else {
            ImGui::Text("Your GPU does not support mesh shaders!");
        }
        if (ImGui::Checkbox("Use Indirect Draws", &indirectDrawsEnabled)) {
            setIndirectDrawsEnabled(indirectDrawsEnabled);
        }
        if (ImGui::Checkbox("Occlusion Culling", &occlusionCulling)) {
            setOcclusionCullingEnabled(occlusionCulling);
        }
		if (ImGui::Checkbox("Meshlet Culling", &meshletCulling)) {
			setMeshletCullingEnabled(meshletCulling);
		}
		if (ImGui::Checkbox("Wireframe", &wireframeEnabled)) {
			setWireframeEnabled(wireframeEnabled);
		}
        if (ImGui::Checkbox("Uncap Framerate", &allowTearing)) {
			setAllowTearing(allowTearing);
        }
		if (ImGui::Checkbox("Draw Bounding Spheres", &drawBoundingSpheres)) {
			setDrawBoundingSpheres(drawBoundingSpheres);
		}
        if (ImGui::Checkbox("Clustered Lighting", &clusteredLighting)) {
			setClusteredLightingEnabled(clusteredLighting);
        }
        if (ImGui::Checkbox("Visibility Rendering", &m_visibilityRenderingEnabled)) {
            setVisibilityRenderingEnabled(m_visibilityRenderingEnabled);
		}
		if (ImGui::Checkbox("Enable GTAO", &m_gtaoEnabled)) {
			setGTAOEnabled(m_gtaoEnabled);
		}
		if (ImGui::Checkbox("Enable Bloom", &m_bloomEnabled)) {
			setBloomEnabled(m_bloomEnabled);
		}
        if (ImGui::Checkbox("Enable Screen Space Reflections", &m_screenSpaceReflectionsEnabled)) {
            setScreenSpaceReflectionsEnabled(m_screenSpaceReflectionsEnabled);
		}
        if (ImGui::Checkbox("Enable Jitter", &m_jitterEnabled)) {
            setJitterEnabled(m_jitterEnabled);
        }
		if (ImGui::Checkbox("Collect Pipeline Statistics", &m_collectPipelineStatistics)) {
			setCollectPipelineStatistics(m_collectPipelineStatistics);
		}
        DrawUpscalingCombo();
        DrawUpscalingQualityCombo();
        DrawTonemapTypeDropdown();

        DrawEnvironmentsDropdown();
        DrawBrowseButton(environmentsDir.wstring());
		DrawOutputTypeDropdown();
        DrawLoadModelButton();
		if (ImGui::Checkbox("Use Async Compute", &m_useAsyncCompute)) {
			setUseAsyncCompute(m_useAsyncCompute);
		}
        ImGui::Checkbox("Render Graph Inspector", &showRG);
        ImGui::Checkbox("Memory introspection", &showMemoryIntrospection);
        rhi::ma::Budget localBudget;
        std::string memoryString = "Memory usage: ";
        DeviceManager::GetInstance().GetAllocator()->GetBudget(&localBudget, nullptr);
        const double KiB = 1024.0;
        const double MiB = KiB * 1024.0;
        const double GiB = MiB * 1024.0;
        const auto usage = static_cast<double>(localBudget.usageBytes);

        const auto [div, suffix] =
            (usage >= GiB) ? std::pair{ GiB, "GB" } :
            (usage >= MiB) ? std::pair{ MiB, "MB" } :
            (usage >= KiB) ? std::pair{ KiB, "KB" } :
            std::pair{ 1.0, "B" };

        memoryString += std::format("{:.2f} {} / {:.2f} GB",
            usage / div, suffix,
            static_cast<double>(localBudget.budgetBytes) / GiB);

        ImGui::Text(memoryString.c_str());
        ImGui::Text("Render Resolution: %d x %d | Output Resolution: %d x %d", context.renderResolution.x, context.renderResolution.y, context.outputResolution.x, context.outputResolution.y);
		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
		ImGui::End();
	}
	if (showMemoryIntrospection) {
        static ui::MemoryIntrospectionWidget g_memWidget;

        ui::MemorySnapshot snap;
        PerResourceMemIndex memIndex;
        BuildMemorySnapshotFromFlecs(snap, m_memQuery, &memIndex);

        static std::unordered_map<uint64_t, MemInfo> s_idToMem;
		BuildIdToMemInfoIndex(s_idToMem, m_memQuery);    
		ui::FrameGraphSnapshot fgSnap;

        const auto& batches = m_renderGraph->GetBatches();
        BuildFrameGraphSnapshotFromBatches(fgSnap, batches, memIndex);


        ImGui::Begin("Memory Introspection", nullptr);
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsedSeconds = now - m_startTime;
        uint64_t totalBytes = snap.totalBytes;
        g_memWidget.PushFrameSample(elapsedSeconds.count(), totalBytes);
        bool open = true;
        g_memWidget.Draw(&open, &snap, &fgSnap);
		ImGui::End();
	}

    {
		ImGui::Begin("Scene Graph", nullptr);
		DisplaySceneGraph();
		ImGui::End();

		DisplaySelectedNode();

        DrawPassTimingWindow();
    }
    
    if (showRG) {
		ImGui::Begin("Render Graph Inspector", nullptr);
        RGInspectorOptions opts;
        RGInspector::Show(m_renderGraph->GetBatches(),
            PassUsesResourceAdapter,
            opts);
        ImGui::End();

    }

	// Rendering
	ImGui::Render();

    context.commandList.SetDescriptorHeaps(g_pd3dSrvDescHeap->GetHandle(), std::nullopt);

	rhi::PassBeginInfo beginInfo{};
	rhi::ColorAttachment attchment{};
    attchment.loadOp = rhi::LoadOp::Load;
	attchment.rtv = { context.rtvHeap.GetHandle() , context.frameIndex }; // Index into the swapchain RTV heap
	beginInfo.colors = { &attchment };
	beginInfo.height = static_cast<uint32_t>(ImGui::GetIO().DisplaySize.y);
	beginInfo.width = static_cast<uint32_t>(ImGui::GetIO().DisplaySize.x);

	context.commandList.BeginPass(beginInfo);

	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), rhi::dx12::get_cmd_list(context.commandList));

}

inline int Menu::FindFileIndex(const std::vector<std::string>& inputHdrFiles, const std::string& existingFile) {
    for (unsigned int i = 0; i < inputHdrFiles.size(); ++i)
    {
        if (inputHdrFiles[i] == existingFile)
        {
            return i;
        }
    }
    return -1;
}

inline void Menu::DrawEnvironmentsDropdown() {
    static int selectedItemIndex = FindFileIndex(hdrFiles, environmentName);

    const char* previewValue = (selectedItemIndex >= 0)
        ? hdrFiles[selectedItemIndex].c_str()
        : "Select Environment";

    if (ImGui::BeginCombo("HDR Files", previewValue))
    {
        for (int i = 0; i < (int)hdrFiles.size(); ++i)
        {
            bool isSelected = (selectedItemIndex == i);
            if (ImGui::Selectable(hdrFiles[i].c_str(), isSelected))
            {
                selectedItemIndex = i;
                environmentName   = hdrFiles[i];
                setEnvironment(hdrFiles[i]);
            }
            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

inline void Menu::DrawOutputTypeDropdown() {
	static unsigned int selectedItemIndex = 0;
    if (ImGui::BeginCombo("Output Type", OutputTypeNames[selectedItemIndex].c_str())) {
		for (unsigned int i = 0; i < OutputTypeNames.size(); ++i) {
			bool isSelected = (selectedItemIndex == i);
			if (ImGui::Selectable(OutputTypeNames[i].c_str(), isSelected)) {
				selectedItemIndex = i;
				setOutputType(i);
			}
			if (isSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();

    }
}

inline void Menu::DrawUpscalingCombo()
{
    int modeIdx = static_cast<int>(m_currentUpscalingMode);

    if (ImGui::Combo("Upscaling Mode", &modeIdx, UpscalingModeNames, UpscalingModeCount))
    {
        m_currentUpscalingMode = static_cast<UpscalingMode>(modeIdx);
		setUpscalingMode(m_currentUpscalingMode);
    }
}

inline void Menu::DrawUpscalingQualityCombo()
{
    int modeIdx = static_cast<int>(m_currentUpscalingQualityMode);

    if (ImGui::Combo("Upscaling Quality", &modeIdx, UpscaleQualityModeNames, UpscaleQualityModeCount))
    {
        m_currentUpscalingQualityMode = static_cast<UpscaleQualityMode>(modeIdx);
        setUpscalingQualityMode(m_currentUpscalingQualityMode);
    }
}

inline void Menu::DrawTonemapTypeDropdown() {
    static unsigned int selectedItemIndex = 0;
	selectedItemIndex = getTonemapType();
    if (ImGui::BeginCombo("Tonemap Type", TonemapTypeNames[selectedItemIndex].c_str())) {
        for (unsigned int i = 0; i < TonemapTypeNames.size(); ++i) {
            bool isSelected = (selectedItemIndex == i);
            if (ImGui::Selectable(TonemapTypeNames[i].c_str(), isSelected)) {
                selectedItemIndex = i;
                setTonemapType(i);
            }
            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();

    }
}


inline void Menu::DrawBrowseButton(const std::wstring& targetDirectory) {
    if (ImGui::Button("Browse"))
    {
        std::wstring selectedFile;
        std::wstring customFilter = L"HDR Files\0*.hdr\0All Files\0*.*\0";
        if (OpenFileDialog(selectedFile, customFilter))
        {
            spdlog::info("Selected file: {}", ws2s(selectedFile));

            CopyFileToDirectory(selectedFile, targetDirectory);
            hdrFiles = GetFilesInDirectoryMatchingExtension(environmentsDir, L".hdr");
        }
        else
        {
            spdlog::warn("No file selected.");
        }
    }
}

inline void Menu::DrawLoadModelButton() {
    if (ImGui::Button("Load Model"))
    {
        std::wstring selectedFile;
        std::wstring customFilter = L"GLB Files\0*.glb\0All Files\0*.*\0";
        if (OpenFileDialog(selectedFile, customFilter))
        {
			//auto exePath = GetExePath();
			//// Strip EXE path from selectedFile
			//if (selectedFile.find(exePath) == 0) {
			//	selectedFile.erase(0, exePath.length());
			//}
			//// Strip filename from selectedFile
   //         auto pathCopy = selectedFile;
			//auto lastSlash = selectedFile.find_last_of(L"\\/");
			//if (lastSlash != std::wstring::npos) {
			//	selectedFile.erase(lastSlash, selectedFile.size()-1);
			//}

            spdlog::info("Selected file: {}", ws2s(selectedFile));
			auto scene = LoadModel(ws2s(selectedFile));
			scene->GetRoot().set<Components::Name>(ws2s(getFileNameFromPath(selectedFile)));
			appendScene(scene->Clone());
        }
        else
        {
            spdlog::warn("No file selected.");
        }
    }
}

inline void Menu::DisplaySceneNode(flecs::entity node, bool isOnlyChild) {
    if (!node) return;

    // Set flags for automatically expanding nodes if they are the only child
    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

    // If the node is currently selected, add the ImGuiTreeNodeFlags_Selected flag
    if (node == selectedNode) {
        nodeFlags |= ImGuiTreeNodeFlags_Selected;
    }

    if (isOnlyChild) {
        nodeFlags |= ImGuiTreeNodeFlags_DefaultOpen;
    }

    // Check if the node has any children.
    bool hasChild = false;
    node.children([&hasChild](flecs::entity child) {
        hasChild = true;
        });
    // If there are no children, mark it as a leaf node.
    if (!hasChild) {
        nodeFlags |= ImGuiTreeNodeFlags_Leaf;
    }

    // Show the node with its name
	auto nameComponent = node.try_get<Components::Name>();
	std::string name = nameComponent ? nameComponent->name : "Unnamed Node";
    void* uniqueId = reinterpret_cast<void*>(static_cast<intptr_t>(node.id()));
    if (ImGui::TreeNodeEx(uniqueId, nodeFlags, "%s", name.c_str())) {
        // Detect if the node is clicked to select it
        if (ImGui::IsItemClicked()) {
            selectedNode = node;
        }

        // Display information specific to RenderableObject, if the node is of that type.
		if (node.has<Components::RenderableObject>()) {
            // Display meshes
			auto meshInstances = node.try_get<Components::MeshInstances>();
			if (meshInstances) {
				ImGui::Text("Meshes: %d", meshInstances->meshInstances.size());
			}

            if (node.has<Components::Skinned>()) {
                ImGui::Text("Has Skinned: Yes");
            }
            else {
                ImGui::Text("Has Skinned: No");
            }
		}

        // Recursively display child nodes
        // Count children
        uint64_t num = 0;
        node.children(([&num](flecs::entity) {
            num++;
            }));

        bool childIsOnly = num <= 1;
		node.children(([&](flecs::entity child) {
			// Display the child node
			DisplaySceneNode(child, childIsOnly);
			}));

        ImGui::TreePop();
    }
    else {
        // Allow selection
        if (ImGui::IsItemClicked()) {
            selectedNode = node;
        }
    }
}

inline void Menu::DisplaySceneGraph() {
    auto root = getSceneRoot();
    DisplaySceneNode(root, true);
}

inline void Menu::DisplaySelectedNode() {
    if (selectedNode) {
        ImGui::Begin("Selected Node Transform", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

        // Display the transform details
        ImGui::Text("Position:");
        XMFLOAT3 pos;
        auto& position = selectedNode.get<Components::Position>();
        XMStoreFloat3(&pos, position.pos);
        if (ImGui::InputFloat3("Position", &pos.x)) {
			selectedNode.set<Components::Position>(XMLoadFloat3(&pos));
            //selectedNode->transform.isDirty = true;
        }
        ImGui::Text("Scale:");
		XMFLOAT3 scale;
		XMStoreFloat3(&scale, selectedNode.get<Components::Scale>().scale);
        if (ImGui::InputFloat("Scale", &scale.x)) {
            //selectedNode->transform.isDirty = true;
			scale.y = scale.x;
			scale.z = scale.x;
			selectedNode.set<Components::Scale>(XMLoadFloat3(&scale));
        }

        // Display rotation
        XMFLOAT4 rotation;
        XMStoreFloat4(&rotation, selectedNode.get<Components::Rotation>().rot);
        ImGui::Text("Rotation (quaternion): (%.3f, %.3f, %.3f, %.3f)", rotation.x, rotation.y, rotation.z, rotation.w);

        ImGui::End();
    }
}

inline void Menu::DrawPassTimingWindow() {
    auto& names     = StatisticsManager::GetInstance().GetPassNames();
    auto& stats     = StatisticsManager::GetInstance().GetPassStats();
    auto& meshStats = StatisticsManager::GetInstance().GetMeshStats();
    // you’ll need an accessor or public m_isGeometryPass here:
    auto& isGeom    = StatisticsManager::GetInstance().GetIsGeometryPassVector(); 

    if (names.empty()) return;

    static std::vector<bool> pinned;
    static bool sortEnabled = true;
    if (pinned.size() != names.size()) 
        pinned.assign(names.size(), false);

    // split pinned vs unpinned
    std::vector<int> pins;
    std::vector<std::pair<int,double>> unsorted;
    for (int i = 0; i < (int)names.size(); ++i) {
        if (pinned[i]) pins.push_back(i);
        else           unsorted.emplace_back(i, stats[i].ema);
    }
    if (sortEnabled)
        std::sort(unsorted.begin(), unsorted.end(), [](auto &a, auto &b){ return a.second > b.second; });

    std::vector<int> order;
    order.insert(order.end(), pins.begin(), pins.end());
    for (auto &p : unsorted) order.push_back(p.first);

    // measure column widths
    ImGuiStyle& style = ImGui::GetStyle();
    float wName = ImGui::CalcTextSize("Pass").x;
    float wNum  = ImGui::CalcTextSize("Avg (ms)").x;
    char buf[64];
    for (int idx : order) {
        std::string label = (pinned[idx] ? "[P] " : "P") + names[idx];
        wName = std::max(wName, ImGui::CalcTextSize(label.c_str()).x);
        snprintf(buf, sizeof(buf), "%.3f", stats[idx].ema);
        wNum  = std::max(wNum, ImGui::CalcTextSize(buf).x);
    }
    wName += style.CellPadding.x*2;
    wNum  += style.CellPadding.x*2 + style.ItemSpacing.x + ImGui::CalcTextSize(sortEnabled?"v":">").x + style.FramePadding.x*2;

    ImGui::Begin("Pass Timings");
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, wName);
    ImGui::SetColumnWidth(1, wNum);

    // header
    ImGui::TextUnformatted("Pass"); ImGui::NextColumn();
    ImGui::TextUnformatted("Avg (ms)"); ImGui::SameLine();
    if (ImGui::SmallButton(sortEnabled ? "v" : ">")) sortEnabled = !sortEnabled;
    ImGui::NextColumn();
    ImGui::Separator();

    // rows
    for (int idx : order) {
        ImGui::PushID(idx);
        if (pinned[idx]) {
            if (ImGui::SmallButton(">")) pinned[idx] = false;
        } else {
            if (ImGui::SmallButton("Pin")) pinned[idx] = true;
        }
        ImGui::SameLine();
        bool open = ImGui::TreeNodeEx(names[idx].c_str(), ImGuiTreeNodeFlags_SpanFullWidth);
        ImGui::PopID();

        ImGui::NextColumn();
        snprintf(buf, sizeof(buf), "%.3f", stats[idx].ema);
        ImGui::TextUnformatted(buf);
        ImGui::NextColumn();

        if (open) {
            if (isGeom[idx]) {
                ImGui::Indent();
                ImGui::Text("Mesh Invocations: %.0f", meshStats[idx].invocationsEma);
                ImGui::Text("Mesh Primitives:  %.0f", meshStats[idx].primitivesEma);
                ImGui::Unindent();
            }
            ImGui::TreePop();
            ImGui::Separator();
        }
    }

    ImGui::Columns(1);
    ImGui::End();
}
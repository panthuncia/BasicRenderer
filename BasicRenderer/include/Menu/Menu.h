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
#include <cstring>

#include "Render/RenderContext.h"
#include "Utilities/Utilities.h"
#include "Render/OutputTypes.h"
#include "Import/ModelLoader.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Render/TonemapTypes.h"
#include "Managers/Singletons/UpscalingManager.h"
#include "DebugUI/RenderGraphInspector.h"
#include "DebugUI/MemoryIntrospectionWidget.h"
#include "Resources/ReadbackRequest.h"
#include "Resources/components.h"
#include "Render/MemoryIntrospectionAPI.h"
#include "ShaderBuffers.h"
#include "Render/GraphExtensions/CLodExtensionComponents.h"
#include "Render/GraphExtensions/CLodTelemetry.h"
#include "Managers/Singletons/ECSManager.h"

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

static void BuildMemorySnapshotFromRecords(
    ui::MemorySnapshot& out,
    const std::vector<rg::memory::ResourceMemoryRecord>& records,
    PerResourceMemIndex* outIndex /*= nullptr*/)
{
    out.categories.clear();
    out.resources.clear();
    out.totalBytes = 0;

    std::unordered_map<std::string, uint64_t> minorBuckets;
    minorBuckets.reserve(256);

    if (outIndex) { outIndex->clear(); outIndex->reserve(2048); }

    for (const auto& record : records) {
        const uint64_t bytes = record.bytes;
        out.totalBytes += bytes;

        const char* major = MajorCategory(record.resourceType);

        const char* usage = !record.usage.empty() ? record.usage.c_str()
            : "Unspecified";

        const std::string cat = std::string(major) + "/" + usage;

        minorBuckets[cat] += bytes;

        ui::MemoryResourceRow row{};
        row.bytes = bytes;
        row.uid = record.resourceID;

        if (!record.resourceName.empty()) row.name = record.resourceName;
        else if (record.resourceID != 0) row.name = "Resource " + std::to_string((unsigned long long)record.resourceID);
        else row.name = "Unknown resource";

        row.type = cat;
        out.resources.push_back(row);

        if (outIndex && record.resourceID != 0) {
            auto& info = (*outIndex)[record.resourceID];
            info.bytes = bytes;
            info.category = cat;
            if (!record.resourceName.empty()) info.name = record.resourceName;
        }
    }

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
    const std::vector<rg::memory::ResourceMemoryRecord>& records)
{
    out.clear();
    out.reserve(2048);

    for (const auto& record : records) {
        if (record.resourceID == 0) {
            continue;
        }

        MemInfo info;
        info.bytes = record.bytes;

        if (!record.identifier.empty()) {
            info.name = record.identifier;
        }

        out[record.resourceID] = std::move(info);
    }
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

        for (size_t phaseIndex = 0; phaseIndex < static_cast<size_t>(RenderGraph::BatchTransitionPhase::Count); ++phaseIndex) {
            const auto phase = static_cast<RenderGraph::BatchTransitionPhase>(phaseIndex);
            for (size_t queueIndex = 0; queueIndex < static_cast<size_t>(QueueKind::Count); ++queueIndex) {
                const auto queue = static_cast<QueueKind>(queueIndex);
                scanTransitions(b.Transitions(queue, phase));
            }
        }

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
        row.hasEndTransitions = b.HasTransitions(QueueKind::Graphics, RenderGraph::BatchTransitionPhase::AfterPasses);

        size_t totalPassCount = 0;
        for (size_t queueIndex = 0; queueIndex < static_cast<size_t>(QueueKind::Count); ++queueIndex) {
            totalPassCount += b.Passes(static_cast<QueueKind>(queueIndex)).size();
        }
        row.passNames.reserve(totalPassCount);
        for (size_t queueIndex = 0; queueIndex < static_cast<size_t>(QueueKind::Count); ++queueIndex) {
            const auto queue = static_cast<QueueKind>(queueIndex);
            for (const auto& queuedPass : b.Passes(queue)) {
                std::visit(
                    [&](const auto& pass) {
                        row.passNames.push_back(pass.name);
                    },
                    queuedPass);
            }
        }

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
    void Render(RenderContext& context, rhi::CommandList commandList);
    bool HandleInput(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	void SetRenderGraph(RenderGraph* renderGraph) { m_renderGraph = renderGraph; }
    void Cleanup() {
		m_settingSubscriptions.clear();
		m_telemetryQuery = {};
		m_visibleClustersQuery = {};
		m_visibleCounterQuery = {};
    }

private:
    rhi::DescriptorHeapPtr g_pd3dSrvDescHeap;
    Menu() { 
        ImGui::CreateContext();
		ImPlot::CreateContext();
    };
	IDXGISwapChain3* m_pSwapChain = nullptr;
	
	flecs::entity selectedNode;

	RenderGraph* m_renderGraph;

    struct CLodCaptureStats {
        uint32_t visibleClusterCount = 0;
        uint32_t uniqueViews = 0;
        uint32_t uniqueInstances = 0;
        uint32_t uniqueMeshlets = 0;
        uint32_t maxClustersPerView = 0;
        uint32_t maxClustersPerInstance = 0;
        float avgClustersPerView = 0.0f;
        float avgClustersPerInstance = 0.0f;
        float dominantViewPercent = 0.0f;
        float dominantInstancePercent = 0.0f;
    };

    CLodWorkGraphTelemetryCounters m_clodTelemetryCounters{};
    bool m_clodTelemetryHasData = false;
    bool m_clodTelemetryCapturePending = false;
    uint64_t m_clodTelemetryCaptureCount = 0;
    std::string m_clodTelemetryStatus = "No captures yet.";

    struct CLodStreamingOpsHistorySample {
        std::chrono::steady_clock::time_point timestamp;
        CLodStreamingOperationStats stats{};
    };

    uint64_t m_clodStreamingOpsLastSequence = 0;
    CLodStreamingOperationStats m_clodStreamingOpsLatest{};
    std::vector<CLodStreamingOpsHistorySample> m_clodStreamingOpsHistory;

    bool m_clodCaptureStatsPending = false;
    uint64_t m_clodCaptureStatsId = 0;
    bool m_clodCaptureHasPendingCounter = false;
    bool m_clodCaptureHasPendingClusters = false;
    uint32_t m_clodCapturePendingVisibleCount = 0;
    std::vector<VisibleCluster> m_clodCapturePendingClusters;
    bool m_clodCaptureStatsAvailable = false;
    CLodCaptureStats m_clodCaptureStats{};

    flecs::query<const Components::Resource> m_telemetryQuery;
	flecs::query<const Components::Resource> m_visibleClustersQuery;
	flecs::query<const Components::Resource> m_visibleCounterQuery;

    int FindFileIndex(const std::vector<std::string>& hdrFiles, const std::string& existingFile);
    void DrawCLodTelemetryWindow();
    void DrawAutoAliasPlannerWindow();
    void TryFinalizeCLodCaptureStats(uint64_t captureId);

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

    AutoAliasMode m_autoAliasMode = AutoAliasMode::Balanced;
    std::function<AutoAliasMode()> getAutoAliasMode;
    std::function<void(AutoAliasMode)> setAutoAliasMode;

    AutoAliasPackingStrategy m_autoAliasPackingStrategy = AutoAliasPackingStrategy::GreedySweepLine;
    std::function<AutoAliasPackingStrategy()> getAutoAliasPackingStrategy;
    std::function<void(AutoAliasPackingStrategy)> setAutoAliasPackingStrategy;

    bool m_autoAliasLogExclusionReasons = false;
    std::function<bool()> getAutoAliasLogExclusionReasons;
    std::function<void(bool)> setAutoAliasLogExclusionReasons;

    uint32_t m_autoAliasPoolRetireIdleFrames = 120;
    std::function<uint32_t()> getAutoAliasPoolRetireIdleFrames;
    std::function<void(uint32_t)> setAutoAliasPoolRetireIdleFrames;

    uint32_t m_clodStreamingCpuUploadBudgetRequests = 64;
    std::function<uint32_t()> getCLodStreamingCpuUploadBudgetRequests;
    std::function<void(uint32_t)> setCLodStreamingCpuUploadBudgetRequests;

    float m_autoAliasPoolGrowthHeadroom = 1.5f;
    std::function<float()> getAutoAliasPoolGrowthHeadroom;
    std::function<void(float)> setAutoAliasPoolGrowthHeadroom;

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

    getAutoAliasMode = settingsManager.getSettingGetter<AutoAliasMode>("autoAliasMode");
    setAutoAliasMode = settingsManager.getSettingSetter<AutoAliasMode>("autoAliasMode");
    m_autoAliasMode = getAutoAliasMode();
    observerSetting(m_autoAliasMode, "autoAliasMode");

    getAutoAliasPackingStrategy = settingsManager.getSettingGetter<AutoAliasPackingStrategy>("autoAliasPackingStrategy");
    setAutoAliasPackingStrategy = settingsManager.getSettingSetter<AutoAliasPackingStrategy>("autoAliasPackingStrategy");
    m_autoAliasPackingStrategy = getAutoAliasPackingStrategy();
    observerSetting(m_autoAliasPackingStrategy, "autoAliasPackingStrategy");

    getAutoAliasLogExclusionReasons = settingsManager.getSettingGetter<bool>("autoAliasLogExclusionReasons");
    setAutoAliasLogExclusionReasons = settingsManager.getSettingSetter<bool>("autoAliasLogExclusionReasons");
    m_autoAliasLogExclusionReasons = getAutoAliasLogExclusionReasons();
    observerSetting(m_autoAliasLogExclusionReasons, "autoAliasLogExclusionReasons");

    getAutoAliasPoolRetireIdleFrames = settingsManager.getSettingGetter<uint32_t>("autoAliasPoolRetireIdleFrames");
    setAutoAliasPoolRetireIdleFrames = settingsManager.getSettingSetter<uint32_t>("autoAliasPoolRetireIdleFrames");
    m_autoAliasPoolRetireIdleFrames = getAutoAliasPoolRetireIdleFrames();
    observerSetting(m_autoAliasPoolRetireIdleFrames, "autoAliasPoolRetireIdleFrames");

    getCLodStreamingCpuUploadBudgetRequests = settingsManager.getSettingGetter<uint32_t>("clodStreamingCpuUploadBudgetRequests");
    setCLodStreamingCpuUploadBudgetRequests = settingsManager.getSettingSetter<uint32_t>("clodStreamingCpuUploadBudgetRequests");
    m_clodStreamingCpuUploadBudgetRequests = getCLodStreamingCpuUploadBudgetRequests();
    observerSetting(m_clodStreamingCpuUploadBudgetRequests, "clodStreamingCpuUploadBudgetRequests");

    getAutoAliasPoolGrowthHeadroom = settingsManager.getSettingGetter<float>("autoAliasPoolGrowthHeadroom");
    setAutoAliasPoolGrowthHeadroom = settingsManager.getSettingSetter<float>("autoAliasPoolGrowthHeadroom");
    m_autoAliasPoolGrowthHeadroom = getAutoAliasPoolGrowthHeadroom();
    observerSetting(m_autoAliasPoolGrowthHeadroom, "autoAliasPoolGrowthHeadroom");

	appendScene = settingsManager.getSettingGetter<std::function<std::shared_ptr<Scene>(std::shared_ptr<Scene>)>>("appendScene")();

    m_meshShadersSupported = DeviceManager::GetInstance().GetMeshShadersSupported();

    // CLod queries
    m_telemetryQuery = ECSManager::GetInstance().GetWorld()
        .query_builder<const Components::Resource>()
        .with<CLodWorkGraphTelemetryBufferTag>()
        .build();
    m_visibleClustersQuery = ECSManager::GetInstance().GetWorld()
        .query_builder<const Components::Resource>()
        .with<VisibleClustersBufferTag>()
        .build();
    m_visibleCounterQuery = ECSManager::GetInstance().GetWorld()
        .query_builder<const Components::Resource>()
        .with<VisibleClustersCounterTag>()
        .build();
}

static bool PassUsesResourceAdapter(const void* passAndRes, uint64_t resourceId, bool isCompute) {
    if (isCompute) {
        auto& pr = *reinterpret_cast<const RenderGraph::ComputePassAndResources*>(passAndRes);
		bool found = false;
        for (const auto& req : pr.resources.frameResourceRequirements) {
            if (req.resourceHandleAndRange.resource.GetGlobalResourceID() == resourceId) {
				found = true;
            }
        }
		return found;
    }
    else {
        auto& pr = *reinterpret_cast<const RenderGraph::RenderPassAndResources*>(passAndRes);
        bool found = false;
        for (const auto& req : pr.resources.frameResourceRequirements) {
            if (req.resourceHandleAndRange.resource.GetGlobalResourceID() == resourceId) {
                found = true;
            }
        }
        return found;
    }
}

inline void Menu::Render(RenderContext& context, rhi::CommandList commandList) {
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();

	ImGui::NewFrame();
    static bool showRG = false;
    static bool showMemoryIntrospection = false;
    static bool showCLodTelemetry = false;
    static bool showAutoAliasPlanner = false;

    SetCLodWorkGraphTelemetryEnabled(showCLodTelemetry || m_clodTelemetryCapturePending || m_clodCaptureStatsPending);

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
        int clodCpuUploadBudget = static_cast<int>(std::min<uint32_t>(m_clodStreamingCpuUploadBudgetRequests, 4096u));
        if (ImGui::SliderInt("CLod CPU Upload Budget", &clodCpuUploadBudget, 1, 4096)) {
            m_clodStreamingCpuUploadBudgetRequests = static_cast<uint32_t>(std::max(clodCpuUploadBudget, 1));
            setCLodStreamingCpuUploadBudgetRequests(m_clodStreamingCpuUploadBudgetRequests);
        }
        ImGui::Checkbox("Render Graph Inspector", &showRG);
        ImGui::Checkbox("Memory introspection", &showMemoryIntrospection);
        ImGui::Checkbox("CLod telemetry", &showCLodTelemetry);
        ImGui::Checkbox("Auto Alias Planner", &showAutoAliasPlanner);
        std::string memoryString = "Memory usage: unavailable";
        const double KiB = 1024.0;
        const double MiB = KiB * 1024.0;
        const double GiB = MiB * 1024.0;
        if (m_renderGraph) {
            if (auto* statisticsService = m_renderGraph->GetStatisticsService()) {
                const auto memoryBudgetStats = statisticsService->GetMemoryBudgetStats();
                if (memoryBudgetStats.valid) {
                    const auto usage = static_cast<double>(memoryBudgetStats.usageBytes);

                    const auto [div, suffix] =
                        (usage >= GiB) ? std::pair{ GiB, "GB" } :
                        (usage >= MiB) ? std::pair{ MiB, "MB" } :
                        (usage >= KiB) ? std::pair{ KiB, "KB" } :
                        std::pair{ 1.0, "B" };

                    memoryString = std::format("Memory usage: {:.2f} {} / {:.2f} GB",
                        usage / div, suffix,
                        static_cast<double>(memoryBudgetStats.budgetBytes) / GiB);
                }
            }
        }

        ImGui::Text(memoryString.c_str());
        ImGui::Text("Render Resolution: %d x %d | Output Resolution: %d x %d", context.renderResolution.x, context.renderResolution.y, context.outputResolution.x, context.outputResolution.y);
		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
		ImGui::End();
	}
	if (showMemoryIntrospection) {
        static ui::MemoryIntrospectionWidget g_memWidget;

        std::vector<rg::memory::ResourceMemoryRecord> memoryRecords;
    if (m_renderGraph) {
        m_renderGraph->GetMemorySnapshotProvider().BuildSnapshot(memoryRecords);
    }

        ui::MemorySnapshot snap;
        PerResourceMemIndex memIndex;
        BuildMemorySnapshotFromRecords(snap, memoryRecords, &memIndex);

        static std::unordered_map<uint64_t, MemInfo> s_idToMem;
		BuildIdToMemInfoIndex(s_idToMem, memoryRecords);
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
            [this](uint64_t resourceId) -> std::string {
                if (!m_renderGraph) return {};
                auto resource = m_renderGraph->GetResourceByID(resourceId);
                if (!resource) return {};
                return resource->GetName();
            },
            [this](uint64_t resourceId) -> Resource* {
                if (!m_renderGraph) return nullptr;
                auto resource = m_renderGraph->GetResourceByID(resourceId);
                return resource ? resource.get() : nullptr;
            },
            [this](const std::string& passName, Resource* resource, const RangeSpec& range, ReadbackCaptureCallback callback) {
                if (!m_renderGraph) {
                    return;
                }
                if (auto* readbackService = m_renderGraph->GetReadbackService()) {
                    readbackService->RequestReadbackCapture(passName, resource, range, std::move(callback));
                }
            },
            opts);
        ImGui::End();

    }

    if (showCLodTelemetry) {
        DrawCLodTelemetryWindow();
    }

    if (showAutoAliasPlanner) {
        DrawAutoAliasPlannerWindow();
    }

	// Rendering
	ImGui::Render();

    commandList.SetDescriptorHeaps(g_pd3dSrvDescHeap->GetHandle(), std::nullopt);

	rhi::PassBeginInfo beginInfo{};
	rhi::ColorAttachment attchment{};
    attchment.loadOp = rhi::LoadOp::Load;
	attchment.rtv = { context.rtvHeap.GetHandle() , context.frameIndex }; // Index into the swapchain RTV heap
	beginInfo.colors = { &attchment };
	beginInfo.height = static_cast<uint32_t>(ImGui::GetIO().DisplaySize.y);
	beginInfo.width = static_cast<uint32_t>(ImGui::GetIO().DisplaySize.x);

	commandList.BeginPass(beginInfo);

	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), rhi::dx12::get_cmd_list(commandList));

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
        std::wstring customFilter = L"glTF Files\0*.glb;*.gltf\0All Files\0*.*\0";
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

inline void Menu::TryFinalizeCLodCaptureStats(uint64_t captureId) {
    if (!m_clodCaptureStatsPending || m_clodCaptureStatsId != captureId) {
        return;
    }

    if (!m_clodCaptureHasPendingCounter || !m_clodCaptureHasPendingClusters) {
        return;
    }

    const uint32_t requestedCount = m_clodCapturePendingVisibleCount;
    const uint32_t availableCount = static_cast<uint32_t>(m_clodCapturePendingClusters.size());
    const uint32_t decodeCount = (std::min)(requestedCount, availableCount);

    std::unordered_map<uint32_t, uint32_t> viewHistogram;
    std::unordered_map<uint32_t, uint32_t> instanceHistogram;
    std::unordered_set<uint64_t> uniqueMeshlets;

    viewHistogram.reserve(16);
    instanceHistogram.reserve(512);
    uniqueMeshlets.reserve(decodeCount > 0 ? decodeCount : 1);

    for (uint32_t i = 0; i < decodeCount; ++i) {
        const VisibleCluster& cluster = m_clodCapturePendingClusters[i];
        viewHistogram[cluster.viewID]++;
        instanceHistogram[cluster.instanceID]++;
        const uint64_t key = (static_cast<uint64_t>(cluster.instanceID) << 32ull) | (static_cast<uint64_t>(cluster.groupID) << 16ull) | static_cast<uint64_t>(cluster.localMeshletIndex);
        uniqueMeshlets.insert(key);
    }

    CLodCaptureStats stats{};
    stats.visibleClusterCount = decodeCount;
    stats.uniqueViews = static_cast<uint32_t>(viewHistogram.size());
    stats.uniqueInstances = static_cast<uint32_t>(instanceHistogram.size());
    stats.uniqueMeshlets = static_cast<uint32_t>(uniqueMeshlets.size());

    for (const auto& [_, count] : viewHistogram) {
        stats.maxClustersPerView = (std::max)(stats.maxClustersPerView, count);
    }
    for (const auto& [_, count] : instanceHistogram) {
        stats.maxClustersPerInstance = (std::max)(stats.maxClustersPerInstance, count);
    }

    if (stats.uniqueViews > 0) {
        stats.avgClustersPerView = static_cast<float>(decodeCount) / static_cast<float>(stats.uniqueViews);
    }
    if (stats.uniqueInstances > 0) {
        stats.avgClustersPerInstance = static_cast<float>(decodeCount) / static_cast<float>(stats.uniqueInstances);
    }
    if (decodeCount > 0) {
        stats.dominantViewPercent = 100.0f * static_cast<float>(stats.maxClustersPerView) / static_cast<float>(decodeCount);
        stats.dominantInstancePercent = 100.0f * static_cast<float>(stats.maxClustersPerInstance) / static_cast<float>(decodeCount);
    }

    m_clodCaptureStats = stats;
    m_clodCaptureStatsAvailable = true;
    m_clodCaptureStatsPending = false;

    spdlog::info(
        "CLod WG stats capture: visible={}, views={}, instances={}, uniqueMeshlets={}, maxPerView={}, maxPerInstance={}",
        stats.visibleClusterCount,
        stats.uniqueViews,
        stats.uniqueInstances,
        stats.uniqueMeshlets,
        stats.maxClustersPerView,
        stats.maxClustersPerInstance);
}

inline void Menu::DrawCLodTelemetryWindow() {
    ImGui::Begin("CLod Work Graph Telemetry", nullptr);

    Resource* clodTelemetryResource = nullptr;
    Resource* clodVisibleClustersResource = nullptr;
    Resource* clodVisibleCounterResource = nullptr;
    {
        m_telemetryQuery.each([&](flecs::entity, const Components::Resource& resourceComponent) {
            if (clodTelemetryResource == nullptr) {
                if (auto resource = resourceComponent.resource.lock()) {
                    clodTelemetryResource = resource.get();
                }
            }
            });

        m_visibleClustersQuery.each([&](flecs::entity, const Components::Resource& resourceComponent) {
            if (clodVisibleClustersResource == nullptr) {
                if (auto resource = resourceComponent.resource.lock()) {
                    clodVisibleClustersResource = resource.get();
                }
            }
            });

        m_visibleCounterQuery.each([&](flecs::entity, const Components::Resource& resourceComponent) {
            if (clodVisibleCounterResource == nullptr) {
                if (auto resource = resourceComponent.resource.lock()) {
                    clodVisibleCounterResource = resource.get();
                }
            }
            });
    }

    const bool captureStatsResourcesReady = (clodVisibleClustersResource != nullptr) && (clodVisibleCounterResource != nullptr);
    auto* readbackService = m_renderGraph ? m_renderGraph->GetReadbackService() : nullptr;
    const bool canCapture = (clodTelemetryResource != nullptr) && (readbackService != nullptr) && (!m_clodTelemetryCapturePending) && (!m_clodCaptureStatsPending);

    if (!captureStatsResourcesReady) {
        ImGui::TextDisabled("Extended stats unavailable: visible cluster resources not found.");
    }

    if (!canCapture) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Capture CLod WG Metrics")) {
        m_clodTelemetryCapturePending = true;
        m_clodTelemetryStatus = "Capture requested.";

        const bool requestCaptureStats = captureStatsResourcesReady;
        if (requestCaptureStats) {
            m_clodCaptureStatsPending = true;
            m_clodCaptureStatsId++;
            m_clodCaptureHasPendingCounter = false;
            m_clodCaptureHasPendingClusters = false;
            m_clodCapturePendingVisibleCount = 0;
            m_clodCapturePendingClusters.clear();
        }

        if (readbackService) {
            readbackService->RequestReadbackCapture(
                "CLod::HierarchialCullingPass2",
                clodTelemetryResource,
                RangeSpec{},
                [this](ReadbackCaptureResult&& result) {
                m_clodTelemetryCapturePending = false;

                constexpr size_t telemetryBytes = sizeof(uint32_t) * static_cast<size_t>(CLodWorkGraphCounterCount);
                if (result.data.size() < telemetryBytes) {
                    m_clodTelemetryStatus = "Capture failed: telemetry payload too small.";
                    spdlog::warn("CLod telemetry capture payload too small ({} bytes).", result.data.size());
                    return;
                }

                CLodWorkGraphTelemetryCounters decoded{};
                std::memcpy(decoded.counters.data(), result.data.data(), telemetryBytes);

                m_clodTelemetryCounters = decoded;
                m_clodTelemetryHasData = true;
                m_clodTelemetryCaptureCount++;
                m_clodTelemetryStatus = "Capture completed.";

                auto counter = [&](CLodWorkGraphCounterIndex idx) -> uint32_t {
                    return decoded.counters[static_cast<size_t>(idx)];
                    };

                const uint32_t objectThreads = counter(CLodWorkGraphCounterIndex::ObjectCullThreads);
                const uint32_t objectActive = counter(CLodWorkGraphCounterIndex::ObjectCullInRangeThreads);
                const uint32_t traverseThreads = counter(CLodWorkGraphCounterIndex::TraverseNodesThreads);
                const uint32_t traverseActive = counter(CLodWorkGraphCounterIndex::TraverseNodesActiveChildThreads);
                const uint32_t groupThreads = counter(CLodWorkGraphCounterIndex::GroupEvaluateThreads);
                const uint32_t groupActive = counter(CLodWorkGraphCounterIndex::GroupEvaluateActiveChildThreads);
                const uint32_t clusterThreads = counter(CLodWorkGraphCounterIndex::ClusterCullThreads);
                const uint32_t clusterActive = counter(CLodWorkGraphCounterIndex::ClusterCullInRangeThreads);
                const uint32_t visibleWrites = counter(CLodWorkGraphCounterIndex::ClusterCullVisibleClusterWrites);
                const uint32_t replayNodeGroupInput = counter(CLodWorkGraphCounterIndex::Phase2ReplayNodeGroupInputRecords);
                const uint32_t replayMeshletInput = counter(CLodWorkGraphCounterIndex::Phase2ReplayMeshletInputRecords);

                spdlog::info(
                    "CLod WG telemetry: ObjectCull {}/{} active, Traverse {}/{} active-child, GroupEval {}/{} active-child, ClusterCull {}/{} in-range, visible writes {}, replay(node/group={}, meshlet={})",
                    objectActive,
                    objectThreads,
                    traverseActive,
                    traverseThreads,
                    groupActive,
                    groupThreads,
                    clusterActive,
                    clusterThreads,
                    visibleWrites,
                    replayNodeGroupInput,
                    replayMeshletInput);
                });
        }

        if (requestCaptureStats) {
            const uint64_t captureId = m_clodCaptureStatsId;

            if (readbackService) {
                readbackService->RequestReadbackCapture(
                    "CLod::HierarchialCullingPass2",
                    clodVisibleCounterResource,
                    RangeSpec{},
                    [this, captureId](ReadbackCaptureResult&& result) {
                    if (!m_clodCaptureStatsPending || m_clodCaptureStatsId != captureId) {
                        return;
                    }

                    if (result.data.size() < sizeof(uint32_t)) {
                        m_clodTelemetryStatus = "Capture failed: visible counter payload too small.";
                        m_clodCaptureStatsPending = false;
                        return;
                    }

                    std::memcpy(&m_clodCapturePendingVisibleCount, result.data.data(), sizeof(uint32_t));
                    m_clodCaptureHasPendingCounter = true;
                    TryFinalizeCLodCaptureStats(captureId);
                    });

                readbackService->RequestReadbackCapture(
                    "CLod::HierarchialCullingPass2",
                    clodVisibleClustersResource,
                    RangeSpec{},
                    [this, captureId](ReadbackCaptureResult&& result) {
                    if (!m_clodCaptureStatsPending || m_clodCaptureStatsId != captureId) {
                        return;
                    }

                    const size_t clusterBytes = sizeof(VisibleCluster);
                    const size_t count = result.data.size() / clusterBytes;
                    m_clodCapturePendingClusters.resize(count);
                    if (count > 0) {
                        std::memcpy(m_clodCapturePendingClusters.data(), result.data.data(), count * clusterBytes);
                    }

                    m_clodCaptureHasPendingClusters = true;
                    TryFinalizeCLodCaptureStats(captureId);
                    });
            }

            m_clodTelemetryStatus = "Capture requested (extended stats).";
        }
    }
    if (!canCapture) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    ImGui::Text("Status: %s", m_clodTelemetryStatus.c_str());

    {
        CLodStreamingOperationStats latestOps{};
        if (TryReadCLodStreamingOperationStats(m_clodStreamingOpsLastSequence, latestOps)) {
            m_clodStreamingOpsLatest = latestOps;
            m_clodStreamingOpsHistory.push_back({ std::chrono::steady_clock::now(), latestOps });
        }

        const auto now = std::chrono::steady_clock::now();
        const auto horizon = std::chrono::seconds(5);
        m_clodStreamingOpsHistory.erase(
            std::remove_if(
                m_clodStreamingOpsHistory.begin(),
                m_clodStreamingOpsHistory.end(),
                [&](const CLodStreamingOpsHistorySample& sample) {
                    return (now - sample.timestamp) > horizon;
                }),
            m_clodStreamingOpsHistory.end());

        CLodStreamingOperationStats max5s{};
        for (const auto& sample : m_clodStreamingOpsHistory) {
            max5s.loadRequested = std::max(max5s.loadRequested, sample.stats.loadRequested);
            max5s.loadUnique = std::max(max5s.loadUnique, sample.stats.loadUnique);
            max5s.loadApplied = std::max(max5s.loadApplied, sample.stats.loadApplied);
            max5s.loadFailed = std::max(max5s.loadFailed, sample.stats.loadFailed);

            max5s.unloadRequested = std::max(max5s.unloadRequested, sample.stats.unloadRequested);
            max5s.unloadUnique = std::max(max5s.unloadUnique, sample.stats.unloadUnique);
            max5s.unloadApplied = std::max(max5s.unloadApplied, sample.stats.unloadApplied);
            max5s.unloadFailed = std::max(max5s.unloadFailed, sample.stats.unloadFailed);

            max5s.residentGroups = std::max(max5s.residentGroups, sample.stats.residentGroups);
            max5s.residentAllocations = std::max(max5s.residentAllocations, sample.stats.residentAllocations);
            max5s.queuedRequests = std::max(max5s.queuedRequests, sample.stats.queuedRequests);
            max5s.completedResults = std::max(max5s.completedResults, sample.stats.completedResults);
            max5s.residentAllocationBytes = std::max(max5s.residentAllocationBytes, sample.stats.residentAllocationBytes);
            max5s.completedResultBytes = std::max(max5s.completedResultBytes, sample.stats.completedResultBytes);
            max5s.streamedBytesThisFrame = std::max(max5s.streamedBytesThisFrame, sample.stats.streamedBytesThisFrame);
        }

        auto formatBytes = [](uint64_t bytes) {
            const double kib = 1024.0;
            const double mib = kib * 1024.0;
            const double gib = mib * 1024.0;

            if (bytes >= static_cast<uint64_t>(gib)) {
                return std::format("{:.2f} GiB", static_cast<double>(bytes) / gib);
            }
            if (bytes >= static_cast<uint64_t>(mib)) {
                return std::format("{:.2f} MiB", static_cast<double>(bytes) / mib);
            }
            if (bytes >= static_cast<uint64_t>(kib)) {
                return std::format("{:.2f} KiB", static_cast<double>(bytes) / kib);
            }

            return std::format("{} B", bytes);
        };

        ImGui::Separator();
        ImGui::TextUnformatted("Streaming operations (per frame)");
        ImGui::Text("Load: requested=%u unique=%u applied=%u failed=%u",
            m_clodStreamingOpsLatest.loadRequested,
            m_clodStreamingOpsLatest.loadUnique,
            m_clodStreamingOpsLatest.loadApplied,
            m_clodStreamingOpsLatest.loadFailed);
        ImGui::Text("Unload: requested=%u unique=%u applied=%u failed=%u",
            m_clodStreamingOpsLatest.unloadRequested,
            m_clodStreamingOpsLatest.unloadUnique,
            m_clodStreamingOpsLatest.unloadApplied,
            m_clodStreamingOpsLatest.unloadFailed);
        ImGui::Text("Resident: groups=%u allocations=%u bytes=%s",
            m_clodStreamingOpsLatest.residentGroups,
            m_clodStreamingOpsLatest.residentAllocations,
            formatBytes(m_clodStreamingOpsLatest.residentAllocationBytes).c_str());
        ImGui::Text("Backlog: queued=%u completed=%u completedBytes=%s",
            m_clodStreamingOpsLatest.queuedRequests,
            m_clodStreamingOpsLatest.completedResults,
            formatBytes(m_clodStreamingOpsLatest.completedResultBytes).c_str());
        {
            const double kbPerFrame = static_cast<double>(m_clodStreamingOpsLatest.streamedBytesThisFrame) / 1024.0;
            const float fps = ImGui::GetIO().Framerate;
            const double gbPerSec = (fps > 0.0f)
                ? (static_cast<double>(m_clodStreamingOpsLatest.streamedBytesThisFrame) * static_cast<double>(fps)) / (1024.0 * 1024.0 * 1024.0)
                : 0.0;
            ImGui::Text("Throughput: %.1f KB/frame  %.3f GB/s", kbPerFrame, gbPerSec);
        }

        ImGui::TextUnformatted("Max in last 5 seconds");
        ImGui::Text("Load max: requested=%u unique=%u applied=%u failed=%u",
            max5s.loadRequested,
            max5s.loadUnique,
            max5s.loadApplied,
            max5s.loadFailed);
        ImGui::Text("Unload max: requested=%u unique=%u applied=%u failed=%u",
            max5s.unloadRequested,
            max5s.unloadUnique,
            max5s.unloadApplied,
            max5s.unloadFailed);
        ImGui::Text("Resident max: groups=%u allocations=%u bytes=%s",
            max5s.residentGroups,
            max5s.residentAllocations,
            formatBytes(max5s.residentAllocationBytes).c_str());
        ImGui::Text("Backlog max: queued=%u completed=%u completedBytes=%s",
            max5s.queuedRequests,
            max5s.completedResults,
            formatBytes(max5s.completedResultBytes).c_str());
        {
            const double kbPerFrame = static_cast<double>(max5s.streamedBytesThisFrame) / 1024.0;
            const float fps = ImGui::GetIO().Framerate;
            const double gbPerSec = (fps > 0.0f)
                ? (static_cast<double>(max5s.streamedBytesThisFrame) * static_cast<double>(fps)) / (1024.0 * 1024.0 * 1024.0)
                : 0.0;
            ImGui::Text("Throughput max: %.1f KB/frame  %.3f GB/s", kbPerFrame, gbPerSec);
        }
    }

    if (m_clodTelemetryHasData) {
        auto counter = [&](CLodWorkGraphCounterIndex idx) -> uint32_t {
            return m_clodTelemetryCounters.counters[static_cast<size_t>(idx)];
            };

        auto drawUtilizationRow = [&](const char* label, uint32_t active, uint32_t total) {
            const float efficiency = (total > 0)
                ? (100.0f * static_cast<float>(active) / static_cast<float>(total))
                : 0.0f;
            ImGui::Text("%s: %u / %u (%.1f%%)", label, active, total, efficiency);
            };

        ImGui::Text("Telemetry captures: %llu", static_cast<unsigned long long>(m_clodTelemetryCaptureCount));
        drawUtilizationRow(
            "ObjectCull active draw threads",
            counter(CLodWorkGraphCounterIndex::ObjectCullInRangeThreads),
            counter(CLodWorkGraphCounterIndex::ObjectCullThreads));
        drawUtilizationRow(
            "TraverseNodes active child threads",
            counter(CLodWorkGraphCounterIndex::TraverseNodesActiveChildThreads),
            counter(CLodWorkGraphCounterIndex::TraverseNodesThreads));
        drawUtilizationRow(
            "GroupEvaluate active child threads",
            counter(CLodWorkGraphCounterIndex::GroupEvaluateActiveChildThreads),
            counter(CLodWorkGraphCounterIndex::GroupEvaluateThreads));
        drawUtilizationRow(
            "ClusterCull in-range threads",
            counter(CLodWorkGraphCounterIndex::ClusterCullInRangeThreads),
            counter(CLodWorkGraphCounterIndex::ClusterCullThreads));

        const uint32_t clusterActiveLanes = counter(CLodWorkGraphCounterIndex::ClusterCullActiveLanes);
        const uint32_t clusterSurvivingLanes = counter(CLodWorkGraphCounterIndex::ClusterCullSurvivingLanes);
        drawUtilizationRow("ClusterCull surviving lanes", clusterSurvivingLanes, clusterActiveLanes);

        ImGui::Text("Traverse node records: internal=%u leaf=%u culled=%u rejectedByError=%u",
            counter(CLodWorkGraphCounterIndex::TraverseNodesInternalNodeRecords),
            counter(CLodWorkGraphCounterIndex::TraverseNodesLeafNodeRecords),
            counter(CLodWorkGraphCounterIndex::TraverseNodesCulledNodeRecords),
            counter(CLodWorkGraphCounterIndex::TraverseNodesRejectedByErrorRecords));

        const uint32_t traverseCoalescedLaunches = counter(CLodWorkGraphCounterIndex::TraverseNodesCoalescedLaunches);
        const uint32_t traverseCoalescedInputRecords = counter(CLodWorkGraphCounterIndex::TraverseNodesCoalescedInputRecords);
        const float avgRecordsPerLaunch = (traverseCoalescedLaunches > 0)
            ? (static_cast<float>(traverseCoalescedInputRecords) / static_cast<float>(traverseCoalescedLaunches))
            : 0.0f;
        const float packingPercent = 100.0f * avgRecordsPerLaunch / 8.0f;

        ImGui::Text("Traverse coalesced launches: %u | input records: %u | avg records/launch: %.2f (%.1f%% of 8)",
            traverseCoalescedLaunches,
            traverseCoalescedInputRecords,
            avgRecordsPerLaunch,
            packingPercent);

        std::array<uint32_t, 8> traverseInputHistogram = {
            counter(CLodWorkGraphCounterIndex::TraverseNodesCoalescedInputCount1),
            counter(CLodWorkGraphCounterIndex::TraverseNodesCoalescedInputCount2),
            counter(CLodWorkGraphCounterIndex::TraverseNodesCoalescedInputCount3),
            counter(CLodWorkGraphCounterIndex::TraverseNodesCoalescedInputCount4),
            counter(CLodWorkGraphCounterIndex::TraverseNodesCoalescedInputCount5),
            counter(CLodWorkGraphCounterIndex::TraverseNodesCoalescedInputCount6),
            counter(CLodWorkGraphCounterIndex::TraverseNodesCoalescedInputCount7),
            counter(CLodWorkGraphCounterIndex::TraverseNodesCoalescedInputCount8)
        };

        ImGui::TextUnformatted("Traverse coalesced input histogram (records per launch):");
        float histogramValues[8] = {};
        for (size_t i = 0; i < traverseInputHistogram.size(); ++i) {
            histogramValues[i] = static_cast<float>(traverseInputHistogram[i]);
        }

        static const char* kHistogramLabels[8] = { "1", "2", "3", "4", "5", "6", "7", "8" };
        if (ImPlot::BeginPlot("##TraverseCoalescedInputHistogram", ImVec2(-1.0f, 150.0f), ImPlotFlags_NoLegend)) {
            ImPlot::SetupAxes("Records", "Launches", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::SetupAxisTicks(ImAxis_X1, 0.0, 7.0, 8, kHistogramLabels);
            ImPlot::PlotBars("Launches", histogramValues, 8, 0.6f, 0.0f);
            ImPlot::EndPlot();
        }

        ImGui::Text("GroupEvaluate outcomes: groups=%u culled=%u rejectedByError=%u emitBucket=%u refinedTraversal=%u nonResidentRefined=%u nonResidentFallbackEmit=%u",
            counter(CLodWorkGraphCounterIndex::GroupEvaluateGroupRecords),
            counter(CLodWorkGraphCounterIndex::GroupEvaluateCulledGroupRecords),
            counter(CLodWorkGraphCounterIndex::GroupEvaluateRejectedByErrorRecords),
            counter(CLodWorkGraphCounterIndex::GroupEvaluateEmitBucketThreads),
            counter(CLodWorkGraphCounterIndex::GroupEvaluateRefinedTraversalThreads),
            counter(CLodWorkGraphCounterIndex::GroupEvaluateNonResidentRefinedChildThreads),
            counter(CLodWorkGraphCounterIndex::GroupEvaluateNonResidentFallbackBucketThreads));

        const uint32_t groupCoalescedLaunches = counter(CLodWorkGraphCounterIndex::GroupEvaluateCoalescedLaunches);
        const uint32_t groupCoalescedInputRecords = counter(CLodWorkGraphCounterIndex::GroupEvaluateCoalescedInputRecords);
        const float groupAvgRecordsPerLaunch = (groupCoalescedLaunches > 0)
            ? (static_cast<float>(groupCoalescedInputRecords) / static_cast<float>(groupCoalescedLaunches))
            : 0.0f;
        const float groupPackingPercent = 100.0f * groupAvgRecordsPerLaunch / 8.0f;

        ImGui::Text("GroupEvaluate coalesced launches: %u | input records: %u | avg records/launch: %.2f (%.1f%% of 8)",
            groupCoalescedLaunches,
            groupCoalescedInputRecords,
            groupAvgRecordsPerLaunch,
            groupPackingPercent);

        std::array<uint32_t, 8> groupInputHistogram = {
            counter(CLodWorkGraphCounterIndex::GroupEvaluateCoalescedInputCount1),
            counter(CLodWorkGraphCounterIndex::GroupEvaluateCoalescedInputCount2),
            counter(CLodWorkGraphCounterIndex::GroupEvaluateCoalescedInputCount3),
            counter(CLodWorkGraphCounterIndex::GroupEvaluateCoalescedInputCount4),
            counter(CLodWorkGraphCounterIndex::GroupEvaluateCoalescedInputCount5),
            counter(CLodWorkGraphCounterIndex::GroupEvaluateCoalescedInputCount6),
            counter(CLodWorkGraphCounterIndex::GroupEvaluateCoalescedInputCount7),
            counter(CLodWorkGraphCounterIndex::GroupEvaluateCoalescedInputCount8)
        };

        ImGui::TextUnformatted("GroupEvaluate coalesced input histogram (records per launch):");
        float groupHistogramValues[8] = {};
        for (size_t i = 0; i < groupInputHistogram.size(); ++i) {
            groupHistogramValues[i] = static_cast<float>(groupInputHistogram[i]);
        }

        static const char* kGroupHistogramLabels[8] = { "1", "2", "3", "4", "5", "6", "7", "8" };
        if (ImPlot::BeginPlot("##GroupEvaluateCoalescedInputHistogram", ImVec2(-1.0f, 150.0f), ImPlotFlags_NoLegend)) {
            ImPlot::SetupAxes("Records", "Launches", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::SetupAxisTicks(ImAxis_X1, 0.0, 7.0, 8, kGroupHistogramLabels);
            ImPlot::PlotBars("Launches", groupHistogramValues, 8, 0.6f, 0.0f);
            ImPlot::EndPlot();
        }

        const uint32_t clusterWaves = counter(CLodWorkGraphCounterIndex::ClusterCullWaves);
        const uint32_t zeroSurvivorWaves = counter(CLodWorkGraphCounterIndex::ClusterCullZeroSurvivorWaves);
        const uint32_t survivingWaves = (clusterWaves > zeroSurvivorWaves)
            ? (clusterWaves - zeroSurvivorWaves)
            : 0u;
        drawUtilizationRow("ClusterCull waves with survivors", survivingWaves, clusterWaves);

        ImGui::Text("Visible cluster writes: %u", counter(CLodWorkGraphCounterIndex::ClusterCullVisibleClusterWrites));

        ImGui::Separator();
        ImGui::TextUnformatted("Occlusion -> Phase 2 enqueue attempts");
        ImGui::Text("Node attempts: %u | Group attempts: %u | Cluster attempts: %u",
            counter(CLodWorkGraphCounterIndex::Phase1OcclusionNodeReplayEnqueueAttempts),
            counter(CLodWorkGraphCounterIndex::Phase1OcclusionGroupReplayEnqueueAttempts),
            counter(CLodWorkGraphCounterIndex::Phase1OcclusionClusterReplayEnqueueAttempts));

        ImGui::TextUnformatted("Phase 2 replay launch validation");
        ImGui::Text("ReplayNodeGroup launches: %u | input records: %u",
            counter(CLodWorkGraphCounterIndex::Phase2ReplayNodeGroupLaunches),
            counter(CLodWorkGraphCounterIndex::Phase2ReplayNodeGroupInputRecords));
        ImGui::Text("ReplayNodeGroup input split: nodes=%u groups=%u",
            counter(CLodWorkGraphCounterIndex::Phase2ReplayNodeInputRecords),
            counter(CLodWorkGraphCounterIndex::Phase2ReplayGroupInputRecords));
        ImGui::Text("ReplayNodeGroup emitted: traverse=%u groupEval=%u",
            counter(CLodWorkGraphCounterIndex::Phase2ReplayNodeRecordsEmitted),
            counter(CLodWorkGraphCounterIndex::Phase2ReplayGroupRecordsEmitted));
        ImGui::Text("ReplayMeshlet launches: %u | input records: %u | emitted bucket records: %u",
            counter(CLodWorkGraphCounterIndex::Phase2ReplayMeshletLaunches),
            counter(CLodWorkGraphCounterIndex::Phase2ReplayMeshletInputRecords),
            counter(CLodWorkGraphCounterIndex::Phase2ReplayMeshletBucketRecordsEmitted));

        ImGui::TextUnformatted("Phase 2 downstream consumption");
        ImGui::Text("Replay Traverse records consumed: %u",
            counter(CLodWorkGraphCounterIndex::Phase2ReplayTraverseRecordsConsumed));
        ImGui::Text("Replay GroupEvaluate records consumed: %u",
            counter(CLodWorkGraphCounterIndex::Phase2ReplayGroupRecordsConsumed));
        ImGui::Text("Replay ClusterCull bucket records consumed: %u",
            counter(CLodWorkGraphCounterIndex::Phase2ReplayClusterBucketRecordsConsumed));
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Extended capture statistics");
    if (m_clodCaptureStatsPending) {
        ImGui::Text("Capture stats status: pending...");
    }
    else if (!m_clodCaptureStatsAvailable) {
        ImGui::TextDisabled("No extended capture results yet.");
    }
    else {
        ImGui::Text("Visible clusters: %u", m_clodCaptureStats.visibleClusterCount);
        ImGui::Text("Unique views: %u | Unique instances: %u | Unique meshlets: %u",
            m_clodCaptureStats.uniqueViews,
            m_clodCaptureStats.uniqueInstances,
            m_clodCaptureStats.uniqueMeshlets);
        ImGui::Text("Avg clusters/view: %.2f | Avg clusters/instance: %.2f",
            m_clodCaptureStats.avgClustersPerView,
            m_clodCaptureStats.avgClustersPerInstance);
        ImGui::Text("Max clusters/view: %u (%.1f%% of total)",
            m_clodCaptureStats.maxClustersPerView,
            m_clodCaptureStats.dominantViewPercent);
        ImGui::Text("Max clusters/instance: %u (%.1f%% of total)",
            m_clodCaptureStats.maxClustersPerInstance,
            m_clodCaptureStats.dominantInstancePercent);
    }

    ImGui::End();
}

inline void Menu::DrawPassTimingWindow() {
    if (!m_renderGraph) {
        return;
    }

    auto* statisticsService = m_renderGraph->GetStatisticsService();
    if (!statisticsService) {
        return;
    }

    auto& names     = statisticsService->GetPassNames();
    auto& stats     = statisticsService->GetPassStats();
    auto& meshStats = statisticsService->GetMeshStats();
    auto& isGeom    = statisticsService->GetIsGeometryPassVector();
    static int maxStaleFrames = 240;

    if (names.empty()) {
        return;
    }

    ImGui::Begin("Pass Timings");
    ImGui::SliderInt("Max Stale Frames", &maxStaleFrames, 0, 2000);

    const auto& visible = statisticsService->GetVisiblePassIndices(static_cast<uint64_t>(maxStaleFrames));
    if (visible.empty()) {
        ImGui::TextUnformatted("No pass timings within selected staleness window.");
        ImGui::End();
        return;
    }

    static std::vector<bool> pinned;
    static bool sortEnabled = true;
    if (pinned.size() != names.size()) {
        pinned.assign(names.size(), false);
    }

    // split pinned vs unpinned
    std::vector<int> pins;
    std::vector<std::pair<int,double>> unsorted;
    for (unsigned rawIdx : visible) {
        const int i = static_cast<int>(rawIdx);
        if (pinned[i]) {
            pins.push_back(i);
        }
        else {
            unsorted.emplace_back(i, stats[i].ema);
        }
    }
    if (sortEnabled) {
        std::sort(unsorted.begin(), unsorted.end(), [](auto& a, auto& b) { return a.second > b.second; });
    }

    std::vector<int> order;
    order.insert(order.end(), pins.begin(), pins.end());
    for (auto& p : unsorted) {
        order.push_back(p.first);
    }

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

    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, wName);
    ImGui::SetColumnWidth(1, wNum);

    // header
    ImGui::TextUnformatted("Pass"); ImGui::NextColumn();
    ImGui::TextUnformatted("Avg (ms)"); ImGui::SameLine();
    if (ImGui::SmallButton(sortEnabled ? "v" : ">")) {
        sortEnabled = !sortEnabled;
    }
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

inline void Menu::DrawAutoAliasPlannerWindow() {
    if (!ImGui::Begin("Auto Alias Planner", nullptr)) {
        ImGui::End();
        return;
    }

    constexpr const char* kAutoAliasModeNames[] = {
        "Off",
        "Conservative",
        "Balanced",
        "Aggressive"
    };

    int autoAliasModeIndex = static_cast<int>(m_autoAliasMode);
    if (ImGui::Combo("Mode", &autoAliasModeIndex, kAutoAliasModeNames, IM_ARRAYSIZE(kAutoAliasModeNames))) {
        autoAliasModeIndex = std::clamp(autoAliasModeIndex, 0, static_cast<int>(IM_ARRAYSIZE(kAutoAliasModeNames) - 1));
        m_autoAliasMode = static_cast<AutoAliasMode>(autoAliasModeIndex);
        setAutoAliasMode(m_autoAliasMode);
    }

    constexpr const char* kPackingStrategyNames[] = {
        "Greedy Sweep-Line",
        "Beam Search (Near-Optimal)",
    };

    int packingStrategyIndex = static_cast<int>(m_autoAliasPackingStrategy);
    if (ImGui::Combo("Packing Strategy", &packingStrategyIndex, kPackingStrategyNames, IM_ARRAYSIZE(kPackingStrategyNames))) {
        packingStrategyIndex = std::clamp(packingStrategyIndex, 0, static_cast<int>(IM_ARRAYSIZE(kPackingStrategyNames) - 1));
        m_autoAliasPackingStrategy = static_cast<AutoAliasPackingStrategy>(packingStrategyIndex);
        setAutoAliasPackingStrategy(m_autoAliasPackingStrategy);
    }

    if (ImGui::Checkbox("Log Exclusions", &m_autoAliasLogExclusionReasons)) {
        setAutoAliasLogExclusionReasons(m_autoAliasLogExclusionReasons);
    }

    int retireIdleFrames = static_cast<int>(m_autoAliasPoolRetireIdleFrames);
    if (ImGui::SliderInt("Pool Retire Idle Frames", &retireIdleFrames, 0, 2000)) {
        retireIdleFrames = std::max(retireIdleFrames, 0);
        m_autoAliasPoolRetireIdleFrames = static_cast<uint32_t>(retireIdleFrames);
        setAutoAliasPoolRetireIdleFrames(m_autoAliasPoolRetireIdleFrames);
    }

    if (ImGui::SliderFloat("Pool Growth Headroom", &m_autoAliasPoolGrowthHeadroom, 1.0f, 3.0f, "%.2fx")) {
        m_autoAliasPoolGrowthHeadroom = std::max(1.0f, m_autoAliasPoolGrowthHeadroom);
        setAutoAliasPoolGrowthHeadroom(m_autoAliasPoolGrowthHeadroom);
    }

    if (m_renderGraph) {
        ImGui::Separator();
        auto formatBytes = [](uint64_t bytes) {
            constexpr double kKB = 1024.0;
            constexpr double kMB = 1024.0 * 1024.0;
            constexpr double kGB = 1024.0 * 1024.0 * 1024.0;

            const double value = static_cast<double>(bytes);
            if (value >= kGB) {
                return std::format("{:.2f} GB", value / kGB);
            }
            if (value >= kMB) {
                return std::format("{:.2f} MB", value / kMB);
            }
            if (value >= kKB) {
                return std::format("{:.2f} KB", value / kKB);
            }
            return std::format("{:.2f} B", value);
        };

        const auto snapshot = m_renderGraph->GetAutoAliasDebugSnapshot();
        constexpr const char* kModeNames[] = { "Off", "Conservative", "Balanced", "Aggressive" };
        constexpr const char* kStrategyNames[] = { "Greedy Sweep-Line", "Beam Search (Near-Optimal)" };
        const int modeIdx = std::clamp(static_cast<int>(snapshot.mode), 0, static_cast<int>(IM_ARRAYSIZE(kModeNames) - 1));
        const int strategyIdx = std::clamp(static_cast<int>(snapshot.packingStrategy), 0, static_cast<int>(IM_ARRAYSIZE(kStrategyNames) - 1));
        ImGui::Text("Active mode: %s", kModeNames[modeIdx]);
        ImGui::Text("Active strategy: %s", kStrategyNames[strategyIdx]);

        ImGui::Text("Candidates: %llu | Manual: %llu | Auto: %llu | Excluded: %llu",
            static_cast<unsigned long long>(snapshot.candidatesSeen),
            static_cast<unsigned long long>(snapshot.manuallyAssigned),
            static_cast<unsigned long long>(snapshot.autoAssigned),
            static_cast<unsigned long long>(snapshot.excluded));

        ImGui::Text("Candidate MB: %.2f | Auto MB: %.2f",
            static_cast<double>(snapshot.candidateBytes) / (1024.0 * 1024.0),
            static_cast<double>(snapshot.autoAssignedBytes) / (1024.0 * 1024.0));

        const double independentMB = static_cast<double>(snapshot.pooledIndependentBytes) / (1024.0 * 1024.0);
        const double pooledMB = static_cast<double>(snapshot.pooledActualBytes) / (1024.0 * 1024.0);
        const double savedMB = static_cast<double>(snapshot.pooledSavedBytes) / (1024.0 * 1024.0);
        const double savedPct = (snapshot.pooledIndependentBytes > 0)
            ? (100.0 * static_cast<double>(snapshot.pooledSavedBytes) / static_cast<double>(snapshot.pooledIndependentBytes))
            : 0.0;

        ImGui::Text("Pooling memory (alias candidates)");
        ImGui::BulletText("Independent: %.2f MB", independentMB);
        ImGui::BulletText("Pooled: %.2f MB", pooledMB);
        ImGui::BulletText("Saved: %.2f MB (%.1f%%)", savedMB, savedPct);

        if (!snapshot.poolDebug.empty()) {
            ImGui::Separator();
            ImGui::TextUnformatted("Pool byte overlap view");

            for (const auto& pool : snapshot.poolDebug) {
                ImGui::PushID(static_cast<int>(pool.poolID & 0x7fffffff));

                const double requiredMB = static_cast<double>(pool.requiredBytes) / (1024.0 * 1024.0);
                const double reservedMB = static_cast<double>(pool.reservedBytes) / (1024.0 * 1024.0);
                const std::string header = std::format(
                    "Pool {} (resources={}, required={:.2f} MB, reserved={:.2f} MB)",
                    static_cast<unsigned long long>(pool.poolID),
                    pool.ranges.size(),
                    requiredMB,
                    reservedMB);

                if (ImGui::TreeNode(header.c_str())) {
                    if (pool.ranges.empty()) {
                        ImGui::TextDisabled("No ranges");
                        ImGui::TreePop();
                        ImGui::PopID();
                        continue;
                    }

                    std::vector<RenderGraph::AutoAliasPoolRangeDebug> ranges = pool.ranges;
                    std::sort(ranges.begin(), ranges.end(), [](const auto& a, const auto& b) {
                        if (a.startByte != b.startByte) {
                            return a.startByte < b.startByte;
                        }
                        return a.resourceID < b.resourceID;
                        });

                    uint64_t maxByte = std::max<uint64_t>(1ull, std::max(pool.requiredBytes, pool.reservedBytes));
                    for (const auto& r : ranges) {
                        maxByte = std::max(maxByte, r.endByte);
                    }

                    const float rowHeight = 18.0f;
                    const float plotHeight = std::max(80.0f, rowHeight * static_cast<float>(ranges.size()) + 28.0f);
                    const float labelWidth = 260.0f;
                    ImVec2 canvasSize(ImGui::GetContentRegionAvail().x, plotHeight);
                    if (canvasSize.x < 320.0f) {
                        canvasSize.x = 320.0f;
                    }

                    const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
                    ImGui::InvisibleButton("##AliasPoolOverlapPlot", canvasSize);
                    ImDrawList* draw = ImGui::GetWindowDrawList();

                    const float left = canvasPos.x;
                    const float top = canvasPos.y;
                    const float right = canvasPos.x + canvasSize.x;
                    const float bottom = canvasPos.y + canvasSize.y;

                    const float plotLeft = left + labelWidth;
                    const float plotRight = right - 10.0f;
                    const float plotWidth = std::max(1.0f, plotRight - plotLeft);
                    const float plotTop = top + 6.0f;

                    draw->AddRectFilled(ImVec2(left, top), ImVec2(right, bottom), IM_COL32(20, 20, 20, 100));
                    draw->AddRect(ImVec2(left, top), ImVec2(right, bottom), IM_COL32(255, 255, 255, 40));

                    auto toX = [&](uint64_t byteOffset) {
                        const double t = static_cast<double>(byteOffset) / static_cast<double>(maxByte);
                        return plotLeft + static_cast<float>(t) * plotWidth;
                        };

                    draw->AddLine(ImVec2(plotLeft, bottom - 14.0f), ImVec2(plotRight, bottom - 14.0f), IM_COL32(220, 220, 220, 140), 1.0f);
                    draw->AddText(ImVec2(plotLeft, bottom - 13.0f), IM_COL32(220, 220, 220, 200), "0");
                    const std::string maxLabel = std::format("{} B", static_cast<unsigned long long>(maxByte));
                    draw->AddText(ImVec2(plotRight - ImGui::CalcTextSize(maxLabel.c_str()).x, bottom - 13.0f), IM_COL32(220, 220, 220, 200), maxLabel.c_str());

                    if (pool.reservedBytes > 0 && pool.reservedBytes != maxByte) {
                        const float reservedX = toX(pool.reservedBytes);
                        draw->AddLine(ImVec2(reservedX, plotTop), ImVec2(reservedX, bottom - 16.0f), IM_COL32(255, 230, 120, 120), 1.0f);
                    }

                    for (size_t i = 0; i < ranges.size(); ++i) {
                        const auto& r = ranges[i];
                        const float y0 = plotTop + static_cast<float>(i) * rowHeight;
                        const float y1 = y0 + rowHeight - 4.0f;
                        const float x0 = toX(r.startByte);
                        const float x1 = toX(r.endByte);

                        draw->AddRectFilled(ImVec2(x0, y0), ImVec2(std::max(x0 + 1.0f, x1), y1), IM_COL32(90, 170, 250, 180));
                        draw->AddRect(ImVec2(x0, y0), ImVec2(std::max(x0 + 1.0f, x1), y1), IM_COL32(15, 30, 45, 220));

                        std::vector<std::pair<uint64_t, uint64_t>> overlapSegments;
                        overlapSegments.reserve(ranges.size());
                        for (size_t j = 0; j < ranges.size(); ++j) {
                            if (i == j) {
                                continue;
                            }
                            const auto& other = ranges[j];
                            const uint64_t overlapStart = std::max(r.startByte, other.startByte);
                            const uint64_t overlapEnd = std::min(r.endByte, other.endByte);
                            if (overlapStart < overlapEnd) {
                                overlapSegments.emplace_back(overlapStart, overlapEnd);
                            }
                        }

                        if (!overlapSegments.empty()) {
                            std::sort(overlapSegments.begin(), overlapSegments.end());
                            std::vector<std::pair<uint64_t, uint64_t>> merged;
                            for (const auto& seg : overlapSegments) {
                                if (merged.empty() || seg.first > merged.back().second) {
                                    merged.push_back(seg);
                                }
                                else {
                                    merged.back().second = std::max(merged.back().second, seg.second);
                                }
                            }

                            for (const auto& seg : merged) {
                                const float ox0 = toX(seg.first);
                                const float ox1 = toX(seg.second);
                                draw->AddRectFilled(ImVec2(ox0, y0), ImVec2(std::max(ox0 + 1.0f, ox1), y1), IM_COL32(255, 80, 80, 210));
                            }
                        }

                        const std::string label = std::format(
                            "{} ({})",
                            r.resourceName,
                            formatBytes(r.sizeBytes));
                        draw->AddText(ImVec2(left + 6.0f, y0), IM_COL32(230, 230, 230, 230), label.c_str());
                    }

                    ImGui::TreePop();
                }

                ImGui::PopID();
            }
        }

        if (!snapshot.exclusionReasons.empty()) {
            ImGui::Separator();
            ImGui::TextUnformatted("Top exclusion reasons:");
            const size_t maxReasons = std::min<size_t>(snapshot.exclusionReasons.size(), 8);
            for (size_t i = 0; i < maxReasons; ++i) {
                ImGui::BulletText("%s (%llu)",
                    snapshot.exclusionReasons[i].reason.c_str(),
                    static_cast<unsigned long long>(snapshot.exclusionReasons[i].count));
            }
        }
    }
    else {
        ImGui::Separator();
        ImGui::TextDisabled("Render graph not available.");
    }

    ImGui::End();
}
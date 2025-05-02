#pragma once


#include <directx/d3d12.h>
#include <wrl/client.h>
#include <memory>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>
#include <functional>
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <flecs.h>
#include <vector>
#include <algorithm>

#include "Render/RenderContext.h"
#include "Utilities/Utilities.h"
#include "Render/OutputTypes.h"
#include "Import/ModelLoader.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Render/TonemapTypes.h"
#include "Managers/Singletons/StatisticsManager.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct FrameContext {
    ID3D12CommandAllocator* CommandAllocator;
    UINT64                  FenceValue;
};

static UINT g_frameIndex = 0;
static HANDLE g_hSwapChainWaitableObject = nullptr;
constexpr unsigned int NUM_FRAMES_IN_FLIGHT = 3;
static FrameContext g_frameContext[NUM_FRAMES_IN_FLIGHT] = {};
static ID3D12Fence* g_fence = nullptr;
static HANDLE g_fenceEvent = nullptr;
static UINT64 g_fenceLastSignaledValue = 0;

class Menu {
public:
    static Menu& GetInstance();

    void Initialize(HWND hwnd, Microsoft::WRL::ComPtr<ID3D12Device> device, Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue, Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain);
    void Render(const RenderContext& context);
    bool HandleInput(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> g_pd3dSrvDescHeap = nullptr;
    Menu() { ImGui::CreateContext(); };
	Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Device> device = nullptr;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue = nullptr;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;

    flecs::entity selectedNode;

    FrameContext* WaitForNextFrameResources();

    int FindFileIndex(const std::vector<std::string>& hdrFiles, const std::string& existingFile);

    void DrawEnvironmentsDropdown();
	void DrawOutputTypeDropdown();
    void DrawTonemapTypeDropdown();
    void DrawBrowseButton(const std::wstring& targetDirectory);
    void DrawLoadModelButton();
    void DisplaySceneNode(flecs::entity node, bool isOnlyChild);
    void DisplaySceneGraph();
    void DisplaySelectedNode();
    void DrawPassTimingWindow();

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

    bool meshShaderEnabled = false;
    bool indirectDrawsWereEnabled = false;
    std::function<bool()> getMeshShaderEnabled;
	std::function<void(bool)> setMeshShaderEnabled;

	bool indirectDrawsEnabled = false;
	std::function<bool()> getIndirectDrawsEnabled;
	std::function<void(bool)> setIndirectDrawsEnabled;

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

	bool deferredRendering = true;
	std::function<bool()> getDeferredRenderingEnabled;
	std::function<void(bool)> setDeferredRenderingEnabled;

	bool m_gtaoEnabled = true;
	std::function<bool()> getGTAOEnabled;
	std::function<void(bool)> setGTAOEnabled;
};

inline Menu& Menu::GetInstance() {
    static Menu instance;
    return instance;
}

inline bool Menu::HandleInput(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
}

inline void Menu::Initialize(HWND hwnd, Microsoft::WRL::ComPtr<ID3D12Device> device, Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue, Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain) {
    this->device = device;
    this->commandQueue = queue;
	m_swapChain = swapChain;

    environmentsDir = std::filesystem::path(GetExePath()) / "textures" / "environment";

    for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_frameContext[i].CommandAllocator)));

    ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frameContext[0].CommandAllocator, nullptr, IID_PPV_ARGS(&m_commandList)));
    m_commandList->Close();

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dSrvDescHeap)) != S_OK)
        throw std::runtime_error("Failed to create descriptor heap");

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX12_Init(device.Get(), NUM_FRAMES_IN_FLIGHT,
        DXGI_FORMAT_R8G8B8A8_UNORM, g_pd3dSrvDescHeap.Get(),
        g_pd3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
        g_pd3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart());
    ImGui_ImplWin32_EnableDpiAwareness();


    IMGUI_CHECKVERSION();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.FontGlobalScale = 1.2f;
	io.DisplaySize = ImVec2(3420.0f, 3080.0f);

    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();


    g_hSwapChainWaitableObject = m_swapChain->GetFrameLatencyWaitableObject();

	getEnvironmentName = SettingsManager::GetInstance().getSettingGetter<std::string>("environmentName");
	setEnvironment = SettingsManager::GetInstance().getSettingSetter<std::string>("environmentName");

    auto& settingsManager = SettingsManager::GetInstance();
    getImageBasedLightingEnabled = settingsManager.getSettingGetter<bool>("enableImageBasedLighting");
    setImageBasedLightingEnabled = settingsManager.getSettingSetter<bool>("enableImageBasedLighting");
	imageBasedLightingEnabled = getImageBasedLightingEnabled();

	getPunctualLightingEnabled = settingsManager.getSettingGetter<bool>("enablePunctualLighting");
	setPunctualLightingEnabled = settingsManager.getSettingSetter<bool>("enablePunctualLighting");
	punctualLightingEnabled = getPunctualLightingEnabled();

	getShadowsEnabled = settingsManager.getSettingGetter<bool>("enableShadows");
	setShadowsEnabled = settingsManager.getSettingSetter<bool>("enableShadows");
	shadowsEnabled = getShadowsEnabled();

    hdrFiles = GetFilesInDirectoryMatchingExtension(environmentsDir.wstring(), L".hdr");
	environmentName = getEnvironmentName();
    settingsManager.addObserver<std::string>("environmentName", [this](const std::string& newValue) {
        environmentName = getEnvironmentName();
        });

	setOutputType = settingsManager.getSettingSetter<unsigned int>("outputType");
	setTonemapType = settingsManager.getSettingSetter<unsigned int>("tonemapType");

    getSceneRoot = settingsManager.getSettingGetter<std::function<flecs::entity()>>("getSceneRoot")();

	setMeshShaderEnabled = settingsManager.getSettingSetter<bool>("enableMeshShader");
	getMeshShaderEnabled = settingsManager.getSettingGetter<bool>("enableMeshShader");
	meshShaderEnabled = getMeshShaderEnabled();

	setIndirectDrawsEnabled = settingsManager.getSettingSetter<bool>("enableIndirectDraws");
	getIndirectDrawsEnabled = settingsManager.getSettingGetter<bool>("enableIndirectDraws");
	indirectDrawsEnabled = getIndirectDrawsEnabled();

	setWireframeEnabled = settingsManager.getSettingSetter<bool>("enableWireframe");
	getWireframeEnabled = settingsManager.getSettingGetter<bool>("enableWireframe");
	wireframeEnabled = getWireframeEnabled();

	setAllowTearing = settingsManager.getSettingSetter<bool>("allowTearing");
	getAllowTearing = settingsManager.getSettingGetter<bool>("allowTearing");
	allowTearing = getAllowTearing();

	setDrawBoundingSpheres = settingsManager.getSettingSetter<bool>("drawBoundingSpheres");
	getDrawBoundingSpheres = settingsManager.getSettingGetter<bool>("drawBoundingSpheres");
	drawBoundingSpheres = getDrawBoundingSpheres();

	setClusteredLightingEnabled = settingsManager.getSettingSetter<bool>("enableClusteredLighting");
	getClusteredLightingEnabled = settingsManager.getSettingGetter<bool>("enableClusteredLighting");
	clusteredLighting = getClusteredLightingEnabled();

	setDeferredRenderingEnabled = settingsManager.getSettingSetter<bool>("enableDeferredRendering");
	getDeferredRenderingEnabled = settingsManager.getSettingGetter<bool>("enableDeferredRendering");
	deferredRendering = getDeferredRenderingEnabled();

	getGTAOEnabled = settingsManager.getSettingGetter<bool>("enableGTAO");
	setGTAOEnabled = settingsManager.getSettingSetter<bool>("enableGTAO");
	m_gtaoEnabled = getGTAOEnabled();

    m_meshShadersSupported = DeviceManager::GetInstance().GetMeshShadersSupported();
}

inline void Menu::Render(const RenderContext& context) {
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();

	ImGui::NewFrame();

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
                if (!meshShaderEnabled) {
                    if (indirectDrawsEnabled) {
                        setIndirectDrawsEnabled(false);
                    }
                }
                else {
                    if (indirectDrawsEnabled) {
                        setIndirectDrawsEnabled(true);
                    }
                }
                setMeshShaderEnabled(meshShaderEnabled);
            }
        }
        else {
            ImGui::Text("Your GPU does not support mesh shaders!");
        }
		if (!meshShaderEnabled) {
			ImGui::Text("Mesh Shaders must be enabled to use Indirect Draws.");
		}
        else {
            if (ImGui::Checkbox("Use Indirect Draws", &indirectDrawsEnabled)) {
                setIndirectDrawsEnabled(indirectDrawsEnabled);
            }
        }
		if (ImGui::Checkbox("Wireframe", &wireframeEnabled)) {
			setWireframeEnabled(wireframeEnabled);
		}
        if (ImGui::Checkbox("Uncap framerate", &allowTearing)) {
			setAllowTearing(allowTearing);
        }
		if (ImGui::Checkbox("Draw Bounding Spheres", &drawBoundingSpheres)) {
			setDrawBoundingSpheres(drawBoundingSpheres);
		}
        if (ImGui::Checkbox("Clustered Lighting", &clusteredLighting)) {
			setClusteredLightingEnabled(clusteredLighting);
        }
		if (ImGui::Checkbox("Deferred Rendering", &deferredRendering)) {
			setDeferredRenderingEnabled(deferredRendering);
		}
		if (ImGui::Checkbox("Enable GTAO", &m_gtaoEnabled)) {
			setGTAOEnabled(m_gtaoEnabled);
		}
        DrawTonemapTypeDropdown();

        DrawEnvironmentsDropdown();
        DrawBrowseButton(environmentsDir.wstring());
		DrawOutputTypeDropdown();
        DrawLoadModelButton();

		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
		ImGui::End();
	}

    {
		ImGui::Begin("Scene Graph", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
		DisplaySceneGraph();
		ImGui::End();

		DisplaySelectedNode();

        DrawPassTimingWindow();
    }

	// Rendering
	ImGui::Render();

    FrameContext* frameCtx = WaitForNextFrameResources();

    frameCtx->CommandAllocator->Reset();
    m_commandList->Reset(frameCtx->CommandAllocator, nullptr);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(context.rtvHeap->GetCPUDescriptorHandleForHeapStart(), context.frameIndex, context.rtvDescriptorSize);
	auto dsvHandle = context.pPrimaryDepthBuffer->GetDSVInfos()[0].cpuHandle;
    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
	auto heap = g_pd3dSrvDescHeap.Get();
    m_commandList->SetDescriptorHeaps(1, &heap);

	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList.Get());

	m_commandList->Close();

	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	commandQueue->ExecuteCommandLists(1, ppCommandLists);
}

inline FrameContext* Menu::WaitForNextFrameResources() {
    UINT nextFrameIndex = g_frameIndex + 1;
    g_frameIndex = nextFrameIndex;

    HANDLE waitableObjects[] = { g_hSwapChainWaitableObject, nullptr };
    DWORD numWaitableObjects = 1;

    FrameContext* frameCtx = &g_frameContext[nextFrameIndex % NUM_FRAMES_IN_FLIGHT];
    UINT64 fenceValue = frameCtx->FenceValue;
    if (fenceValue != 0) // means no fence was signaled
    {
        frameCtx->FenceValue = 0;
        g_fence->SetEventOnCompletion(fenceValue, g_fenceEvent);
        waitableObjects[1] = g_fenceEvent;
        numWaitableObjects = 2;
    }

    WaitForMultipleObjects(numWaitableObjects, waitableObjects, TRUE, INFINITE);

    return frameCtx;
}

inline int Menu::FindFileIndex(const std::vector<std::string>& hdrFiles, const std::string& existingFile) {
    for (int i = 0; i < hdrFiles.size(); ++i)
    {
        if (hdrFiles[i] == existingFile)
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
	static int selectedItemIndex = 0;
    if (ImGui::BeginCombo("Output Type", OutputTypeNames[selectedItemIndex].c_str())) {
		for (int i = 0; i < OutputTypeNames.size(); ++i) {
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

inline void Menu::DrawTonemapTypeDropdown() {
    static int selectedItemIndex = 0;
    if (ImGui::BeginCombo("Tonemap Type", TonemapTypeNames[selectedItemIndex].c_str())) {
        for (int i = 0; i < TonemapTypeNames.size(); ++i) {
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
            spdlog::info("Selected file: {}", ws2s(selectedFile));
			auto scene = LoadModel(ws2s(selectedFile));
			//scene->GetRoot().m_name = getFileNameFromPath(selectedFile);
			//appendScene(*scene);
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
	auto nameComponent = node.get<Components::Name>();
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
			auto opaqueMeshInstances = node.get<Components::OpaqueMeshInstances>();
			auto alphaTestMeshInstances = node.get<Components::AlphaTestMeshInstances>();
			auto blendMeshInstances = node.get<Components::BlendMeshInstances>();
			if (opaqueMeshInstances) {
				ImGui::Text("Opaque Meshes: %d", opaqueMeshInstances->meshInstances.size());
			}
			if (alphaTestMeshInstances) {
                ImGui::Text("Alpha Test Meshes: %d", alphaTestMeshInstances->meshInstances.size());
			}
			if (blendMeshInstances) {
                ImGui::Text("Blend Meshes: %d", blendMeshInstances->meshInstances.size());
			}

            if (node.has<Components::OpaqueSkinned>() || node.has<Components::AlphaTestSkinned>() || node.has<Components::BlendSkinned>()) {
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
        auto position = selectedNode.get<Components::Position>();
        XMStoreFloat3(&pos, position->pos);
        if (ImGui::InputFloat3("Position", &pos.x)) {
			selectedNode.set<Components::Position>(XMLoadFloat3(&pos));
            //selectedNode->transform.isDirty = true;
        }
        ImGui::Text("Scale:");
		XMFLOAT3 scale;
		XMStoreFloat3(&scale, selectedNode.get<Components::Scale>()->scale);
        if (ImGui::InputFloat("Scale", &scale.x)) {
            //selectedNode->transform.isDirty = true;
			scale.y = scale.x;
			scale.z = scale.x;
			selectedNode.set<Components::Scale>(XMLoadFloat3(&scale));
        }

        // Display rotation
        XMFLOAT4 rotation;
        XMStoreFloat4(&rotation, selectedNode.get<Components::Rotation>()->rot);
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
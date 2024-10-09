#pragma once


#include <d3d12.h>
#include <wrl/client.h>
#include <memory>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>
#include <functional>
#include <windows.h>
#include <filesystem>

#include "RenderContext.h"
#include "utilities.h"
#include "OutputTypes.h"
#include "SceneNode.h"
#include "RenderableObject.h"
#include "GlTFLoader.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct FrameContext {
    ID3D12CommandAllocator* CommandAllocator;
    UINT64                  FenceValue;
};

static UINT g_frameIndex = 0;
static HANDLE g_hSwapChainWaitableObject = nullptr;
constexpr unsigned int NUM_FRAMES_IN_FLIGHT = 2;
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

    SceneNode* selectedNode = nullptr;

    FrameContext* WaitForNextFrameResources();

    int FindFileIndex(const std::vector<std::string>& hdrFiles, const std::string& existingFile);

    void DrawEnvironmentsDropdown();
	void DrawOutputTypeDropdown();
    void DrawBrowseButton(const std::wstring& targetDirectory);
    void DrawLoadModelButton();
    void DisplaySceneNode(SceneNode* node, bool isOnlyChild);
    void DisplaySceneGraph();
    void DisplaySelectedNode();

    
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

	std::function < std::unordered_map<UINT, std::shared_ptr<RenderableObject>>&()> getRenderableObjects;
	std::function<SceneNode& ()> getSceneRoot;
	std::function < std::shared_ptr<SceneNode>(Scene& scene)> appendScene;
	std::function<void(std::shared_ptr<void>)> markForDelete;

    std::vector<std::shared_ptr<Scene>> loadedScenes;
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
	getRenderableObjects = settingsManager.getSettingGetter<std::function<std::unordered_map<UINT, std::shared_ptr<RenderableObject>>&()>>("getRenderableObjects")();
	getSceneRoot = settingsManager.getSettingGetter<std::function<SceneNode&()>>("getSceneRoot")();
	appendScene = settingsManager.getSettingGetter<std::function<std::shared_ptr<SceneNode>(Scene& scene)>>("appendScene")();
    markForDelete = settingsManager.getSettingGetter<std::function<void(std::shared_ptr<void>)>>("markForDelete")();
}

inline void Menu::Render(const RenderContext& context) {
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    auto displaySize = io.DisplaySize;
    spdlog::info("Display size: ({}, {})", displaySize.x, displaySize.y);
	io.DisplaySize = ImVec2(context.xRes, context.yRes);
    displaySize = io.DisplaySize;
	spdlog::info("New Display size: ({}, {})", displaySize.x, displaySize.y);
	io.DisplayFramebufferScale = ImVec2(1.2f, 1.2f);

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

    }

	// Rendering
	ImGui::Render();

    FrameContext* frameCtx = WaitForNextFrameResources();

    frameCtx->CommandAllocator->Reset();
    m_commandList->Reset(frameCtx->CommandAllocator, nullptr);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(context.rtvHeap->GetCPUDescriptorHandleForHeapStart(), context.frameIndex, context.rtvDescriptorSize);
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(context.dsvHeap->GetCPUDescriptorHandleForHeapStart());
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
    if (ImGui::BeginCombo("HDR Files", hdrFiles[selectedItemIndex].c_str())) // Current item
    {
        for (int i = 0; i < hdrFiles.size(); ++i)
        {
            bool isSelected = (selectedItemIndex == i);
            if (ImGui::Selectable(hdrFiles[i].c_str(), isSelected))
            {
                selectedItemIndex = i;  // Update the selected index
				environmentName = hdrFiles[i];
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
			auto scene = loadGLB(ws2s(selectedFile));
			scene->GetRoot().m_name = getFileNameFromPath(selectedFile);
			appendScene(*scene);
			loadedScenes.push_back(scene);
			//markForDelete(scene);
        }
        else
        {
            spdlog::warn("No file selected.");
        }
    }
}

inline void Menu::DisplaySceneNode(SceneNode* node, bool isOnlyChild) {
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

    // Show the node with its name
    if (ImGui::TreeNodeEx(node, nodeFlags, "%S", node->m_name.c_str())) {
        // Detect if the node is clicked to select it
        if (ImGui::IsItemClicked()) {
            selectedNode = node;
        }

        // Display information specific to RenderableObject, if the node is of that type.
        auto renderableObject = dynamic_cast<RenderableObject*>(node);
        if (renderableObject) {
            // Display meshes
            ImGui::Text("Opaque Meshes: %d", renderableObject->GetOpaqueMeshes().size());
            ImGui::Text("Transparent Meshes: %d", renderableObject->GetTransparentMeshes().size());

            if (renderableObject->GetSkin()) {
                ImGui::Text("Has Skin: Yes");
            }
            else {
                ImGui::Text("Has Skin: No");
            }
        }

        // Recursively display child nodes
        for (const auto& childPair : node->children) {
            bool childIsOnly = (node->children.size() == 1);
            DisplaySceneNode(childPair.second.get(), childIsOnly);
        }

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
    auto& root = getSceneRoot();
    DisplaySceneNode(&root, true);
}

inline void Menu::DisplaySelectedNode() {
    if (selectedNode) {
        ImGui::Begin("Selected Node Transform", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

        // Display the transform details
        ImGui::Text("Position:");
        if (ImGui::InputFloat3("Position", &selectedNode->transform.pos.x)) {
            selectedNode->transform.isDirty = true; // Mark as dirty if modified
        }
        ImGui::Text("Scale:");
        if (ImGui::InputFloat("Scale", &selectedNode->transform.scale.x)) {
            selectedNode->transform.isDirty = true; // Mark as dirty if modified
			selectedNode->transform.scale.y = selectedNode->transform.scale.x;
			selectedNode->transform.scale.z = selectedNode->transform.scale.x;
        }

        // Display rotation (you may want to convert it to Euler angles for readability)
        XMFLOAT4 rotation;
        XMStoreFloat4(&rotation, selectedNode->transform.rot);
        ImGui::Text("Rotation (quaternion): (%.3f, %.3f, %.3f, %.3f)", rotation.x, rotation.y, rotation.z, rotation.w);

        ImGui::End();
    }
}
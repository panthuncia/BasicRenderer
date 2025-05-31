#include <directx/d3dx12.h> // Included here to avoid conflicts with Windows SDK headers
#include <iostream>
#define NOMINMAX
#include <Windows.h>
#include <windowsx.h>
#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <iostream>
#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <imgui.h>
#include <random>

#include "ThirdParty/pix/pix3.h"
#include "Mesh/Mesh.h"
#include "DX12Renderer.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/PSOManager.h"
#include "Materials/Material.h"
#include "Menu.h"
#include "Materials/MaterialFlags.h"
#include "Render/PSOFlags.h"
#include "Managers/Singletons/DeletionManager.h"
#include "Import/ModelLoader.h"

// Activate dedicated GPU on NVIDIA laptops with both integrated and dedicated GPUs
extern "C" {
    _declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
}

// Set Agility SDK version
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 614;}

extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D\\"; }


#define USE_PIX
#pragma comment(lib, "WinPixEventRuntime.lib")

DX12Renderer renderer;
UINT default_x_res = 1920;
UINT default_y_res = 1080;


void ProcessRawInput(LPARAM lParam) {
    UINT dwSize = 0;

    // Get the size of the raw input data
    GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &dwSize, sizeof(RAWINPUTHEADER));

    // Allocate memory for the raw input data
    LPBYTE lpb = new BYTE[dwSize];
    if (lpb == nullptr) {
        return;
    }

    // Get the raw input data
    if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb, &dwSize, sizeof(RAWINPUTHEADER)) != dwSize) {
        std::cerr << "GetRawInputData does not return correct size!" << std::endl;
    }

    RAWINPUT* raw = (RAWINPUT*)lpb;

    if (raw->header.dwType == RIM_TYPEKEYBOARD) {
        // Process keyboard input
        RAWKEYBOARD& rawKB = raw->data.keyboard;
        std::cout << "Virtual key: " << rawKB.VKey << ", Scan code: " << rawKB.MakeCode << std::endl;

        // Check if the escape key is pressed
        if (rawKB.VKey == VK_ESCAPE) {
            PostQuitMessage(0); // Exit the application
        }

    }
    else if (raw->header.dwType == RIM_TYPEMOUSE) {
        // Process mouse input
        RAWMOUSE& rawMouse = raw->data.mouse;
        std::cout << "Mouse move: (" << rawMouse.lLastX << ", " << rawMouse.lLastY << ")" << std::endl;

        if (rawMouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN) {
            std::cout << "Left mouse button down" << std::endl;
        }
    }

    delete[] lpb;
}

// Window callback procedure
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

void RegisterRawInputDevices(HWND hwnd) {
    RAWINPUTDEVICE rid[2];

    // Register keyboard
    rid[0].usUsagePage = 0x01;
    rid[0].usUsage = 0x06;
    rid[0].dwFlags = RIDEV_INPUTSINK; // Receive input even when not in focus
    rid[0].hwndTarget = hwnd;

    // Register mouse
    rid[1].usUsagePage = 0x01;
    rid[1].usUsage = 0x02;
    rid[1].dwFlags = RIDEV_INPUTSINK; // Receive input even when not in focus
    rid[1].hwndTarget = hwnd;

    if (!RegisterRawInputDevices(rid, 2, sizeof(rid[0]))) {
        MessageBox(nullptr, L"Failed to register raw input devices", L"Error", MB_OK);
        throw std::runtime_error("Failed to register raw input devices.");
    }
}


HWND InitWindow(HINSTANCE hInstance, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"DX12WindowClass";

    WNDCLASS wc = { };

    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    if (!RegisterClass(&wc)) {
        MessageBox(nullptr, L"Failed to register window class", L"Error", MB_OK);
        throw std::runtime_error("Failed to register window class.");
    }

    HWND hwnd = CreateWindowEx(
        0,
        CLASS_NAME,
        L"DirectX 12 Basic Renderer",
        WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT, default_x_res, default_y_res,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (hwnd == nullptr) {
        MessageBox(nullptr, L"Failed to create window", L"Error", MB_OK);
        throw std::runtime_error("Failed to create window.");
    }

    ShowWindow(hwnd, nCmdShow);

    RegisterRawInputDevices(hwnd);

    int screen_width = GetSystemMetrics(SM_CXSCREEN);
    int screen_height = GetSystemMetrics(SM_CYSCREEN);

    // Set window size to screen dimensions
    SetWindowPos(hwnd, nullptr, 0, 0, screen_width, screen_height, SWP_NOZORDER | SWP_NOACTIVATE);

    return hwnd;
}

struct point {
	float x, y, z;
};

point randomPointInSphere(float radius) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    float x, y, z, len2;
    do {
        x = dist(gen);
        y = dist(gen);
        z = dist(gen);
        len2 = x * x + y * y + z * z;
    } while (len2 > 1.0f); // Ensure the point is inside the unit sphere

    // Scale to desired radius
    x *= radius;
    y *= radius;
    z *= radius;

    return {x, y, z};
}

point getRandomPointInVolume(double xmin, double xmax, 
    double ymin, double ymax, 
    double zmin, double zmax)
{
    std::random_device rd;
    std::mt19937 gen(rd());

    std::uniform_real_distribution<> distX(xmin, xmax);
    std::uniform_real_distribution<> distY(ymin, ymax);
    std::uniform_real_distribution<> distZ(zmin, zmax);

    point p;
    p.x = distX(gen);
    p.y = distY(gen);
    p.z = distZ(gen);
    return p;
}

float randomFloat(float min, float max) {
	static std::random_device rd;
	static std::mt19937 gen(rd());
	std::uniform_real_distribution<float> dist(min, max);
	return dist(gen);
}

int main() {
    float radius = 5.0f;
    auto [x, y, z] = randomPointInSphere(radius);
    std::cout << "Random point in sphere: (" << x << ", " << y << ", " << z << ")\n";
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    auto file_logger = spdlog::basic_logger_mt("file_logger", "logs/log.txt");
    spdlog::set_default_logger(file_logger);
    file_logger->flush_on(spdlog::level::info);

    HINSTANCE hGetPixDLL = LoadLibrary(L"WinPixEventRuntime.dll");

    if (!hGetPixDLL) {
        spdlog::warn("could not load the PIX library");
    }

    // Aftermath

#if BUILD_TYPE == BUILD_TYPE_DEBUG
    HMODULE pixLoaded = PIXLoadLatestWinPixGpuCapturerLibrary();
    if (!pixLoaded) {
        // Print the error code for debugging purposes
        spdlog::warn("Could not load PIX! Error: ", GetLastError());
    }
#endif

    SetDllDirectoryA(".\\D3D\\");

    HWND hwnd = InitWindow(hInstance, nShowCmd);

    spdlog::info("initializing renderer...");
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    UINT x_res = clientRect.right - clientRect.left;
    UINT y_res = clientRect.bottom - clientRect.top;
    renderer.Initialize(hwnd, x_res, y_res);
    spdlog::info("Renderer initialized.");
    renderer.SetInputMode(InputMode::wasd);

    //std::vector<std::byte> vertices = {
    //{{-1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
    //{{1.0f,  -1.0f, -1.0f}, {1.0f,  -1.0f, -1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
    //{{ 1.0f,  1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
    //{{ -1.0f, 1.0f, -1.0f}, { 1.0f,  1.0f, -1.0f}, {1.0f, 1.0f, 0.0f, 1.0f}},
    //{{-1.0f, -1.0f,  1.0f}, {-1.0f, -1.0f,  1.0f}, {0.0f, 1.0f, 1.0f, 1.0f}},
    //{{1.0f,  -1.0f,  1.0f}, {1.0f,  -1.0f,  1.0f}, {1.0f, 0.0f, 1.0f, 1.0f}},
    //{{ 1.0f,  1.0f,  1.0f}, { 1.0f,  1.0f,  1.0f}, {0.5f, 0.5f, 0.5f, 1.0f}},
    //{{ -1.0f, 1.0f,  1.0f}, { -1.0f, 1.0f,  1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    //};

    std::vector<UINT32> indices = {
        3, 1, 0, 2, 1, 3,
        2, 5, 1, 6, 5, 2,
        6, 4, 5, 7, 4, 6,
        7, 0, 4, 3, 0, 7,
        7, 2, 3, 6, 2, 7,
        0, 5, 4, 1, 5, 0
    };

    auto baseScene = std::make_shared<Scene>();
    //auto dragonScene1 = loadGLB("models/dragon.glb");

    auto dragonScene = LoadModel("models/dragon.glb");
    dragonScene->GetRoot().set<Components::Scale>({5, 5, 5});
    dragonScene->GetRoot().set<Components::Position>({ 0.0, 1, 0.0 });
	//dragonScene->GetRoot().m_name = L"dragonRoot";

    auto tigerScene = LoadModel("models/tiger.glb");
    tigerScene->GetRoot().set<Components::Scale>({ 0.01, 0.01, 0.01 });
	//tigerScene->GetRoot().transform.setLocalPosition({ 0.0, 0.0, 0.0 });
	//tigerScene->GetRoot().m_name = L"tigerRoot";

    auto phoenixScene = LoadModel("models/phoenix.glb");
    phoenixScene->GetRoot().set<Components::Scale>({ 0.01, 0.01, 0.01 });
    phoenixScene->GetRoot().set<Components::Position>({ -1.0, 0.0, 0.0 });

    auto carScene = LoadModel("models/porche.glb");
    carScene->GetRoot().set<Components::Scale>({ 0.6, 0.6, 0.6 });
    carScene->GetRoot().set<Components::Position>({ 1.0, 0.0, 1.0 });
	//carScene->GetRoot().m_name = L"carRoot";

    auto mountainScene = LoadModel("models/terrain.glb");
	mountainScene->GetRoot().set<Components::Scale>({ 50.0, 50.0, 50.0 });
	mountainScene->GetRoot().set<Components::Position>({ 0.0, -2.0, 0.0 });
	//mountainScene->GetRoot().m_name = L"mountainRoot";

    //auto bistro = LoadModel("models/BistroExterior.fbx");
    //auto bistro = LoadModel("models/bistro.glb");
    //bistro->GetRoot().set<Components::Scale>({ 0.01, 0.01, 0.01 });

	//auto sponza = LoadModel("models/sponza.glb");
    //auto street = LoadModel("models/street.obj");

    auto cubeScene = LoadModel("models/sphere.glb");
    cubeScene->GetRoot().set<Components::Position>({0, 5, 3});
    cubeScene->GetRoot().set<Components::Rotation>(QuaternionFromAxisAngle({1, 1, 1}));
	//cubeScene->GetRoot().set<Components::Scale>({ 0.1, 0.1, 0.1 });
    //cubeScene->DisableShadows();
    //cubeScene->GetRoot().transform.setLocalRotationFromEuler({45.0, 45.0, 45.0});
    //auto heightMap = loadTextureFromFileSTBI("textures/height.jpg");
    //for (auto& pair : cubeScene->GetOpaqueRenderableObjectIDMap()) {
    //    auto& renderable = pair.second;
    //    for (auto& mesh : renderable->GetOpaqueMeshes()) {
    //        mesh->material->SetHeightmap(heightMap);
    //        mesh->material->SetHeightmapScale(0.1);
    //        mesh->material->SetTextureScale(2.0);
    //    }
    //}
    //for (auto& pair : cubeScene->GetAlphaTestRenderableObjectIDMap()) {
    //    auto& renderable = pair.second;
    //    for (auto& mesh : renderable->GetAlphaTestMeshes()) {
    //        mesh->material->SetHeightmap(heightMap);
    //        mesh->material->SetHeightmapScale(0.1);
    //        mesh->material->SetTextureScale(2.0);
    //    }
    //}

	//auto sanMiguel = LoadModel("models/SanMiguel.fbx");
	//sanMiguel->GetRoot().set<Components::Scale>({ 0.01, 0.01, 0.01 });

    renderer.SetCurrentScene(baseScene);

    //renderer.GetCurrentScene()->AppendScene(sanMiguel->Clone());

    //mountainScene->AppendScene(dragonScene->Clone());
    //renderer.GetCurrentScene()->AppendScene(dragonScene->Clone());
    renderer.GetCurrentScene()->AppendScene(cubeScene->Clone());
    //renderer.GetCurrentScene()->AppendScene(cubeScene->Clone());

    renderer.GetCurrentScene()->AppendScene(mountainScene->Clone());
    //renderer.GetCurrentScene()->AppendScene(tigerScene->Clone());

    //auto root = renderer.GetCurrentScene()->AppendScene(dragonScene->Clone());
	//root.set<Components::Position>({ 0.0, 3.0, 0.0 });
    
	for (int i = 0; i < 0000; i++) {
		float animationSpeed = randomFloat(0.5, 2.0);
   //     for (auto& object : tigerScene->GetOpaqueRenderableObjectIDMap()) {
			//object.second->SetAnimationSpeed(animationSpeed);
   //     }
	    renderer.GetCurrentScene()->AppendScene(cubeScene->Clone());
		auto point = randomPointInSphere(8.0);
        cubeScene->GetRoot().set<Components::Position>({ point.x, point.y, point.z});
	}

    //renderer.GetCurrentScene()->AppendScene(phoenixScene->Clone());
    //auto root = renderer.GetCurrentScene()->AppendScene(carScene->Clone());
    //renderer.GetCurrentScene()->RemoveEntityByID(root->GetLocalID(), true);
    //renderer.GetCurrentScene()->AppendScene(*cubeScene);
	//renderer.GetCurrentScene()->AppendScene(bistro);
	//renderer.GetCurrentScene()->AppendScene(*sponza);

    //renderer.GetCurrentScene()->AppendScene(*street);

	//DeletionManager::GetInstance().MarkForDelete(carScene);
    //DeletionManager::GetInstance().MarkForDelete(dragonScene);
    //DeletionManager::GetInstance().MarkForDelete(tigerScene);
    //DeletionManager::GetInstance().MarkForDelete(mountainScene);
	//DeletionManager::GetInstance().MarkForDelete(phoenixScene);
	//DeletionManager::GetInstance().MarkForDelete(bistro);
	//DeletionManager::GetInstance().MarkForDelete(sponza);

	//carScene.reset();
	//dragonScene.reset();
	//tigerScene.reset();
	//mountainScene.reset();
	//phoenixScene.reset();
	//bistro.reset();
	//sponza.reset();

    renderer.SetEnvironment("sky");

    XMFLOAT3 lookAt = XMFLOAT3(0.0f, 0.0f, 0.0f);
    XMFLOAT3 up = XMFLOAT3(0.0f, 1.0f, 0.0f);
    float fov = 80.0f * (XM_PI / 180.0f); // Converting degrees to radians
    float aspectRatio;
    float zNear = 0.1f;
    float zFar = 100.0f;


    int clientWidth = x_res; // TODO
    int clientHeight = y_res; // TODO

    aspectRatio = static_cast<float>(clientWidth) / static_cast<float>(clientHeight);
    auto& scene = renderer.GetCurrentScene();
    scene->SetCamera(lookAt, up, fov, aspectRatio, zNear, zFar);

    //auto cubeMaterial = std::make_shared<Material>("cubeMaterial", MaterialFlags::MATERIAL_FLAGS_NONE, PSOFlags::PSO_FLAGS_NONE);
    //auto cubeMesh = Mesh::CreateShared(vertices, indices, cubeMaterial, VertexFlags::VERTEX_COLORS);
    //std::vector<std::shared_ptr<Mesh>> vec = { cubeMesh };
    //std::shared_ptr<RenderableObject> cubeObject = std::make_shared<RenderableObject>(L"CubeObject", vec);
    //auto cubeScaleNode = std::make_shared<SceneNode>();
    //cubeScaleNode->transform.setLocalScale({ 0.1, 0.1, 0.1 });
    //cubeScaleNode->AddChild(cubeObject);
    //scene->AddNode(cubeScaleNode);
    //scene->AddObject(cubeObject);

    auto animation = std::make_shared<AnimationClip>();
    animation->addPositionKeyframe(0, { -2, 2, -2 });
    animation->addPositionKeyframe(2, { 2, 2, -2 });
    animation->addPositionKeyframe(4, { 2, 2, 2 });
    animation->addPositionKeyframe(6, { -2, 2, 2 });
    animation->addPositionKeyframe(8, { -2, 2, -2 });
    //animation->addRotationKeyframe(0, DirectX::XMQuaternionRotationRollPitchYaw(0, 0, 0));
    //animation->addRotationKeyframe(1, DirectX::XMQuaternionRotationRollPitchYaw(0, DirectX::XM_PIDIV2, DirectX::XM_PIDIV2)); // 90 degrees
    //animation->addRotationKeyframe(2, DirectX::XMQuaternionRotationRollPitchYaw(0, DirectX::XM_PI, DirectX::XM_PI)); // 180 degrees
    //animation->addRotationKeyframe(4, DirectX::XMQuaternionRotationRollPitchYaw(0, DirectX::XM_2PI, DirectX::XM_2PI)); // 360 degrees
    
	auto light = renderer.GetCurrentScene()->CreateDirectionalLightECS(L"light1", XMFLOAT3(1, 1, 1), 10.0, XMFLOAT3(0, -1, -1));
    auto light3 = renderer.GetCurrentScene()->CreateSpotLightECS(L"light3", XMFLOAT3(0, 10, 3), XMFLOAT3(1, 1, 1), 2000.0, {0, -1, 0}, .5, .8, 0.0, 0.0, 1.0);
    //auto light1 = renderer.GetCurrentScene()->CreatePointLightECS(L"light1", XMFLOAT3(0, 1, 3), XMFLOAT3(1, 1, 1), 1.0, 0.0, 0.0, 1.0);
    
    for (int i = 0; i < 0; i++) {
		auto point = getRandomPointInVolume(-20, 20, -2, 0, -20, 20);
		auto color = XMFLOAT3(randomFloat(0.0, 1.0), randomFloat(0.0, 1.0), randomFloat(0.0, 1.0));
        auto light1 = renderer.GetCurrentScene()->CreatePointLightECS(L"light"+std::to_wstring(i), XMFLOAT3(point.x, point.y, point.z), color, 3.0, 0.0, 0.0, 1.0, false);
    }

    //renderer.SetDebugTexture(renderer.GetCurrentScene()->GetPrimaryCamera().get<Components::DepthMap>()->linearDepthMap);

    MSG msg = {};
    unsigned int frameIndex = 0;
    auto lastUpdateTime = std::chrono::system_clock::now();
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {

            auto currentTime = std::chrono::system_clock::now();
            std::chrono::duration<double> elapsedSeconds = currentTime - lastUpdateTime;
            lastUpdateTime = currentTime;

            frameIndex += 1;
            renderer.Update(elapsedSeconds.count());
            if (frameIndex % 100 == 0) {
                spdlog::info("FPS: {}", 1 / elapsedSeconds.count());
            }
            renderer.Render();
        }
    }

    renderer.Cleanup();

    return 0;
}

// Window callback procedure
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {

    if (toupper(wParam) == VK_ESCAPE) {
        message = WM_DESTROY;
    }

	if (Menu::GetInstance().HandleInput(hWnd, message, wParam, lParam)) {
		return true;
    }

    ImGuiIO& io = ImGui::GetIO();

    // Check if the mouse is hovering any ImGui window
    bool isMouseOverAnyWindow = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || ImGui::IsAnyItemHovered();

    // Otherwise, we rely on ImGui's own input capture logic
    bool isMouseCaptured = io.WantCaptureMouse;
    bool isKeyboardCaptured = io.WantCaptureKeyboard;

    // If neither the mouse nor the keyboard is captured by ImGui, pass input to the renderer
	// Also allow the renderer to process key up events to prevent the camera from getting stuck moving.
    if ((!isMouseCaptured && !isKeyboardCaptured) || message == WM_KEYUP || !isMouseOverAnyWindow) {
        renderer.GetInputManager().ProcessInput(message, wParam, lParam);
    }

    switch (message)
    {
    case WM_INPUT:
        //ProcessRawInput(lParam);
        break;
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            UINT newWidth = LOWORD(lParam);
            UINT newHeight = HIWORD(lParam);
            renderer.OnResize(newWidth, newHeight);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

#include <iostream>
#include <Windows.h>
#include <windowsx.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <iostream>
#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include "Mesh.h"
#include "DX12Renderer.h"
#include "Utilities.h"
#include "RenderableObject.h"
#include "GlTFLoader.h"
#include "PSOManager.h"
#include "Light.h"

DX12Renderer renderer;
UINT x_res = 1920;
UINT y_res = 1080;

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
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, x_res, y_res,
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

    return hwnd;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    HWND hwnd = InitWindow(hInstance, nShowCmd);

    auto file_logger = spdlog::basic_logger_mt("file_logger", "logs/log.txt");
    spdlog::set_default_logger(file_logger);
    file_logger->flush_on(spdlog::level::info);

    spdlog::info("initializing renderer...");
    renderer.Initialize(hwnd, x_res, y_res);
    spdlog::info("Renderer initialized.");
    renderer.SetInputMode(InputMode::wasd);

    std::vector<Vertex> vertices = {
    VertexColored{{-1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
    VertexColored{{1.0f,  -1.0f, -1.0f}, {1.0f,  -1.0f, -1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
    VertexColored{{ 1.0f,  1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
    VertexColored{{ -1.0f, 1.0f, -1.0f}, { 1.0f,  1.0f, -1.0f}, {1.0f, 1.0f, 0.0f, 1.0f}},
    VertexColored{{-1.0f, -1.0f,  1.0f}, {-1.0f, -1.0f,  1.0f}, {0.0f, 1.0f, 1.0f, 1.0f}},
    VertexColored{{1.0f,  -1.0f,  1.0f}, {1.0f,  -1.0f,  1.0f}, {1.0f, 0.0f, 1.0f, 1.0f}},
    VertexColored{{ 1.0f,  1.0f,  1.0f}, { 1.0f,  1.0f,  1.0f}, {0.5f, 0.5f, 0.5f, 1.0f}},
    VertexColored{{ -1.0f, 1.0f,  1.0f}, { -1.0f, 1.0f,  1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    };

    std::vector<UINT32> indices = {
        3, 1, 0, 2, 1, 3,
        2, 5, 1, 6, 5, 2,
        6, 4, 5, 7, 4, 6,
        7, 0, 4, 3, 0, 7,
        7, 2, 3, 6, 2, 7,
        0, 5, 4, 1, 5, 0
    };

    auto baseScene = std::make_shared<Scene>();

    auto dragonScene = loadGLB("models/dragon.glb");
    dragonScene->GetRoot().transform.setLocalScale({5, 5, 5});
    dragonScene->GetRoot().transform.setLocalPosition({ 1.0, 0.0, 0.0 });

    auto tigerScene = loadGLB("models/tiger.glb");
    tigerScene->GetRoot().transform.setLocalScale({ 0.1, 0.1, 0.1 });

    auto phoenixScene = loadGLB("models/phoenix.glb");
    phoenixScene->GetRoot().transform.setLocalScale({ 0.05, 0.05, 0.05 });
    phoenixScene->GetRoot().transform.setLocalPosition({ -1.0, 0.0, 0.0 });

    auto carScene = loadGLB("models/datsun.glb");
    carScene->GetRoot().transform.setLocalScale({ 0.6, 0.6, 0.6 });
    carScene->GetRoot().transform.setLocalPosition({ 1.0, 0.0, 1.0 });

	auto mountainScene = loadGLB("models/mountain.glb");
	mountainScene->GetRoot().transform.setLocalScale({ 0.0003, 0.0003, 0.0003 });
	mountainScene->GetRoot().transform.setLocalPosition({ 0.0, -2.0, 0.0 });

    //auto cubeScene = loadGLB("models/cube.glb");
    //cubeScene->GetRoot().transform.setLocalScale({ 0.5, 0.5, 0.5 });
    //auto heightMap = loadTextureFromFile("textures/height.jpg");
    //for (auto& pair : cubeScene->GetOpaqueRenderableObjectIDMap()) {
    //    auto& renderable = pair.second;
    //    for (auto& mesh : renderable->GetOpaqueMeshes()) {
    //        mesh.material->SetHeightmap(heightMap);
    //        mesh.material->SetHeightmapScale(0.1);
    //        mesh.material->SetTextureScale(2.0);
    //    }
    //}
    //for (auto& pair : cubeScene->GetTransparentRenderableObjectIDMap()) {
    //    auto& renderable = pair.second;
    //    for (auto& mesh : renderable->GetTransparentMeshes()) {
    //        mesh.material->SetHeightmap(heightMap);
    //        mesh.material->SetHeightmapScale(0.1);
    //        mesh.material->SetTextureScale(2.0);
    //    }
    //}


    renderer.SetCurrentScene(baseScene);
    renderer.GetCurrentScene()->AppendScene(*dragonScene);
    renderer.GetCurrentScene()->AppendScene(*tigerScene);
    //renderer.GetCurrentScene()->AppendScene(*phoenixScene);
    //renderer.GetCurrentScene()->AppendScene(*carScene);
	renderer.GetCurrentScene()->AppendScene(*mountainScene);
    //renderer.GetCurrentScene()->AppendScene(*cubeScene);



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

    auto cubeMaterial = std::make_shared<Material>("cubeMaterial", PSOFlags::VERTEX_COLORS);
    auto cubeMesh = Mesh(vertices, indices, cubeMaterial, false);
    std::vector<Mesh> vec = { cubeMesh };
    std::shared_ptr<RenderableObject> cubeObject = std::make_shared<RenderableObject>("CubeObject", vec);
    auto cubeScaleNode = std::make_shared<SceneNode>();
    cubeScaleNode->transform.setLocalScale({ 0.1, 0.1, 0.1 });
    //cubeScaleNode->AddChild(cubeObject);
    scene->AddNode(cubeScaleNode);
    //scene->AddObject(cubeObject);

    auto animation = std::make_shared<AnimationClip>();
    animation->addPositionKeyframe(0, { -10, 10, -10 });
    animation->addPositionKeyframe(2, { 10, 10, -10 });
    animation->addPositionKeyframe(4, { 10, 10, 10 });
    animation->addPositionKeyframe(6, { -10, 10, 10 });
    animation->addPositionKeyframe(8, { -10, 10, -10 });
    //animation->addRotationKeyframe(0, DirectX::XMQuaternionRotationRollPitchYaw(0, 0, 0));
    //animation->addRotationKeyframe(1, DirectX::XMQuaternionRotationRollPitchYaw(0, DirectX::XM_PIDIV2, DirectX::XM_PIDIV2)); // 90 degrees
    //animation->addRotationKeyframe(2, DirectX::XMQuaternionRotationRollPitchYaw(0, DirectX::XM_PI, DirectX::XM_PI)); // 180 degrees
    //animation->addRotationKeyframe(4, DirectX::XMQuaternionRotationRollPitchYaw(0, DirectX::XM_2PI, DirectX::XM_2PI)); // 360 degrees
    
	auto light1 = Light::CreatePointLight("light1", XMFLOAT3(0, 0, 0), XMFLOAT3(1, 1, 1), 100.0, 1.0, 0.09, 0.032);
    light1->animationController->setAnimationClip(animation);
    cubeScaleNode->AddChild(light1);
    scene->AddLight(light1, true);
	auto light2 = Light::CreateDirectionalLight("light2", XMFLOAT3(1, 1, 1), 20.0, XMFLOAT3(1, -1, 1));
    scene->AddLight(light2, true);
    auto light3 = Light::CreateSpotLight("light3", XMFLOAT3(0, 4, 0), XMFLOAT3(1, 1, 1), 100.0, {0, -1, 0}, .5, .8, 1.0, 0.09, 0.032);
	scene->AddLight(light3, true);
    //light3->AddChild(cubeScaleNode);

	renderer.SetDebugTexture(light3->getShadowMap().get());

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
            light1->animationController->update(elapsedSeconds.count());

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
    renderer.GetInputManager().ProcessInput(message, wParam, lParam);

    switch (message)
    {
    case WM_INPUT:
        ProcessRawInput(lParam);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

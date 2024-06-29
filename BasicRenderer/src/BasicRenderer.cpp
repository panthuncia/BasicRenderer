#include <iostream>
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <iostream>
#include <memory>
#include "Mesh.h"
#include "DX12Renderer.h"
#include "Utilities.h"
#include "RenderableObject.h"


// Window callback procedure

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

HWND InitWindow(HINSTANCE hInstance, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"DX12WindowClass";

    WNDCLASSW wc = {}; // Use WNDCLASSW for Unicode
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClassW(&wc); // Use RegisterClassW for Unicode

    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"DirectX 12 Basic Renderer", // Use wide string for window title
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (hwnd == nullptr) {
        throw std::runtime_error("Failed to create window.");
    }

    ShowWindow(hwnd, nCmdShow);

    return hwnd;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    HWND hwnd = InitWindow(hInstance, nShowCmd);

    DX12Renderer renderer;

    print("initializing renderer...");
    renderer.Initialize(hwnd);
    print("Renderer initialized.");

    std::vector<Vertex> vertices = {
    {{-1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
    {{1.0f,  -1.0f, -1.0f}, {1.0f,  -1.0f, -1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
    {{ 1.0f,  1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
    {{ -1.0f, 1.0f, -1.0f}, { 1.0f,  1.0f, -1.0f}, {1.0f, 1.0f, 0.0f, 1.0f}},
    {{-1.0f, -1.0f,  1.0f}, {-1.0f, -1.0f,  1.0f}, {0.0f, 1.0f, 1.0f, 1.0f}},
    {{1.0f,  -1.0f,  1.0f}, {1.0f,  -1.0f,  1.0f}, {1.0f, 0.0f, 1.0f, 1.0f}},
    {{ 1.0f,  1.0f,  1.0f}, { 1.0f,  1.0f,  1.0f}, {0.5f, 0.5f, 0.5f, 1.0f}},
    {{ -1.0f, 1.0f,  1.0f}, { -1.0f, 1.0f,  1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    };

    std::vector<UINT16> indices = {
        0, 1, 3, 3, 1, 2,
        1, 5, 2, 2, 5, 6,
        5, 4, 6, 6, 4, 7,
        4, 0, 7, 7, 0, 3,
        3, 2, 7, 7, 2, 6,
        4, 5, 0, 0, 5, 1
    };

    auto cubeMesh = Mesh(renderer.GetDevice().Get(), vertices, indices);
    std::vector<Mesh> vec = { cubeMesh };
    std::shared_ptr<RenderableObject> cubeObject = std::make_shared<RenderableObject>("CubeObject", vec);
    renderer.GetCurrentScene().addObject(cubeObject);

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            print("Updating scene...");
            renderer.Update();
            print("Scene updated.");
            print("Rendering frame...");
            renderer.Render();
            print("Frame complete.");
        }
    }

    renderer.Cleanup();

    return 0;
}
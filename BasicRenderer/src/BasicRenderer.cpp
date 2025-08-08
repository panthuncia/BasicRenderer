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
#include <io.h>        // _pipe, _dup2, _read, _close
#include <fcntl.h>     // _O_BINARY
#include <thread>

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
#include "spdlogStreambuf.h"

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

    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(hMon, &mi);

    // mi.rcMonitor is the *entire* display area, including taskbar‐covered parts
    int monX = mi.rcMonitor.left;
    int monY = mi.rcMonitor.top;
    int monWidth = mi.rcMonitor.right - mi.rcMonitor.left;
    int monHeight = mi.rcMonitor.bottom - mi.rcMonitor.top;

    SetWindowPos(
        hwnd,
        HWND_TOP,           // or HWND_TOPMOST if you want to stay above every other window
        monX, monY,        // top-left corner of the monitor
        monWidth, monHeight,  // exactly fill it
        SWP_NOACTIVATE      // don’t steal focus, or add SWP_SHOWWINDOW if needed
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

void redirectStdoutToSpdlog(std::shared_ptr<spdlog::logger> logger) {
    int fds[2];
    // create a binary pipe with a 4 KB buffer
    if (_pipe(fds, 4096, _O_BINARY) != 0) {
        logger->error("Failed to create pipe for stdout redirection");
        return;
    }

    // flush C library buffers so nothing's lost
    fflush(stdout);

    // dup the pipe's write end onto STDOUT_FILENO
    _dup2(fds[1], _fileno(stdout));

    // we no longer need the original write handle
    _close(fds[1]);

    // spawn a thread to read from the pipe's read end
    std::thread([logger, readFd = fds[0]]() {
        std::vector<char> buf(1024);
        while (true) {
            int n = _read(readFd, buf.data(), (int)buf.size());
            if (n <= 0) break;

            logger->info(std::string(buf.data(), n));
        }
        _close(readFd);
        }).detach();
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto file_logger = spdlog::basic_logger_mt("file_logger", "logs/log.txt");
    spdlog::set_default_logger(file_logger);
    file_logger->flush_on(spdlog::level::info);

    static spdlog_streambuf sci{ file_logger };
    std::cout.rdbuf(&sci);
    std::cerr.rdbuf(&sci);
    redirectStdoutToSpdlog(file_logger);

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

    // Initialize Nvidia Streamline

    renderer.Initialize(hwnd, x_res, y_res);
    spdlog::info("Renderer initialized.");
    renderer.SetInputMode(InputMode::wasd);

    auto baseScene = std::make_shared<Scene>();

    auto usdScene = LoadModel("models/sponza.usdz");

    renderer.SetCurrentScene(baseScene);

    renderer.GetCurrentScene()->AppendScene(usdScene->Clone());

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
    
	auto light = renderer.GetCurrentScene()->CreateDirectionalLightECS(L"light1", XMFLOAT3(1, 1, 1), 10.0, XMFLOAT3(0, -1, -1));
    //auto light3 = renderer.GetCurrentScene()->CreateSpotLightECS(L"light3", XMFLOAT3(0, 10, 3), XMFLOAT3(1, 1, 1), 2000.0, {0, -1, 0}, .5, .8, 0.0, 0.0, 1.0);
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

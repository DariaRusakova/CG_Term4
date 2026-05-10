#include "D3D12App.h"
#include <windows.h>

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static D3D12App* app = nullptr;

    switch (msg) {
    case WM_CREATE:
        app = (D3D12App*)((CREATESTRUCT*)lParam)->lpCreateParams;
        return 0;

    case WM_MOUSEWHEEL:
        if (app) app->OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;

    case WM_LBUTTONDOWN:
        if (app) app->OnMouseDown(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_LBUTTONUP:
        if (app) app->OnMouseUp();
        return 0;

    case WM_MOUSEMOVE:
        if (app) app->OnMouseMove(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_KEYDOWN:
        if (app) {
            if (wParam == VK_ESCAPE) PostQuitMessage(0);
            else if (wParam == 'R') app->ResetCamera();
            else app->OnKeyDown(wParam);
        }
        return 0;

    case WM_DESTROY:
        OutputDebugStringA("WM_DESTROY received\n");
        if (app) {
            app->Shutdown();
        }
        PostQuitMessage(0);
        return 0;

    case WM_CLOSE:
        OutputDebugStringA("WM_CLOSE received\n");
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    OutputDebugStringA("WinMain started\n");

    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "D3D12App";
    RegisterClassA(&wc);

    D3D12App app;

    HWND hwnd = CreateWindowExA(0, "D3D12App", "D3D12 Sponza Renderer",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1024, 768,
        nullptr, nullptr, hInstance, &app);

    if (!hwnd) {
        OutputDebugStringA("Failed to create window\n");
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);

    if (!app.Initialize(hwnd)) {
        OutputDebugStringA("Failed to initialize app\n");
        return 1;
    }

    MSG msg = {};
    while (true) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                OutputDebugStringA("WM_QUIT received, exiting\n");
                return 0;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        try {
            app.RenderFrame();
        }
        catch (...) {
            OutputDebugStringA("Exception in main loop\n");
            break;
        }
    }

    return 0;
}
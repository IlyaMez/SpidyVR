// Spidy Launcher: finds the game, the VR runtime and what Spidy needs on this
// PC, and starts VR sessions (tools/run_game_vr.py) with one button.
#include "ui.hpp"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include <d3d11.h>
#include <dwmapi.h>
#include <filesystem>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {
ID3D11Device* device{};
ID3D11DeviceContext* context{};
IDXGISwapChain* swapChain{};
ID3D11RenderTargetView* target{};
UINT resizeWidth{}, resizeHeight{};
float dpiScale = 1;
bool dpiChanged{};
launcher::App* app{};
constexpr wchar_t kClass[] = L"SpidyLauncher";

bool createDevice(HWND window) {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL level{};
    HRESULT result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                                   D3D11_SDK_VERSION, &desc, &swapChain, &device, &level, &context);
    if (FAILED(result)) // e.g. a remote desktop session
        result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                               &desc, &swapChain, &device, &level, &context);
    return SUCCEEDED(result);
}

void createTarget() {
    ID3D11Texture2D* buffer{};
    if (SUCCEEDED(swapChain->GetBuffer(0, IID_PPV_ARGS(&buffer)))) {
        device->CreateRenderTargetView(buffer, nullptr, &target);
        buffer->Release();
    }
}

void releaseTarget() {
    if (target) {
        target->Release();
        target = nullptr;
    }
}

void releaseDevice() {
    releaseTarget();
    if (swapChain) swapChain->Release();
    if (context) context->Release();
    if (device) device->Release();
    swapChain = nullptr;
    context = nullptr;
    device = nullptr;
}

void applyStyle(float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    launcher::theme(style);
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
}

launcher::Fonts loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    wchar_t windows[MAX_PATH];
    GetWindowsDirectoryW(windows, MAX_PATH);
    const std::filesystem::path folder = std::filesystem::path(windows) / L"Fonts";
    const auto file = [&](const wchar_t* name) {
        const auto path = folder / name;
        std::error_code error;
        return std::filesystem::is_regular_file(path, error) ? launcher::narrow(path.wstring()) : std::string();
    };
    std::string icons = file(L"SegoeIcons.ttf"); // Windows 11
    if (icons.empty())
        icons = file(L"segmdl2.ttf"); // Windows 10
    launcher::Fonts fonts;
    fonts.icons = !icons.empty();
    const auto load = [&](const wchar_t* name, float size, bool withIcons) -> ImFont* {
        const std::string path = file(name);
        if (path.empty())
            return nullptr;
        ImFont* font = io.Fonts->AddFontFromFileTTF(path.c_str(), size);
        if (font && withIcons && fonts.icons) {
            ImFontConfig merge;
            merge.MergeMode = true;
            merge.GlyphOffset = ImVec2(0, 2);
            io.Fonts->AddFontFromFileTTF(icons.c_str(), size, &merge);
        }
        return font;
    };
    fonts.body = load(L"segoeui.ttf", 15, true);
    if (!fonts.body) {
        fonts.body = io.Fonts->AddFontDefault();
        fonts.icons = false;
    }
    fonts.semibold = load(L"seguisb.ttf", 15, true);
    fonts.bold = load(L"segoeuib.ttf", 15, true);
    fonts.title = load(L"seguibl.ttf", 34, false);
    fonts.mono = load(L"consola.ttf", 13, false);
    if (!fonts.semibold) fonts.semibold = fonts.body;
    if (!fonts.bold) fonts.bold = fonts.semibold;
    if (!fonts.title) fonts.title = fonts.bold;
    if (!fonts.mono) fonts.mono = fonts.body;
    return fonts;
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(window, message, w, l))
        return TRUE;
    switch (message) {
    case WM_SIZE:
        if (w != SIZE_MINIMIZED) {
            resizeWidth = LOWORD(l);
            resizeHeight = HIWORD(l);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(l);
        info->ptMinTrackSize = {static_cast<LONG>(980 * dpiScale), static_cast<LONG>(700 * dpiScale)};
        return 0;
    }
    case WM_DPICHANGED: {
        dpiScale = HIWORD(w) / 96.f;
        dpiChanged = true;
        const auto* suggested = reinterpret_cast<RECT*>(l);
        SetWindowPos(window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_CLOSE:
        if (app && !app->allowClose())
            return 0;
        DestroyWindow(window);
        return 0;
    case WM_SYSCOMMAND:
        if ((w & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    // One launcher at a time: a second start brings the first one forward.
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\SpidyLauncher");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(kClass, nullptr)) {
            ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ImGui_ImplWin32_EnableDpiAwareness();
    const HMONITOR monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    dpiScale = ImGui_ImplWin32_GetDpiScaleForMonitor(monitor);
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(monitor, &info);
    const int areaWidth = info.rcWork.right - info.rcWork.left, areaHeight = info.rcWork.bottom - info.rcWork.top;
    const int width = std::min(static_cast<int>(1120 * dpiScale), areaWidth * 94 / 100);
    const int height = std::min(static_cast<int>(860 * dpiScale), areaHeight * 94 / 100);

    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.style = CS_CLASSDC;
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = CreateSolidBrush(RGB(13, 15, 20));
    windowClass.lpszClassName = kClass;
    RegisterClassExW(&windowClass);
    HWND window = CreateWindowExW(0, kClass, L"Spidy VR", WS_OVERLAPPEDWINDOW,
                                  info.rcWork.left + (areaWidth - width) / 2, info.rcWork.top + (areaHeight - height) / 2,
                                  width, height, nullptr, nullptr, instance, nullptr);
    // A dark title bar in the window's own colour (Windows 11 takes the colour; 10 only the dark mode).
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
    const COLORREF caption = RGB(13, 15, 20);
    DwmSetWindowAttribute(window, 35, &caption, sizeof(caption));
    if (!createDevice(window)) {
        releaseDevice();
        MessageBoxW(nullptr, L"Spidy's launcher needs DirectX 11 graphics, which this PC did not provide.", L"Spidy VR",
                    MB_ICONERROR);
        return 1;
    }
    createTarget();
    ShowWindow(window, show);
    UpdateWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    const launcher::Fonts fonts = loadFonts();
    applyStyle(dpiScale);
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(device, context);

    {
        launcher::App application(window, fonts);
        app = &application;
        bool done = false, occluded = false;
        while (!done) {
            // In the background (VR running, the game in front) a few frames a second are plenty.
            const bool foreground = GetForegroundWindow() == window;
            if (!foreground || IsIconic(window) || occluded)
                MsgWaitForMultipleObjects(0, nullptr, FALSE, application.busy() ? 100 : 400, QS_ALLINPUT);
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
                if (message.message == WM_QUIT)
                    done = true;
            }
            if (done)
                break;
            application.poll();
            if (application.shouldQuit()) {
                app = nullptr;
                DestroyWindow(window);
                continue;
            }
            if (IsIconic(window))
                continue;
            if (occluded && swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
                continue;
            occluded = false;
            if (dpiChanged) {
                applyStyle(dpiScale);
                dpiChanged = false;
            }
            if (resizeWidth && resizeHeight) {
                releaseTarget();
                swapChain->ResizeBuffers(0, resizeWidth, resizeHeight, DXGI_FORMAT_UNKNOWN, 0);
                resizeWidth = resizeHeight = 0;
                createTarget();
            }
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            application.frame(dpiScale);
            ImGui::Render();
            const float clear[4] = {13 / 255.f, 15 / 255.f, 20 / 255.f, 1};
            context->OMSetRenderTargets(1, &target, nullptr);
            context->ClearRenderTargetView(target, clear);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            occluded = swapChain->Present(foreground ? 1 : 0, 0) == DXGI_STATUS_OCCLUDED;
        }
        app = nullptr;
    }
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    releaseDevice();
    CoUninitialize();
    if (single)
        CloseHandle(single);
    return 0;
}

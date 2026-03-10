// =============================================================================
// Direct3D 11 Backend
// Creates the window, swap chain, and renders the recompiled game's output
// Target: 640x480 internal (matching GameCube EFB), upscaled to window size
// =============================================================================

#ifdef _WIN32

#include "ww/gx/gx.h"
#include <cstdio>
#include <cstdint>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace ww::gx {

// D3D11 state
static ID3D11Device*           g_device = nullptr;
static ID3D11DeviceContext*    g_context = nullptr;
static IDXGISwapChain*         g_swap_chain = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static ID3D11DepthStencilView* g_dsv = nullptr;
static ID3D11Texture2D*        g_depth_buffer = nullptr;
static HWND                    g_hwnd = nullptr;

// Internal framebuffer (GameCube EFB is 640x528, we use 640x480 for simplicity)
static const uint32_t EFB_WIDTH = 640;
static const uint32_t EFB_HEIGHT = 480;

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        // TODO: Handle resize
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            PostQuitMessage(0);
            return 0;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool GXInitBackend(void* hwnd_or_null, uint32_t width, uint32_t height) {
    printf("[D3D11] Initializing backend (%ux%u)\n", width, height);

    HWND hwnd = (HWND)hwnd_or_null;

    if (!hwnd) {
        // Create window
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = wnd_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.lpszClassName = L"WindWakerRecomp";
        RegisterClassExW(&wc);

        RECT rc = { 0, 0, (LONG)width, (LONG)height };
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

        hwnd = CreateWindowExW(
            0, L"WindWakerRecomp",
            L"The Wind Waker - Static Recompilation",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            rc.right - rc.left, rc.bottom - rc.top,
            nullptr, nullptr, wc.hInstance, nullptr
        );

        ShowWindow(hwnd, SW_SHOW);
    }

    g_hwnd = hwnd;

    // Create device and swap chain
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = width;
    scd.BufferDesc.Height = height;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        &feature_level, 1, D3D11_SDK_VERSION,
        &scd, &g_swap_chain, &g_device, nullptr, &g_context
    );

    if (FAILED(hr)) {
        fprintf(stderr, "[D3D11] Failed to create device: 0x%08lX\n", hr);
        return false;
    }

    // Create render target view
    ID3D11Texture2D* back_buffer;
    g_swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back_buffer);
    g_device->CreateRenderTargetView(back_buffer, nullptr, &g_rtv);
    back_buffer->Release();

    // Create depth buffer
    D3D11_TEXTURE2D_DESC dd = {};
    dd.Width = width;
    dd.Height = height;
    dd.MipLevels = 1;
    dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.SampleDesc.Count = 1;
    dd.Usage = D3D11_USAGE_DEFAULT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    g_device->CreateTexture2D(&dd, nullptr, &g_depth_buffer);
    g_device->CreateDepthStencilView(g_depth_buffer, nullptr, &g_dsv);

    // Set render targets
    g_context->OMSetRenderTargets(1, &g_rtv, g_dsv);

    // Set viewport
    D3D11_VIEWPORT vp = {};
    vp.Width = (float)width;
    vp.Height = (float)height;
    vp.MaxDepth = 1.0f;
    g_context->RSSetViewports(1, &vp);

    printf("[D3D11] Backend ready. The Great Sea awaits.\n");
    return true;
}

void GXShutdownBackend() {
    if (g_dsv) { g_dsv->Release(); g_dsv = nullptr; }
    if (g_depth_buffer) { g_depth_buffer->Release(); g_depth_buffer = nullptr; }
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    if (g_swap_chain) { g_swap_chain->Release(); g_swap_chain = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
    printf("[D3D11] Shutdown complete.\n");
}

void GXPresent() {
    if (g_swap_chain) {
        g_swap_chain->Present(1, 0); // VSync on

        // Clear for next frame - Wind Waker blue sky
        float clear_color[4] = { 0.4f, 0.7f, 0.95f, 1.0f };
        g_context->ClearRenderTargetView(g_rtv, clear_color);
        g_context->ClearDepthStencilView(g_dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    }
}

} // namespace ww::gx

#endif // _WIN32

// Includes
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d11.h>
#include <tchar.h>

#include "../include/rtwinaudio.hh"

// Libraries
#pragma comment(lib, "d3d11")

// Contrib (ImGui)
#include "../contrib/imgui/imgui.h"
#include "../contrib/imgui/imgui_internal.h"
#include "../contrib/imgui/imgui_impl_win32.h"
#include "../contrib/imgui/imgui_impl_dx11.h"

// Contrib (KissFFT)
#include "../contrib/kissfft/kiss_fft.h"

// Data
ID3D11Device* gD3DDevice = 0;
ID3D11DeviceContext* gD3DDeviceCtx = 0;
IDXGISwapChain* gSwapChain = 0;
ID3D11RenderTargetView* gRTV = 0;

//=============================================================
// 
//  Audio Data Class
// 
//=============================================================

class RtWinAudioData
{
public:
    float* mBuffer = 0;
    UINT32 mNumFrames = 0;

    kiss_fft_cfg mFFT_cfg = kiss_fft_alloc(1024, 0, 0, 0);
    kiss_fft_cpx mFFT_in[1024];
    kiss_fft_cpx mFFT_out[1024];

    bool Update(RtWinAudio& rtAudio, float volume)
    {
        UINT32 numFrames;
        auto buffer = static_cast<float*>(rtAudio.GetBuffer(numFrames));
        if (!buffer) {
            return false;
        }

        mBuffer = buffer;
        mNumFrames = numFrames;

        volume = powf(10.f, (volume * 0.5f) / 20.f);

        // FFT

        memset(mFFT_in, 0, sizeof(mFFT_in));

        int numChannels = rtAudio.GetNumChannels();

        for (UINT32 i = 0; numFrames > i; ++i)
        {
            int index = (i * numChannels);
            float height = 0.f;
            for (int c = 0; numChannels > c; ++c)
            {
                float& b = buffer[index + c];
                b *= volume;
                height += b;
            }

            mFFT_in[i].r = height;
            mFFT_in[i].i = 0;
        }

        kiss_fft(mFFT_cfg, mFFT_in, mFFT_out);
        return true;
    }

    float* Get()
    {
        if (mNumFrames) {
            return mBuffer;
        }

        return 0;
    }
};

//=============================================================
// 
//  Helper Functions
// 
//=============================================================

void RenderWaveform(ImDrawList* draw_list, const ImRect& rect, RtWinAudio& rtAudio, RtWinAudioData& rtAudioData, int peak)
{
    auto buffer = rtAudioData.Get();
    if (!buffer) {
        return;
    }

    int numChannels = rtAudio.GetNumChannels();

    ImVec2 scale = {
        rect.GetWidth() / static_cast<float>(rtAudioData.mNumFrames - 1),
        rect.GetHeight() * static_cast<float>(peak)
    };

    const float rectH = rect.GetHeight();

    ImVec2 pos = { rect.Min.x, rect.Min.y + ImFloor(rect.GetHeight() * 0.5f) };

    draw_list->PathClear();
    for (UINT32 i = 0; rtAudioData.mNumFrames > i; ++i)
    {
        int index = (i * numChannels);
        float height = 0.f;
        for (int c = 0; numChannels > c; ++c) {
            height += buffer[index + c];
        }

        height /= static_cast<float>(numChannels);
        height *= scale.y;

        draw_list->PathLineTo({ pos.x, pos.y + height });

        pos.x += scale.x;
    }

    auto flags = draw_list->Flags;
    draw_list->Flags = ImDrawListFlags_None;

    draw_list->PathStroke(IM_COL32(255, 255, 255, 255), 0, 2.f);

    draw_list->Flags = flags;
}

void RenderSpectrum(ImDrawList* draw_list, const ImRect& rect, RtWinAudio& rtAudio, RtWinAudioData& rtAudioData, bool limit_peak, int offset)
{
    auto buffer = rtAudioData.Get();
    if (!buffer) {
        return;
    }

    int numChannels = rtAudio.GetNumChannels();

    ImVec2 scale = {
        rect.GetWidth() / static_cast<float>(rtAudioData.mNumFrames - 1),
        rect.GetHeight() * 0.1f
    };

    ImVec2 pos = { rect.Min.x, rect.Max.y };

    if (offset != 0)
    {
        float perc = static_cast<float>(abs(offset)) * 0.02f;
        scale.x *= 1.f + perc;
    }

    if (limit_peak)
    {
        for (UINT32 i = 0; rtAudioData.mNumFrames > i; ++i)
        {
            float z = sqrtf(rtAudioData.mFFT_out[i].r * rtAudioData.mFFT_out[i].r + rtAudioData.mFFT_out[i].i * rtAudioData.mFFT_out[i].i);

            while (rect.Min.y > (pos.y - (z * scale.y))) {
                scale.y *= 0.75f;
            }
        }
    }

    for (UINT32 i = 0; rtAudioData.mNumFrames > i; ++i)
    {
        float z = sqrtf(rtAudioData.mFFT_out[i].r * rtAudioData.mFFT_out[i].r + rtAudioData.mFFT_out[i].i * rtAudioData.mFFT_out[i].i);
        z *= scale.y;

        draw_list->AddRectFilled({ pos.x - scale.x, pos.y - z - 1.f }, { pos.x + scale.x + scale.x + 1.f, pos.y }, IM_COL32(50, 0, 125, 255));
        draw_list->AddRectFilled({ pos.x + scale.x, pos.y - z }, { pos.x + scale.x + scale.x, pos.y }, IM_COL32(255, 0, 125, 255));
        draw_list->AddRectFilled({ pos.x, pos.y - z }, { pos.x + scale.x, pos.y }, IM_COL32(125, 0, 255, 255));

        pos.x += scale.x;
    }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* backbuffer;
    gSwapChain->GetBuffer(0, IID_PPV_ARGS(&backbuffer));
    gD3DDevice->CreateRenderTargetView(backbuffer, 0, &gRTV);
    backbuffer->Release();
}

bool CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    if (D3D11CreateDeviceAndSwapChain(0, D3D_DRIVER_TYPE_HARDWARE, 0, 0, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &gSwapChain, &gD3DDevice, &featureLevel, &gD3DDeviceCtx) != S_OK) {
        return false;
    }

    CreateRenderTarget();
    return true;
}

void CleanupRenderTarget()
{
    if (gRTV) { gRTV->Release(); gRTV = 0; }
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (gSwapChain) { gSwapChain->Release(); gSwapChain = 0; }
    if (gD3DDeviceCtx) { gD3DDeviceCtx->Release(); gD3DDeviceCtx = 0; }
    if (gD3DDevice) { gD3DDevice->Release(); gD3DDevice = 0; }
}

//=============================================================
// 
//  Render Functions
// 
//=============================================================

// Window Procedure

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
        return true;
    }

    switch (msg)
    {
    case WM_SIZE:
        if (gD3DDevice && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            gSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

// Entrypoint

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow)
{
    WNDCLASSEXA wc = { sizeof(wc), CS_CLASSDC, WndProc, 0, 0, GetModuleHandleA(0), 0, 0, 0, 0, "RtWinAudio_Ex", 0 };
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowA(wc.lpszClassName, "RtWinAudio", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 720, 0, 0, wc.hInstance, 0);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        UnregisterClassA(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    // Setup Dear ImGui context
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = 0;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(gD3DDevice, gD3DDeviceCtx);

    // Our state
    RtWinAudio rtAudio;
    bool listening_started = rtAudio.Start();

    RtWinAudioData rtAudioData;
    float volume = 0.f;

    bool show_waveform_window = true;
    int peak_waveform = 5;

    bool show_spectrum_window = true;
    bool limit_spectrum_peak = false;
    int spectrum_offset = 0;

    bool vsync = true;

    // Main loop
    bool done = false;
    while (!done)
    {
        MSG msg;
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
            if (msg.message == WM_QUIT) {
                done = true;
            }
        }
        if (done) {
            break;
        }

        // Start the Dear ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (listening_started) {
            rtAudioData.Update(rtAudio, volume);
        }
        else
        {
            auto drawList = ImGui::GetBackgroundDrawList();
            drawList->AddText({ 5.f, 5.f }, IM_COL32(255, 0, 0, 255), "Failed to start audio listening!");
        }

        // Main Window Render

        ImGui::SetNextWindowPos({ 10.f, 10.f }, ImGuiCond_Once);
        ImGui::SetNextWindowSize({ 720.f, 250.f }, ImGuiCond_Once);
        ImGui::Begin("Main");
        {
            ImGui::SliderFloat("Volume", &volume, -10.f, 50.f, "%.1f dB");

            ImGui::Checkbox("Waveform Window", &show_waveform_window);
            ImGui::SliderInt("Waveform Peak Scale", &peak_waveform, 1, 10);

            ImGui::Checkbox("Spectrum Window", &show_spectrum_window);
            ImGui::Checkbox("Limit Spectrum Peak", &limit_spectrum_peak);
            ImGui::SliderInt("Spectrum Offset", &spectrum_offset, 0, 100, "%d%%");

            ImGui::Checkbox("Use V-Sync", &vsync);
            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.f / io.Framerate, io.Framerate);
        }
        ImGui::End();

        if (show_waveform_window)
        {
            ImGui::SetNextWindowPos({ 740.f, 10.f }, ImGuiCond_Once);
            ImGui::SetNextWindowSize({ 500.f, 250.f }, ImGuiCond_Once);
            ImGui::Begin("Waveform", &show_waveform_window);
            {
                auto window = ImGui::GetCurrentWindow();
                RenderWaveform(window->DrawList, window->InnerClipRect, rtAudio, rtAudioData, peak_waveform);
            }
            ImGui::End();
        }

        if (show_spectrum_window)
        {
            ImGui::SetNextWindowPos({ 10.f, 270.f }, ImGuiCond_Once);
            ImGui::SetNextWindowSize({ 1230.f, 400.f }, ImGuiCond_Once);
            ImGui::Begin("Spectrum", &show_spectrum_window);
            {
                auto window = ImGui::GetCurrentWindow();
                RenderSpectrum(window->DrawList, window->InnerClipRect, rtAudio, rtAudioData, limit_spectrum_peak, spectrum_offset);
            }
            ImGui::End();
        }

        // Rendering
        ImGui::Render();

        const float clearRGBA[4] = { 0.f, 0.f, 0.f, 1.f };
        gD3DDeviceCtx->OMSetRenderTargets(1, &gRTV, 0);
        gD3DDeviceCtx->ClearRenderTargetView(gRTV, clearRGBA);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        gSwapChain->Present(vsync, 0);
    }

    // Cleanup

    if (listening_started) {
        rtAudio.Stop();
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);

    return 0;
}
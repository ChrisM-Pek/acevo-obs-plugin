// DLL injectée dans AssettoCorsaEVO.exe (D3D12).
//
// Deux captures vers OBS :
//   - CleanView (source 1) : la scene principale tonemappee SANS HUD. On copie
//     le swapchain juste apres le tonemap, avant le composite du HUD.
//   - Camera1  (source 2) : le retroviseur (mirror_texture0, 1024x256,
//     R11G11B10_FLOAT HDR, rempli par les Colour Pass #7 puis #8). On copie sa
//     RT a chaque sortie d'etat RENDER_TARGET (la derniere copie de la frame,
//     apres le pass #8, gagne), puis on tonemappe en RGBA8 via une petite passe
//     D3D11 (sinon l'image serait sombre).
//
// Detection des RT via OMSetRenderTargets + map descripteur->ressource
// (CreateRenderTargetView). Pour le swapchain on apprend le handle a Present.
// Pour le miroir, injecter depuis le MENU (avant chargement session) pour que
// la creation de sa RTV soit captee.

#include <windows.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <d3d11on12.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <atomic>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "MinHook.h"
#include "../shared/ipc.h"

// Proxy dxgi : on transfere tous les exports vers dxgi_orig.dll (copie du vrai
// dxgi systeme placee a cote du jeu). Permet de charger notre DLL au demarrage.
#pragma comment(linker, "/EXPORT:ApplyCompatResolutionQuirking=dxgi_orig.ApplyCompatResolutionQuirking,@1")
#pragma comment(linker, "/EXPORT:CompatString=dxgi_orig.CompatString,@2")
#pragma comment(linker, "/EXPORT:CompatValue=dxgi_orig.CompatValue,@3")
#pragma comment(linker, "/EXPORT:DXGIDumpJournal=dxgi_orig.DXGIDumpJournal,@4")
#pragma comment(linker, "/EXPORT:PIXBeginCapture=dxgi_orig.PIXBeginCapture,@5")
#pragma comment(linker, "/EXPORT:PIXEndCapture=dxgi_orig.PIXEndCapture,@6")
#pragma comment(linker, "/EXPORT:PIXGetCaptureState=dxgi_orig.PIXGetCaptureState,@7")
#pragma comment(linker, "/EXPORT:SetAppCompatStringPointer=dxgi_orig.SetAppCompatStringPointer,@8")
#pragma comment(linker, "/EXPORT:UpdateHMDEmulationStatus=dxgi_orig.UpdateHMDEmulationStatus,@9")
#pragma comment(linker, "/EXPORT:CreateDXGIFactory=dxgi_orig.CreateDXGIFactory,@10")
#pragma comment(linker, "/EXPORT:CreateDXGIFactory1=dxgi_orig.CreateDXGIFactory1,@11")
#pragma comment(linker, "/EXPORT:CreateDXGIFactory2=dxgi_orig.CreateDXGIFactory2,@12")
#pragma comment(linker, "/EXPORT:DXGID3D10CreateDevice=dxgi_orig.DXGID3D10CreateDevice,@13")
#pragma comment(linker, "/EXPORT:DXGID3D10CreateLayeredDevice=dxgi_orig.DXGID3D10CreateLayeredDevice,@14")
#pragma comment(linker, "/EXPORT:DXGID3D10GetLayeredDeviceSize=dxgi_orig.DXGID3D10GetLayeredDeviceSize,@15")
#pragma comment(linker, "/EXPORT:DXGID3D10RegisterLayers=dxgi_orig.DXGID3D10RegisterLayers,@16")
#pragma comment(linker, "/EXPORT:DXGIDeclareAdapterRemovalSupport=dxgi_orig.DXGIDeclareAdapterRemovalSupport,@17")
#pragma comment(linker, "/EXPORT:DXGIDisableVBlankVirtualization=dxgi_orig.DXGIDisableVBlankVirtualization,@18")
#pragma comment(linker, "/EXPORT:DXGIGetDebugInterface1=dxgi_orig.DXGIGetDebugInterface1,@19")
#pragma comment(linker, "/EXPORT:DXGIReportAdapterConfiguration=dxgi_orig.DXGIReportAdapterConfiguration,@20")

using namespace acevo_obs;

namespace {

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ExecuteFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
using OMSetRTFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT,
                                           const D3D12_CPU_DESCRIPTOR_HANDLE*, BOOL,
                                           const D3D12_CPU_DESCRIPTOR_HANDLE*);
using CreateRTVFn = void(STDMETHODCALLTYPE*)(ID3D12Device*, ID3D12Resource*,
                                             const D3D12_RENDER_TARGET_VIEW_DESC*,
                                             D3D12_CPU_DESCRIPTOR_HANDLE);

PresentFn   g_origPresent = nullptr;
ExecuteFn   g_origExecute = nullptr;
OMSetRTFn   g_origOMSetRT = nullptr;
CreateRTVFn g_origCreateRTV = nullptr;

ID3D12CommandQueue*  g_queue = nullptr;
ID3D12Device*        g_device = nullptr;
IDXGISwapChain3*     g_swap3 = nullptr;
ID3D11On12Device*    g_11on12 = nullptr;
ID3D11Device*        g_d11device = nullptr;
ID3D11DeviceContext* g_d11ctx = nullptr;
bool                 g_initOk = false;
std::atomic<uint64_t> g_frame{0};
std::atomic<SIZE_T>   g_lastRtHandle{0};

// CleanView (swapchain)
ID3D12Resource*  g_captureD3D12 = nullptr;
ID3D11Texture2D* g_sharedTex = nullptr;

// Camera1 (miroir)
ID3D12Resource*  g_mirrorCapture = nullptr;     // copie HDR R11G11B10
ID3D11Texture2D* g_mirrorShared = nullptr;      // RGBA8 tonemappee (pour OBS)
ID3D11RenderTargetView* g_mirrorRTV = nullptr;
ID3D11VertexShader* g_vs = nullptr;
ID3D11PixelShader*  g_ps = nullptr;
ID3D11SamplerState* g_samp = nullptr;
std::atomic<ID3D12Resource*> g_mirrorRes{nullptr};
D3D12_RESOURCE_DESC g_mirrorDesc{};
bool g_mirrorReady = false;
std::atomic<bool> g_mirrorCopied{false};

HANDLE       g_mapping = nullptr;
SharedBlock* g_shared = nullptr;

std::mutex g_mtx;
std::unordered_map<SIZE_T, ID3D12Resource*> g_rtvMap;
std::unordered_set<ID3D12Resource*>         g_swapBuffers;
std::unordered_set<ID3D12Resource*>         g_loggedRT;
std::unordered_set<SIZE_T>                  g_loggedHandles;
std::vector<ID3D12Resource*>                g_backbuffers;
struct CmdPrev { bool swap; bool mirror; };
std::unordered_map<ID3D12GraphicsCommandList*, CmdPrev> g_prev;

void OpenConsole() {
    AllocConsole();
    FILE* f = nullptr; freopen_s(&f, "CONOUT$", "w", stdout);
    puts("[acevo-obs] DLL attachee.");
}

bool InitIpc() {
    g_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                   0, sizeof(SharedBlock), kSharedMemoryName);
    if (!g_mapping) return false;
    g_shared = static_cast<SharedBlock*>(
        MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedBlock)));
    if (!g_shared) return false;
    ZeroMemory(g_shared, sizeof(SharedBlock));
    g_shared->protocolVersion = kProtocolVersion;
    g_shared->pid = GetCurrentProcessId();
    return true;
}

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* r, D3D12_RESOURCE_STATES from,
                                  D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from; b.Transition.StateAfter = to;
    return b;
}

// Copie src (en RENDER_TARGET) -> dst (en COMMON) dans le command list du jeu.
void RecordCopy(ID3D12GraphicsCommandList* cl, ID3D12Resource* src, ID3D12Resource* dst) {
    D3D12_RESOURCE_BARRIER b[2];
    b[0] = Transition(src, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    b[1] = Transition(dst, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    cl->ResourceBarrier(2, b);
    cl->CopyResource(dst, src);
    b[0] = Transition(src, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    b[1] = Transition(dst, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    cl->ResourceBarrier(2, b);
}

ID3D11Texture2D* CreateSharedRGBA(UINT w, UINT h, DXGI_FORMAT fmt, HANDLE* outHandle) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = fmt; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(g_d11device->CreateTexture2D(&td, nullptr, &tex))) return nullptr;
    IDXGIResource* r = nullptr;
    if (SUCCEEDED(tex->QueryInterface(IID_PPV_ARGS(&r)))) { r->GetSharedHandle(outHandle); r->Release(); }
    return tex;
}

bool InitTonemap() {
    const char* hlsl =
        "Texture2D t:register(t0); SamplerState s:register(s0);"
        "struct V{float4 p:SV_Position;float2 uv:TEXCOORD0;};"
        "V vsmain(uint id:SV_VertexID){V o;o.uv=float2((id<<1)&2,id&2);"
        "o.p=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;}"
        "float4 psmain(V i):SV_Target{float3 c=t.Sample(s,i.uv).rgb;"
        "c=c/(c+1.0);c=pow(saturate(c),1.0/2.2);return float4(c,1);}";
    ID3DBlob *vb=nullptr,*pb=nullptr,*err=nullptr;
    if (FAILED(D3DCompile(hlsl, strlen(hlsl), nullptr, nullptr, nullptr, "vsmain", "vs_5_0", 0, 0, &vb, &err))) return false;
    if (FAILED(D3DCompile(hlsl, strlen(hlsl), nullptr, nullptr, nullptr, "psmain", "ps_5_0", 0, 0, &pb, &err))) return false;
    g_d11device->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &g_vs);
    g_d11device->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &g_ps);
    vb->Release(); pb->Release();
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    g_d11device->CreateSamplerState(&sd, &g_samp);
    return g_vs && g_ps && g_samp;
}

bool LazyInit(IDXGISwapChain* swap) {
    if (g_initOk) return true;
    if (!g_queue) return false;
    if (!g_device && FAILED(swap->GetDevice(IID_PPV_ARGS(&g_device)))) return false;
    if (!g_swap3) swap->QueryInterface(IID_PPV_ARGS(&g_swap3));

    DXGI_SWAP_CHAIN_DESC sd{}; swap->GetDesc(&sd);
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_backbuffers.assign(sd.BufferCount, nullptr);
        for (UINT i = 0; i < sd.BufferCount; ++i) {
            ID3D12Resource* bb = nullptr;
            if (SUCCEEDED(swap->GetBuffer(i, IID_PPV_ARGS(&bb)))) {
                g_backbuffers[i] = bb; g_swapBuffers.insert(bb);
            }
        }
    }
    if (g_backbuffers.empty() || !g_backbuffers[0]) return false;
    D3D12_RESOURCE_DESC rd = g_backbuffers[0]->GetDesc();

    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    if (FAILED(g_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&g_captureD3D12)))) return false;

    IUnknown* queues[] = { g_queue };
    if (FAILED(D3D11On12CreateDevice(g_device, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr, 0, queues, 1, 0, &g_d11device, &g_d11ctx, nullptr))) return false;
    g_d11device->QueryInterface(IID_PPV_ARGS(&g_11on12));

    HANDLE shared = nullptr;
    g_sharedTex = CreateSharedRGBA((UINT)rd.Width, rd.Height, rd.Format, &shared);
    if (!g_sharedTex || !shared) return false;

    auto& src = g_shared->sources[(uint32_t)SourceId::CleanView];
    src.width = (UINT)rd.Width; src.height = rd.Height; src.dxgiFormat = rd.Format;
    src.kmtHandle = (uint64_t)(uintptr_t)shared; src.valid = 1;

    InitTonemap();
    printf("[acevo-obs] CleanView prete %llux%u fmt=%d\n",
           (unsigned long long)rd.Width, rd.Height, rd.Format);
    g_initOk = true;
    return true;
}

void CreateMirrorTargets() {
    if (g_mirrorReady) return;
    ID3D12Resource* mr = g_mirrorRes.load();
    if (!mr) return;

    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    if (FAILED(g_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &g_mirrorDesc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&g_mirrorCapture)))) return;

    HANDLE shared = nullptr;
    g_mirrorShared = CreateSharedRGBA((UINT)g_mirrorDesc.Width, g_mirrorDesc.Height,
                                      DXGI_FORMAT_R8G8B8A8_UNORM, &shared);
    if (!g_mirrorShared || !shared) return;
    g_d11device->CreateRenderTargetView(g_mirrorShared, nullptr, &g_mirrorRTV);

    auto& src = g_shared->sources[(uint32_t)SourceId::Camera1];
    src.width = (UINT)g_mirrorDesc.Width; src.height = g_mirrorDesc.Height;
    src.dxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    src.kmtHandle = (uint64_t)(uintptr_t)shared; src.valid = 1;

    printf("[acevo-obs] Camera1 (retroviseur) prete %llux%u\n",
           (unsigned long long)g_mirrorDesc.Width, g_mirrorDesc.Height);
    g_mirrorReady = true;
}

void PublishCleanView() {
    D3D11_RESOURCE_FLAGS rf{}; rf.BindFlags = 0;
    ID3D11Resource* w = nullptr;
    if (SUCCEEDED(g_11on12->CreateWrappedResource(g_captureD3D12, &rf,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON, IID_PPV_ARGS(&w)))) {
        g_11on12->AcquireWrappedResources(&w, 1);
        g_d11ctx->CopyResource(g_sharedTex, w);
        g_11on12->ReleaseWrappedResources(&w, 1);
        g_d11ctx->Flush(); w->Release();
        g_shared->sources[(uint32_t)SourceId::CleanView].frameIndex = ++g_frame;
    }
}

void PublishMirror() {
    if (!g_mirrorReady || !g_mirrorCopied.load()) return;
    D3D11_RESOURCE_FLAGS rf{}; rf.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ID3D11Resource* w = nullptr;
    if (FAILED(g_11on12->CreateWrappedResource(g_mirrorCapture, &rf,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON, IID_PPV_ARGS(&w)))) return;
    g_11on12->AcquireWrappedResources(&w, 1);

    ID3D11ShaderResourceView* srv = nullptr;
    g_d11device->CreateShaderResourceView(w, nullptr, &srv);

    D3D11_VIEWPORT vp{}; vp.Width = (float)g_mirrorDesc.Width; vp.Height = (float)g_mirrorDesc.Height; vp.MaxDepth = 1;
    g_d11ctx->OMSetRenderTargets(1, &g_mirrorRTV, nullptr);
    g_d11ctx->RSSetViewports(1, &vp);
    g_d11ctx->VSSetShader(g_vs, nullptr, 0);
    g_d11ctx->PSSetShader(g_ps, nullptr, 0);
    g_d11ctx->PSSetSamplers(0, 1, &g_samp);
    g_d11ctx->PSSetShaderResources(0, 1, &srv);
    g_d11ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_d11ctx->Draw(3, 0);

    ID3D11ShaderResourceView* nullsrv = nullptr;
    g_d11ctx->PSSetShaderResources(0, 1, &nullsrv);
    if (srv) srv->Release();
    g_11on12->ReleaseWrappedResources(&w, 1);
    g_d11ctx->Flush(); w->Release();
    g_shared->sources[(uint32_t)SourceId::Camera1].frameIndex = ++g_frame;
}

bool IsMirrorDesc(const D3D12_RESOURCE_DESC& d) {
    return d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
           d.Format == DXGI_FORMAT_R11G11B10_FLOAT &&
           (d.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) &&
           d.Width >= 3 * d.Height && d.Width <= 2048;
}

// ---- Hooks ----
void STDMETHODCALLTYPE HookedCreateRTV(ID3D12Device* dev, ID3D12Resource* res,
                                       const D3D12_RENDER_TARGET_VIEW_DESC* desc,
                                       D3D12_CPU_DESCRIPTOR_HANDLE dest) {
    if (res) { std::lock_guard<std::mutex> lk(g_mtx); g_rtvMap[dest.ptr] = res; }
    g_origCreateRTV(dev, res, desc, dest);
}

void STDMETHODCALLTYPE HookedOMSetRT(ID3D12GraphicsCommandList* cl, UINT num,
                                     const D3D12_CPU_DESCRIPTOR_HANDLE* rts, BOOL single,
                                     const D3D12_CPU_DESCRIPTOR_HANDLE* ds) {
    if (g_initOk) {
        bool curSwap = false, curMirror = false;
        ID3D12Resource* boundRes = nullptr;
        if (num > 0 && rts) {
            g_lastRtHandle = rts[0].ptr;
            std::lock_guard<std::mutex> lk(g_mtx);
            auto it = g_rtvMap.find(rts[0].ptr);
            if (it != g_rtvMap.end()) {
                boundRes = it->second;
                if (g_swapBuffers.count(boundRes)) curSwap = true;
            }
        }
        // Diagnostic : logge chaque handle RTV bind (resolu ou non), une fois.
        if (num > 0 && rts) {
            bool firstTime;
            { std::lock_guard<std::mutex> lk(g_mtx);
              firstTime = g_loggedHandles.insert(rts[0].ptr).second; }
            if (firstTime) {
                if (boundRes) {
                    D3D12_RESOURCE_DESC d = boundRes->GetDesc();
                    printf("[acevo-obs][RT] handle=%p RESOLU %llux%u fmt=%d flags=0x%x\n",
                           (void*)rts[0].ptr, (unsigned long long)d.Width, d.Height, d.Format, d.Flags);
                } else {
                    printf("[acevo-obs][RT] handle=%p NON-RESOLU (RTV creee avant injection)\n",
                           (void*)rts[0].ptr);
                }
            }
        }
        // Detection du miroir (hors lock principal pour GetDesc).
        if (boundRes && !curSwap && !g_mirrorRes.load()) {
            D3D12_RESOURCE_DESC d = boundRes->GetDesc();
            if (IsMirrorDesc(d)) {
                g_mirrorDesc = d; boundRes->AddRef(); g_mirrorRes = boundRes;
                puts("[acevo-obs] RT retroviseur detectee.");
            }
        }
        if (boundRes && boundRes == g_mirrorRes.load()) curMirror = true;

        std::lock_guard<std::mutex> lk(g_mtx);
        CmdPrev& p = g_prev[cl];
        if (p.swap && !curSwap && g_captureD3D12 && g_swap3) {
            UINT idx = g_swap3->GetCurrentBackBufferIndex();
            if (idx < g_backbuffers.size() && g_backbuffers[idx])
                RecordCopy(cl, g_backbuffers[idx], g_captureD3D12);
        }
        if (p.mirror && !curMirror && g_mirrorReady && g_mirrorRes.load()) {
            RecordCopy(cl, g_mirrorRes.load(), g_mirrorCapture);
            g_mirrorCopied = true;
        }
        p.swap = curSwap; p.mirror = curMirror;
    }
    g_origOMSetRT(cl, num, rts, single, ds);
}

void STDMETHODCALLTYPE HookedExecute(ID3D12CommandQueue* q, UINT n, ID3D12CommandList* const* lists) {
    if (!g_queue && q->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
        g_queue = q; puts("[acevo-obs] Command queue DIRECT capturee.");
    }
    g_origExecute(q, n, lists);
}

HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* swap, UINT sync, UINT flags) {
    if (LazyInit(swap)) {
        SIZE_T h = g_lastRtHandle.load();
        if (h) {
            std::lock_guard<std::mutex> lk(g_mtx);
            UINT idx = g_swap3 ? g_swap3->GetCurrentBackBufferIndex() : 0;
            if (idx < g_backbuffers.size() && g_backbuffers[idx]) g_rtvMap[h] = g_backbuffers[idx];
        }
        if (g_mirrorRes.load() && !g_mirrorReady) CreateMirrorTargets();
        PublishCleanView();
        PublishMirror();
    }
    return g_origPresent(swap, sync, flags);
}

bool ResolveVtables(void** present, void** execute, void** omSetRT, void** createRTV) {
    ID3D12Device* dev = nullptr;
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev)))) return false;
    *createRTV = (*reinterpret_cast<void***>(dev))[20];
    D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* q = nullptr; dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&q));
    *execute = (*reinterpret_cast<void***>(q))[10];
    ID3D12CommandAllocator* alloc = nullptr;
    dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc));
    ID3D12GraphicsCommandList* cl = nullptr;
    dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, IID_PPV_ARGS(&cl));
    *omSetRT = (*reinterpret_cast<void***>(cl))[46];
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"acevo_dummy";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0,0,16,16,nullptr,nullptr,wc.hInstance,nullptr);
    // On charge le vrai dxgi (copie dxgi_orig.dll) pour creer la factory sans
    // dependre de notre propre table d'import (notre DLL s'appelle dxgi.dll).
    using CreateFactory1Fn = HRESULT(WINAPI*)(REFIID, void**);
    CreateFactory1Fn realCreateFactory1 = nullptr;
    HMODULE realDxgi = LoadLibraryW(L"dxgi_orig.dll");
    if (!realDxgi) realDxgi = LoadLibraryW(L"dxgi.dll");
    if (realDxgi) realCreateFactory1 =
        reinterpret_cast<CreateFactory1Fn>(GetProcAddress(realDxgi, "CreateDXGIFactory1"));
    IDXGIFactory4* factory = nullptr;
    if (realCreateFactory1) realCreateFactory1(IID_PPV_ARGS(&factory));
    if (!factory) {
        cl->Release(); alloc->Release(); q->Release(); dev->Release();
        DestroyWindow(hwnd); UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return false;
    }
    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount = 2; scd.BufferDesc.Width = 16; scd.BufferDesc.Height = 16;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1; scd.Windowed = TRUE; scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain* sc = nullptr; bool ok = false;
    if (SUCCEEDED(factory->CreateSwapChain(q, &scd, &sc)) && sc) {
        *present = (*reinterpret_cast<void***>(sc))[8]; ok = true; sc->Release();
    }
    factory->Release(); DestroyWindow(hwnd); UnregisterClassW(wc.lpszClassName, wc.hInstance);
    cl->Release(); alloc->Release(); q->Release(); dev->Release();
    return ok;
}

DWORD WINAPI InitThread(LPVOID) {
    OpenConsole();
    if (!InitIpc()) { puts("[acevo-obs] InitIpc echoue."); return 1; }
    if (MH_Initialize() != MH_OK) { puts("[acevo-obs] MH_Initialize echoue."); return 1; }
    void *present=nullptr,*execute=nullptr,*omSetRT=nullptr,*createRTV=nullptr;
    if (!ResolveVtables(&present,&execute,&omSetRT,&createRTV)) {
        puts("[acevo-obs] ResolveVtables echoue."); return 1;
    }
    MH_CreateHook(execute,   &HookedExecute,   reinterpret_cast<void**>(&g_origExecute));
    MH_CreateHook(present,   &HookedPresent,   reinterpret_cast<void**>(&g_origPresent));
    MH_CreateHook(omSetRT,   &HookedOMSetRT,   reinterpret_cast<void**>(&g_origOMSetRT));
    MH_CreateHook(createRTV, &HookedCreateRTV, reinterpret_cast<void**>(&g_origCreateRTV));
    MH_EnableHook(MH_ALL_HOOKS);
    puts("[acevo-obs] Hooks installes. Injecte depuis le MENU pour capter le retroviseur.");
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    } else if (reason == DLL_PROCESS_DETACH) {
        MH_DisableHook(MH_ALL_HOOKS); MH_Uninitialize();
    }
    return TRUE;
}

#include "render.h"

#include "input.h"
#include "log.h"
#include "menu.h"

#include "../third_party/imgui/backends/imgui_impl_dx12.h"
#include "../third_party/imgui/imgui.h"
#include "../third_party/minhook/include/MinHook.h"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <atomic>
#include <mutex>
#include <vector>

namespace render {

namespace {

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ResizeBuffers1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT*,
                                                     IUnknown* const*);
using ExecuteFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
using CreateSwapChainFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
using CreateSwapChainForHwndFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
                                                             const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*,
                                                             IDXGISwapChain1**);

// vtable slots (dxgi1_4.h / d3d12.h declaration order)
constexpr int kPresent = 8, kResizeBuffers = 13, kPresent1 = 22, kResizeBuffers1 = 39;
constexpr int kExecuteCommandLists = 10;
constexpr int kCreateSwapChain = 10, kCreateSwapChainForHwnd = 15;

PresentFn oPresent = nullptr;
Present1Fn oPresent1 = nullptr;
ResizeBuffersFn oResizeBuffers = nullptr;
ResizeBuffers1Fn oResizeBuffers1 = nullptr;
ExecuteFn oExecute = nullptr;
CreateSwapChainFn oCreateSwapChain = nullptr;
CreateSwapChainForHwndFn oCreateSwapChainForHwnd = nullptr;

std::mutex g_lock; // guards everything below (Present / ResizeBuffers / queue capture)
ID3D12CommandQueue* g_queue = nullptr; // the queue the game's swapchain presents on (AddRef'd)
bool g_queueFromSwapChain = false;

struct Frame {
    ID3D12CommandAllocator* allocator = nullptr;
    UINT64 fence = 0;
};

struct Renderer {
    IDXGISwapChain3* swapChain = nullptr; // not AddRef'd; identity only
    ID3D12Device* device = nullptr;
    ID3D12DescriptorHeap* rtvHeap = nullptr;
    ID3D12DescriptorHeap* srvHeap = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Fence* fence = nullptr;
    HANDLE fenceEvent = nullptr;
    UINT64 fenceValue = 0;
    std::vector<Frame> frames;
    UINT rtvSize = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT width = 0, height = 0;
    HWND window = nullptr;
    bool imgui = false;
    bool failed = false;
} g_r;

// Simple SRV descriptor allocator for the ImGui backend's textures.
constexpr UINT kSrvCount = 64;
UINT g_srvSize = 0;
bool g_srvUsed[kSrvCount] = {};

void SrvAlloc(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
    for (UINT i = 0; i < kSrvCount; ++i) {
        if (g_srvUsed[i]) continue;
        g_srvUsed[i] = true;
        cpu->ptr = g_r.srvHeap->GetCPUDescriptorHandleForHeapStart().ptr + static_cast<SIZE_T>(i) * g_srvSize;
        gpu->ptr = g_r.srvHeap->GetGPUDescriptorHandleForHeapStart().ptr + static_cast<UINT64>(i) * g_srvSize;
        return;
    }
    cpu->ptr = 0;
    gpu->ptr = 0;
}

void SrvFree(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE) {
    const SIZE_T i = (cpu.ptr - g_r.srvHeap->GetCPUDescriptorHandleForHeapStart().ptr) / g_srvSize;
    if (i < kSrvCount) g_srvUsed[i] = false;
}

template <typename T>
void SafeRelease(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

void WaitIdle() {
    if (!g_r.fence || !g_queue) return;
    const UINT64 v = ++g_r.fenceValue;
    if (FAILED(g_queue->Signal(g_r.fence, v))) return;
    if (g_r.fence->GetCompletedValue() < v && SUCCEEDED(g_r.fence->SetEventOnCompletion(v, g_r.fenceEvent)))
        WaitForSingleObject(g_r.fenceEvent, 2000);
}

// Frees the per-swapchain resources (buffer count / format / size dependent).
void ReleaseFrames() {
    WaitIdle();
    for (auto& f : g_r.frames) SafeRelease(f.allocator);
    g_r.frames.clear();
    SafeRelease(g_r.list);
    SafeRelease(g_r.rtvHeap);
}

void ShutdownAll() {
    ReleaseFrames();
    if (g_r.imgui) {
        ImGui_ImplDX12_Shutdown();
        g_r.imgui = false;
    }
    SafeRelease(g_r.srvHeap);
    SafeRelease(g_r.fence);
    SafeRelease(g_r.device);
    if (g_r.fenceEvent) CloseHandle(g_r.fenceEvent);
    g_r.fenceEvent = nullptr;
    g_r.swapChain = nullptr;
}

DXGI_FORMAT RtvFormat(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return f;
    }
}

bool Setup(IDXGISwapChain3* sc) {
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(sc->GetDesc(&desc))) return false;
    const DXGI_FORMAT fmt = RtvFormat(desc.BufferDesc.Format);

    if (g_r.swapChain != sc || g_r.format != fmt) {
        if (g_r.swapChain) logx::Write("render: swapchain or format changed, re-initialising");
        ShutdownAll();
    }
    if (!g_r.device) {
        if (FAILED(sc->GetDevice(IID_PPV_ARGS(&g_r.device)))) return false;
        D3D12_DESCRIPTOR_HEAP_DESC sd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSrvCount, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
        if (FAILED(g_r.device->CreateDescriptorHeap(&sd, IID_PPV_ARGS(&g_r.srvHeap)))) return false;
        g_srvSize = g_r.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        std::fill(std::begin(g_srvUsed), std::end(g_srvUsed), false);
        if (FAILED(g_r.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_r.fence)))) return false;
        g_r.fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        g_r.swapChain = sc;
        g_r.format = fmt;
        g_r.window = desc.OutputWindow;
        logx::Write("render: swapchain %ux%u, %u buffers, format %d, window 0x%p", desc.BufferDesc.Width,
                    desc.BufferDesc.Height, desc.BufferCount, fmt, desc.OutputWindow);
    }
    if (g_r.frames.size() != desc.BufferCount) {
        ReleaseFrames();
        D3D12_DESCRIPTOR_HEAP_DESC rd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, desc.BufferCount, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
        if (FAILED(g_r.device->CreateDescriptorHeap(&rd, IID_PPV_ARGS(&g_r.rtvHeap)))) return false;
        g_r.rtvSize = g_r.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        g_r.frames.resize(desc.BufferCount);
        for (auto& f : g_r.frames)
            if (FAILED(g_r.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&f.allocator)))) return false;
        if (FAILED(g_r.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_r.frames[0].allocator, nullptr,
                                                 IID_PPV_ARGS(&g_r.list))))
            return false;
        g_r.list->Close();
    }
    g_r.width = desc.BufferDesc.Width;
    g_r.height = desc.BufferDesc.Height;

    if (!g_r.imgui) {
        if (!ImGui::GetCurrentContext()) menu::CreateContext();
        ImGui_ImplDX12_InitInfo info;
        info.Device = g_r.device;
        info.CommandQueue = g_queue;
        info.NumFramesInFlight = static_cast<int>(desc.BufferCount);
        info.RTVFormat = fmt;
        info.SrvDescriptorHeap = g_r.srvHeap;
        info.SrvDescriptorAllocFn = SrvAlloc;
        info.SrvDescriptorFreeFn = SrvFree;
        if (!ImGui_ImplDX12_Init(&info)) return false;
        g_r.imgui = true;
        input::AttachWindow(g_r.window);
        logx::Write("render: ImGui ready");
    }
    return true;
}

void Draw(IDXGISwapChain3* sc) {
    std::lock_guard<std::mutex> l(g_lock);
    if (g_r.failed || !g_queue) return;
    if (!Setup(sc)) {
        logx::Write("ERROR: render setup failed; the menu cannot be shown.");
        ShutdownAll();
        g_r.failed = true;
        return;
    }

    ImGui_ImplDX12_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(g_r.width), static_cast<float>(g_r.height));
    input::FeedImGui(g_r.window, io.DisplaySize.x, io.DisplaySize.y);
    ImGui::NewFrame();
    menu::Draw();
    ImGui::Render();
    ImDrawData* dd = ImGui::GetDrawData();
    if (!dd || dd->CmdLists.Size == 0) return; // nothing on screen this frame

    const UINT idx = sc->GetCurrentBackBufferIndex();
    if (idx >= g_r.frames.size()) return;
    Frame& fr = g_r.frames[idx];
    if (g_r.fence->GetCompletedValue() < fr.fence && SUCCEEDED(g_r.fence->SetEventOnCompletion(fr.fence, g_r.fenceEvent)))
        WaitForSingleObject(g_r.fenceEvent, 1000);

    ID3D12Resource* bb = nullptr;
    if (FAILED(sc->GetBuffer(idx, IID_PPV_ARGS(&bb)))) return;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_r.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(idx) * g_r.rtvSize;
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = g_r.format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    g_r.device->CreateRenderTargetView(bb, &rtvDesc, rtv);

    fr.allocator->Reset();
    g_r.list->Reset(fr.allocator, nullptr);
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = bb;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    g_r.list->ResourceBarrier(1, &b);
    g_r.list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    g_r.list->SetDescriptorHeaps(1, &g_r.srvHeap);
    ImGui_ImplDX12_RenderDrawData(dd, g_r.list);
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    g_r.list->ResourceBarrier(1, &b);
    g_r.list->Close();
    ID3D12CommandList* lists[] = {g_r.list};
    oExecute(g_queue, 1, lists);
    fr.fence = ++g_r.fenceValue;
    g_queue->Signal(g_r.fence, fr.fence);
    bb->Release();
}

thread_local bool t_inPresent = false; // Present1 may run inside Present: draw once

void OnPresent(IDXGISwapChain3* sc, UINT flags) {
    if (flags & DXGI_PRESENT_TEST) return;
    static bool logged = false;
    if (!logged) {
        logged = true;
        logx::Write("render: first Present seen (queue %s)", g_queue ? "captured" : "not captured yet");
    }
    __try {
        Draw(sc);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Never take the game down because of the overlay.
        logx::Write("ERROR: exception 0x%08X while drawing the menu; overlay disabled.", GetExceptionCode());
        g_r.failed = true;
    }
}

HRESULT STDMETHODCALLTYPE HkPresent(IDXGISwapChain3* sc, UINT sync, UINT flags) {
    if (t_inPresent) return oPresent(sc, sync, flags);
    t_inPresent = true;
    OnPresent(sc, flags);
    const HRESULT hr = oPresent(sc, sync, flags);
    t_inPresent = false;
    return hr;
}

HRESULT STDMETHODCALLTYPE HkPresent1(IDXGISwapChain3* sc, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* p) {
    if (t_inPresent) return oPresent1(sc, sync, flags, p);
    t_inPresent = true;
    OnPresent(sc, flags);
    const HRESULT hr = oPresent1(sc, sync, flags, p);
    t_inPresent = false;
    return hr;
}

HRESULT STDMETHODCALLTYPE HkResizeBuffers(IDXGISwapChain3* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT flags) {
    {
        std::lock_guard<std::mutex> l(g_lock);
        ReleaseFrames();
    }
    return oResizeBuffers(sc, n, w, h, f, flags);
}

HRESULT STDMETHODCALLTYPE HkResizeBuffers1(IDXGISwapChain3* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT flags,
                                           const UINT* mask, IUnknown* const* queues) {
    {
        std::lock_guard<std::mutex> l(g_lock);
        ReleaseFrames();
    }
    return oResizeBuffers1(sc, n, w, h, f, flags, mask, queues);
}

void CaptureQueue(IUnknown* device, bool fromSwapChain) {
    ID3D12CommandQueue* q = nullptr;
    if (!device || FAILED(device->QueryInterface(IID_PPV_ARGS(&q)))) return;
    std::lock_guard<std::mutex> l(g_lock);
    if (g_queue == q || (g_queue && g_queueFromSwapChain && !fromSwapChain)) {
        q->Release();
        return;
    }
    if (g_queue) {
        ShutdownAll(); // new swapchain queue: rebuild on it
        g_queue->Release();
    }
    g_queue = q; // keeps the reference
    g_queueFromSwapChain = fromSwapChain;
    logx::Write("render: command queue 0x%p captured (%s)", q, fromSwapChain ? "swapchain creation" : "first direct submit");
}

HRESULT STDMETHODCALLTYPE HkCreateSwapChain(IDXGIFactory* f, IUnknown* dev, DXGI_SWAP_CHAIN_DESC* d, IDXGISwapChain** out) {
    const HRESULT hr = oCreateSwapChain(f, dev, d, out);
    if (SUCCEEDED(hr)) CaptureQueue(dev, true);
    return hr;
}

HRESULT STDMETHODCALLTYPE HkCreateSwapChainForHwnd(IDXGIFactory2* f, IUnknown* dev, HWND w, const DXGI_SWAP_CHAIN_DESC1* d,
                                                   const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fs, IDXGIOutput* o,
                                                   IDXGISwapChain1** out) {
    const HRESULT hr = oCreateSwapChainForHwnd(f, dev, w, d, fs, o, out);
    if (SUCCEEDED(hr)) CaptureQueue(dev, true);
    return hr;
}

// Fallback when the swapchain was created before the hooks went in: take the
// first direct queue that submits work.
void STDMETHODCALLTYPE HkExecute(ID3D12CommandQueue* q, UINT n, ID3D12CommandList* const* lists) {
    if (!g_queue && q->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) CaptureQueue(q, false);
    oExecute(q, n, lists);
}

bool HookSlot(void** vtable, int slot, void* detour, void** original, const char* name) {
    if (MH_CreateHook(vtable[slot], detour, original) != MH_OK || MH_EnableHook(vtable[slot]) != MH_OK) {
        logx::Write("render: hooking %s failed", name);
        return false;
    }
    return true;
}

LRESULT CALLBACK DummyProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

} // namespace

bool Install() {
    // Build a throwaway device/queue/swapchain on a hidden window to read the
    // vtables of the system DXGI and D3D12 objects.
    HMODULE d3d12 = LoadLibraryW(L"d3d12.dll");
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    if (!d3d12 || !dxgi) {
        logx::Write("render: d3d12.dll/dxgi.dll not available");
        return false;
    }
    auto createDevice = reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(GetProcAddress(d3d12, "D3D12CreateDevice"));
    using CreateFactoryFn = HRESULT(WINAPI*)(UINT, REFIID, void**);
    auto createFactory = reinterpret_cast<CreateFactoryFn>(GetProcAddress(dxgi, "CreateDXGIFactory2"));
    if (!createDevice || !createFactory) return false;

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = DummyProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"w1zsMenuDummy";
    RegisterClassExW(&wc);
    HWND wnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);

    bool ok = false;
    IDXGIFactory4* factory = nullptr;
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    IDXGISwapChain1* sc = nullptr;
    do {
        if (!wnd || FAILED(createFactory(0, IID_PPV_ARGS(&factory)))) break;
        if (FAILED(createDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) break;
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) break;
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = sd.Height = 64;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (FAILED(factory->CreateSwapChainForHwnd(queue, wnd, &sd, nullptr, nullptr, &sc))) break;

        void** scVt = *reinterpret_cast<void***>(sc);
        void** qVt = *reinterpret_cast<void***>(queue);
        void** fVt = *reinterpret_cast<void***>(factory);
        ok = HookSlot(scVt, kPresent, reinterpret_cast<void*>(&HkPresent), reinterpret_cast<void**>(&oPresent), "Present") &&
             HookSlot(scVt, kPresent1, reinterpret_cast<void*>(&HkPresent1), reinterpret_cast<void**>(&oPresent1), "Present1") &&
             HookSlot(scVt, kResizeBuffers, reinterpret_cast<void*>(&HkResizeBuffers), reinterpret_cast<void**>(&oResizeBuffers), "ResizeBuffers") &&
             HookSlot(scVt, kResizeBuffers1, reinterpret_cast<void*>(&HkResizeBuffers1), reinterpret_cast<void**>(&oResizeBuffers1), "ResizeBuffers1") &&
             HookSlot(qVt, kExecuteCommandLists, reinterpret_cast<void*>(&HkExecute), reinterpret_cast<void**>(&oExecute), "ExecuteCommandLists");
        if (ok) {
            HookSlot(fVt, kCreateSwapChain, reinterpret_cast<void*>(&HkCreateSwapChain), reinterpret_cast<void**>(&oCreateSwapChain), "CreateSwapChain");
            HookSlot(fVt, kCreateSwapChainForHwnd, reinterpret_cast<void*>(&HkCreateSwapChainForHwnd),
                     reinterpret_cast<void**>(&oCreateSwapChainForHwnd), "CreateSwapChainForHwnd");
        }
    } while (false);

    if (sc) sc->Release();
    if (queue) queue->Release();
    if (device) device->Release();
    if (factory) factory->Release();
    if (wnd) DestroyWindow(wnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    logx::Write("render: DX12 hooks %s", ok ? "installed" : "FAILED (menu cannot be shown)");
    return ok;
}

} // namespace render

#include "includes.hpp"

#if defined _M_X64
typedef uint64_t uintx_t;
#elif defined _M_IX86
typedef uint32_t uintx_t;
#endif

typedef HRESULT(APIENTRY* Present12)(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags);
Present12 oPresent = NULL;

typedef void(APIENTRY* ExecuteCommandLists)(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* ppCommandLists);
ExecuteCommandLists oExecuteCommandLists = NULL;

typedef HRESULT(APIENTRY* ResizeBuffers)(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
ResizeBuffers oResizeBuffers = NULL;

typedef void(__fastcall* tProcessEvent)(SDK::UObject*, SDK::UFunction*, void*);
tProcessEvent oProcessEvent = nullptr;

namespace Process {
    DWORD ID;
    HANDLE Handle;
    HWND Hwnd;
    HMODULE Module;
    WNDPROC WndProc;
    int WindowWidth;
    int WindowHeight;
}

namespace DirectX12Interface {
    ID3D12Device* Device = nullptr;
    ID3D12DescriptorHeap* DescriptorHeapBackBuffers;
    ID3D12DescriptorHeap* DescriptorHeapImGuiRender;
    ID3D12GraphicsCommandList* CommandList;
    ID3D12CommandQueue* CommandQueue;

    struct _FrameContext {
        ID3D12CommandAllocator* CommandAllocator;
        ID3D12Resource* Resource;
        D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHandle;
        UINT64 FenceValue;  // GPU fence value of the last submission that used this context's allocator
    };

    uintx_t BuffersCounts = -1;
    _FrameContext* FrameContext;

    // Single fence shared across frame contexts; each context records the value it was last
    // signaled with so we can wait for the GPU to finish before reusing its allocator.
    ID3D12Fence* Fence = nullptr;
    HANDLE FenceEvent = nullptr;
    UINT64 FenceLastSignaledValue = 0;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui::GetCurrentContext() && ImGui::GetIO().BackendPlatformUserData && ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
        return true;

    if (cfg->bMenuOpen && ImGui::GetCurrentContext() && ImGui::GetIO().BackendPlatformUserData && ImGui::GetIO().WantCaptureMouse)
    {
        switch (uMsg)
        {
        case WM_LBUTTONDOWN: case WM_LBUTTONUP:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP:
        case WM_XBUTTONDOWN: case WM_XBUTTONUP:
        case WM_MOUSEWHEEL:  case WM_MOUSEHWHEEL:
            return true;
        }
    }

    return CallWindowProc(Process::WndProc, hWnd, uMsg, wParam, lParam);
}

void __fastcall hkProcessEvent(SDK::UObject* pObject, SDK::UFunction* pFunction, void* pParms)
{
    if (cfg && cfg->bForceCharacterVisibility && g_OnRepBodyVisibilityFunc && pFunction == g_OnRepBodyVisibilityFunc)
    {
        // Force BodyVisibility = true before OnRep reads it, so the character appears visible
        static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_C*>(pObject)->BodyVisibility = true;
    }

    // g_KickOnline is most likely the only needed one,
    // but hook everything just to make sure we catch all the kick functions
    if (!g_KickLINK)
        g_KickLINK = SDK::ABP_FirstPersonCharacter_LINK_C::StaticClass()->GetFunction("BP_FirstPersonCharacter_LINK_C", "Kick");

    if (!g_KickOnline)
        g_KickOnline = SDK::ABP_FirstPersonPlayerState_Online_C::StaticClass()->GetFunction("BP_FirstPersonPlayerState_Online_C", "Kick");

    if (!g_ClientWasKicked)
        g_ClientWasKicked = SDK::APlayerController::StaticClass()->GetFunction("PlayerController", "ClientWasKicked");

    if (!g_ClientReturnToMainMenuWithTextReason)
        g_ClientReturnToMainMenuWithTextReason = SDK::APlayerController::StaticClass()->GetFunction("PlayerController", "ClientReturnToMainMenuWithTextReason");

    if (cfg && cfg->bPreventKick && (pFunction == g_KickLINK || pFunction == g_KickOnline || pFunction == g_ClientWasKicked || pFunction == g_ClientReturnToMainMenuWithTextReason))
    {
        std::cout << "[ProcessEvent] Prevented kick function: " << pFunction->GetFullName() << std::endl;
        return;
    }

    return oProcessEvent(pObject, pFunction, pParms);
}

bool init = false;
// Controls whether the hook is active. Set to false to disable the hook and restore the original Present() function.
static std::atomic<bool> bRunning(true);
// Guards Unload() so it runs exactly once - both the unload hotkey and
// DLL_PROCESS_DETACH funnel through it, and tearing down twice would
// double-disable MinHook and double-release the D3D12 resources.
static std::atomic<bool> bUnloaded(false);

HRESULT __stdcall hkPresent(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags)
{
    if (!bRunning) return oPresent(pSwapChain, SyncInterval, Flags);
    if (!init) {
        if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D12Device), (void**)&DirectX12Interface::Device))) {
            ImGui::CreateContext();

            ImGuiIO& io = ImGui::GetIO(); (void)io;
            ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantTextInput || ImGui::GetIO().WantCaptureKeyboard;
            io.IniFilename = NULL;
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableSetMousePos;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

            // Load Unicode fonts so non-ASCII player names (Chinese, Arabic, Cyrillic, etc.) render correctly.
            // Segoe UI ships on all Windows 10/11 and covers Latin, Cyrillic, Greek, Arabic, Hebrew, Thai, and more.
            // CJK fonts are merged in when present; AddFontFromFileTTF silently returns nullptr if the file is missing.
            {
                ImFontConfig cfg;
                cfg.OversampleH = 1;
                cfg.OversampleV = 1;

                if (io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 15.0f, &cfg, io.Fonts->GetGlyphRangesDefault()))
                {
                    cfg.MergeMode = true;
                    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 15.0f, &cfg, io.Fonts->GetGlyphRangesCyrillic());
                    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 15.0f, &cfg, io.Fonts->GetGlyphRangesGreek());
                    static const ImWchar arabic_ranges[] = { 0x0600, 0x06FF, 0xFB50, 0xFDFF, 0xFE70, 0xFEFF, 0 };
                    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 15.0f, &cfg, arabic_ranges);
                    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc",    15.0f, &cfg, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
                    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\meiryo.ttc",  15.0f, &cfg, io.Fonts->GetGlyphRangesJapanese());
                    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\malgun.ttf",  15.0f, &cfg, io.Fonts->GetGlyphRangesKorean());
                }
            }

            DXGI_SWAP_CHAIN_DESC Desc;
            pSwapChain->GetDesc(&Desc);
            Process::Hwnd = Desc.OutputWindow; // use the window the swapchain actually presents to, not GetForegroundWindow()'s guess
            Desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
            Desc.Windowed = ((GetWindowLongPtr(Process::Hwnd, GWL_STYLE) & WS_POPUP) != 0) ? false : true;

            DirectX12Interface::BuffersCounts = Desc.BufferCount;
            DirectX12Interface::FrameContext = new DirectX12Interface::_FrameContext[DirectX12Interface::BuffersCounts];

            D3D12_DESCRIPTOR_HEAP_DESC DescriptorImGuiRender = {};
            DescriptorImGuiRender.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            DescriptorImGuiRender.NumDescriptors = DirectX12Interface::BuffersCounts;
            DescriptorImGuiRender.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

            if (DirectX12Interface::Device->CreateDescriptorHeap(&DescriptorImGuiRender, IID_PPV_ARGS(&DirectX12Interface::DescriptorHeapImGuiRender)) != S_OK)
                return oPresent(pSwapChain, SyncInterval, Flags);

            // One command allocator per frame context. Sharing a single allocator and
            // resetting it every frame corrupts state while the GPU is still executing the
            // previous frame's commands; a per-context allocator gated by the fence below
            // is only reset once the GPU is provably done with it.
            for (size_t i = 0; i < DirectX12Interface::BuffersCounts; i++)
            {
                if (DirectX12Interface::Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&DirectX12Interface::FrameContext[i].CommandAllocator)) != S_OK)
                    return oPresent(pSwapChain, SyncInterval, Flags);
                DirectX12Interface::FrameContext[i].FenceValue = 0;
            }

            if (DirectX12Interface::Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, DirectX12Interface::FrameContext[0].CommandAllocator, NULL, IID_PPV_ARGS(&DirectX12Interface::CommandList)) != S_OK ||
                DirectX12Interface::CommandList->Close() != S_OK)
                return oPresent(pSwapChain, SyncInterval, Flags);

            // Fence + event survive resizes (init reruns), so guard creation to avoid leaking
            // one per resize. FenceLastSignaledValue keeps counting across re-inits.
            if (!DirectX12Interface::Fence)
            {
                if (DirectX12Interface::Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&DirectX12Interface::Fence)) != S_OK)
                    return oPresent(pSwapChain, SyncInterval, Flags);

                DirectX12Interface::FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
                if (!DirectX12Interface::FenceEvent)
                    return oPresent(pSwapChain, SyncInterval, Flags);
            }

            D3D12_DESCRIPTOR_HEAP_DESC DescriptorBackBuffers;
            DescriptorBackBuffers.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            DescriptorBackBuffers.NumDescriptors = DirectX12Interface::BuffersCounts;
            DescriptorBackBuffers.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            DescriptorBackBuffers.NodeMask = 1;

            if (DirectX12Interface::Device->CreateDescriptorHeap(&DescriptorBackBuffers, IID_PPV_ARGS(&DirectX12Interface::DescriptorHeapBackBuffers)) != S_OK)
            {
                return oPresent(pSwapChain, SyncInterval, Flags);
            }

            const auto RTVDescriptorSize = DirectX12Interface::Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            D3D12_CPU_DESCRIPTOR_HANDLE RTVHandle = DirectX12Interface::DescriptorHeapBackBuffers->GetCPUDescriptorHandleForHeapStart();

            for (size_t i = 0; i < DirectX12Interface::BuffersCounts; i++) {
                ID3D12Resource* pBackBuffer = nullptr;
                DirectX12Interface::FrameContext[i].DescriptorHandle = RTVHandle;
                pSwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
                DirectX12Interface::Device->CreateRenderTargetView(pBackBuffer, nullptr, RTVHandle);
                DirectX12Interface::FrameContext[i].Resource = pBackBuffer;
                RTVHandle.ptr += RTVDescriptorSize;
            }

            ImGui_ImplWin32_Init(Process::Hwnd);
            ImGui_ImplDX12_Init(DirectX12Interface::Device, DirectX12Interface::BuffersCounts, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX12Interface::DescriptorHeapImGuiRender, DirectX12Interface::DescriptorHeapImGuiRender->GetCPUDescriptorHandleForHeapStart(), DirectX12Interface::DescriptorHeapImGuiRender->GetGPUDescriptorHandleForHeapStart());
            ImGui_ImplDX12_CreateDeviceObjects();
            //ImGui::GetIO().ImeWindowHandle = Process::Hwnd;
			ImGui::GetMainViewport()->PlatformHandleRaw = Process::Hwnd;
            // Only hook WndProc once - on resize init reruns, Process::WndProc already holds
            // the original game proc. Hooking again would overwrite it with our own hook,
            // causing CallWindowProc -> WndProc -> CallWindowProc infinite recursion.
            if (!Process::WndProc)
                Process::WndProc = (WNDPROC)SetWindowLongPtr(Process::Hwnd, GWLP_WNDPROC, (__int3264)(LONG_PTR)WndProc);

            // Mark initialized only after every resource above was created - any failed step
            // returns early and leaves init false, so the next frame retries cleanly instead
            // of falling through to the render path with null D3D state.
            init = true;
        }
    }

    // Init hasn't completed (device not ready, or a create step failed this frame) - don't
    // run the render path against half-built state.
    if (!init)
        return oPresent(pSwapChain, SyncInterval, Flags);

    if (DirectX12Interface::CommandQueue == nullptr)
    {
		std::cout << "CommandQueue is nullptr!" << std::endl;
        return oPresent(pSwapChain, SyncInterval, Flags);
    }

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin(("##scene"), nullptr, ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoTitleBar);

    auto& io = ImGui::GetIO();
    ImGui::SetWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y), ImGuiCond_Always);

    if (cfg->bInitHooks)
        cheat->Init();

    if (GetAsyncKeyState(0x47) & 1) // G key
        cfg->bMagnetEnabled = !cfg->bMagnetEnabled;

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    if (GetAsyncKeyState(VK_INSERT) & 1)
        cfg->bMenuOpen = !cfg->bMenuOpen;

    ImGui::GetIO().MouseDrawCursor = cfg->bMenuOpen;

    if (cfg->bMenuOpen)
    {
        ImGui::StyleColorsDark();
        gui->Init();
    }

    ImGui::EndFrame();

    DirectX12Interface::_FrameContext& CurrentFrameContext = DirectX12Interface::FrameContext[pSwapChain->GetCurrentBackBufferIndex()];

    // Wait for the GPU to finish the last frame that used this context's allocator before
    // resetting it — resetting an allocator with in-flight commands is undefined behaviour.
    // Bounded wait so a wedged/removed GPU can't hard-hang the game's render thread.
    if (DirectX12Interface::Fence->GetCompletedValue() < CurrentFrameContext.FenceValue)
    {
        DirectX12Interface::Fence->SetEventOnCompletion(CurrentFrameContext.FenceValue, DirectX12Interface::FenceEvent);
        WaitForSingleObject(DirectX12Interface::FenceEvent, 2000);
    }

    CurrentFrameContext.CommandAllocator->Reset();

    D3D12_RESOURCE_BARRIER Barrier;
    Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    Barrier.Transition.pResource = CurrentFrameContext.Resource;
    Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

    DirectX12Interface::CommandList->Reset(CurrentFrameContext.CommandAllocator, nullptr);
    DirectX12Interface::CommandList->ResourceBarrier(1, &Barrier);
    DirectX12Interface::CommandList->OMSetRenderTargets(1, &CurrentFrameContext.DescriptorHandle, FALSE, nullptr);
    DirectX12Interface::CommandList->SetDescriptorHeaps(1, &DirectX12Interface::DescriptorHeapImGuiRender);

    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), DirectX12Interface::CommandList);
    Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    DirectX12Interface::CommandList->ResourceBarrier(1, &Barrier);
    DirectX12Interface::CommandList->Close();
    DirectX12Interface::CommandQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList* const*>(&DirectX12Interface::CommandList));

    // Signal the fence on the queue and record the value for this frame context, so next time
    // we cycle back to it we know when the GPU has finished this frame's commands.
    const UINT64 signalValue = ++DirectX12Interface::FenceLastSignaledValue;
    DirectX12Interface::CommandQueue->Signal(DirectX12Interface::Fence, signalValue);
    CurrentFrameContext.FenceValue = signalValue;

    return oPresent(pSwapChain, SyncInterval, Flags);
}

void __stdcall hkExecuteCommandLists(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* ppCommandLists)
{
    // Only capture the DIRECT (graphics) queue — the game also runs copy/compute queues, and
    // submitting our graphics command list to one of those would be an invalid submission and
    // remove the device. When the device is already known, also require the queue to belong to
    // it, so on a multi-adapter machine (iGPU + dGPU) we don't grab a DIRECT queue from the
    // wrong device. (Best effort: ExecuteCommandLists often fires before Present sets Device,
    // in which case we fall back to capturing the first DIRECT queue, as before.)
    if (!DirectX12Interface::CommandQueue && queue && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        bool sameDevice = true;
        if (DirectX12Interface::Device)
        {
            ID3D12Device* queueDevice = nullptr;
            if (SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&queueDevice))))
            {
                sameDevice = (queueDevice == DirectX12Interface::Device);
                queueDevice->Release();
            }
        }

        if (sameDevice)
            DirectX12Interface::CommandQueue = queue;
    }

    return oExecuteCommandLists(queue, NumCommandLists, ppCommandLists);
}

// Wait until the GPU has finished every frame we've submitted, reusing the persistent render
// fence, so the command list and per-frame allocators can be released without freeing memory
// the GPU is still reading. Bounded so a wedged GPU can't hang teardown forever.
static void WaitForGpuIdle()
{
    if (!DirectX12Interface::Fence || !DirectX12Interface::FenceEvent)
        return;

    const UINT64 value = DirectX12Interface::FenceLastSignaledValue;
    if (value != 0 && DirectX12Interface::Fence->GetCompletedValue() < value)
    {
        DirectX12Interface::Fence->SetEventOnCompletion(value, DirectX12Interface::FenceEvent);
        WaitForSingleObject(DirectX12Interface::FenceEvent, 1000);
    }
}

HRESULT __stdcall hkResizeBuffers(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    if (!bRunning) return oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

    if (init)
    {
        // Make sure the GPU is done with our command list / allocators before freeing them.
        WaitForGpuIdle();

        // Backends must be shut down before the context is destroyed
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        // The command list is recreated on the next hkPresent init, so drop the old one.
        if (DirectX12Interface::CommandList) {
            DirectX12Interface::CommandList->Release();
            DirectX12Interface::CommandList = nullptr;
        }

        // Release per-frame back buffer resources (DXGI requires all GetBuffer() references
        // dropped before ResizeBuffers) and the per-frame command allocators, which hkPresent
        // recreates after the resize.
        for (size_t i = 0; i < DirectX12Interface::BuffersCounts; i++)
        {
            if (DirectX12Interface::FrameContext[i].Resource) {
                DirectX12Interface::FrameContext[i].Resource->Release();
                DirectX12Interface::FrameContext[i].Resource = nullptr;
            }
            if (DirectX12Interface::FrameContext[i].CommandAllocator) {
                DirectX12Interface::FrameContext[i].CommandAllocator->Release();
                DirectX12Interface::FrameContext[i].CommandAllocator = nullptr;
            }
        }
        delete[] DirectX12Interface::FrameContext;
        DirectX12Interface::FrameContext = nullptr;

        if (DirectX12Interface::DescriptorHeapBackBuffers) {
            DirectX12Interface::DescriptorHeapBackBuffers->Release();
            DirectX12Interface::DescriptorHeapBackBuffers = nullptr;
        }

        if (DirectX12Interface::DescriptorHeapImGuiRender) {
            DirectX12Interface::DescriptorHeapImGuiRender->Release();
            DirectX12Interface::DescriptorHeapImGuiRender = nullptr;
        }

        // Device is NOT tied to swap chain buffers and must not be released here
        init = false;
    }

    return oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

void Unload()
{
    // Run the teardown exactly once, no matter how many paths call us.
    bool expected = false;
    if (!bUnloaded.compare_exchange_strong(expected, true))
        return;

    // Signal hooks to bail out immediately, then wait long enough for any call
    // that is already mid-execution on the render thread to return before we
    // start freeing shared state. MH_DisableHook only blocks future calls -
    // it cannot stop one that is already inside hkPresent/hkResizeBuffers.
    bRunning = false;
    Sleep(100);

    // Restore the original WndProc before this module is unmapped, otherwise the window
    // keeps a dangling pointer into our WndProc and crashes on the next message.
    if (Process::WndProc)
        SetWindowLongPtr(Process::Hwnd, GWLP_WNDPROC, (__int3264)(LONG_PTR)Process::WndProc);

    // MinHook
	MH_DisableHook(MH_ALL_HOOKS);
	MH_Uninitialize();

    // ImGui + D3D12 resources
    if (init)
    {
        // Make sure the GPU is done with our command list / allocators before freeing them.
        WaitForGpuIdle();

        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        if (DirectX12Interface::CommandList) {
            DirectX12Interface::CommandList->Release();
            DirectX12Interface::CommandList = nullptr;
        }

        // Release back buffer COM references so the game's ResizeBuffers can succeed
        // after re-injection. hkPresent re-acquires these after every resize, so they
        // must be explicitly released here — not doing so leaks the references. The
        // per-frame command allocators are owned by us too and need releasing.
        if (DirectX12Interface::FrameContext)
        {
            for (size_t i = 0; i < DirectX12Interface::BuffersCounts; i++)
            {
                if (DirectX12Interface::FrameContext[i].Resource)
                    DirectX12Interface::FrameContext[i].Resource->Release();
                if (DirectX12Interface::FrameContext[i].CommandAllocator)
                    DirectX12Interface::FrameContext[i].CommandAllocator->Release();
            }
            delete[] DirectX12Interface::FrameContext;
            DirectX12Interface::FrameContext = nullptr;
        }

        if (DirectX12Interface::DescriptorHeapBackBuffers)
        {
            DirectX12Interface::DescriptorHeapBackBuffers->Release();
            DirectX12Interface::DescriptorHeapBackBuffers = nullptr;
        }

        if (DirectX12Interface::DescriptorHeapImGuiRender)
        {
            DirectX12Interface::DescriptorHeapImGuiRender->Release();
            DirectX12Interface::DescriptorHeapImGuiRender = nullptr;
        }
    }

    // Fence + event persist across resizes (guarded creation), so release them once here.
    if (DirectX12Interface::Fence)
    {
        DirectX12Interface::Fence->Release();
        DirectX12Interface::Fence = nullptr;
    }
    if (DirectX12Interface::FenceEvent)
    {
        CloseHandle(DirectX12Interface::FenceEvent);
        DirectX12Interface::FenceEvent = nullptr;
    }

    // misc
    // Detach the CRT's stdio from the console before freeing it. While the
    // freopen_s'd CONOUT$/CONIN$ handles stay open the console host keeps the
    // window alive, so FreeConsole alone leaves it lingering after ejecting.
    FILE* Dummy;
    freopen_s(&Dummy, "NUL", "w", stdout);
    freopen_s(&Dummy, "NUL", "r", stdin);
    FreeConsole();
}

void InitProcess(bool *WindowFocus)
{
    DWORD ForegroundWindowProcessID;
    GetWindowThreadProcessId(GetForegroundWindow(), &ForegroundWindowProcessID);
    if (GetCurrentProcessId() == ForegroundWindowProcessID) {

        Process::ID = GetCurrentProcessId();
        Process::Handle = GetCurrentProcess();
        Process::Hwnd = GetForegroundWindow();

        RECT TempRect;
        GetWindowRect(Process::Hwnd, &TempRect);
        Process::WindowWidth = TempRect.right - TempRect.left;
        Process::WindowHeight = TempRect.bottom - TempRect.top;

        *WindowFocus = true;
    }
}

DWORD MainThread(HMODULE Module)
{
    /* Code to open a console window */
    AllocConsole();
    FILE* Dummy;
    freopen_s(&Dummy, "CONOUT$", "w", stdout);
    freopen_s(&Dummy, "CONIN$", "r", stdin);

    _mkdir("C:\\chameleonEsp");

	// Initialize global instances
    cfg = new Settings();
    if (!cfg) return 0;

    cheat = new CheatManager();
    if (!cheat) return 0;

    gui = new Menu();
    if (!gui) return 0;

    draw = new Drawings();
    if (!draw) return 0;

    cfg->LoadSettings();

	// Wait for the game window to be in focus before proceeding
    bool WindowFocus = false;
    while (WindowFocus == false)
    {
        InitProcess(&WindowFocus);
        Sleep(100);
    }

	if (MH_Initialize() != MH_OK)
	{
		std::cout << "Failed to initialize MinHook!" << std::endl;
		return 1;
	}
    
    kiero::D3D12Output d3d12;
    while(true)
    {
        auto err = kiero::locate<kiero::Implementation_D3D12>(nullptr, &d3d12);
        if (err == kiero::Error_Nil) {
			std::cout << "DirectX 12 interface located!" << std::endl;
            break;
        }
        Sleep(100);
    }

    void* tExecuteCommandLists = d3d12.command_queue_methods[10];
    void* tPresent = d3d12.swapchain_methods[8];
    void* tResizeBuffers = d3d12.swapchain_methods[13];

    if (MH_CreateHook(tExecuteCommandLists, hkExecuteCommandLists, (LPVOID*)&oExecuteCommandLists) != MH_OK) {
        std::cout << "Failed to hook ExecuteCommandLists!" << std::endl;
    }

    if (MH_CreateHook(tPresent, hkPresent, (LPVOID*)&oPresent) != MH_OK) {
        std::cout << "Failed to hook Present!" << std::endl;
    }

    if (MH_CreateHook(tResizeBuffers, hkResizeBuffers, (LPVOID*)&oResizeBuffers) != MH_OK) {
        std::cout << "Failed to hook ResizeBuffers!" << std::endl;
    }

    void* tProcessEvent = reinterpret_cast<void*>(SDK::InSDKUtils::GetImageBase() + SDK::Offsets::ProcessEvent);
    if (MH_CreateHook(tProcessEvent, hkProcessEvent, (LPVOID*)&oProcessEvent) != MH_OK) {
        std::cout << "Failed to hook ProcessEvent!" << std::endl;
    }

    MH_EnableHook(MH_ALL_HOOKS);

	// poll for the END key to be pressed to unload the DLL
    while (bRunning)
    {
        if (GetAsyncKeyState(VK_END) & 1)
            break;
        Sleep(50);
    }

    Unload();
    FreeLibraryAndExitThread(Process::Module, 0);

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        Process::Module = hModule;
        CreateThread(0, 0, (LPTHREAD_START_ROUTINE)MainThread, hModule, 0, 0);
        break;
	case DLL_PROCESS_DETACH:
		Unload();
		break;
    }

    return TRUE;
}

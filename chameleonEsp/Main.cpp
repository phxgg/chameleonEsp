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
    LPCSTR Title;
    LPCSTR ClassName;
    LPCSTR Path;
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
    };

    uintx_t BuffersCounts = -1;
    _FrameContext* FrameContext;
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
    return oProcessEvent(pObject, pFunction, pParms);
}

bool init = false;
static std::atomic<bool> bRunning(true);

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

            ID3D12CommandAllocator* Allocator;
            if (DirectX12Interface::Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&Allocator)) != S_OK)
                return oPresent(pSwapChain, SyncInterval, Flags);

            for (size_t i = 0; i < DirectX12Interface::BuffersCounts; i++)
            {
                DirectX12Interface::FrameContext[i].CommandAllocator = Allocator;
            }

            if (DirectX12Interface::Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator, NULL, IID_PPV_ARGS(&DirectX12Interface::CommandList)) != S_OK ||
                DirectX12Interface::CommandList->Close() != S_OK)
                return oPresent(pSwapChain, SyncInterval, Flags);

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
            // Only hook WndProc once — on resize init reruns, Process::WndProc already holds
            // the original game proc. Hooking again would overwrite it with our own hook,
            // causing CallWindowProc -> WndProc -> CallWindowProc infinite recursion.
            if (!Process::WndProc)
                Process::WndProc = (WNDPROC)SetWindowLongPtr(Process::Hwnd, GWLP_WNDPROC, (__int3264)(LONG_PTR)WndProc);
        }
        init = true;
    }

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

    return oPresent(pSwapChain, SyncInterval, Flags);
}

void __stdcall hkExecuteCommandLists(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* ppCommandLists)
{
    if (!DirectX12Interface::CommandQueue)
    {
        DirectX12Interface::CommandQueue = queue;
    }

    return oExecuteCommandLists(queue, NumCommandLists, ppCommandLists);
}

HRESULT __stdcall hkResizeBuffers(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    if (!bRunning) return oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

    if (init)
    {
        // Backends must be shut down before the context is destroyed
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        // Release per-frame back buffer resources — DXGI requires all GetBuffer()
        // references to be dropped before ResizeBuffers is called
        for (size_t i = 0; i < DirectX12Interface::BuffersCounts; i++)
        {
            if (DirectX12Interface::FrameContext[i].Resource) {
                DirectX12Interface::FrameContext[i].Resource->Release();
                DirectX12Interface::FrameContext[i].Resource = nullptr;
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
    // Signal hooks to bail out immediately, then wait long enough for any call
    // that is already mid-execution on the render thread to return before we
    // start freeing shared state. MH_DisableHook only blocks future calls —
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
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        // Release back buffer COM references so the game's ResizeBuffers can succeed
        // after re-injection. hkPresent re-acquires these after every resize, so they
        // must be explicitly released here — not doing so leaks the references.
        if (DirectX12Interface::FrameContext)
        {
            for (size_t i = 0; i < DirectX12Interface::BuffersCounts; i++)
            {
                if (DirectX12Interface::FrameContext[i].Resource)
                    DirectX12Interface::FrameContext[i].Resource->Release();
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

    // misc
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

        char TempTitle[MAX_PATH];
        GetWindowTextA(Process::Hwnd, TempTitle, sizeof(TempTitle));
        Process::Title = TempTitle;

        char TempClassName[MAX_PATH];
        GetClassNameA(Process::Hwnd, TempClassName, sizeof(TempClassName));
        Process::ClassName = TempClassName;

        char TempPath[MAX_PATH];
        GetModuleFileNameExA(Process::Handle, NULL, TempPath, sizeof(TempPath));
        Process::Path = TempPath;

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

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        Process::Module = hModule;
        CreateThread(0, 0, (LPTHREAD_START_ROUTINE)MainThread, hModule, 0, 0);
        break;
	case DLL_PROCESS_DETACH:
		Unload();
		break;
    }

    return TRUE;
}

//D3D12 Methods Table:
//[0]   QueryInterface
//[1]   AddRef
//[2]   Release
//[3]   GetPrivateData
//[4]   SetPrivateData
//[5]   SetPrivateDataInterface
//[6]   SetName
//[7]   GetNodeCount
//[8]   CreateCommandQueue
//[9]   CreateCommandAllocator
//[10]  CreateGraphicsPipelineState
//[11]  CreateComputePipelineState
//[12]  CreateCommandList
//[13]  CheckFeatureSupport
//[14]  CreateDescriptorHeap
//[15]  GetDescriptorHandleIncrementSize
//[16]  CreateRootSignature
//[17]  CreateConstantBufferView
//[18]  CreateShaderResourceView
//[19]  CreateUnorderedAccessView
//[20]  CreateRenderTargetView
//[21]  CreateDepthStencilView
//[22]  CreateSampler
//[23]  CopyDescriptors
//[24]  CopyDescriptorsSimple
//[25]  GetResourceAllocationInfo
//[26]  GetCustomHeapProperties
//[27]  CreateCommittedResource
//[28]  CreateHeap
//[29]  CreatePlacedResource
//[30]  CreateReservedResource
//[31]  CreateSharedHandle
//[32]  OpenSharedHandle
//[33]  OpenSharedHandleByName
//[34]  MakeResident
//[35]  Evict
//[36]  CreateFence
//[37]  GetDeviceRemovedReason
//[38]  GetCopyableFootprints
//[39]  CreateQueryHeap
//[40]  SetStablePowerState
//[41]  CreateCommandSignature
//[42]  GetResourceTiling
//[43]  GetAdapterLuid
//[44]  QueryInterface
//[45]  AddRef
//[46]  Release
//[47]  GetPrivateData
//[48]  SetPrivateData
//[49]  SetPrivateDataInterface
//[50]  SetName
//[51]  GetDevice
//[52]  UpdateTileMappings
//[53]  CopyTileMappings
//[54]  ExecuteCommandLists
//[55]  SetMarker
//[56]  BeginEvent
//[57]  EndEvent
//[58]  Signal
//[59]  Wait
//[60]  GetTimestampFrequency
//[61]  GetClockCalibration
//[62]  GetDesc
//[63]  QueryInterface
//[64]  AddRef
//[65]  Release
//[66]  GetPrivateData
//[67]  SetPrivateData
//[68]  SetPrivateDataInterface
//[69]  SetName
//[70]  GetDevice
//[71]  Reset
//[72]  QueryInterface
//[73]  AddRef
//[74]  Release
//[75]  GetPrivateData
//[76]  SetPrivateData
//[77]  SetPrivateDataInterface
//[78]  SetName
//[79]  GetDevice
//[80]  GetType
//[81]  Close
//[82]  Reset
//[83]  ClearState
//[84]  DrawInstanced
//[85]  DrawIndexedInstanced
//[86]  Dispatch
//[87]  CopyBufferRegion
//[88]  CopyTextureRegion
//[89]  CopyResource
//[90]  CopyTiles
//[91]  ResolveSubresource
//[92]  IASetPrimitiveTopology
//[93]  RSSetViewports
//[94]  RSSetScissorRects
//[95]  OMSetBlendFactor
//[96]  OMSetStencilRef
//[97]  SetPipelineState
//[98]  ResourceBarrier
//[99]  ExecuteBundle
//[100] SetDescriptorHeaps
//[101] SetComputeRootSignature
//[102] SetGraphicsRootSignature
//[103] SetComputeRootDescriptorTable
//[104] SetGraphicsRootDescriptorTable
//[105] SetComputeRoot32BitConstant
//[106] SetGraphicsRoot32BitConstant
//[107] SetComputeRoot32BitConstants
//[108] SetGraphicsRoot32BitConstants
//[109] SetComputeRootConstantBufferView
//[110] SetGraphicsRootConstantBufferView
//[111] SetComputeRootShaderResourceView
//[112] SetGraphicsRootShaderResourceView
//[113] SetComputeRootUnorderedAccessView
//[114] SetGraphicsRootUnorderedAccessView
//[115] IASetIndexBuffer
//[116] IASetVertexBuffers
//[117] SOSetTargets
//[118] OMSetRenderTargets
//[119] ClearDepthStencilView
//[120] ClearRenderTargetView
//[121] ClearUnorderedAccessViewUint
//[122] ClearUnorderedAccessViewFloat
//[123] DiscardResource
//[124] BeginQuery
//[125] EndQuery
//[126] ResolveQueryData
//[127] SetPredication
//[128] SetMarker
//[129] BeginEvent
//[130] EndEvent
//[131] ExecuteIndirect
//[132] QueryInterface
//[133] AddRef
//[134] Release
//[135] SetPrivateData
//[136] SetPrivateDataInterface
//[137] GetPrivateData
//[138] GetParent
//[139] GetDevice
//[140] Present
//[141] GetBuffer
//[142] SetFullscreenState
//[143] GetFullscreenState
//[144] GetDesc
//[145] ResizeBuffers
//[146] ResizeTarget
//[147] GetContainingOutput
//[148] GetFrameStatistics
//[149] GetLastPresentCount
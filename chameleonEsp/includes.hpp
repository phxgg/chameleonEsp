#define _CRT_SECURE_NO_WARNINGS

#include <Windows.h>
#include <atomic>
#include <iostream>
#include <vector>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <direct.h>
#include <Psapi.h>
#include <d3d12.h>
#include <dxgi1_5.h>

#pragma comment(lib, "dxgi.lib")

#include "minhook/include/MinHook.h"
#include "kiero/kiero.hpp"
#include "kiero/kiero_d3d12.hpp"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx12.h"

#include "SDK/Engine_classes.hpp"
#include "SDK/BP_FirstPersonCharacter_LINK_classes.hpp"
#include "SDK/BP_FirstPersonPlayerState_Online_classes.hpp"
#include "SDK/BP_FirstPersonCharacter_Main_classes.hpp"
#include "SDK/BP_FirstPersonCharacter_cLeon_Character_classes.hpp"
#include "SDK/BP_FirstPersonPlayerState_Online_classes.hpp"
#include "SDK/BP_FirstPersonCharacter_cLeon_Character_Hunter_classes.hpp"
#include "SDK/BP_FirstPersonCharacter_cLeon_Character_Survivor_classes.hpp"
#include "skeleton.hpp"
#include "CheatManager.hpp"
#include "Menu.hpp"
#include "Settings.hpp"
#include "Drawings.hpp"

// Main global variables
inline CheatManager* cheat;
inline Menu* gui;
inline Settings* cfg;
inline FILE* file;
inline Drawings* draw;

// Function pointers for event handling
inline SDK::UFunction* g_OnRepBodyVisibilityFunc = nullptr;
inline SDK::UFunction* g_KickLINK = nullptr;
inline SDK::UFunction* g_KickOnline = nullptr;
inline SDK::UFunction* g_ClientWasKicked = nullptr;
inline SDK::UFunction* g_ClientReturnToMainMenuWithTextReason = nullptr;

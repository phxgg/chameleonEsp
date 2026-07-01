#pragma once

#define _CRT_SECURE_NO_WARNINGS

#include <Windows.h>
#include <tlhelp32.h>
#include <atomic>
#include <iostream>
#include <vector>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include <mutex>
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
#include "SDK/BP_FirstPersonCharacter_Main_classes.hpp"
#include "SDK/BP_FirstPersonPlayerState_Online_classes.hpp"
#include "SDK/BP_FirstPersonPlayerState_Online_parameters.hpp"
#include "SDK/BP_FirstPersonPlayerState_Online_cLeon_classes.hpp"
#include "SDK/BP_FirstPersonCharacter_cLeon_Character_classes.hpp"
#include "SDK/BP_FirstPersonCharacter_cLeon_Character_Hunter_classes.hpp"
#include "SDK/BP_FirstPersonCharacter_cLeon_Character_Hunter_parameters.hpp"
#include "SDK/BP_FirstPersonCharacter_cLeon_Character_Survivor_classes.hpp"
#include "SDK/BP_cLeonDecoy_Base_classes.hpp"
#include "skeleton.hpp"
#include "CheatManager.hpp"
#include "Menu.hpp"
#include "Settings.hpp"
#include "Drawings.hpp"

// Set by the GatherGuard in hkProcessEvent for the duration of CheatManager::Init(). Init's many SDK
// calls - the world reads and the inline game-state mutations - internally call UObject::ProcessEvent,
// which re-enters hkProcessEvent on this same (game) thread. This flag lets the hook recognise those
// nested, self invoked calls and pass them straight through to the engine.
inline std::atomic<bool> g_inGameThreadFlush{ false };

// Set by the render thread (hkPresent) once per frame to ask the game thread to refresh the ESP
// snapshot. The game thread (hkProcessEvent) consumes it with exchange(false) and runs the world
// scan there - never on the render thread, which would race the engine's actor list and fault.
inline std::atomic<bool> g_gatherRequested{ false };

// Main global variables
inline CheatManager* cheat;
inline Menu* gui;
inline Settings* cfg;
inline FILE* file;
inline Drawings* draw;

// Function pointers for event handling
inline SDK::UFunction* g_fnKickLINK = nullptr;
inline SDK::UFunction* g_fnKickOnline = nullptr;
inline SDK::UFunction* g_fnClientWasKicked = nullptr;
inline SDK::UFunction* g_fnClientReturnToMainMenuWithTextReason = nullptr;

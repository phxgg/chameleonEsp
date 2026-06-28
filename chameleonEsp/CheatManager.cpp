#include "includes.hpp"

#pragma warning (disable: 4244)

// GAME THREAD: scan the world and publish a render-ready snapshot.
//
// Everything that touches a live UObject - GetAllActorsOfClass, the per-actor reads, the screen
// projections - happens here, on the thread the engine actually mutates the actor list from. Doing
// it on the render thread (the old behaviour, called straight from hkPresent) raced the game thread
// during round/level transitions and faulted deep inside the engine's GetAllActorsOfClass. The
// render thread now only draws the snapshot we publish at the end (see RenderEsp).
//
// All scan state lives in locals (ctx, actor/baseClass) threaded through the helpers, not in
// members, so a single invocation owns everything it touches. The game-thread pinning in
// hkProcessEvent guarantees only one thread ever runs this, and the GatherGuard there blocks
// re-entry; the local-state design keeps that ownership explicit rather than relying on it.
void CheatManager::Init()
{
	EspSnapshot snap;

	FrameContext ctx;
	if (!ResolveContext(ctx))
	{
		// Publish an empty snapshot so the overlay clears (rather than freezing on the last frame)
		// while we have no valid world/player - main menu, loading screen, level transition.
		std::lock_guard<std::mutex> lock(snapshotMutex);
		pendingSnapshot = std::move(snap);
		return;
	}

	snap.screenX = static_cast<float>(ctx.screenX);
	snap.screenY = static_cast<float>(ctx.screenY);

	const auto MyLocation = ctx.MyPlayer->K2_GetActorLocation();

	ctx.PlayerController->FOV(cfg->bFovChanger ? cfg->fFovValue : 90); // fov changer

	// get players
	SDK::TArray<SDK::AActor*> Players;
	ctx.GStatics->GetAllActorsOfClass(ctx.World, SDK::ABP_FirstPersonCharacter_cLeon_Character_C::StaticClass(), &Players);

	// Track which actors exist this frame so we can drop stale entries from the latched
	// dead set below - otherwise a destroyed corpse's pointer could later be reused by a
	// live actor and wrongly suppress its ESP.
	std::unordered_set<SDK::AActor*> currentActors;

	for (int i = 0; i < Players.Num(); i++)
	{
		if (!Players.IsValidIndex(i)) continue;

		SDK::AActor* actor = Players[i];
		if (!actor || !IsObjectValid(actor)) continue;
		auto* baseClass = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_C*>(actor);
		if (!baseClass) continue;

		currentActors.insert(actor);

		// Skip dead/ragdolled corpses (see IsDead for why the obvious flags don't work).
		if (IsDead(actor))
			continue;

		const auto Location = baseClass->K2_GetActorLocation();
		const std::string PlayerName = ResolvePlayerName(actor, baseClass);
		const bool IsVisible = ctx.PlayerController->LineOfSightTo(actor, { 0,0,0 }, false); // visible check

		if (actor == ctx.MyPlayer)
		{
			// Hunter
			if (IsHunter(baseClass))
			{
				auto* hunter = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_Hunter_C*>(baseClass);

				if (!hunter)
					continue;

				if (cfg->bNoGunCooldown && IsObjectValid(hunter))
					hunter->GunCoolTime = 0.0;

				if (cfg->bMagnetEnabled)
					HandleMagnet(ctx.MyPlayer, actor, currentActors, MyLocation, Players, snap);
			}
			// Survivor
			else if (IsSurvivor(baseClass))
			{
				auto* survivor = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_Survivor_C*>(baseClass);

				if (!survivor)
					continue;

				if (cfg->bAntiDetection && IsObjectValid(survivor))
					survivor->OverlapCheckCapsules.Clear();
			}
			continue;
		}

		snap.players.push_back({ PlayerName, Location, actor, IsSurvivor(baseClass) });

		UpdateForcedVisibility(actor, baseClass);

		if (cfg->bDumpBones) {
			DumpBones(baseClass);
			cfg->bDumpBones = false;
		}

		if (cfg->bEnemyOnly && !IsEnemy(ctx.MyPlayer, baseClass))
			continue;

		EspEntry entry;
		BuildEspEntry(ctx.PlayerController, baseClass, entry, PlayerName, Location, MyLocation, IsVisible);
		snap.entries.push_back(std::move(entry));
	}

	// Drop dead-latch entries for actors that no longer exist (round restart, corpse despawn),
	// keeping the set bounded and preventing pointer reuse from suppressing a live actor's ESP.
	for (auto it = deadActors.begin(); it != deadActors.end(); )
	{
		if (currentActors.count(*it))
			++it;
		else
			it = deadActors.erase(it);
	}

	HandleTeleport(ctx.MyPlayer, currentActors);
	HandleKillTarget(ctx.MyPlayer, currentActors);
	HandleKillAllSurvivors(ctx.MyPlayer, currentActors);
	HandleChangeName(ctx.MyPlayer);

	// Publish the finished frame for the render thread to draw.
	std::lock_guard<std::mutex> lock(snapshotMutex);
	pendingSnapshot = std::move(snap);
}

// RENDER THREAD: draw the most recently published snapshot. Touches only ImGui and the snapshot
// copy - never a live UObject - so it cannot race the game thread's actor list. The game thread
// fills pendingSnapshot in Init(); we copy it out under the lock and render from our private copy.
void CheatManager::RenderEsp()
{
	{
		std::lock_guard<std::mutex> lock(snapshotMutex);
		drawSnapshot = pendingSnapshot;
	}

	// Hand the menu its teleport list (the same render thread reads PlayerInfos right after this).
	PlayerInfos = drawSnapshot.players;

	for (const auto& entry : drawSnapshot.entries)
		DrawEntry(entry);

	if (drawSnapshot.magnetActive)
	{
		const float redColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
		const ImU32 colRed = ImGui::ColorConvertFloat4ToU32(*(ImVec4*)redColor);

		const char* magnetText = "MAGNET ACTIVE";
		const ImVec2 textSize = ImGui::CalcTextSize(magnetText);
		const float textX = (drawSnapshot.screenX / 2.0f) - (textSize.x / 2.0f);
		const float textY = drawSnapshot.screenY - 30.0f;

		ImGui::GetForegroundDrawList()->AddText(ImVec2(textX, textY), colRed, magnetText);
	}
}

// Walk the world -> game instance -> local player -> controller -> pawn chain, plus the
// gameplay statics, into ctx. Any null link aborts the whole frame.
bool CheatManager::ResolveContext(FrameContext& ctx)
{
	SDK::UWorld* world = SDK::UWorld::GetWorld();
	if (!world) return false;

	SDK::UGameInstance* gameInstance = world->OwningGameInstance;
	if (!gameInstance) return false;

	if (gameInstance->LocalPlayers.Num() <= 0) return false;
	SDK::ULocalPlayer* localPlayer = gameInstance->LocalPlayers[0];
	if (!localPlayer) return false;

	if (!localPlayer->ViewportClient) return false;

	SDK::APlayerController* playerController = localPlayer->PlayerController;
	if (!playerController) return false;

	playerController->GetViewportSize(&ctx.screenX, &ctx.screenY);

	SDK::APawn* myPlayer = playerController->K2_GetPawn();
	if (!myPlayer) return false;

	auto* gStatics = (SDK::UGameplayStatics*)SDK::UGameplayStatics::StaticClass();
	if (!gStatics) return false;

	// Resolved purely as a readiness guard - the scan doesn't use it, but a null here means the
	// engine's kismet libraries aren't up yet, so we treat the world as not ready.
	auto* mathLib = (SDK::UKismetMathLibrary*)SDK::UKismetMathLibrary::StaticClass();
	if (!mathLib) return false;

	ctx.World = world;
	ctx.PlayerController = playerController;
	ctx.MyPlayer = myPlayer;
	ctx.GStatics = gStatics;
	return true;
}

// Resolve the display name for the current actor, falling back to the last-known cached name.
std::string CheatManager::ResolvePlayerName(SDK::AActor* actor, SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass)
{
	// PlayerState replicates as its own actor, independently of the pawn, so on clients its
	// pointer routinely blips to null for a frame or two even while the pawn is fully valid.
	// Don't drop the whole ESP over that - just fall back to the last name we saw for this actor.
	if (!baseClass->PlayerState)
	{
		auto it = playerNameCache.find(actor);
		return it != playerNameCache.end() ? it->second : "Unknown";
	}

	// Prefer the custom in-game name (CustomPlayerName) over the platform/Steam name
	// (PlayerNamePrivate). Guard the cast with IsA in case a non-Online PlayerState shows up,
	// and fall back to the Steam name if the custom name hasn't replicated in yet.
	SDK::FString* Name = &baseClass->PlayerState->PlayerNamePrivate;
	if (baseClass->PlayerState->IsA(SDK::ABP_FirstPersonPlayerState_Online_C::StaticClass()))
	{
		auto* ps = static_cast<SDK::ABP_FirstPersonPlayerState_Online_C*>(baseClass->PlayerState);
		if (ps->CustomPlayerName.IsValid())
			Name = &ps->CustomPlayerName;
	}

	if (Name->IsValid())
	{
		std::string PlayerName = Name->ToString();
		playerNameCache[actor] = PlayerName; // remember it for the null windows
		return PlayerName;
	}

	return "Unknown";
}

// Force the current actor's body visibility on/off, tracking who we touched so they can be restored.
void CheatManager::UpdateForcedVisibility(SDK::AActor* actor, SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass)
{
	if (cfg->bForceCharacterVisibility && !baseClass->BodyVisibility)
	{
		if (IsObjectValid(baseClass))
		{
			// Resolve the function fresh from the object's current class at call time and invoke it
			// directly, instead of the SDK wrapper's cached-once static which dangles after a round.
			SDK::UFunction* fn = baseClass->Class->GetFunction("BP_FirstPersonCharacter_cLeon_Character_C", "OnRep_BodyVisibility");
			if (fn)
			{
				baseClass->BodyVisibility = true;
				baseClass->ProcessEvent(fn, nullptr);
			}
		}
		forcedVisibleActors.insert(actor);
	}
	else if (!cfg->bForceCharacterVisibility && forcedVisibleActors.count(actor))
	{
		if (IsObjectValid(baseClass))
		{
			SDK::UFunction* fn = baseClass->Class->GetFunction("BP_FirstPersonCharacter_cLeon_Character_C", "OnRep_BodyVisibility");
			if (fn)
			{
				baseClass->BodyVisibility = false;
				baseClass->ProcessEvent(fn, nullptr);
			}
		}
		forcedVisibleActors.erase(actor);
	}
}

// True when the current actor (obj/BaseClass) should be treated as a dead corpse and skipped.
//
// We can't use the obvious signals: the raw `Dead` field isn't replicated (stays 0 on remote
// corpses), IsLive() returns true for dead bodies in infection, and BodyVisibility is reserved
// for the Force Character Visibility feature (which reveals stealthed/invisible survivors), so it
// can't double as a death flag.
//
// Instead detect death by ragdoll: a live character - survivor or hunter - is animation-driven, so
// its mesh isn't simulating physics; only a dead body ragdolls (confirmed by logging: live = 0,
// corpse = 1). That flag is transient though - in infection the game hides the corpse and resets
// the ragdoll, flipping it back to 0 while the player is still dead - so we latch it: once an actor
// has ragdolled it stays dead for as long as it exists. The latch set is pruned to live actors back
// in Init() to avoid stale-pointer reuse.
bool CheatManager::IsDead(SDK::AActor* actor)
{
	if (!actor) return false;
	auto* baseClass = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_C*>(actor);
	if (!baseClass || !baseClass->Mesh) return false;

	if (baseClass->Mesh && IsObjectValid(baseClass->Mesh) && baseClass->Mesh->IsAnySimulatingPhysics())
		deadActors.insert(actor);
	return deadActors.count(actor) > 0;
}

bool CheatManager::IsSurvivor(SDK::AActor* actor)
{
	if (!actor) return false;
	auto* baseClass = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_C*>(actor);
	if (!baseClass) return false;
	return baseClass->IsA(SDK::ABP_FirstPersonCharacter_cLeon_Character_Survivor_C::StaticClass());
}

bool CheatManager::IsSurvivor(SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass)
{
	if (!baseClass) return false;
	return baseClass->IsA(SDK::ABP_FirstPersonCharacter_cLeon_Character_Survivor_C::StaticClass());
}

bool CheatManager::IsHunter(SDK::AActor* actor)
{
	if (!actor) return false;
	auto* baseClass = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_C*>(actor);
	if (!baseClass) return false;
	return baseClass->IsA(SDK::ABP_FirstPersonCharacter_cLeon_Character_Hunter_C::StaticClass());
}

bool CheatManager::IsHunter(SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass)
{
	if (!baseClass) return false;
	return baseClass->IsA(SDK::ABP_FirstPersonCharacter_cLeon_Character_Hunter_C::StaticClass());
}

// True when the given actor is on the opposing team (survivor vs. hunter) from us.
bool CheatManager::IsEnemy(SDK::APawn* myPlayer, SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass)
{
	if (!myPlayer->IsA(SDK::ABP_FirstPersonCharacter_cLeon_Character_C::StaticClass()))
		return false;
	auto* MyChar = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_C*>(myPlayer);
	return MyChar->IsHunter != baseClass->IsHunter;
}

// GAME THREAD: project the current actor's skeleton (bone-pair segments) into screen space for the
// render thread to draw later. Each projection is an SDK call, so it has to happen here.
void CheatManager::BuildSkeletonLines(SDK::APlayerController* pc, SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass, std::vector<std::pair<SDK::FVector2D, SDK::FVector2D>>& out)
{
	if (!baseClass || !baseClass->Mesh || !IsObjectValid(baseClass->Mesh))
		return;

	SDK::FVector2D BoneScreen, PrevBoneScreen;
	for (const std::pair<int, int>& Connection : skeleton::Connections)
	{
		const auto BoneLoc1 = baseClass->Mesh->GetSocketLocation(baseClass->Mesh->GetBoneName(Connection.first));
		const auto BoneLoc2 = baseClass->Mesh->GetSocketLocation(baseClass->Mesh->GetBoneName(Connection.second));
		if (pc->ProjectWorldLocationToScreen(BoneLoc1, &BoneScreen, false) && pc->ProjectWorldLocationToScreen(BoneLoc2, &PrevBoneScreen, false))
			out.emplace_back(BoneScreen, PrevBoneScreen);
	}
}

// Build a 2D bounding box from every bone's screen position so it stays correct in any pose
// (crouch, prone, etc.). Returns false when no bone projected on-screen.
bool CheatManager::ComputeBoundingBox(SDK::APlayerController* pc, SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass, SDK::FVector2D& BoxMin, SDK::FVector2D& BoxMax)
{
	bool bHasBox = false;
	for (int BoneIdx = skeleton::amm; BoneIdx < skeleton::None; BoneIdx++)
	{
		const auto BoneLoc = baseClass->Mesh->GetSocketLocation(baseClass->Mesh->GetBoneName(BoneIdx));

		SDK::FVector2D BoneScreenPos;
		if (!pc->ProjectWorldLocationToScreen(BoneLoc, &BoneScreenPos, false))
			continue;

		if (!bHasBox)
		{
			BoxMin = BoxMax = BoneScreenPos;
			bHasBox = true;
			continue;
		}

		if (BoneScreenPos.X < BoxMin.X) BoxMin.X = BoneScreenPos.X;
		if (BoneScreenPos.Y < BoxMin.Y) BoxMin.Y = BoneScreenPos.Y;
		if (BoneScreenPos.X > BoxMax.X) BoxMax.X = BoneScreenPos.X;
		if (BoneScreenPos.Y > BoxMax.Y) BoxMax.Y = BoneScreenPos.Y;
	}
	return bHasBox;
}

// GAME THREAD: project the current actor's world state (role, distance, skeleton, box, snapline)
// into a render-ready EspEntry. All SDK/UObject access for one player's overlay lives here.
void CheatManager::BuildEspEntry(SDK::APlayerController* pc, SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass, EspEntry& entry, const std::string& PlayerName, SDK::FVector Location, SDK::FVector MyLocation, bool IsVisible)
{
	entry.name = PlayerName;
	entry.isVisible = IsVisible;
	entry.role = IsHunter(baseClass) ? 1 : (IsSurvivor(baseClass) ? 2 : 0);
	entry.distanceMeters = MyLocation.GetDistanceToInMeters(Location);

	if (baseClass && baseClass->Mesh && IsObjectValid(baseClass->Mesh))
	{
		if (cfg->bSkeleton)
			BuildSkeletonLines(pc, baseClass, entry.skeletonLines);

		entry.hasBox = ComputeBoundingBox(pc, baseClass, entry.boxMin, entry.boxMax);
	}

	// snapline target: the player's world location projected to screen
	if (cfg->bLines)
	{
		SDK::FVector2D Screen;
		if (pc->ProjectWorldLocationToScreen(Location, &Screen, false))
		{
			entry.hasSnapline = true;
			entry.snaplineScreen = Screen;
		}
	}
}

// RENDER THREAD: draw the enabled ESP overlays (skeleton, box, name, role, distance, snapline) for
// one prebuilt entry. ImGui only - no SDK calls, no UObject access (everything was resolved in
// BuildEspEntry on the game thread).
void CheatManager::DrawEntry(const EspEntry& entry)
{
	const ImU32 colEsp  = ImGui::ColorConvertFloat4ToU32(entry.isVisible ? *(ImVec4*)cfg->colVisible : *(ImVec4*)cfg->colNotVisible);
	const ImU32 colLine = ImGui::ColorConvertFloat4ToU32(*(ImVec4*)cfg->colLines);

	// white color
	const float fff[4] = { 1.0f,  1.0f,  1.0f, 1.0f };
	const ImU32 colWhite = ImGui::ColorConvertFloat4ToU32(*(ImVec4*)fff);

	if (cfg->bSkeleton)
		for (const auto& seg : entry.skeletonLines)
			ImGui::GetForegroundDrawList()->AddLine(ImVec2(seg.first.X, seg.first.Y), ImVec2(seg.second.X, seg.second.Y), colEsp, 1.0f);

	if (entry.hasBox)
	{
		if (cfg->bNames)
			ImGui::GetForegroundDrawList()->AddText(ImVec2(entry.boxMin.X, entry.boxMin.Y - 15), colEsp, entry.name.c_str());

		if (cfg->bRoles)
		{
			const char* roleText = entry.role == 1 ? "Hunter" : (entry.role == 2 ? "Survivor" : nullptr);
			if (roleText)
			{
				const float nameWidth = cfg->bNames ? ImGui::CalcTextSize(entry.name.c_str()).x + 5 : 0.0f;
				ImGui::GetForegroundDrawList()->AddText(ImVec2(entry.boxMin.X + nameWidth, entry.boxMin.Y - 15), colWhite, roleText);
			}
		}

		if (cfg->bBox)
			draw->DrawBox(entry.boxMin.X, entry.boxMin.Y, entry.boxMax.X - entry.boxMin.X, entry.boxMax.Y - entry.boxMin.Y, colEsp, 1.0f);

		if (cfg->bDistance)
		{
			char DistanceText[32];
			snprintf(DistanceText, sizeof(DistanceText), "%.0fm", entry.distanceMeters);

			// center the label just under the box
			const ImVec2 TextSize = ImGui::CalcTextSize(DistanceText);
			const float TextX = (entry.boxMin.X + entry.boxMax.X) * 0.5f - TextSize.x * 0.5f;
			ImGui::GetForegroundDrawList()->AddText(ImVec2(TextX, entry.boxMax.Y + 2), colEsp, DistanceText);
		}
	}

	// draw a snapline from the bottom-center of the screen to the player's world location
	if (cfg->bLines && entry.hasSnapline)
	{
		const auto& io = ImGui::GetIO();
		ImGui::GetForegroundDrawList()->AddLine(ImVec2(static_cast<float>(io.DisplaySize.x / 2), static_cast<float>(io.DisplaySize.y)), ImVec2(entry.snaplineScreen.X, entry.snaplineScreen.Y), colLine, 0.7f);
	}
}

// Teleport us onto the requested actor, then clear the request. The target is resolved by actor
// pointer rather than a PlayerInfos index, since that list (and the dead-player latch that filters
// it) is rebuilt every frame and a captured index can drift to the wrong entry or go out of range
// by the time this runs. currentActors confirms the actor still exists this frame before we use it.
void CheatManager::HandleTeleport(SDK::APawn* myPlayer, const std::unordered_set<SDK::AActor*>& currentActors)
{
	if (TeleportTarget && currentActors.count(TeleportTarget) && myPlayer)
	{
		SDK::AActor* target = TeleportTarget;
		if (IsObjectValid(myPlayer) && IsObjectValid(target))
		{
			SDK::FRotator CurrentRotation = myPlayer->K2_GetActorRotation();
			myPlayer->K2_TeleportTo(target->K2_GetActorLocation(), CurrentRotation);
		}
	}
	TeleportTarget = nullptr;
}

void CheatManager::HandleMagnet(SDK::APawn* myPlayer, SDK::AActor* selfActor, const std::unordered_set<SDK::AActor*>& currentActors, const SDK::FVector& MyLocation, SDK::TArray<SDK::AActor*>& Players, EspSnapshot& snap)
{
	// Flag the overlay so the render thread draws the "MAGNET ACTIVE" banner (ImGui can't run here
	// on the game thread).
	snap.magnetActive = true;

	// Get the player's forward direction from their rotation
	SDK::FVector ForwardDirection = myPlayer->GetActorForwardVector();
	ForwardDirection.Normalize();

	// Magnet effect: pull all other players in front of the local player's view
	int depthIndex = 0;
	for (int j = 0; j < Players.Num(); j++)
	{
		if (!Players.IsValidIndex(j)) continue;

		SDK::AActor* otherActor = Players[j];
		if (!otherActor || otherActor == selfActor) continue;

		SDK::ABP_FirstPersonCharacter_cLeon_Character_C* otherBaseClass = (SDK::ABP_FirstPersonCharacter_cLeon_Character_C*)otherActor;
		if (!otherBaseClass) continue;

		// Skip dead
		if (IsDead(otherActor))
			continue;

		// Only pull survivors
		if (!IsSurvivor(otherBaseClass))
			continue;

		// Spread players in depth to prevent stacking
		float depthSpread = depthIndex * 120.0f;
		SDK::FVector targetPosition = MyLocation + ForwardDirection * (150.0f + depthSpread);
		if (IsObjectValid(otherBaseClass))
			otherBaseClass->K2_SetActorLocation(targetPosition, false, nullptr, true);
		++depthIndex;
	}
}

void CheatManager::KillSurvivor(SDK::APawn* myPlayer, SDK::AActor* actor)
{
	if (!myPlayer || !actor || myPlayer == actor || !IsHunter(myPlayer) || !IsSurvivor(actor) || IsDead(actor))
		return;

	auto* hunter = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_Hunter_C*>(myPlayer);
	auto* survivor = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_Survivor_C*>(actor);
	if (!IsObjectValid(hunter) || !IsObjectValid(survivor))
		return;

	// Resolve the function fresh from the hunter's current class at call time and invoke it
	// directly, instead of the SDK wrapper's cached-once static UFunction* (see KillPlayer in
	// the generated functions.cpp). The engine recreates BP-generated functions between rounds,
	// so that static dangles after a round transition and ProcessEvent then walks a freed
	// function's garbage parameter layout - faulting with a write AV deep inside the engine.
	SDK::UFunction* fn = hunter->Class->GetFunction("BP_FirstPersonCharacter_cLeon_Character_Hunter_C", "KillPlayer");
	if (!fn)
		return;

	SDK::Params::BP_FirstPersonCharacter_cLeon_Character_Hunter_C_KillPlayer parms{};
	parms.FirstpersonCharacter = survivor;
	parms.SourcePlayerState = hunter->MyPlayerState;
	hunter->ProcessEvent(fn, &parms);
}

// Kill a single requested survivor. Like HandleTeleport, the target is resolved by actor pointer and
// confirmed against currentActors before use, since the snapshot/dead-latch is rebuilt every frame.
// KillSurvivor itself re-validates role/liveness, so a wrong pick (e.g. a hunter) is just a no-op.
void CheatManager::HandleKillTarget(SDK::APawn* myPlayer, const std::unordered_set<SDK::AActor*>& currentActors)
{
	if (KillTarget && currentActors.count(KillTarget))
		KillSurvivor(myPlayer, KillTarget);
	KillTarget = nullptr;
}

void CheatManager::HandleKillAllSurvivors(SDK::APawn* myPlayer, const std::unordered_set<SDK::AActor*>& currentActors)
{
	// A fresh request seeds the pending set with everyone alive this frame. We then kill at most one
	// per frame (below) instead of looping with Sleep() - a blocking loop here would stall the game
	// thread for tens of ms per survivor, since this whole scan runs inside the engine's ProcessEvent.
	if (bKillAllSurvivorsRequested)
	{
		bKillAllSurvivorsRequested = false;
		killAllQueue = currentActors;
	}

	if (killAllQueue.empty())
		return;

	// Discard targets that no longer exist this frame, then kill the next one that's still present.
	// KillSurvivor itself no-ops non-survivors / already-dead actors, so a stale pick is harmless.
	for (auto it = killAllQueue.begin(); it != killAllQueue.end(); )
	{
		SDK::AActor* actor = *it;
		if (!currentActors.count(actor))
		{
			it = killAllQueue.erase(it);
			continue;
		}

		KillSurvivor(myPlayer, actor);
		killAllQueue.erase(it);
		break; // one per frame paces the kills without blocking the game thread
	}
}

void CheatManager::HandleChangeName(SDK::APawn* myPlayer)
{
	if (!bChangeNameRequested)
		return;
	bChangeNameRequested = false;

	std::string name = std::move(pendingChangeName);
	pendingChangeName.clear();

	if (name.empty() || !myPlayer || !IsObjectValid(myPlayer))
		return;

	auto* myChar = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_C*>(myPlayer);
	if (!myChar)
		return;

	auto* playerState = myChar->PlayerState;
	if (!playerState || !IsObjectValid(playerState))
		return;

	if (!playerState->IsA(SDK::ABP_FirstPersonPlayerState_Online_C::StaticClass()))
		return;

	auto* onlineState = static_cast<SDK::ABP_FirstPersonPlayerState_Online_C*>(playerState);

	SDK::UFunction* fn = onlineState->Class->GetFunction("BP_FirstPersonPlayerState_Online_C", "SetName(Server)");
	if (!fn)
		return;

	// Names come through as UTF-8 (FString::ToString); widen properly so non-ASCII names survive.
	std::wstring wname;
	int wlen = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nullptr, 0);
	if (wlen > 1)
	{
		wname.resize(wlen - 1); // drop the counted null terminator from the string's length
		MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, &wname[0], wlen);
	}

	SDK::Params::BP_FirstPersonPlayerState_Online_C_SetName_Server_ parms{};
	parms.CustomPlayerName_0 = SDK::FString(wname.c_str());
	onlineState->ProcessEvent(fn, &parms);
}

// True if Obj is still a fully-live object. The scan and its mutations run on the game thread, but an
// SDK call earlier in the same scan can destroy/GC an actor, so re-check right before touching one.
bool CheatManager::IsObjectValid(SDK::UObject* Obj)
{
	if (!Obj || Obj->Index < 0) return false;
	if (SDK::UObject::GObjects->GetByIndex(Obj->Index) != Obj) return false;
	return true;
}

void CheatManager::DumpBones(SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass)
{
	// Guard the whole pointer chain - any of these can be null on proxies/streaming actors.
	if (!baseClass || !baseClass->Mesh || !baseClass->Mesh->SkeletalMesh || !baseClass->Mesh->SkeletalMesh->Skeleton)
		return;

	FILE* Log = fopen("C:\\bones.txt", "w");

	if (Log) {
		auto meshname = baseClass->Mesh->SkeletalMesh->Name;
		auto bonetree = baseClass->Mesh->SkeletalMesh->Skeleton->BoneTree;
		for (int i = 0; i < bonetree.Num(); i++) {
			auto boneName = baseClass->Mesh->GetBoneName(i);

			fprintf(Log, "%s = %d,\n", boneName.GetRawString().c_str(), i);
		}

		fclose(Log);
		Beep(500, 500);
	}
	else {
		printf("Failed to open file for writing bones.\n");
	}
}
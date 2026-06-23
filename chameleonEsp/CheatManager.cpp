#include "includes.hpp"

void CheatManager::Init()
{
	PlayerInfos.clear();

	if (!ResolveContext()) return;

	const auto MyLocation = MyPlayer->K2_GetActorLocation();

	PlayerController->FOV(cfg->bFovChanger ? cfg->fFovValue : 90); // fov changer

	// get players
	SDK::TArray<SDK::AActor*> Players;
	UGStatics->GetAllActorsOfClass(gWorld, SDK::ABP_FirstPersonCharacter_cLeon_Character_C::StaticClass(), &Players);

	// Track which actors exist this frame so we can drop stale entries from the latched
	// dead set below - otherwise a destroyed corpse's pointer could later be reused by a
	// live actor and wrongly suppress its ESP.
	std::unordered_set<SDK::AActor*> currentActors;

	for (int i = 0; i < Players.Num(); i++)
	{
		if (!Players.IsValidIndex(i)) continue;

		objActor = Players[i];
		if (!objActor) continue;
		BaseClass = (SDK::ABP_FirstPersonCharacter_cLeon_Character_C*)objActor;
		if (!BaseClass) continue;

		currentActors.insert(objActor);

		// Skip dead/ragdolled corpses (see IsDead for why the obvious flags don't work).
		if (IsDead())
			continue;

		const auto Location = BaseClass->K2_GetActorLocation();
		const std::string PlayerName = ResolvePlayerName();
		const bool IsVisible = PlayerController->LineOfSightTo(objActor, { 0,0,0 }, false); // visible check

		if (objActor == MyPlayer)
		{
			// Hunter
			if (IsHunter())
			{
				auto* hunter = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_Hunter_C*>(BaseClass);

				if (!hunter)
					continue;
				
				if (cfg->bNoGunCooldown)
					hunter->GunCoolTime = 0.0;
				
				if (cfg->bMagnetEnabled)
					HandleMagnet(currentActors, MyLocation, Players);
			}
			// Survivor
			else if (IsSurvivor())
			{
				auto* survivor = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_Survivor_C*>(BaseClass);

				if (!survivor)
					continue;

				if (cfg->bAntiDetection)
					survivor->OverlapCheckCapsules.Clear();
			}
			continue;
		}

		PlayerInfos.push_back({ PlayerName, Location, objActor });

		UpdateForcedVisibility();

		if (cfg->bDumpBones) {
			DumpBones();
			cfg->bDumpBones = false;
		}

		if (cfg->bEnemyOnly && !IsEnemy())
			continue;

		DrawEsp(PlayerName, Location, MyLocation, IsVisible);
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

	HandleTeleport(currentActors);
}

// Walk the world -> game instance -> local player -> controller -> pawn chain, plus the
// gameplay/math statics, caching each into the members. Any null link aborts the whole frame.
bool CheatManager::ResolveContext()
{
	gWorld = SDK::UWorld::GetWorld();
	if (!gWorld) return false;

	OwningGameInstance = gWorld->OwningGameInstance;
	if (!OwningGameInstance) return false;

	if (OwningGameInstance->LocalPlayers.Num() <= 0) return false;
	LocalPlayer = OwningGameInstance->LocalPlayers[0];
	if (!LocalPlayer) return false;

	GameViewportClient = LocalPlayer->ViewportClient;
	if (!GameViewportClient) return false;

	PlayerController = LocalPlayer->PlayerController;
	if (!PlayerController) return false;

	PlayerController->GetViewportSize(&x, &y);

	MyPlayer = PlayerController->K2_GetPawn();
	if (!MyPlayer) return false;

	UGStatics = (SDK::UGameplayStatics*)SDK::UGameplayStatics::StaticClass();
	if (!UGStatics) return false;

	MathLib = (SDK::UKismetMathLibrary*)SDK::UKismetMathLibrary::StaticClass();
	if (!MathLib) return false;

	return true;
}

// Resolve the display name for the current actor, falling back to the last-known cached name.
std::string CheatManager::ResolvePlayerName()
{
	// PlayerState replicates as its own actor, independently of the pawn, so on clients its
	// pointer routinely blips to null for a frame or two even while the pawn is fully valid.
	// Don't drop the whole ESP over that - just fall back to the last name we saw for this actor.
	if (!BaseClass->PlayerState)
	{
		auto it = playerNameCache.find(objActor);
		return it != playerNameCache.end() ? it->second : "Unknown";
	}

	// Prefer the custom in-game name (CustomPlayerName) over the platform/Steam name
	// (PlayerNamePrivate). Guard the cast with IsA in case a non-Online PlayerState shows up,
	// and fall back to the Steam name if the custom name hasn't replicated in yet.
	SDK::FString* Name = &BaseClass->PlayerState->PlayerNamePrivate;
	if (BaseClass->PlayerState->IsA(SDK::ABP_FirstPersonPlayerState_Online_C::StaticClass()))
	{
		auto* ps = static_cast<SDK::ABP_FirstPersonPlayerState_Online_C*>(BaseClass->PlayerState);
		if (ps->CustomPlayerName.IsValid())
			Name = &ps->CustomPlayerName;
	}

	if (Name->IsValid())
	{
		std::string PlayerName = Name->ToString();
		playerNameCache[objActor] = PlayerName; // remember it for the null windows
		return PlayerName;
	}

	return "Unknown";
}

// Force the current actor's body visibility on/off, tracking who we touched so they can be restored.
void CheatManager::UpdateForcedVisibility()
{
	// Cache the OnRep_BodyVisibility UFunction for the ProcessEvent hook (done once)
	if (!g_OnRepBodyVisibilityFunc)
		g_OnRepBodyVisibilityFunc = SDK::ABP_FirstPersonCharacter_cLeon_Character_C::StaticClass()->GetFunction("BP_FirstPersonCharacter_cLeon_Character_C", "OnRep_BodyVisibility");

	if (cfg->bForceCharacterVisibility && !BaseClass->BodyVisibility)
	{
		BaseClass->BodyVisibility = true;
		BaseClass->OnRep_BodyVisibility();
		forcedVisibleActors.insert(objActor);
	}
	else if (!cfg->bForceCharacterVisibility && forcedVisibleActors.count(objActor))
	{
		BaseClass->BodyVisibility = false;
		BaseClass->OnRep_BodyVisibility();
		forcedVisibleActors.erase(objActor);
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
bool CheatManager::IsDead()
{
	if (BaseClass->Mesh && BaseClass->Mesh->IsAnySimulatingPhysics())
		deadActors.insert(objActor);
	return deadActors.count(objActor) > 0;
}

bool CheatManager::IsDead(SDK::AActor* actor)
{
	if (!actor) return false;
	auto* baseClass = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_C*>(actor);
	if (!baseClass || !baseClass->Mesh) return false;

	if (baseClass->Mesh->IsAnySimulatingPhysics())
		deadActors.insert(actor);
	return deadActors.count(actor) > 0;
}

bool CheatManager::IsSurvivor()
{
	return IsSurvivor(BaseClass);
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

bool CheatManager::IsHunter()
{
	return IsHunter(BaseClass);
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

// True when the current actor is on the opposing team (survivor vs. hunter) from us.
bool CheatManager::IsEnemy()
{
	if (!MyPlayer->IsA(SDK::ABP_FirstPersonCharacter_cLeon_Character_C::StaticClass()))
		return false;
	auto* MyChar = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_C*>(MyPlayer);
	return MyChar->IsHunter != BaseClass->IsHunter;
}

// Draw the current actor's skeleton by connecting bone pairs in screen space.
void CheatManager::DrawSkeleton(ImU32 colEsp)
{
	SDK::FVector2D BoneScreen, PrevBoneScreen;
	for (const std::pair<int, int>& Connection : skeleton::Connections)
	{
		const auto BoneLoc1 = BaseClass->Mesh->GetSocketLocation(BaseClass->Mesh->GetBoneName(Connection.first));
		const auto BoneLoc2 = BaseClass->Mesh->GetSocketLocation(BaseClass->Mesh->GetBoneName(Connection.second));
		if (PlayerController->ProjectWorldLocationToScreen(BoneLoc1, &BoneScreen, false) && PlayerController->ProjectWorldLocationToScreen(BoneLoc2, &PrevBoneScreen, false))
			ImGui::GetForegroundDrawList()->AddLine(ImVec2(BoneScreen.X, BoneScreen.Y), ImVec2(PrevBoneScreen.X, PrevBoneScreen.Y), colEsp, 1.0f);
	}
}

// Build a 2D bounding box from every bone's screen position so it stays correct in any pose
// (crouch, prone, etc.). Returns false when no bone projected on-screen.
bool CheatManager::ComputeBoundingBox(SDK::FVector2D& BoxMin, SDK::FVector2D& BoxMax)
{
	bool bHasBox = false;
	for (int BoneIdx = skeleton::amm; BoneIdx < skeleton::None; BoneIdx++)
	{
		const auto BoneLoc = BaseClass->Mesh->GetSocketLocation(BaseClass->Mesh->GetBoneName(BoneIdx));

		SDK::FVector2D BoneScreenPos;
		if (!PlayerController->ProjectWorldLocationToScreen(BoneLoc, &BoneScreenPos, false))
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

// Render the enabled ESP overlays (skeleton, box, name, distance, snapline) for the current actor.
void CheatManager::DrawEsp(const std::string& PlayerName, SDK::FVector Location, SDK::FVector MyLocation, bool IsVisible)
{
	const ImU32 colEsp  = ImGui::ColorConvertFloat4ToU32(IsVisible ? *(ImVec4*)cfg->colVisible : *(ImVec4*)cfg->colNotVisible);
	const ImU32 colLine = ImGui::ColorConvertFloat4ToU32(*(ImVec4*)cfg->colLines);

	// white color
	const float fff[4] = { 1.0f,  1.0f,  1.0f, 1.0f };
	const ImU32 colWhite = ImGui::ColorConvertFloat4ToU32(*(ImVec4*)fff);

	SDK::FVector2D BoxMin{}, BoxMax{};
	bool bHasBox = false;

	if (BaseClass->Mesh)
	{
		if (cfg->bSkeleton)
			DrawSkeleton(colEsp);

		bHasBox = ComputeBoundingBox(BoxMin, BoxMax);
	}

	if (bHasBox)
	{
		if (cfg->bNames)
			ImGui::GetForegroundDrawList()->AddText(ImVec2(BoxMin.X, BoxMin.Y - 15), colEsp, PlayerName.c_str());

		if (cfg->bRoles)
		{
			const char* roleText = nullptr;
			if (IsHunter()) roleText = "Hunter";
			else if (IsSurvivor()) roleText = "Survivor";

			if (roleText)
			{
				const float nameWidth = cfg->bNames ? ImGui::CalcTextSize(PlayerName.c_str()).x + 5 : 0.0f;
				ImGui::GetForegroundDrawList()->AddText(ImVec2(BoxMin.X + nameWidth, BoxMin.Y - 15), colWhite, roleText);
			}
		}

		if (cfg->bBox)
			draw->DrawBox(BoxMin.X, BoxMin.Y, BoxMax.X - BoxMin.X, BoxMax.Y - BoxMin.Y, colEsp, 1.0f);

		if (cfg->bDistance)
		{
			char DistanceText[32];
			snprintf(DistanceText, sizeof(DistanceText), "%.0fm", MyLocation.GetDistanceToInMeters(Location));

			// center the label just under the box
			const ImVec2 TextSize = ImGui::CalcTextSize(DistanceText);
			const float TextX = (BoxMin.X + BoxMax.X) * 0.5f - TextSize.x * 0.5f;
			ImGui::GetForegroundDrawList()->AddText(ImVec2(TextX, BoxMax.Y + 2), colEsp, DistanceText);
		}
	}

	// draw a snapline from the bottom-center of the screen to the player's world location
	SDK::FVector2D Screen;
	if (cfg->bLines && PlayerController->ProjectWorldLocationToScreen(Location, &Screen, false))
	{
		const auto& io = ImGui::GetIO();
		ImGui::GetForegroundDrawList()->AddLine(ImVec2(static_cast<float>(io.DisplaySize.x / 2), static_cast<float>(io.DisplaySize.y)), ImVec2(Screen.X, Screen.Y), colLine, 0.7f);
	}
}

// Teleport us onto the requested actor, then clear the request. The target is resolved by actor
// pointer rather than a PlayerInfos index, since that list (and the dead-player latch that filters
// it) is rebuilt every frame and a captured index can drift to the wrong entry or go out of range
// by the time this runs. currentActors confirms the actor still exists this frame before we use it.
void CheatManager::HandleTeleport(const std::unordered_set<SDK::AActor*>& currentActors)
{
	if (TeleportTarget && currentActors.count(TeleportTarget) && MyPlayer)
	{
		SDK::FRotator CurrentRotation = MyPlayer->K2_GetActorRotation();
		MyPlayer->K2_TeleportTo(TeleportTarget->K2_GetActorLocation(), CurrentRotation);
	}
	TeleportTarget = nullptr;
}

void CheatManager::HandleMagnet(const std::unordered_set<SDK::AActor*>& currentActors, const SDK::FVector& MyLocation, SDK::TArray<SDK::AActor*>& Players)
{
	// Draw "MAGNET ACTIVE" text at the bottom center in red
	const float redColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
	const ImU32 colRed = ImGui::ColorConvertFloat4ToU32(*(ImVec4*)redColor);

	const char* magnetText = "MAGNET ACTIVE";
	const ImVec2 textSize = ImGui::CalcTextSize(magnetText);
	const float textX = (x / 2.0f) - (textSize.x / 2.0f);
	const float textY = y - 30.0f;

	ImGui::GetForegroundDrawList()->AddText(ImVec2(textX, textY), colRed, magnetText);

	// Get the player's forward direction from their rotation
	SDK::FVector ForwardDirection = MyPlayer->GetActorForwardVector();
	ForwardDirection.Normalize();

	// Magnet effect: pull all other players in front of the local player's view
	int depthIndex = 0;
	for (int j = 0; j < Players.Num(); j++)
	{
		if (!Players.IsValidIndex(j)) continue;

		SDK::AActor* otherActor = Players[j];
		if (!otherActor || otherActor == objActor) continue;

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
		otherBaseClass->K2_SetActorLocation(targetPosition, false, nullptr, true);
		++depthIndex;
	}
}

void CheatManager::DumpBones()
{
	// Guard the whole pointer chain - any of these can be null on proxies/streaming actors.
	if (!BaseClass || !BaseClass->Mesh || !BaseClass->Mesh->SkeletalMesh || !BaseClass->Mesh->SkeletalMesh->Skeleton)
		return;

	FILE* Log = fopen("C:\\bones.txt", "w");

	if (Log) {
		auto meshname = BaseClass->Mesh->SkeletalMesh->Name;
		auto bonetree = BaseClass->Mesh->SkeletalMesh->Skeleton->BoneTree;
		for (int i = 0; i < bonetree.Num(); i++) {
			auto boneName = BaseClass->Mesh->GetBoneName(i);

			fprintf(Log, "%s = %d,\n", boneName.GetRawString().c_str(), i);
		}

		fclose(Log);
		Beep(500, 500);
	}
	else {
		printf("Failed to open file for writing bones.\n");
	}
}
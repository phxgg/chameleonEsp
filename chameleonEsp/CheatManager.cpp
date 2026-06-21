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

		obj = Players[i];
		if (!obj) continue;
		BaseClass = (SDK::ABP_FirstPersonCharacter_cLeon_Character_C*)obj;
		if (!BaseClass) continue;

		currentActors.insert(obj);

		// Skip dead/ragdolled corpses (see IsDead for why the obvious flags don't work).
		if (IsDead())
			continue;

		const auto Location = BaseClass->K2_GetActorLocation();
		const std::string PlayerName = ResolvePlayerName();
		const bool IsVisible = PlayerController->LineOfSightTo(obj, { 0,0,0 }, false); // visible check

		if (obj == MyPlayer)
			continue;
		PlayerInfos.push_back({ PlayerName, Location });

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

	HandleTeleport();
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
		auto it = playerNameCache.find(obj);
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
		playerNameCache[obj] = PlayerName; // remember it for the null windows
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
		forcedVisibleActors.insert(obj);
	}
	else if (!cfg->bForceCharacterVisibility && forcedVisibleActors.count(obj))
	{
		BaseClass->BodyVisibility = false;
		BaseClass->OnRep_BodyVisibility();
		forcedVisibleActors.erase(obj);
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
		deadActors.insert(obj);
	return deadActors.count(obj) > 0;
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

	//draw a snapline from the bottom-center of the screen to the player's world location
	SDK::FVector2D Screen;
	if (cfg->bLines && PlayerController->ProjectWorldLocationToScreen(Location, &Screen, false))
	{
		const auto& io = ImGui::GetIO();
		ImGui::GetForegroundDrawList()->AddLine(ImVec2(static_cast<float>(io.DisplaySize.x / 2), static_cast<float>(io.DisplaySize.y)), ImVec2(Screen.X, Screen.Y), colLine, 0.7f);
	}
}

// Teleport us onto the selected player from the last-built PlayerInfos list, then clear the request.
void CheatManager::HandleTeleport()
{
	if (cfg->iTeleportTarget != -1 && cfg->iTeleportTarget < (int)PlayerInfos.size() && MyPlayer)
	{
		SDK::FRotator CurrentRotation = MyPlayer->K2_GetActorRotation();
		MyPlayer->K2_TeleportTo(PlayerInfos[cfg->iTeleportTarget].Location, CurrentRotation);
		cfg->iTeleportTarget = -1;
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
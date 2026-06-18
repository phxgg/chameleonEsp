#include "includes.hpp"

void CheatManager::Init()
{
	auto& io = ImGui::GetIO();

	PlayerInfos.clear();

	gWorld = SDK::UWorld::GetWorld();
	if (!gWorld) return;

	OwningGameInstance = gWorld->OwningGameInstance;
	if (!OwningGameInstance) return;

	if (OwningGameInstance->LocalPlayers.Num() <= 0) return;
	LocalPlayer = OwningGameInstance->LocalPlayers[0];
	if (!LocalPlayer) return;

	GameViewportClient = LocalPlayer->ViewportClient;
	if (!GameViewportClient) return;

	PlayerController = LocalPlayer->PlayerController;
	if (!PlayerController) return;

	PlayerController->GetViewportSize(&x, &y);

	MyPlayer = PlayerController->K2_GetPawn();
	if (!MyPlayer) return;

	auto MyLocation = MyPlayer->K2_GetActorLocation();

	UGStatics = (SDK::UGameplayStatics*)SDK::UGameplayStatics::StaticClass();
	if (!UGStatics) return;

	// get players
	SDK::TArray<SDK::AActor*> Players;
	UGStatics->GetAllActorsOfClass(gWorld, SDK::ABP_FirstPersonCharacter_cLeon_Character_C::StaticClass(), &Players);

	// class to math operations
	MathLib = (SDK::UKismetMathLibrary*)SDK::UKismetMathLibrary::StaticClass();
	if (!MathLib) return;

	for (int i = 0; i < Players.Num(); i++)
	{
		if (!Players.IsValidIndex(i)) continue;

		obj = Players[i];
		if (!obj) continue;
		BaseClass = (SDK::ABP_FirstPersonCharacter_cLeon_Character_C*)obj;
		if (!BaseClass) continue;

		auto Location = BaseClass->K2_GetActorLocation();

		if (PlayerController)
			PlayerController->FOV(cfg->bFovChanger ? cfg->fFovValue : 90); // fov changer

		// Skip ragdolled corpses. Neither the raw `Dead` field (not replicated - stays 0 on remote
		// corpses) nor IsLive() (returns true even for dead bodies in infection mode) works here.
		// A live character - survivor or hunter - is animation-driven, so its mesh isn't simulating
		// physics; only a dead body ragdolls. That makes IsAnySimulatingPhysics() the clean signal,
		// confirmed by logging: every live player reads 0, only the corpse reads 1.
		if (BaseClass->Mesh && BaseClass->Mesh->IsAnySimulatingPhysics())
			continue;

		// PlayerState replicates as its own actor, independently of the pawn, so on clients its
		// pointer routinely blips to null for a frame or two even while the pawn is fully valid.
		// Don't drop the whole ESP over that - just fall back to the last name we saw for this actor.
		std::string PlayerName = "Unknown";
		if (BaseClass->PlayerState)
		{
			auto Name = BaseClass->PlayerState->PlayerNamePrivate;
			if (Name.IsValid())
			{
				PlayerName = Name.ToString();
				playerNameCache[obj] = PlayerName; // remember it for the null windows
			}
		}
		else
		{
			auto it = playerNameCache.find(obj);
			if (it != playerNameCache.end())
				PlayerName = it->second;
		}

		bool IsVisible = PlayerController->LineOfSightTo(obj, { 0,0,0 }, false); // visible check

		if (obj != MyPlayer)
			PlayerInfos.push_back({ PlayerName, Location });
		else
			continue;

		// Cache the OnRep_BodyVisibility UFunction for the ProcessEvent hook (done once)
		if (!g_OnRepBodyVisibilityFunc)
			g_OnRepBodyVisibilityFunc = SDK::ABP_FirstPersonCharacter_cLeon_Character_C::StaticClass()->GetFunction("BP_FirstPersonCharacter_cLeon_Character_C", "OnRep_BodyVisibility");

		// Force character visibility on/off, tracking who we changed so we can restore them
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

		if (cfg->bDumpBones) {
			DumpBones();
			cfg->bDumpBones = false;
		}

		if (cfg->bEnemyOnly)
		{
			if (!MyPlayer->IsA(SDK::ABP_FirstPersonCharacter_cLeon_Character_C::StaticClass()))
				continue;
			auto* MyChar = static_cast<SDK::ABP_FirstPersonCharacter_cLeon_Character_C*>(MyPlayer);
			if (MyChar->IsHunter == BaseClass->IsHunter)
				continue;
		}

		ImU32 colEsp  = ImGui::ColorConvertFloat4ToU32(IsVisible ? *(ImVec4*)cfg->colVisible : *(ImVec4*)cfg->colNotVisible);
		ImU32 colLine = ImGui::ColorConvertFloat4ToU32(*(ImVec4*)cfg->colLines);

		SDK::FVector2D BoxMin{}, BoxMax{};
		bool bHasBox = false;

		if (BaseClass->Mesh)
		{
			// draw bones
			SDK::FVector2D BoneScreen, PrevBoneScreen;
			for (const std::pair<int, int>& Connection : skeleton::Connections)
			{
				const auto Bone1 = Connection.first;
				const auto Bone2 = Connection.second;
				const auto BoneLoc1 = BaseClass->Mesh->GetSocketLocation(BaseClass->Mesh->GetBoneName(Bone1));
				const auto BoneLoc2 = BaseClass->Mesh->GetSocketLocation(BaseClass->Mesh->GetBoneName(Bone2));
				if (PlayerController->ProjectWorldLocationToScreen(BoneLoc1, &BoneScreen, false) && PlayerController->ProjectWorldLocationToScreen(BoneLoc2, &PrevBoneScreen, false))
				{
					if (cfg->bSkeleton)
					{
						ImGui::GetForegroundDrawList()->AddLine(ImVec2(BoneScreen.X, BoneScreen.Y), ImVec2(PrevBoneScreen.X, PrevBoneScreen.Y), colEsp, 1.0f);
					}
				}
			}

			//build a 2D bounding box from every bone's screen position so it stays correct in any pose (crouch, prone, etc.)
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

		//example how to draw without bones in relative location
		SDK::FVector2D Screen;
		if (PlayerController->ProjectWorldLocationToScreen(Location, &Screen, false))
		{
			ImVec2 Pos(Screen.X, Screen.Y);

			if (cfg->bLines)
				ImGui::GetForegroundDrawList()->AddLine(ImVec2(static_cast<float>(io.DisplaySize.x / 2), static_cast<float>(io.DisplaySize.y)), Pos, colLine, 0.7f);
		}
	}

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
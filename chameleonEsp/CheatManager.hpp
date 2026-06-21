class CheatManager
{
private:
	SDK::UWorld** _UWorld;
	SDK::UWorld* gWorld;
	SDK::APlayerController* PlayerController;
	SDK::ULocalPlayer* LocalPlayer;
	SDK::UGameInstance* OwningGameInstance;
	SDK::UGameViewportClient* GameViewportClient;
	SDK::AGameStateBase* GameState;
	SDK::AActor* obj;
	SDK::UGameplayStatics* UGStatics;
	SDK::UKismetSystemLibrary* KismetSystemLib;
	SDK::APawn* MyPlayer;
	SDK::ABP_FirstPersonCharacter_cLeon_Character_C* BaseClass; //change a class for each game
	SDK::UKismetMathLibrary* MathLib;
	int x, y = 0;

	// Resolve the world/player pointer chain into the members above. Returns false if any link is null.
	bool ResolveContext();
	// Per-player helpers, operating on the current `obj`/`BaseClass` being iterated.
	std::string ResolvePlayerName();
	void UpdateForcedVisibility();
	bool IsDead();
	bool IsEnemy();
	void DrawSkeleton(ImU32 colEsp);
	bool ComputeBoundingBox(SDK::FVector2D& BoxMin, SDK::FVector2D& BoxMax);
	void DrawEsp(const std::string& PlayerName, SDK::FVector Location, SDK::FVector MyLocation, bool IsVisible);
	void HandleTeleport();
public:
	struct PlayerInfo {
		std::string Name;
		SDK::FVector Location;
	};
	std::vector<PlayerInfo> PlayerInfos;
	std::unordered_set<SDK::AActor*> forcedVisibleActors;
	std::unordered_set<SDK::AActor*> deadActors; // actors seen ragdolling; latched so ESP stays off after the corpse stops simulating physics
	std::unordered_map<SDK::AActor*, std::string> playerNameCache; // last-known name per actor, so ESP survives PlayerState replication blips
	void Init();
	void DumpBones();
};
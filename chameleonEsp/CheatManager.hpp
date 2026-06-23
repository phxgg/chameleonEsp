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
	SDK::AActor* objActor;
	SDK::UGameplayStatics* UGStatics;
	SDK::UKismetSystemLibrary* KismetSystemLib;
	SDK::APawn* MyPlayer;
	SDK::ABP_FirstPersonCharacter_cLeon_Character_C* BaseClass; //change a class for each game
	SDK::UKismetMathLibrary* MathLib;
	int x, y = 0;

	// Resolve the world/player pointer chain into the members above. Returns false if any link is null.
	bool ResolveContext();
	// Per-player helpers, operating on the current `objActor`/`BaseClass` being iterated.
	std::string ResolvePlayerName();
	void UpdateForcedVisibility();
	bool IsDead();
	bool IsDead(SDK::AActor* actor);
	bool IsSurvivor();
	bool IsSurvivor(SDK::AActor* actor);
	bool IsSurvivor(SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass);
	bool IsHunter();
	bool IsHunter(SDK::AActor* actor);
	bool IsHunter(SDK::ABP_FirstPersonCharacter_cLeon_Character_C* baseClass);
	bool IsEnemy();
	void DrawSkeleton(ImU32 colEsp);
	bool ComputeBoundingBox(SDK::FVector2D& BoxMin, SDK::FVector2D& BoxMax);
	void DrawEsp(const std::string& PlayerName, SDK::FVector Location, SDK::FVector MyLocation, bool IsVisible);
	void HandleTeleport(const std::unordered_set<SDK::AActor*>& currentActors);
	void HandleMagnet(const std::unordered_set<SDK::AActor*>& currentActors, const SDK::FVector& MyLocation, SDK::TArray<SDK::AActor*>& Players);
	SDK::AActor* TeleportTarget = nullptr; // resolved by actor pointer, not list index, since PlayerInfos is rebuilt every frame
public:
	struct PlayerInfo {
		std::string Name;
		SDK::FVector Location;
		SDK::AActor* Actor;
	};
	std::vector<PlayerInfo> PlayerInfos;
	void RequestTeleport(SDK::AActor* Actor) { TeleportTarget = Actor; }
	std::unordered_set<SDK::AActor*> forcedVisibleActors;
	std::unordered_set<SDK::AActor*> deadActors; // actors seen ragdolling; latched so ESP stays off after the corpse stops simulating physics
	std::unordered_map<SDK::AActor*, std::string> playerNameCache; // last-known name per actor, so ESP survives PlayerState replication blips
	void Init();
	void DumpBones();
};
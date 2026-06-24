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
	void KillSurvivor(SDK::AActor* actor);
	void HandleTeleport(const std::unordered_set<SDK::AActor*>& currentActors);
	void HandleMagnet(const std::unordered_set<SDK::AActor*>& currentActors, const SDK::FVector& MyLocation, SDK::TArray<SDK::AActor*>& Players);
	void HandleKillAllSurvivors(const std::unordered_set<SDK::AActor*>& currentActors);
	SDK::AActor* TeleportTarget = nullptr; // resolved by actor pointer, not list index, since PlayerInfos is rebuilt every frame
	bool bKillAllSurvivorsRequested = false;

	// Actions that mutate game state (teleport, kill, force-visibility, magnet, etc.)
	// must run on the game thread, not here on the render thread (Init() runs in hkPresent).
	// queue those actions and drain on the next ProcessEvent call
	std::mutex GameThreadQueueMutex;
	std::vector<std::function<void()>> GameThreadQueue;
public:
	struct PlayerInfo {
		std::string Name;
		SDK::FVector Location;
		SDK::AActor* Actor;
	};
	std::vector<PlayerInfo> PlayerInfos;
	void RequestTeleport(SDK::AActor* Actor) { TeleportTarget = Actor; }
	void RequestKillAllSurvivors() { bKillAllSurvivorsRequested = true; }
	std::unordered_set<SDK::AActor*> forcedVisibleActors;
	std::unordered_set<SDK::AActor*> deadActors; // actors seen ragdolling; latched so ESP stays off after the corpse stops simulating physics
	std::unordered_map<SDK::AActor*, std::string> playerNameCache; // last-known name per actor, so ESP survives PlayerState replication blips
	void Init();
	void DumpBones();
	void QueueGameThreadAction(std::function<void()> action);
	void FlushGameThreadActions();

	// True if Obj is still the live object at its GObjects slot. Queued actions capture raw
	// pointers on the render thread but only run later on the game thread (see
	// QueueGameThreadAction), so the actor may have been destroyed/GC'd in between - calling
	// into a freed UObject is exactly the null-pointer-deep-in-engine-code crash this guards.
	static bool IsObjectValid(SDK::UObject* Obj);
};
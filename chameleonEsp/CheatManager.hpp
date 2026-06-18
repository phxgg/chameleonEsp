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
public:
	struct PlayerInfo {
		std::string Name;
		SDK::FVector Location;
	};
	std::vector<PlayerInfo> PlayerInfos;
	std::unordered_set<SDK::AActor*> forcedVisibleActors;
	std::unordered_map<SDK::AActor*, std::string> playerNameCache; // last-known name per actor, so ESP survives PlayerState replication blips
	void Init();
	void DumpBones();
};
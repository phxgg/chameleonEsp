class Settings
{
public:
	bool bMenuOpen;
	bool bInitHooks;
	bool bFovChanger;
	float fFovValue;
	bool bLines;
	bool bNames;
	bool bBox;
	bool bSkeleton;
	bool bDumpBones;
	bool bEnemyOnly;
	int iTeleportTarget;
	float colVisible[4];
	float colNotVisible[4];
	float colLines[4];
	void InitializeSettings();
	void SaveSettings();
	void LoadSettings();
};
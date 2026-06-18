#include "includes.hpp"

#define ConfigFile ("C:\\chameleonEsp\\settings.ini")

void Settings::InitializeSettings()
{
	this->bMenuOpen = false;
	this->bInitHooks = false;
	this->bFovChanger = false;
	this->fFovValue = 90.0f;
	this->bLines = false;
	this->bNames = false;
	this->bBox = false;
	this->bSkeleton = false;
	this->bDistance = false;
	this->bDumpBones = false;
	this->bEnemyOnly = false;
	this->bForceCharacterVisibility = false;
	this->iTeleportTarget = -1;
	float colVisible[4]    = { 0.0f,  1.0f,  0.0f, 1.0f };
	float colNotVisible[4] = { 0.706f, 0.392f, 1.0f, 1.0f };
	float colLines[4]      = { 1.0f,  1.0f,  1.0f, 1.0f };
	memcpy(this->colVisible,    colVisible,    sizeof(colVisible));
	memcpy(this->colNotVisible, colNotVisible, sizeof(colNotVisible));
	memcpy(this->colLines,      colLines,      sizeof(colLines));
}

void Settings::SaveSettings()
{
	_mkdir("C:\\chameleonEsp");
	fopen_s(&file, ConfigFile, "wb");
	if (file) {
		fwrite(cfg, sizeof(*cfg), 1, file);
		fclose(file);
	}
}

void Settings::LoadSettings()
{
	fopen_s(&file, ConfigFile, "rb");
	if (file) {
		fseek(file, 0, SEEK_END);
		auto size = ftell(file);

		if (size == sizeof(*cfg)) {
			fseek(file, 0, SEEK_SET);
			fread(cfg, sizeof(*cfg), 1, file);
			fclose(file);
		}
		else
		{
			fclose(file);
			InitializeSettings();
		}
	}
	else
	{
		InitializeSettings();
	}
}

#include "INIReader.h"

struct config
{
	void load()
	{
		INIReader reader("Data\\SFSE\\plugins\\SPT_Config.ini");
		if (reader.ParseError() != 0)
		{
			REX::WARN("Failed to read config file, using default");
			return;
		}
		this->DisableTakeOffCam = reader.GetBoolean("Config", "DisableTakeOffCam", true);
		this->EnableLoadBlur = reader.GetBoolean("Config", "EnableLoadBlur", true);
		this->TakeoffExtensionLength = reader.GetFloat("Config", "TakeoffExtensionLength", 5.6f);
		if (this->TakeoffExtensionLength == 0)
			this->TakeoffExtensionLength = 0.01f;
		this->EnableLandingProbe = reader.GetBoolean("Config", "EnableLandingProbe", true);
		this->PatchUnloadLoadScreen = reader.GetBoolean("Config", "PatchUnloadLoadScreen", true);
	}

	bool DisableTakeOffCam = 1;
	bool EnableLoadBlur = true;
	float TakeoffExtensionLength = 5.6f;
	// Seamless-landing observation probe: logging only, writes no engine state.
	bool EnableLandingProbe = true;
	// The unloadCurrentLocation load-screen suppression is a GLOBAL patch to a generic
	// function, so landing runs through it too. Set false to isolate whether it is what
	// drops the player into the exterior cell instead of the cockpit after landing.
	bool PatchUnloadLoadScreen = true;
};
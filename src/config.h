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
		this->ScopedLoadScreenPatch = reader.GetBoolean("Config", "ScopedLoadScreenPatch", false);
	}

	bool DisableTakeOffCam = 1;
	bool EnableLoadBlur = true;
	float TakeoffExtensionLength = 5.6f;
	// Seamless-landing observation probe: logging only, writes no engine state.
	bool EnableLandingProbe = true;
	// unloadCurrentLocation's load-screen suppression.
	//   false (default) - original behaviour: patched once at startup, stays patched.
	//                     Proven, but it is a GLOBAL patch to a generic function, so
	//                     landing runs through it too.
	//   true            - apply it ONLY around manualLoadSystem's own call and restore
	//                     immediately after, so landing sees the function unpatched.
	// Do NOT simply disable the patch: manualLoadSystem depends on it and crashes
	// without it (confirmed - the crash lands right after loadSystem returns).
	bool ScopedLoadScreenPatch = false;
};
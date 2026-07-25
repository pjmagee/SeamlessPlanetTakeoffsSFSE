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
	}

	bool DisableTakeOffCam = 1;
	bool EnableLoadBlur = true;
	float TakeoffExtensionLength = 5.6f;
	// Seamless-landing observation probe: logging only, writes no engine state.
	bool EnableLandingProbe = true;
};
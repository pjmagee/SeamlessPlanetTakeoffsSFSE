#include "BobbyRE/BobbyRE.h"
#include "config.h"
#include "landing.h"

config settings;
RE::BGSLocation* prevLocation;
RE::BGSLocation* newLocation;
RE::NiMatrix3 takeoffRot;
RE::NiMatrix3 landedRot;
BobbyRE::atmosphereRenderSettings* atmosphereSettings;
BobbyRE::atmosphereRenderSettings* originalAtmosphereSettings;
BobbyRE::atmosphereRenderSettings* spaceAtmosphereSettings;

RE::TESImageSpace::ImageSpaceSettings* g_spaceImageSpaceSettings;
RE::TESImageSpace::ImageSpaceSettings* g_surfaceImageSpaceSettings;
RE::TESImageSpace::ImageSpaceSettings* g_originalSurfaceImageSpaceSettings;

enum TAKEOFF_STATE
{
	NOT_STARTED,
	TAKEOFF_ANIM_STARTED,
	TAKEOFF_LOAD_STARTED,
	TAKEOFF_LOAD_COMPLETE
};
struct takeoffState
{
	TAKEOFF_STATE state;
	float prefadeTimer;
	float fadeTimer;
	float arrivalTimer;
	float originalStarGlow;
	float originalStarVisibility;
	float blurValue;
	bool originalDisableSimulatedVisibility;
	RE::BGSAtmosphere* atmosphereForm;
	float originalAtmosphereTopRadius;
	std::array<RE::BGSCloudForm::CloudLayer, 4> originalLayers;             
	std::array<RE::BGSCloudForm::CloudPlane, 4> originalPlanes;
	RE::BGSCloudForm *cloudForm;
	RE::NiPoint3 endAnimAngle;
	RE::NiPoint3 endAnimPos;
};

takeoffState g_takeoffState;

void updateDiscoveryInfo(RE::TESObjectREFR* ship) 
{
	using func_updateDiscoveredStatus_t = void* (RE::Actor*, RE::BGSLocation*);
	REL::Relocation<func_updateDiscoveredStatus_t>updatePlanetDiscoveryStatus{ REL::ID(102968) };
	updatePlanetDiscoveryStatus(ship->GetSpaceshipPilot(), newLocation);

	REL::Relocation<uintptr_t*>unkGlobal{ REL::ID(938414) };

	using func_updateStarDiscoveryStatus_t = void* (void*, uint32_t, bool);
	REL::Relocation<func_updateStarDiscoveryStatus_t>updateStarDiscoveryStatus{ REL::ID(102650) };

	BobbyRE::DBInfo* currentStarInfo = *(BobbyRE::DBInfo**)(*unkGlobal.get() + 0x58);

	updateStarDiscoveryStatus(nullptr, currentStarInfo->FormID, 1);
}

void manualLoadSystem(RE::TESObjectREFR* ship)
{
	using func_getRotationMatrix_t = RE::NiMatrix3* (RE::TESObjectREFR *, RE::NiMatrix3 *);
	REL::Relocation<func_getRotationMatrix_t>getRotationMatrix{ REL::ID(63286) };

	getRotationMatrix(ship, &takeoffRot);

	using func_loadSystem_t = int(RE::TESObjectREFR*, RE::TESObjectCELL*, bool, double);
	REL::Relocation<func_loadSystem_t>loadSystem{ REL::ID(102641) };

	using func_removeObjectFromCell_t = void(RE::TESObjectCELL*, RE::TESObjectREFR*, bool);
	REL::Relocation<func_removeObjectFromCell_t>removeObjectFromCell{ REL::ID(62697) };


	RE::TESObjectCELL* parentCell = ship->parentCell;
	removeObjectFromCell(parentCell, ship, false);
	prevLocation = ship->GetCurrentLocation();

	RE::TESObjectCELL* GalaxyCell = (RE::TESObjectCELL*)RE::TESObjectCELL::LookupByID(0x18343);

	ship->SetParentCell(GalaxyCell);
	loadSystem(ship, parentCell, 0, 1);
	newLocation = ship->GetCurrentLocation();

	RE::NiPoint3 a3{ 0,0,0 };

	using func_loadSystem2_t = void* (RE::TES *, RE::TESObjectCELL *, RE::NiPoint3 *, bool);
	REL::Relocation<func_loadSystem2_t>unloadCurrentLocation{ REL::ID(46037) };

	unloadCurrentLocation(RE::TES::GetSingleton(), RE::PlayerCharacter::GetSingleton()->parentCell, &a3, 1);
	ship->SetParentCell(0);

	using func_unkfunc_t = void* (RE::TESObjectREFR**, RE::BGSLocation*, RE::BGSLocation*);
	REL::Relocation<func_unkfunc_t>unkfunc{ REL::ID(64046) };

	using func_unkfunc2_t = void* ();
	REL::Relocation<func_unkfunc2_t>unkfunc2{ REL::ID(62173) };

	using func_unkfunc3_t = void* (void*, RE::TESObjectREFR*, RE::BGSLocation*, RE::BGSLocation*, bool);
	REL::Relocation<func_unkfunc3_t>unkfunc3{ REL::ID(72938) };

	using func_unkfunc4_t = void* (void*, uint32_t, void*);
	REL::Relocation<func_unkfunc4_t>unkfunc4{ REL::ID(72962) };

	using func_attatchObjectToCell_t = double(RE::TESObjectCELL*, RE::TESObjectREFR*, bool, bool);
	REL::Relocation<func_attatchObjectToCell_t>attatchObjectToCell{ REL::ID(63034) };
	
	attatchObjectToCell(GalaxyCell, ship, 0, 0);

	if (prevLocation != newLocation)
	{
		_InterlockedExchangeAdd64((volatile long long*)&ship->refCount, 0x80000200001);
		unkfunc(&ship, prevLocation, newLocation);
		ship->DecRefCount();
		void* v206 = unkfunc2();
		int v456[8];
		void* v207 = unkfunc3(v456, ship, prevLocation, newLocation, 0);

		unkfunc4(v206, 4, v207);
	}
}

template <typename componentType> componentType *getComponent(BobbyRE::DBLookupContext* ctx)
{
	uint32_t leaf = ctx->depth - 1;
	uintptr_t page = ctx->pages[leaf];

	uint32_t dataOffset = *(uint32_t*)(page + 4);
	uint16_t recordOffset =
		*(uint16_t*)(page +
			dataOffset +
			2 * (ctx->indices[leaf] + 1));

	uintptr_t record =
		page +
		dataOffset +
		recordOffset;

	componentType *component = (componentType *)(record + 16);
	return component;
}

BobbyRE::PlanetComponent* getPlanetComponent(uint32_t formID, BobbyRE::DBInfo *DBInfo)
{
	uint16_t pluginIdx = *REL::Relocation<uint16_t *>(REL::ID(937669)).get();
	uint64_t componentID = (formID | (uint64_t)pluginIdx << 32) << 16;

	uintptr_t moduleState = *REL::Relocation<uintptr_t*>(REL::ID(938414)).get();


	BobbyRE::DBLookupContext ctx{ {0}, {0}, 0, 0, 0 };
	BobbyRE::DBInfo* context = *(BobbyRE::DBInfo**)(moduleState + 0x58);
	uint64_t out[4] = { 0 };


	using func_dbLookup_t = void* (void *, void*, uint64_t *);
	REL::Relocation<func_dbLookup_t>dbLookup{ REL::ID(126806) };

	using func_traverseComponentTree_t = bool(BobbyRE::DBInfo *, BobbyRE::DBLookupContext *, uint64_t*);
	REL::Relocation<func_traverseComponentTree_t>traverseComponentTree{ REL::ID(126584) };

	traverseComponentTree(context, &ctx, &componentID);
	BobbyRE::PlanetComponent* result = getComponent<BobbyRE::PlanetComponent>(&ctx);

	return result;

}

RE::BGSAtmosphere* getPlanetAtmosphere(void* a1)
{
	using func_getAtmosphere_t = RE::BGSAtmosphere* (void*);
	REL::Relocation<func_getAtmosphere_t>getAtmosphere{ REL::ID(51351) };

	return getAtmosphere(a1);
}

float lerp(float a, float b, float t)
{
	return a + (b - a) * t;
}

void fadeWeather(float t)
{
	if (t > 1)
		return;
	RE::BGSVolumetricLighting* volumetrics = RE::TES::GetSingleton()->sky->currentWeather->volumetricLighting;
	float target = 0;

	volumetrics->settings.exterior.fogDensity.minFogDensity    = target;
	volumetrics->settings.exterior.fogThickness.minFogThickness = target;

	volumetrics->settings.exterior.fogThickness.maxFogThickness = lerp(volumetrics->settings.exterior.fogThickness.maxFogThickness, target, t);
	volumetrics->settings.exterior.fogThickness.maxFogThickness = lerp(volumetrics->settings.exterior.fogThickness.maxFogThickness, target, t);

	if (g_takeoffState.cloudForm)
	{
		for (int i = 0; i < 4; i++)
		{
			g_takeoffState.cloudForm->layers[i].alphaAdd      = lerp(g_takeoffState.originalLayers[i].alphaAdd, target, t);
			g_takeoffState.cloudForm->planes[i].alphaAdd      = lerp(g_takeoffState.originalPlanes[i].alphaAdd, target, t);
			g_takeoffState.cloudForm->layers[i].alphaMultiply = lerp(g_takeoffState.originalLayers[i].alphaMultiply, target, t);
			g_takeoffState.cloudForm->planes[i].alphaMultiply = lerp(g_takeoffState.originalPlanes[i].alphaMultiply, target, t);
		}
	}
}

void fadeAtmospherics(float t)
{
	if (t > 1)
		return;
	float targetAtmo           = atmosphereSettings->surfaceRadius + 100.0;
	float targetStarVisibility = 1.0;
	g_takeoffState.atmosphereForm->settings.stars.disableSimulatedVisibility = 1;

	atmosphereSettings->atmosphereTopRadius                        = lerp(g_takeoffState.originalAtmosphereTopRadius, targetAtmo, 1 - pow(1 - t, 3));
	g_takeoffState.atmosphereForm->settings.stars.staticVisibility = lerp(g_takeoffState.originalStarVisibility, targetStarVisibility, t);
}

void fadeImageSpaceSettings(float t)
{
	if (t > 1)
		return;
	if (g_surfaceImageSpaceSettings)
	{
		g_surfaceImageSpaceSettings->IndirectLighting.IndirectDiffuseMultiplier.value = 
			lerp(g_originalSurfaceImageSpaceSettings->IndirectLighting.IndirectDiffuseMultiplier.value, 0, t);

		g_surfaceImageSpaceSettings->IndirectLighting.IndirectSpecularMultiplier.value = 
			lerp(g_originalSurfaceImageSpaceSettings->IndirectLighting.IndirectSpecularMultiplier.value, 0, t);
		
		g_surfaceImageSpaceSettings->SunAndSky.SkyLightingMultiplier.value =
			lerp(g_originalSurfaceImageSpaceSettings->SunAndSky.SkyLightingMultiplier.value, 0, t);

//		g_surfaceImageSpaceSettings->SunAndSky.SpaceGlowBackgroundScaleOverride.value =
//			lerp(g_originalSurfaceImageSpaceSettings->SunAndSky.SpaceGlowBackgroundScaleOverride.value, 25.0f, t);

		g_surfaceImageSpaceSettings->SunAndSky.StarfieldStarBrightnessScaleOverride.value =
			lerp(g_originalSurfaceImageSpaceSettings->SunAndSky.StarfieldStarBrightnessScaleOverride.value, 10.0f, t);

		g_surfaceImageSpaceSettings->SunAndSky.StarfieldBackgroundScaleOverride.value =
			lerp(g_originalSurfaceImageSpaceSettings->SunAndSky.StarfieldBackgroundScaleOverride.value, 5.0f, t);
	}
}

void fadeStarGlow(float t)
{
	float target = g_takeoffState.originalStarGlow;

	spaceAtmosphereSettings->surfaceRadius = lerp(0, target, t);
}

void fadeBlur(float t, bool enable)
{
	if (t > 1 || !settings.EnableLoadBlur)
		return;
	if (enable)
		g_takeoffState.blurValue = lerp(0.0, 5.0, t);
	else
		g_takeoffState.blurValue = lerp(5.0, 0.0, t);
}

void extendTakeoffAnim(RE::TESObjectREFR* ship, float t)
{
	float targetAngleY = g_takeoffState.endAnimAngle.y + 0.1;
	float targetAngleZ = g_takeoffState.endAnimAngle.z - 0.1;
	float targetPosZ   = g_takeoffState.endAnimPos.z + 30;

	ship->data.angle.y = lerp(g_takeoffState.endAnimAngle.y, targetAngleY, t);
	ship->data.angle.z = lerp(g_takeoffState.endAnimAngle.z, targetAngleZ, t);

	//camera goes whack if i do this
	//ship->data.location.z = lerp(g_takeoffState.endAnimPos.z, targetPosZ, t);

}

void toggleFrameDraw(bool enable) //kinda dumb but eh
{
	uintptr_t addr = REL::Relocation<uintptr_t>(REL::ID(143837)).address();
	void* call = (void*)(addr + 0x79);

	int8_t instruction[3];

	if (enable)
	{
		int8_t callRax38[3] = { 0xFF, 0x50, 0x38 };
		memcpy(instruction, callRax38, 3);
	}
	else {
		int8_t nop[3] = { 0x90, 0x90, 0x90};
		memcpy(instruction, nop, 3);
	}

	REL::WriteSafeData(call, instruction);
}

void toggleForceCloudRefresh(bool enable)
{
	REX::INFO("toggle force cloud refresh: {}", enable);
	uintptr_t addr = REL::Relocation<uintptr_t>(REL::ID(69241)).address();
	void* shouldRefresh = (void*)(addr + 0x49);

	int8_t instruction[6];

	if (enable)
	{
		int8_t nop[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
		memcpy(instruction, nop, 6);
	}
	else {
		int8_t jz[6] = { 0x0F, 0x84, 0x60, 0x06, 0x00, 0x00 };
		memcpy(instruction, jz, 6);
	}

	REL::WriteSafeData(shouldRefresh, instruction);
}

class TakeOffEventSink : public RE::BSTEventSink<RE::Spaceship::TakeOffEvent> 
{
	using func_playerFastTravel_t = bool(RE::PlayerCharacter*, void*, RE::NiPoint3*, RE::TESObjectCELL*, bool, bool, bool, bool, uint16_t);

	RE::BSEventNotifyControl ProcessEvent(const RE::Spaceship::TakeOffEvent& event, RE::BSTEventSource<RE::Spaceship::TakeOffEvent>* a_source)
	{
		RE::TESObjectREFR* ship = event.ship.get();

		if (g_takeoffState.state == NOT_STARTED) //npc ships send extra event?
		{
			if (RE::PlayerCharacter::GetSingleton()->GetSpaceship() == ship)
			{
				REX::INFO("Player takeoff event");
				REX::INFO("State: {}", event.state);
				if (event.state == 0)
				{
					RE::Sky* sky = RE::TES::GetSingleton()->sky;
					g_takeoffState.cloudForm = sky->currentWeather->clouds;
					if (!g_takeoffState.cloudForm)
					{
						if (sky->lastWeather)
							g_takeoffState.cloudForm = sky->lastWeather->clouds;
					}

					g_takeoffState.atmosphereForm = getPlanetAtmosphere(BobbyRE::BGSPlanet::Manager::GetSingleton()->char110);

					g_takeoffState.originalStarVisibility = g_takeoffState.atmosphereForm->settings.stars.staticVisibility;
					g_takeoffState.originalDisableSimulatedVisibility = g_takeoffState.atmosphereForm->settings.stars.disableSimulatedVisibility;
					g_takeoffState.originalAtmosphereTopRadius = atmosphereSettings->atmosphereTopRadius;

					memcpy(originalAtmosphereSettings, atmosphereSettings, sizeof(BobbyRE::atmosphereRenderSettings));

					toggleForceCloudRefresh(true);

					g_takeoffState.state = TAKEOFF_ANIM_STARTED;

					using func_getRotationMatrix_t = RE::NiMatrix3* (RE::TESObjectREFR*, RE::NiMatrix3*);
					REL::Relocation<func_getRotationMatrix_t>getRotationMatrix{ REL::ID(63286) };

					getRotationMatrix(ship, &landedRot);
				}
			}
		}
		return RE::BSEventNotifyControl::kContinue;
	}
};

namespace hooks
{
	using func_updateSpaceLocation_t = bool(uint32_t, BobbyRE::NiPoint4_double *, RE::NiMatrix3 *, double);
	func_updateSpaceLocation_t* original_updateSpaceLocation;

	using func_setSpacePlanetOrbit_t = void*(va_list, BobbyRE::DBInfo **, void *, uint32_t, double *, RE::NiMatrix3*, BobbyRE::NiPoint4_double*, uint32_t, bool);
	func_setSpacePlanetOrbit_t* original_setSpacePlanetOrbit;

	using func_PCUpdate_t = void(RE::PlayerCharacter *, float);
	func_PCUpdate_t* original_PCUpdate;

	using func_unkFunc_t = void(uintptr_t, void *, uint32_t);
	func_unkFunc_t* original_unkFunc;

	using func_unkFunc2_t = void*(void *, RE::TESImageSpace::ImageSpaceSettings*);
	func_unkFunc2_t* original_unkFunc2;

	bool hook_updateSpaceLocation(uint32_t objectFormID, BobbyRE::NiPoint4_double* pos, RE::NiMatrix3* rot, double a4)
	{
		return original_updateSpaceLocation(objectFormID, pos, rot, a4);
	}

	void* hook_setSpacePlanetOrbit(va_list a1, BobbyRE::DBInfo** DBInfo, void* a3, uint32_t planetFormID, double* planetLocation, RE::NiMatrix3* rot, BobbyRE::NiPoint4_double* pos, uint32_t a8, bool a9)
	{
		void *result = original_setSpacePlanetOrbit(a1, DBInfo, a3, planetFormID, planetLocation, rot, pos, a8, a9);

		if (g_takeoffState.state == TAKEOFF_LOAD_STARTED)
		{
			BobbyRE::BGSPlanet::Manager* planetManager = BobbyRE::BGSPlanet::Manager::GetSingleton();

			float lat = planetManager->tileLatitude;
			float lon = planetManager->tileLongitude;

			RE::NiPoint3 localUp(cos(lat) * cos(lon), cos(lat) * sin(lon), sin(lat));
			RE::NiPoint3 localNorth(-sin(lat) * cos(lon), -sin(lat) * sin(lon), cos(lat));
			RE::NiPoint3 localEast(-sin(lon), cos(lon), 0);

			using func_getPlanetRadius_t = float(void**, uint32_t);
			REL::Relocation<func_getPlanetRadius_t>getPlanetDiameter{ REL::ID(57552) };
			void* componentDB = BobbyRE::BSService::JobSite::GetSingleton()->ComponentDB;

			double radius = getPlanetDiameter(&componentDB, planetFormID) / 2;
			BobbyRE::PlanetComponent* planetComponent = getPlanetComponent(planetFormID, *DBInfo);
			RE::NiMatrix3 planetRot = planetComponent->rotation;

			RE::NiPoint3 worldUp = planetRot.Transpose() * localUp;
			RE::NiPoint3 worldNorth = planetRot.Transpose() * localNorth;
			RE::NiPoint3 worldEast = planetRot.Transpose() * localEast;

			REX::INFO("planetLocation: {}, {}, {}", planetLocation[0], planetLocation[1], planetLocation[2]);
			REX::INFO("Radius: {}", radius);
			REX::INFO("Lat/long: {}, {}", planetManager->tileLatitude, planetManager->tileLongitude);

			double altitude = 2500000.0;
			if (radius < 5000000)
				altitude /= 500;

			pos->x = planetComponent->X + worldUp.x * (radius + altitude);
			pos->y = planetComponent->Y + worldUp.y * (radius + altitude);
			pos->z = planetComponent->Z + worldUp.z * (radius + altitude);

			REX::INFO("final pos: {}, {}, {}", pos->x, pos->y, pos->z);

			RE::NiMatrix3 surfaceMat(
				{ worldEast.x,  worldEast.y,  worldEast.z,  0 },
				{ worldNorth.x, worldNorth.y, worldNorth.z, 0 },
				{ worldUp.x,    worldUp.y,    worldUp.z,    0 }
			);

			RE::NiMatrix3 flatRot(
				{ landedRot[0][0], landedRot[0][1], 0, 0 },
				{ landedRot[1][0], landedRot[1][1], 0, 0 },
				{ 0, 0, 1, 0 }
			);

			RE::NiMatrix3 delta = takeoffRot * flatRot.Transpose();

			RE::NiPoint3 x = landedRot * RE::NiPoint3(1, 0, 0);
			RE::NiPoint3 y = landedRot * RE::NiPoint3(0, 1, 0);
			RE::NiPoint3 z = landedRot * RE::NiPoint3(0, 0, 1);

			*rot = delta * flatRot * surfaceMat;
		}
		return result;
	}

	void hook_PCUpdate(RE::PlayerCharacter* player, float dt)
	{
		if (settings.EnableLandingProbe)
			landing::sample(dt, static_cast<int>(g_takeoffState.state));

		switch (g_takeoffState.state)
		{
		case NOT_STARTED:
		{
			break;
		}
		case TAKEOFF_ANIM_STARTED:
		{
			if (g_takeoffState.prefadeTimer < 9.0)
			{
				g_takeoffState.prefadeTimer += dt;
				if (g_takeoffState.prefadeTimer > 9.0)
				{
					REX::INFO("fade start");
					if (g_takeoffState.cloudForm)
					{
						REX::INFO("clouds detected");
						g_takeoffState.originalLayers = g_takeoffState.cloudForm->layers;
						g_takeoffState.originalPlanes = g_takeoffState.cloudForm->planes;
					}
					else
						REX::INFO("No clouds");
				}
			}
			else
			{
				fadeWeather(g_takeoffState.fadeTimer / (5 + settings.TakeoffExtensionLength * 0.1f));
				fadeAtmospherics(g_takeoffState.fadeTimer / (5 + settings.TakeoffExtensionLength));
				fadeImageSpaceSettings(g_takeoffState.fadeTimer / (5 + settings.TakeoffExtensionLength * 0.17));
				g_takeoffState.fadeTimer += dt;

				//vanilla anim complete at around 4.9 seconds
				if (g_takeoffState.fadeTimer > 5)
				{
					RE::TESObjectREFR* ship = player->GetSpaceship();
					if (g_takeoffState.endAnimAngle == RE::NiPoint3{0.0, 0.0, 0.0})
					{
						g_takeoffState.endAnimAngle = ship->data.angle;
						g_takeoffState.endAnimPos   = ship->data.location;
					}

					if(settings.TakeoffExtensionLength > 1.0)
						extendTakeoffAnim(ship, (g_takeoffState.fadeTimer - 5) / settings.TakeoffExtensionLength);
				}

				if (g_takeoffState.fadeTimer > (5 + settings.TakeoffExtensionLength - 0.5))
				{
					fadeBlur((g_takeoffState.fadeTimer - (5 + settings.TakeoffExtensionLength - 0.5)) 
								/ 0.5
								, true);
				}

				if (g_takeoffState.fadeTimer > 5 + settings.TakeoffExtensionLength)
				{
					g_takeoffState.state = TAKEOFF_LOAD_STARTED;
					REX::INFO("fade complete");
				}
			}
			break;
		}
		case TAKEOFF_LOAD_STARTED:
		{
			toggleFrameDraw(false);
			manualLoadSystem(player->GetSpaceship());
			RE::TES::GetSingleton()->sky->mode = 0;
			g_takeoffState.state = TAKEOFF_LOAD_COMPLETE;
			break;
		}
		case TAKEOFF_LOAD_COMPLETE:
		{
			if (g_takeoffState.arrivalTimer < 0.05)
			{
				g_takeoffState.arrivalTimer += dt;
				if (g_takeoffState.arrivalTimer > 0.05)
					toggleFrameDraw(true);
			}
			else if (g_takeoffState.arrivalTimer < 0.3)
			{
				g_takeoffState.arrivalTimer += dt;
				if (g_takeoffState.arrivalTimer > 0.3)
				{
					if (!spaceAtmosphereSettings)
						spaceAtmosphereSettings = atmosphereSettings;

					g_takeoffState.originalStarGlow = spaceAtmosphereSettings->surfaceRadius;
					spaceAtmosphereSettings->surfaceRadius = 0;
					RE::TES::GetSingleton()->sky->mode = 1;
					REX::INFO("Star glow fade start");
				}
			}
			else if (g_takeoffState.arrivalTimer < 2.5)
			{
				g_takeoffState.arrivalTimer += dt;
				fadeStarGlow(g_takeoffState.arrivalTimer / 2.5);
				fadeBlur((g_takeoffState.arrivalTimer - 0.3) / 2.2, false);
			}
			else
			{
				REX::INFO("star glow fade complete");
				g_takeoffState.state = NOT_STARTED;
				g_takeoffState.prefadeTimer = 0.0;
				g_takeoffState.fadeTimer = 0.0;
				g_takeoffState.arrivalTimer = 0.0;

				g_takeoffState.endAnimAngle = RE::NiPoint3{ 0.0, 0.0, 0.0 };
				g_takeoffState.endAnimPos   = RE::NiPoint3{ 0.0, 0.0, 0.0 };

				g_takeoffState.atmosphereForm->settings.stars.staticVisibility = g_takeoffState.originalStarVisibility;
				g_takeoffState.atmosphereForm->settings.stars.disableSimulatedVisibility = g_takeoffState.originalDisableSimulatedVisibility;

				if (g_takeoffState.cloudForm)
				{
					g_takeoffState.cloudForm->layers = g_takeoffState.originalLayers;
					g_takeoffState.cloudForm->planes = g_takeoffState.originalPlanes;
				}

				if (g_surfaceImageSpaceSettings)
				{
					free((void *)g_surfaceImageSpaceSettings);
					g_surfaceImageSpaceSettings = 0;
				}

				toggleForceCloudRefresh(false);
			}
			break;
		}
		}
		original_PCUpdate(player, dt);
	}

	void hook_unkFunc(uintptr_t a1, void* a2, uint32_t a3)
	{
		original_unkFunc(a1, a2, a3);
		uint64_t v14 = *(uint32_t*)(*(uint64_t *)(a1 + 712) + 4LL * a3);
		atmosphereSettings = (BobbyRE::atmosphereRenderSettings*)(*(uint64_t*)(a1 + 968) + 192 * v14);
	}

	void *hook_unkFunc2(void* a1, RE::TESImageSpace::ImageSpaceSettings *settings)
	{
		if (g_takeoffState.state == TAKEOFF_ANIM_STARTED || g_takeoffState.state == TAKEOFF_LOAD_STARTED)
		{
			if (!g_surfaceImageSpaceSettings)
			{
				g_surfaceImageSpaceSettings = (RE::TESImageSpace::ImageSpaceSettings*)malloc(sizeof(RE::TESImageSpace::ImageSpaceSettings));
				if (g_surfaceImageSpaceSettings)
				{
					*g_surfaceImageSpaceSettings = *settings;
					*g_originalSurfaceImageSpaceSettings = *g_surfaceImageSpaceSettings;
				}
			}
			else
			{
				*settings = *g_surfaceImageSpaceSettings;
			}
		}
		if(g_takeoffState.state != NOT_STARTED)
			settings->Blur.BlurRadiusValue.value = g_takeoffState.blurValue;

		return original_unkFunc2(a1, settings);
	}

	void install() {
		uintptr_t addr1 = REL::Relocation<uintptr_t>(REL::ID(97772)).address();
		uintptr_t addr2 = REL::Relocation<uintptr_t>(REL::ID(57659)).address();
		uintptr_t addr3 = REL::Relocation<uintptr_t>(REL::ID(57653)).address();
		uintptr_t addr4 = REL::Relocation<uintptr_t>(REL::ID(128483)).address();
		uintptr_t addr5 = REL::Relocation<uintptr_t>(REL::ID(99411)).address();
		uintptr_t addr6 = REL::Relocation<uintptr_t>(REL::ID(99415)).address();

		uintptr_t playerShipUpdateCall = addr1 + 0x68;
		uintptr_t updateSpaceLocationCall = addr2 + 0x101f;

		uintptr_t setSpacePlanetOrbitCall;
		if (SFSE::GetSFSEVersion() < 131408)
			setSpacePlanetOrbitCall = addr3 + 0x89e;
		else
			setSpacePlanetOrbitCall = addr3 + 0x8f3;

		uintptr_t unkFuncCall = addr4 + 0x14a;
		uintptr_t PCUpdateCall = addr5 + 0xe2;
		uintptr_t unkFunc2Call = addr6 + 0x350;

		REL::Trampoline& tramp = REL::GetTrampoline();

		//original_updateSpaceLocation = (func_updateSpaceLocation_t*)tramp.write_call<5>(updateSpaceLocationCall, hook_updateSpaceLocation);
		original_setSpacePlanetOrbit = (func_setSpacePlanetOrbit_t*)tramp.write_call<5>(setSpacePlanetOrbitCall, hook_setSpacePlanetOrbit);
		original_PCUpdate = (func_PCUpdate_t*)tramp.write_call<5>(PCUpdateCall, hook_PCUpdate);
		original_unkFunc = (func_unkFunc_t*)tramp.write_call<5>(unkFuncCall, hook_unkFunc);
		original_unkFunc2 = (func_unkFunc2_t*)tramp.write_call<5>(unkFunc2Call, hook_unkFunc2);
	}

}

void OnMessage(SFSE::MessagingInterface::Message* message) 
{
	if (message->type == SFSE::MessagingInterface::kPostDataLoad) 
	{
		REX::INFO("Init");
		settings.load();
		g_takeoffState = {};

		originalAtmosphereSettings = (BobbyRE::atmosphereRenderSettings*)malloc(sizeof(BobbyRE::atmosphereRenderSettings));
		g_originalSurfaceImageSpaceSettings = (RE::TESImageSpace::ImageSpaceSettings*)malloc(sizeof(RE::TESImageSpace::ImageSpaceSettings));

		TakeOffEventSink* TakeOffSink = new TakeOffEventSink();
		auto source = RE::Spaceship::TakeOffEvent::GetEventSource();
		source->RegisterSink(TakeOffSink);

		//stop normal loading
		uintptr_t initiateTakeOffCompleted = REL::Relocation<uintptr_t>(REL::ID(119911)).address();
		void* addCellToLoaderCall = (void *)(initiateTakeOffCompleted + 0x354);

		int8_t nopcall[5] = { 0x90, 0x90, 0x90, 0x90, 0x90 };

		REL::WriteSafeData(addCellToLoaderCall, nopcall);

		//prevent the unload function from showing the load screen
		uintptr_t unloadCurrentLocation = REL::Relocation<uintptr_t>( REL::ID(46037)).address();
		byte* shouldStartLoadScreen = (byte*)(unloadCurrentLocation + 0x9b4);

		byte jmp = 0xEB;

		REL::WriteSafeData(shouldStartLoadScreen, jmp);

		if (settings.DisableTakeOffCam) 
		{
			uintptr_t initiateTakeOffSequence = REL::Relocation<uintptr_t>(REL::ID(119908)).address();
			byte* shouldStartTakeOffCam = (byte*)(initiateTakeOffSequence + 0x1c9);

			REL::WriteSafeData(shouldStartTakeOffCam, jmp);
		}

		hooks::install();

		if (settings.EnableLandingProbe)
			landing::install();
	}
}
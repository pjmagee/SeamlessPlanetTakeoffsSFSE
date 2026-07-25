#pragma once

#include <chrono>

#include "BobbyRE/BobbyRE.h"

// Defined in plugin.cpp. Nops/restores the frame-present call, so the last drawn frame is
// held on screen instead of anything new being rendered. This is exactly how seamless
// takeoff hides its own ~6s load stall.
void toggleFrameDraw(bool enable);

// Defined in plugin.cpp, below this include.
extern config settings;

// Seamless landing — observation probe.
//
// This installs NO new hook sites and writes NO engine state. It piggybacks on the
// already-installed PCUpdate hook to edge-trigger a log line whenever something
// relevant changes, plus a sink for RE::Spaceship::LandingEvent.
//
// Purpose is to answer, from a real landing:
//   1. Does Spaceship::LandingEvent fire for the player, and what is its payload?
//      (CommonLibSF declares the struct with an EMPTY body — TakeOffEvent by contrast
//      declares NiPointer<TESObjectREFR> ship + uint32_t state.)
//   2. Where is the cell-swap boundary in wall-clock terms — i.e. how long the
//      procedural surface load actually takes, which sets the descent length.
//   3. What sky->mode does across the transition.
//
// See docs/SEAMLESS-LANDING-DESIGN.md.

namespace landing
{
	struct probeState
	{
		bool     initialised;
		float    clock;          // seconds since plugin init, accumulated from PCUpdate dt
		float    lastSampleTime;
		float    lastHeartbeat;

		// Wall-clock stall detection. PCUpdate does not run during a loading screen, so
		// a large real-time gap between consecutive calls IS the load. This is how we
		// measure what the descent has to cover.
		std::chrono::steady_clock::time_point lastTick;
		bool                                  haveLastTick;

		// GetSpaceship() returns null mid-transition, so cache the last non-null value
		// to identify the player's ship in a LandingEvent that fires during one.
		void*    lastKnownPlayerShip;

		// edge-tracked values
		void*    lastParentCell;
		uint32_t lastParentCellID;
		uint32_t lastSkyMode;
		void*    lastLocation;
		uint32_t lastLocationID;

		// landing-event bookkeeping
		bool     sawLandingEvent;
		float    landingEventTime;

		// --- load-screen hiding ---------------------------------------------------
		// Armed on the player's own LandingEvent state=0, which fires ~8-10ms before the
		// load stall. Frame draw goes off so the last space frame is held rather than the
		// black loading screen appearing, then back on once the load has clearly finished.
		bool                                  hidingLoad;
		bool                                  loadFinished;
		float                                 loadFinishedAt;
		std::chrono::steady_clock::time_point hideStartedReal;
	};

	inline probeState g_probe{};

	// Safe accessors — every one of these can legitimately be null mid-transition.
	inline RE::TESObjectCELL* currentCell()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		return player ? player->parentCell : nullptr;
	}

	inline uint32_t formIDOf(RE::TESForm* form)
	{
		return form ? form->GetFormID() : 0u;
	}

	inline uint32_t skyMode()
	{
		auto* tes = RE::TES::GetSingleton();
		if (!tes || !tes->sky)
			return 0xFFFFFFFFu;
		return static_cast<uint32_t>(tes->sky->mode);
	}

	// Edge-triggered sampler. Called every frame from hook_PCUpdate; only emits a log
	// line when something actually changes, so a normal session stays quiet.
	inline void sample(float dt, int takeoffState)
	{
		g_probe.clock += dt;

		// --- stall detector -------------------------------------------------------
		// The loading screen is a window in which PCUpdate simply is not called. Compare
		// real elapsed time against the frame dt the engine handed us; a large divergence
		// is the load, and its magnitude is the descent budget we need to cover.
		const auto now = std::chrono::steady_clock::now();
		if (g_probe.haveLastTick)
		{
			const double wall = std::chrono::duration<double>(now - g_probe.lastTick).count();
			if (wall > 0.25)
			{
				REX::INFO("[probe] t={:.3f} STALL {:.3f}s wall (engine dt={:.3f}s) cell={:08X} skyMode={} takeoffState={} <-- load boundary",
					g_probe.clock, wall, dt, formIDOf(currentCell()), skyMode(), takeoffState);

				// A stall this long while we are hiding IS the landing load completing:
				// this is the first frame the engine has run since it began.
				if (g_probe.hidingLoad && !g_probe.loadFinished && wall > 1.0)
				{
					g_probe.loadFinished   = true;
					g_probe.loadFinishedAt = g_probe.clock;
					REX::INFO("[probe] landing load finished after {:.3f}s - restoring frame draw shortly", wall);
				}
			}
		}
		g_probe.lastTick     = now;
		g_probe.haveLastTick = true;

		// --- restore / watchdog ---------------------------------------------------
		if (g_probe.hidingLoad)
		{
			const double heldReal = std::chrono::duration<double>(now - g_probe.hideStartedReal).count();

			// Normal path: brief grace after the load so the first surface frame is ready,
			// matching what takeoff does at src/plugin.cpp:522.
			if (g_probe.loadFinished && (g_probe.clock - g_probe.loadFinishedAt) > 0.05f)
			{
				toggleFrameDraw(true);
				g_probe.hidingLoad   = false;
				g_probe.loadFinished = false;
				REX::INFO("[probe] frame draw restored (held {:.3f}s real)", heldReal);
			}
			// Watchdog. A held frame that never gets restored is a hung-looking game, so
			// this is a hard ceiling on real time, NOT game time - the engine clock does
			// not advance during the load at all.
			else if (heldReal > 25.0)
			{
				toggleFrameDraw(true);
				g_probe.hidingLoad   = false;
				g_probe.loadFinished = false;
				REX::WARN("[probe] WATCHDOG - frame draw force-restored after {:.3f}s real", heldReal);
			}
		}

		if (auto* player = RE::PlayerCharacter::GetSingleton())
		{
			if (void* ship = player->GetSpaceship())
				g_probe.lastKnownPlayerShip = ship;
		}

		RE::TESObjectCELL* cell   = currentCell();
		uint32_t           cellID = formIDOf(cell);
		uint32_t           mode   = skyMode();

		RE::BGSLocation* loc   = nullptr;
		uint32_t         locID = 0;
		if (auto* player = RE::PlayerCharacter::GetSingleton())
		{
			loc   = player->GetCurrentLocation();
			locID = formIDOf(loc);
		}

		if (!g_probe.initialised)
		{
			g_probe.initialised      = true;
			g_probe.lastParentCell   = cell;
			g_probe.lastParentCellID = cellID;
			g_probe.lastSkyMode      = mode;
			g_probe.lastLocation     = loc;
			g_probe.lastLocationID   = locID;
			REX::INFO("[probe] baseline t={:.3f} cell={:08X} skyMode={} loc={:08X} takeoffState={}",
				g_probe.clock, cellID, mode, locID, takeoffState);
			return;
		}

		const bool cellChanged = (cell != g_probe.lastParentCell) || (cellID != g_probe.lastParentCellID);
		const bool modeChanged = (mode != g_probe.lastSkyMode);
		const bool locChanged  = (loc != g_probe.lastLocation) || (locID != g_probe.lastLocationID);

		if (cellChanged || modeChanged || locChanged)
		{
			REX::INFO("[probe] t={:.3f} CHANGE cell={:08X}->{:08X} skyMode={}->{} loc={:08X}->{:08X} takeoffState={}{}",
				g_probe.clock,
				g_probe.lastParentCellID, cellID,
				g_probe.lastSkyMode, mode,
				g_probe.lastLocationID, locID,
				takeoffState,
				g_probe.sawLandingEvent
					? std::format(" (+{:.3f}s since LandingEvent)", g_probe.clock - g_probe.landingEventTime)
					: std::string{});

			g_probe.lastParentCell   = cell;
			g_probe.lastParentCellID = cellID;
			g_probe.lastSkyMode      = mode;
			g_probe.lastLocation     = loc;
			g_probe.lastLocationID   = locID;
		}

		// Low-rate heartbeat while a landing is in flight, so we can see the gap
		// (loading screen) even if nothing else changes during it.
		if (g_probe.sawLandingEvent && (g_probe.clock - g_probe.lastSampleTime) > 0.25f)
		{
			g_probe.lastSampleTime = g_probe.clock;

			double lat = 0.0, lon = 0.0;
			if (auto* pm = BobbyRE::BGSPlanet::Manager::GetSingleton())
			{
				lat = pm->tileLatitude;
				lon = pm->tileLongitude;
			}

			REX::INFO("[probe] t={:.3f} +{:.3f}s landing tick cell={:08X} skyMode={} lat={:.5f} lon={:.5f}",
				g_probe.clock, g_probe.clock - g_probe.landingEventTime, cellID, mode, lat, lon);

			// Stop the heartbeat 30s after the event so a stuck flag can't spam the log.
			if ((g_probe.clock - g_probe.landingEventTime) > 30.0f)
			{
				REX::INFO("[probe] landing heartbeat expiring (30s)");
				g_probe.sawLandingEvent = false;
			}
		}
	}

	// RE::Spaceship::LandingEvent has an EMPTY struct body in CommonLibSF, so we cannot
	// name its fields. Log the leading 16 bytes (the size of the sibling TakeOffEvent:
	// NiPointer<TESObjectREFR> + uint32_t + padding) and compare word 0 against the
	// player's ship POINTER VALUE. Pointer comparison only — nothing is dereferenced,
	// so a wrong layout guess cannot fault.
	class LandingEventSink : public RE::BSTEventSink<RE::Spaceship::LandingEvent>
	{
		RE::BSEventNotifyControl ProcessEvent(
			const RE::Spaceship::LandingEvent&                   event,
			RE::BSTEventSource<RE::Spaceship::LandingEvent>*     a_source) override
		{
			(void)a_source;

			uint64_t words[2] = { 0, 0 };
			memcpy(words, &event, sizeof(words));

			void* playerShip = nullptr;
			if (auto* player = RE::PlayerCharacter::GetSingleton())
				playerShip = player->GetSpaceship();

			// GetSpaceship() is null mid-transition — which is exactly when the player's
			// own LandingEvent fires — so fall back to the cached pointer. Confirmed from
			// the first probe run: word0 matched the cached ship exactly.
			void* compareTo = playerShip ? playerShip : g_probe.lastKnownPlayerShip;

			const bool isPlayerShip = (compareTo != nullptr) &&
			                          (reinterpret_cast<void*>(words[0]) == compareTo);

			g_probe.sawLandingEvent  = true;
			g_probe.landingEventTime = g_probe.clock;
			g_probe.lastSampleTime   = g_probe.clock;

			REX::INFO("[probe] t={:.3f} LandingEvent ship={:016X} state={} isPlayerShip={} (live={:016X} cached={:016X})",
				g_probe.clock, words[0],
				static_cast<uint32_t>(words[1] & 0xFFFFFFFFull),
				isPlayerShip,
				reinterpret_cast<uintptr_t>(playerShip),
				reinterpret_cast<uintptr_t>(g_probe.lastKnownPlayerShip));

			// The player's own state=0 fires ~8-10ms before the load stall (measured twice:
			// 8.88s and 9.93s loads, at +0.010s and +0.008s). That is the only window we get,
			// so arm here and hold the last frame instead of letting the black screen appear.
			const uint32_t state = static_cast<uint32_t>(words[1] & 0xFFFFFFFFull);

			if (settings.HideLandingLoadScreen && isPlayerShip && state == 0 && !g_probe.hidingLoad)
			{
				g_probe.hidingLoad      = true;
				g_probe.loadFinished    = false;
				g_probe.hideStartedReal = std::chrono::steady_clock::now();

				toggleFrameDraw(false);
				REX::INFO("[probe] player landing detected - holding frame, load screen should not appear");
			}

			return RE::BSEventNotifyControl::kContinue;
		}
	};

	inline void install()
	{
		// REL::ID 120551 is resolved through versionlib on first call. If this runtime's
		// address library doesn't carry that ID, the resolution itself throws — catch it
		// so a stale versionlib degrades to "no landing sink" instead of crashing the game.
		// The edge-triggered sampler still runs and still captures the cell-swap boundary,
		// so a failure here costs detail, not the whole run.
		try
		{
			auto* source = RE::Spaceship::LandingEvent::GetEventSource();
			if (!source)
			{
				REX::WARN("[probe] LandingEvent::GetEventSource() returned null - no landing sink registered; sampler still active");
				return;
			}

			source->RegisterSink(new LandingEventSink());
			REX::INFO("[probe] landing probe installed (observation only, no engine writes)");
		}
		catch (const std::exception& e)
		{
			REX::WARN("[probe] could not resolve LandingEvent event source ({}) - sampler still active", e.what());
		}
		catch (...)
		{
			REX::WARN("[probe] could not resolve LandingEvent event source (unknown) - sampler still active");
		}
	}
}

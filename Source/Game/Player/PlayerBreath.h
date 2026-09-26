#pragma once

// Breath / drowning.
//
// Pure bookkeeping: an air supply that drains while the avatar cannot breathe
// and refills while it can, and a damage accumulator that runs once the air is
// gone. It knows nothing about water, brushes, sounds or entities. The owning
// controller decides what "cannot breathe" means each tick, and acts on what
// tick() reports back — deals the damage through GameplaySystem::damageEntity()
// (DAMAGE_TYPE::DROWN, so god/buddha apply and no blood is sprayed) and plays
// the gasp. Keeping it that way is what lets the third-person controller adopt
// it later with nothing but a tick call.
//
// Units: tick() takes MILLISECONDS, like every controller update(). Tunables
// are in seconds because that is how anyone reasons about holding their breath.
//
// Sources of "airless", both pushed in from outside:
//   - IPlayerController::isHeadUnderWater() — the EYE probe of the water-zone
//     test, never the chest (isSwimming) probe. Swimming at the surface with
//     your head up must not drown you.
//   - setInAirlessVolume() — CONTENT_NOAIR brushes, tested at the eye by
//     GameplaySystem::updateBrushVolumes().
class PlayerBreath
{
public:
	// What happened this tick, for the controller to act on.
	struct TickResult
	{
		unsigned int damage = 0;   // whole points to deal now (0 = none)
		bool         gasp   = false; // surfaced after running low — play the gasp
	};

	TickResult tick(float dtMs, bool airless, bool alive, bool noclip);

	// Full air, accumulators cleared. Called on init, noclip, save-state load,
	// and whenever the system is disabled.
	void reset();

	// Session-wide, NOT per instance: a controller is rebuilt on every scene
	// load, and a debug toggle that silently switched itself back on every
	// time you loaded a test map would be worse than useless. Console: breath.
	static void setEnabled(bool on) { s_enabled = on; }
	static bool enabled()           { return s_enabled; }

	// Pushed per frame by GameplaySystem's brush-volume test.
	void setInAirlessVolume(bool in) { m_inAirlessVolume = in; }
	bool inAirlessVolume() const     { return m_inAirlessVolume; }

	// --- Readouts (HUD / console) --------------------------------------------
	float air() const          { return m_air; }            // seconds remaining
	float fraction() const     { return maxAirSec > 0.0f ? m_air / maxAirSec : 1.0f; }
	bool  isAirless() const    { return m_airless; }        // as of the last tick
	bool  isDrowning() const   { return m_airless && m_air <= 0.0f; }
	float hudAlpha() const     { return m_hudAlpha; }       // 0..1, faded
	unsigned int drownedDamage() const { return m_drownedDamage; }

	// Console: breath_set. Clamped to [0, maxAirSec].
	void setAir(float seconds);

	// --- Tuning ----------------------------------------------------------------
	// A 100 HP player at full health survives ~10 s of drowning after 20 s of
	// air — 30 s under in total, which is roughly Half-Life's budget.
	float maxAirSec     = 20.0f;
	float refillRate    = 4.0f;   // multiples of the drain rate; 5 s back to full
	float drownDps      = 10.0f;  // damage per second once the air is gone
	float drownTickSec  = 1.0f;   // dealt in chunks this far apart: reads as gasping
	float gaspThreshold = 0.3f;   // surfacing below this fraction plays the gasp
	float hudFadeSec    = 0.4f;

private:
	static bool s_enabled;

	float m_air = 20.0f;         // seconds
	float m_drownAccum = 0.0f;   // fractional damage not yet dealt
	float m_drownTimer = 0.0f;   // ms until the next damage chunk
	float m_hudAlpha = 0.0f;

	// Lowest fraction reached during the current airless stretch; decides
	// whether surfacing earns a gasp.
	float m_lowestFraction = 1.0f;

	bool m_airless = false;
	bool m_inAirlessVolume = false;

	// Running total for this life, for breath_status and a possible
	// Half-Life-style "drowned health comes back" later.
	unsigned int m_drownedDamage = 0;
};

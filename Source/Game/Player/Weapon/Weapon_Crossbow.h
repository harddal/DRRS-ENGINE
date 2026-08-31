#pragma once

#include "../WeaponData.h"
#include "WeaponEffects.h"

#include <vector>

class Weapon_Crossbow : public PlayerWeapon
{
public:
	void precache();
	void init();
	void update();
	void persist();
	void destroy();
	void equip();
	void unequip();
	void startUnequip() override;
	bool isUnequipping() const override { return m_state == State::Unequipping; }
	void idle();
	void move();
	void fire();
	void reload();

	int displayAmmo() const override { return m_boltLoaded ? 1 : 0; }
	// Magazine contents for the save sidecar. See WeaponMagState — slot 0 is this
	// weapon's only counter.
	void saveMagState(WeaponMagState& out) const override { out.slots[0] = m_boltLoaded ? 1 : 0; }
	void loadMagState(const WeaponMagState& in) override  { if (in.slots[0] >= 0) m_boltLoaded = in.slots[0] > 0; }

private:
	// Explicit state machine, same shape as the shotgun's. The chain that matters
	// here is Firing -> Reloading, and it is NOT optional: the asset has no
	// uncocked pose, so the moment the string is loosed the only way back to a
	// drawable idle is through the recock. See the clip table in init().
	enum class State
	{
		Idle,
		Equipping,
		Unequipping,
		Firing,     // "fire"   -> Reloading, always
		Reloading,  // "reload" -> Idle
	};

	State m_state = State::Idle;

	bool  m_firedThisPress = false; // one bolt per click

	// Is there a bolt on the rail? Survives holstering, which is the whole point:
	// the recock is long enough that switching away mid-reload is a real thing a
	// player will do, and coming back to a loaded crossbow would make it a free
	// reload cancel. Starts true — the crossbow is drawn ready.
	bool  m_boltLoaded = true;
	float m_lastFireTime   = 0.0f;
	float m_fireRate       = 400.0f; // ms floor; the fire+reload chain is the real limiter

	// Clip frames, measured off crossbow_animated.glb (see init()).
	static const int m_fireStart      = 0;
	static const int m_reloadStart    = 30;
	static const int m_boltReturnFrame = 64; // hand brings the next bolt up
	static const int m_boltSeatFrame   = 75; // bolt reaches the rail and stops

	// Seconds from the start of insert_shell_shotgun.wav to the transient that
	// should land on the seat. SECONDS, not frames, so a change to m_reloadSpeed
	// cannot desync it — convert with soundLeadFrames(). Same measured value the
	// shotgun uses for this file (onset 80 ms, peak 100 ms).
	static constexpr float m_insertBoltLeadSec = 0.104f;

	bool m_insertBoltPlayed = false; // one seat cue per reload

	// A bolt is one shot, and the recock is one continuous mechanical action, so
	// unlike the shotgun's per-shell loop there is nothing sane to interrupt.
	static constexpr float m_reloadSpeed = 1.0f;

	// --- Projectile ---------------------------------------------------------
	// Flies nearly flat but on a real arc, solved so it lands where the crosshair
	// points — the grenade launcher's ballistic solve with the numbers pushed to
	// the flat end: fast bolt, weak gravity.
	float m_projectileSpeed = 50.0f;
	float m_gravity         = 5.0f;
	float m_lobAngle        = 0.05f; // fallback tilt when the solve has no solution
	float m_damage          = 85.0f;

	// How far down the crosshair ray the arc is solved to land. Deliberately short:
	// the solve compensates for the FULL drop to whatever it aims at, so a distant
	// target tilts the launch upward. At speed 140 / gravity 5 a 100-unit target
	// costs about 0.7 degrees, but a 1000-unit one costs 7.4 — a visible lob, and
	// wrong for a weapon that is meant to fly nearly flat. Past this the bolt
	// simply drops, which is the honest behaviour for a shot beyond its range.
	float m_maxAimRange = 120.0f;

	// Distance ahead of the rail to spawn, so the bolt does not start inside the
	// weapon's own geometry.
	const float m_spawnOffset = 0.35f;

	// --- Sticking ------------------------------------------------------------
	// Soft surfaces hold a bolt; hard ones shatter it. Which is which is decided
	// by the texture's E_MANAGED_MATERIAL — see materialHoldsBolt() in the .cpp.
	//
	// Two minutes is long by projectile standards, but the crossbow is one bolt
	// per fire-and-recock cycle, so the population stays tiny. A stuck bolt costs
	// one frozen scene node and a countdown — no raycast, no integration.
	float m_stuckLifetime = 120000.0f;

	// How far back along the flight path the bolt is parked from the contact
	// point, so the head buries instead of the whole shaft floating on the
	// surface. World units; tune by eye against the stand-in mesh.
	float m_stickEmbed = 0.12f;

	std::vector<WeaponProjectile> m_projectiles;

	// The 'stick' node IS the bolt, and it plays both parts the way the shotgun's
	// slug does: it launches forward to Z 63.37 by f3, parks out there while
	// "gone", then reappears down in the hand at f64 as the next one. Hidden the
	// instant it is loosed, because from then on the real projectile is the bolt.
	MeshPart m_bolt;

	// Bolt tip in the 'stick' node's own local space, model units. The mesh runs
	// to +23.86 along its own Z, and GltfImport's conversion negates that.
	const irr::core::vector3df m_boltTipOffset =
		irr::core::vector3df(0.0f, 0.0f, -23.86f);

	// missile.obj stands in until a bolt model exists. Scaled per axis to the
	// crossbow's OWN bolt mesh rather than guessed: 32.67 x 1.97 model units at
	// the 0.01 viewmodel scale, against missile.obj's 0.381 x 0.192.
	const irr::core::vector3df m_projectileScale =
		irr::core::vector3df(0.103f, 0.103f, 0.857f);

	WeaponEffects m_effects;

	irr::video::ITexture* m_crosshair = nullptr;

	void enterState(State next);
	void updateBolt(float frame);
	void launchBolt();
	void updateProjectiles(float dt);

public:
	WeaponEffects* debugEffects() override { return &m_effects; }

	// Live arc tuning in the F2 window. Only bolts fired AFTER a change pick it
	// up — ones already in flight keep the velocity they launched with, which is
	// correct, and worth remembering while dragging the sliders.
	bool debugBallistics(BallisticTuning& out) override
	{
		out.speed       = &m_projectileSpeed;
		out.gravity     = &m_gravity;
		out.maxAimRange = &m_maxAimRange;
		return true;
	}
};

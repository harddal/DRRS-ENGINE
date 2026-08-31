#pragma once

#include "../WeaponData.h"
#include "WeaponEffects.h"

// ---------------------------------------------------------------------------
// Weapon_Sawnoffs — a pair of break-action double-barrelled sawn-off shotguns.
//
// Four barrels, and every one of them is authored: sawnoffs_animated.glb gives
// each gun its own base, latch, barrels, extractor and TWO shells, and — unlike
// dual_smgs.glb — it gives each gun its OWN FIRE CLIP. f0-11 kicks the right gun
// alone and f12-23 kicks the left alone, so this weapon alternates for real,
// with the animation behind it, rather than in the effects layer only.
//
// One trigger pull is one barrel. The guns take turns, so a full load reads as
// right, left, right, left — four shots, then both guns break open together and
// throw all four cases at once.
// ---------------------------------------------------------------------------
class Weapon_Sawnoffs : public PlayerWeapon
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

	int displayAmmo() const override { return totalShells(); }

	// Two slots, because this is the one weapon in the pack whose barrels are
	// genuinely independent — a single count could not tell "one in each" from
	// "both in the right", and the alternation would resume on the wrong gun.
	void saveMagState(WeaponMagState& out) const override
	{
		out.slots[0] = m_shells[GUN_RIGHT];
		out.slots[1] = m_shells[GUN_LEFT];
	}

	void loadMagState(const WeaponMagState& in) override
	{
		if (in.slots[0] >= 0)
			m_shells[GUN_RIGHT] = (in.slots[0] < BARREL_COUNT ? in.slots[0] : BARREL_COUNT);

		if (in.slots[1] >= 0)
			m_shells[GUN_LEFT]  = (in.slots[1] < BARREL_COUNT ? in.slots[1] : BARREL_COUNT);
	}

private:
	enum Gun    { GUN_RIGHT = 0, GUN_LEFT = 1, GUN_COUNT = 2 };
	enum Barrel { BARREL_COUNT = 2 };

	enum class State
	{
		Idle,
		Equipping,
		Unequipping,
		Firing,    // "fire_right" / "fire_left" -> Idle
		Reloading, // "reload" -> Idle
	};

	State m_state = State::Idle;

	// Which gun takes the next shot. Stored rather than derived from a shot
	// count, so the alternation survives a reload or a weapon switch — the gun
	// that was owed a turn still gets it. Falls through to the other gun when the
	// preferred one is empty, so the last two shells always fire.
	int m_nextGun = GUN_RIGHT;

	struct SideFX
	{
		// One WeaponEffects per GUN, not per barrel. A single instance owns one
		// muzzle attachment, and rather than pay for four, the offset is moved to
		// whichever barrel is firing just before the flash — see fire(). That is
		// what debugMuzzleOffset()/applyMuzzleOffset() already exist for.
		WeaponEffects effects;

		// The extractor, which the cases are thrown from during the reload.
		irr::scene::IBoneSceneNode* unloader = nullptr;

		// The two shells. Reused meshes: they are flung clear as the spent cases
		// and the SAME meshes come back seated as the fresh pair, so each has to
		// be hidden across the throw or it visibly teleports back into the breech.
		MeshPart             slug[BARREL_COUNT];
		irr::core::vector3df slugRest[BARREL_COUNT];
		bool                 slugRestValid = false;
		bool                 slugThrown[BARREL_COUNT] = { false, false };

		// Which barrels have actually been fired since the last reload, so only
		// their cases become brass on the ground.
		//
		// The ANIMATION cannot express this: both slugs in a gun move on identical
		// tracks — lifted together at f38 and flung together from f40 — so a live
		// round cannot be made to stay in the breech without re-authoring the
		// clip. What it CAN decide is which of them leaves a casing behind, which
		// is the half that survives being looked at.
		bool                 fired[BARREL_COUNT] = { false, false };

	};

	SideFX m_gun[GUN_COUNT];

	// --- Ammunition ----------------------------------------------------------
	// Two per gun, counted per gun rather than pooled: the whole point of a pair
	// of doubles is that you know which hand still has something in it.
	int m_shells[GUN_COUNT] = { BARREL_COUNT, BARREL_COUNT };

	int totalShells() const { return m_shells[GUN_RIGHT] + m_shells[GUN_LEFT]; }

	// --- Cadence -------------------------------------------------------------
	// One barrel per press — these are break actions, not automatics — with a
	// floor under it so mashing cannot outrun the fire clip. 180 ms lets all four
	// go in under a second, which is the entire appeal.
	bool  m_firedThisPress = false;
	float m_lastFireTime   = 0.0f;
	static constexpr float m_fireInterval = 180.0f;

	// The fire clips are 11 frames of travel. At 2.2x that is ~167 ms, just
	// inside the cadence, so a fast four-shot dump never restarts a clip that has
	// not finished.
	static constexpr float m_fireSpeed = 2.2f;

	// --- Pellets -------------------------------------------------------------
	// Wider and slightly weaker per pellet than the pump shotgun: the barrels are
	// a third of the length, which is what sawing them off buys and costs.
	int   m_pelletCount     = 10;
	float m_damagePerPellet = 26.0f;
	float m_spreadAngle     = 5.0f; // degrees, half-angle cone

	// --- Reload --------------------------------------------------------------
	// The authored break-open is 1.9 s for both guns at once. Barely trimmed:
	// reloading four barrels in one motion should be the price of firing them.
	static constexpr float m_reloadSpeed = 1.3f; // -> 1.46 s

	// Cases are thrown on MEASURED displacement, not on a frame number — the LMG
	// shipped with frame-derived part triggers and they did not fire at the
	// frames the .glb analysis predicted. The extractor nudges the shells about
	// 3 units before they are properly flung, and they end up 60 out, so this
	// sits above the nudge and well below the throw.
	static constexpr float m_slugThrownEpsilon = 10.0f;

	bool m_ammoCredited = false;

	// Frames within the reload clip, measured off the .glb (see init())
	static const int m_latchFrame     = 31; // 'release' has turned its full 40 deg
	static const int m_breakOpenFrame = 38; // 'front' reaches 45 deg — barrels open
	static const int m_seatFrame      = 58; // fresh pairs home — ammo lands HERE
	static const int m_closeFrame     = 69; // barrels shut and the latch drops

	bool m_latchPlayed     = false;
	bool m_breakOpenPlayed = false;
	bool m_seatPlayed      = false;
	bool m_closePlayed     = false;

	// Seconds from the start of each .wav to its transient, so a cue can be
	// triggered early enough to land ON the visual event. SECONDS, not frames:
	// the reload runs at m_reloadSpeed, so the frame count a fixed lead-in spans
	// differs from 1x. Convert with soundLeadFrames(), never with a constant.
	static constexpr float m_cockLeadSec        = 0.047f; // cock_rifle.wav peak
	static constexpr float m_insertShellLeadSec = 0.104f; // insert_shell_shotgun.wav peak

	// --- Barrels -------------------------------------------------------------
	// Bore centres at the muzzle face, in 'front'-local model units. The two
	// bores split cleanly at X -1.69 and +1.70 — side by side, not stacked — and
	// GltfImport's handedness conversion negates Z. Index matches the slug index,
	// so barrel 0 is slug1 (X -1.55) and barrel 1 is slug2 (X +1.55).
	//
	// These hang off 'front', which is the BARREL assembly and tips 45 degrees
	// away during the reload. Anything parented to 'base' would leave the flash
	// hanging where the muzzles used to be.
	static const irr::core::vector3df m_barrelMuzzle[BARREL_COUNT];

	irr::video::ITexture* m_crosshair = nullptr;

	void enterState(State next);

	// Tops both guns up out of the shared shell pool, right barrel first.
	void creditShellsFromReserve();
	void resolveGuns();
	void updateSlugs();
	void clearFiredRecord();
	void ejectSpentCase(int gun, int barrel);
	void updateReloadSounds(float frame);
	void fireBarrel(int gun, int barrel);

public:
	// Only the right-hand gun's effects reach the F2 window. Both are configured
	// from the same constants, and its muzzle offset is rewritten per shot
	// anyway, so a second slider would imply a freedom that does not exist.
	WeaponEffects* debugEffects() override { return &m_gun[GUN_RIGHT].effects; }
};

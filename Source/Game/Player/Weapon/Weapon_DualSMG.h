#pragma once

#include <vector>

#include "../WeaponData.h"
#include "WeaponEffects.h"

// ---------------------------------------------------------------------------
// Weapon_DualSMG — akimbo submachine guns.
//
// dual_smgs.glb is two complete weapons in one mesh: 'smg2Right' and 'smg2Left',
// each with its own base, bolt, ejector, trigger, magazine and round. They are
// the SAME geometry mirrored by position (the roots sit at X -13 and X +13 and
// the local meshes are identical), which is why one muzzle offset and one set of
// part constants serve both.
//
// BOTH GUNS FIRE TOGETHER, because that is what the asset animates: the fire
// clip pulls both triggers and cycles both bolts on the same frames. One trigger
// pulse is therefore two rounds, two flashes, two rays and two cases — the
// weapon's whole appeal is that it puts twice as much lead downrange as anything
// else in the pack, and pays for it in spread and in a magazine that empties in
// three seconds.
//
// SINGLE-GUN MODE. m_dualWield false hides the left weapon and drops the pair to
// one gun's rate, ammunition and recoil. See applyDualWield() — and read the
// warning there about the left ARM, which this cannot hide.
// ---------------------------------------------------------------------------
class Weapon_DualSMG : public PlayerWeapon
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
	bool isUnequipping() const override { return m_isUnequipping; }
	void idle();
	void move();
	void fire();
	void reload();

	int displayAmmo() const override { return m_rounds; }
	// Magazine contents for the save sidecar. See WeaponMagState — slot 0 is this
	// weapon's only counter.
	void saveMagState(WeaponMagState& out) const override { out.slots[0] = m_rounds; }
	void loadMagState(const WeaponMagState& in) override  { if (in.slots[0] >= 0) m_rounds = (in.slots[0] < magSize() ? in.slots[0] : magSize()); }

	// --- Dual wield toggle ---------------------------------------------------
	// Off hides the left gun and halves everything it contributed. Safe to call
	// at any time: it re-applies the part visibility and rescales the magazine
	// immediately, clamping the rounds in hand rather than leaving more loaded
	// than the remaining gun can hold.
	void setDualWield(bool enabled);
	bool isDualWield() const { return m_dualWield; }

private:
	// --- The two guns --------------------------------------------------------
	enum Gun { GUN_RIGHT = 0, GUN_LEFT = 1, GUN_COUNT = 2 };

	// False leaves only the right-hand gun. Flip the initialiser for a permanent
	// single-SMG weapon, or drive it from a pickup / console through
	// setDualWield().
	bool m_dualWield = true;

	int activeGuns() const { return m_dualWield ? 2 : 1; }
	int magSize()    const { return m_magPerGun * activeGuns(); }

	struct SideFX
	{
		// One WeaponEffects per gun, because a single instance owns exactly one
		// muzzle attachment and these muzzles are 26 model units apart. Sharing
		// one would put every flash on the right-hand barrel.
		WeaponEffects effects;

		// The gun's ejection port, for brass. A named joint rather than an offset
		// off the weapon node, so the case leaves from where the animation says.
		irr::scene::IBoneSceneNode* ejector = nullptr;

		// Magazine, hidden while it is away from the gun during the reload — and
		// the round in its feed lips, which MUST be hidden with it. The round is
		// a separate joint sitting 13.15 units up the magazine against a mesh
		// that tops out at 14.0, i.e. right at the lips; hiding the magazine
		// alone leaves it floating away on its own, which is exactly the fault
		// the LMG's belt shipped with.
		MeshPart             mag;
		MeshPart             round;
		irr::core::vector3df magRest;
		bool                 magRestValid = false;

		// Every drawable piece of this gun, for the dual-wield toggle. Populated
		// only for the left one — the right is never hidden.
		std::vector<MeshPart> allParts;

		// A case owed by the round just fired, thrown once the clip has moved the
		// ejector off its rest.
		bool caseOwed = false;
	};

	SideFX m_gun[GUN_COUNT];

	// --- State ---------------------------------------------------------------
	// Flags rather than an explicit enum: these guns only fire, reload, draw or
	// holster, and nothing here chains into anything else the way the sniper's
	// fire owes a bolt cycle.
	bool m_isFireAnim    = false; // fire clip running; does NOT block firing
	bool m_isReloading   = false;
	bool m_isEquipping   = false;
	bool m_isUnequipping = false;

	// --- Cadence -------------------------------------------------------------
	// Full auto and level-triggered. 100 ms is 600 rpm out of each gun, which is
	// an ordinary SMG rate — it is the pair of them that makes it 1200 rounds a
	// minute of incoming, not either weapon running implausibly fast.
	float m_lastFireTime = 0.0f;
	static constexpr float m_fireInterval = 100.0f;

	// One dry click per press. The only thing latched per press — these are full
	// auto, so the trigger is otherwise read as a level.
	bool m_dryFiredThisPress = false;

	// The fire clip is 7 frames of travel (see init()). Played at 2.4x it spans
	// close to m_fireInterval, so held fire reads as one continuous hammering
	// rather than as a clip that restarts long before or after it has finished.
	static constexpr float m_fireSpeed = 2.4f;

	int m_damage = 11; // low per round; two of them per pulse is the point

	// --- Ammunition ----------------------------------------------------------
	// Thirty a side. ONE pool across both guns, because the clip swaps both
	// magazines together and there is no authored way to run one gun dry alone.
	static const int m_magPerGun = 30;
	int m_rounds = m_magPerGun * 2;

	// --- Reload --------------------------------------------------------------
	// The authored double reload is a deliberate 1.57 s; trimmed a little, but
	// left long — dropping both magazines at once should hurt.
	static constexpr float m_reloadSpeed = 1.25f; // -> 1.25 s

	// Magazines are hidden while they are away from the gun. The clip throws both
	// clear and then brings the SAME meshes back as the fresh ones, so without
	// this they visibly teleport across the screen — the reuse trick this whole
	// weapon pack is built on.
	//
	// Driven off MEASURED displacement rather than frame numbers: the LMG shipped
	// with frame-derived part triggers and they did not fire at the frames the
	// .glb analysis predicted. Asking the joint where the magazine actually is
	// cannot be wrong about that and reads the same at any clip speed.
	static constexpr float m_magDroppedEpsilon = 25.0f;

	// Ammo lands when the fresh magazines are back in the guns
	bool m_ammoCredited = false;

	// Frames within the reload clip, measured off the .glb (see init())
	static const int m_magOutFrame = 22; // magazines break free of the wells
	static const int m_magInFrame  = 40; // fresh magazines seated — ammo lands HERE

	// Seconds from the start of each .wav to its transient, so a cue can be
	// triggered early enough to land ON the visual event. SECONDS, not frames:
	// the reload runs at m_reloadSpeed, so the frame count a fixed lead-in spans
	// differs from 1x. Convert with soundLeadFrames(), never with a constant.
	static constexpr float m_removeMagLeadSec = 0.276f; // remove_mag.wav peak
	static constexpr float m_insertMagLeadSec = 0.400f; // insert_mag.wav peak

	bool m_magOutPlayed = false;
	bool m_magInPlayed  = false;

	// --- Spread --------------------------------------------------------------
	// Wide by design and wider still under sustained fire: two guns held out at
	// arm's length with nothing braced is the least accurate way to shoot
	// anything, and that is the trade for the volume.
	float m_bloom = 0.0f;
	static constexpr float m_spreadMin    = 0.010f;
	static constexpr float m_spreadMax    = 0.042f;
	static constexpr float m_bloomPerShot = 0.030f;
	static constexpr float m_bloomDecay   = 1.4f;

	static const int m_caseEjectFrame = 2;

	// Port on the -X face of the receiver. The viewmodel carries a 180 degree
	// yaw, so model -X reads as the player's right. Ejector-local model units, so
	// a viewmodel-scale change does not move it.
	const irr::core::vector3df m_ejectPortOffset =
		irr::core::vector3df(-0.9f, 0.0f, 0.0f);

	irr::video::ITexture* m_crosshair = nullptr;

	void resolveGuns();
	void applyDualWield();
	void updateMagazines();
	void fireOneGun(int gun, const irr::core::vector3df& forward,
	                const irr::core::vector3df& right,
	                const irr::core::vector3df& down);
	void ejectSpentCase(int gun);
	void updateReloadSounds(float frame);
	void endReload();

public:
	// Only the right-hand gun's effects are exposed to the F2 window. Both are
	// configured identically from the same constants, so tuning the offset there
	// and copying it across is the intended workflow — a second slider would
	// imply the two can legitimately differ, and they cannot.
	WeaponEffects* debugEffects() override { return &m_gun[GUN_RIGHT].effects; }
};

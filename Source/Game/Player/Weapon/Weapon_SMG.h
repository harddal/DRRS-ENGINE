#pragma once

#include "../WeaponData.h"
#include "WeaponEffects.h"

// ---------------------------------------------------------------------------
// Weapon_SMG — single full-auto submachine gun.
//
// smg_animated.glb is NOT half of dual_smgs.glb. It was checked: 5714 verts and
// 77.7 units of barrel against the dual gun's 3533 and 36.3, a different
// receiver and a different rig. This is its own weapon, and it is the one the
// pack was missing — a fast, low-damage, thirty-two-round automatic that sits under
// the rifle and above nothing.
//
// Like Weapon_Rifle it has TWO reload clips: one that swaps the magazine and
// racks the bolt (f8-87) and one that only swaps it (f88-150). Same reason, same
// mechanism — reload() picks on m_chamberEmpty — and the draw racks the bolt too.
// ---------------------------------------------------------------------------
class Weapon_SMG : public PlayerWeapon
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

	int displayAmmo() const override { return m_rounds; }
	void saveMagState(WeaponMagState& out) const override { out.slots[0] = m_rounds; }
	void loadMagState(const WeaponMagState& in) override
	{
		if (in.slots[0] >= 0)
			m_rounds = (in.slots[0] < m_magSize ? in.slots[0] : m_magSize);
	}

private:
	enum class State
	{
		Idle,
		Equipping,
		Unequipping,
		Firing,    // "fire" -> Idle; the bolt cycles inside the clip
		Reloading, // "reload" / "reload_empty" -> Idle
	};

	State m_state = State::Idle;

	// --- Cadence -------------------------------------------------------------
	// Full auto and LEVEL-triggered — the trigger is read as held, not as a
	// press, which is the difference between this and the rifle. 85 ms is a hair
	// over 700 rpm: faster than either of the dual SMGs individually, because
	// this one is braced in two hands and only has to feed itself.
	float m_lastFireTime = 0.0f;
	static constexpr float m_fireInterval = 85.0f;

	// One dry click per press. The only thing latched per press.
	bool m_dryFiredThisPress = false;

	// The fire clip is 7 frames. At 2.75x it spans ~85 ms, so held fire reads as
	// one continuous cycle instead of a clip restarting mid-travel.
	static constexpr float m_fireSpeed = 2.75f;

	int m_damage = 14;

	// --- Spread --------------------------------------------------------------
	// Blooms under sustained fire and settles when the trigger is released. Much
	// tighter than the akimbo pair — this is a shouldered weapon — but it still
	// cannot be held on a distant target through a full magazine.
	float m_bloom = 0.0f;
	static constexpr float m_spreadMin    = 0.006f;
	static constexpr float m_spreadMax    = 0.026f;
	static constexpr float m_bloomPerShot = 0.0022f;
	static constexpr float m_bloomDecay   = 1.6f;

	// --- Magazine ------------------------------------------------------------
	static const int m_magSize = 32;
	int m_rounds = m_magSize;

	// Only truly empty — bolt locked back, nothing chambered — once the last
	// round is gone, and only then is the longer reload the right one.
	bool m_chamberEmpty = false;

	// --- Clip speeds ---------------------------------------------------------
	// Authored: 2.63 s for the empty swap, 2.07 s for the tactical one. Both are
	// slow for a weapon whose appeal is how fast it empties, so both are trimmed
	// hard; the empty one still lands visibly longer.
	static constexpr float m_reloadSpeed = 1.55f; // -> 1.34 s
	static constexpr float m_emptySpeed  = 1.55f; // -> 1.70 s
	static constexpr float m_equipSpeed  = 1.35f; // the draw racks the bolt

	// --- The round in the breech ---------------------------------------------
	// The usual reused 'bullet' mesh: thrown as the case, back seated as the next
	// round. Hidden across the throw, driven off MEASURED displacement.
	MeshPart m_round;
	irr::core::vector3df m_roundRest;
	bool     m_roundRestValid = false;
	bool     m_caseHandedOff  = false;

	// The case only travels 2.9 units at its peak on this rig — a third of what
	// the rifle throws — so the threshold has to sit much lower. Below 0.5 the
	// joint's own settle noise starts tripping it.
	static constexpr float m_roundLooseEpsilon = 0.9f;

	// --- The magazine part ---------------------------------------------------
	// Hidden while away; the frame it comes home credits the ammunition. No frame
	// constant, so both reload clips share the logic despite the magazine leaving
	// at f28 in one and f108 in the other.
	MeshPart m_mag;
	irr::core::vector3df m_magRest;
	bool     m_magRestValid = false;
	bool     m_magWasAway   = false;
	bool     m_ammoCredited = false;

	static constexpr float m_magAwayEpsilon = 5.0f; // travels 48 at the peak

	// --- Audio ---------------------------------------------------------------
	// Seconds from the start of each .wav to the transient that should land on
	// the visual event. SECONDS, not frames — the clips run at their own speeds.
	static constexpr float m_removeMagLeadSec = 0.276f;
	static constexpr float m_insertMagLeadSec = 0.400f;
	static constexpr float m_boltLeadSec      = 0.047f;

	// Frames measured off the .glb (see init())
	static const int m_magOutFrameEmpty = 28;
	static const int m_magInFrameEmpty  = 48;
	static const int m_boltFrameEmpty   = 68;  // bolt racked after the swap
	static const int m_magOutFrame      = 108;
	static const int m_magInFrame       = 128;
	static const int m_equipBoltFrame   = 182; // the draw chambers a round here

	bool m_magOutPlayed = false;
	bool m_magInPlayed  = false;
	bool m_boltPlayed   = false;

	WeaponEffects m_effects;

	irr::video::ITexture* m_crosshair = nullptr;

	void enterState(State next);
	void updateRound(bool throwCase);
	void updateMagazine();
	void ejectSpentCase();
	void updateReloadSounds(float frame, int magOut, int magIn, int boltFrame);

public:
	WeaponEffects* debugEffects() override { return &m_effects; }
};

#pragma once

#include "../WeaponData.h"
#include "WeaponEffects.h"

// ---------------------------------------------------------------------------
// Weapon_Rifle — semi-automatic battle rifle.
//
// The asset gives it something none of the others have: TWO reload clips. One
// swaps the magazine and then cycles the bolt (f12-91), the other only swaps the
// magazine (f94-149). That is the real distinction between reloading a rifle you
// ran dry and one with a round still chambered, and it costs nothing to honour —
// reload() simply picks the clip that matches the state of the chamber.
//
// Its draw works the bolt too, the same way the sniper's does, so the rifle is
// chambered as it comes up rather than arriving mysteriously ready.
// ---------------------------------------------------------------------------
class Weapon_Rifle : public PlayerWeapon
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
	// Semi-automatic: one round per press, with a floor under it so mashing
	// cannot outrun the clip. 160 ms is a deliberate rate for an aimed rifle —
	// fast enough to answer, slow enough that the LMG still has a job.
	bool  m_firedThisPress = false;
	float m_lastFireTime   = 0.0f;
	static constexpr float m_fireInterval = 160.0f;

	// The fire clip is 11 frames of travel. At 2.3x that spans m_fireInterval, so
	// the bolt completes its cycle exactly once per round rather than being cut
	// off half way by the next shot.
	static constexpr float m_fireSpeed = 2.3f;

	int   m_damage = 34;
	float m_spread = 0.006f;

	// --- Magazine ------------------------------------------------------------
	static const int m_magSize = 20;
	int m_rounds = m_magSize;

	// Which reload clip to play. The gun is only truly empty — bolt locked back,
	// nothing chambered — when the last round has been fired, and only then does
	// the longer clip that re-cycles the bolt make sense.
	bool m_chamberEmpty = false;

	// --- Clip speeds ---------------------------------------------------------
	// The magazine swap is 1.87 s authored and the empty variant 2.67 s, which is
	// slow for a rifle; trimmed, but the empty one stays visibly longer because
	// having to work the bolt afterwards is the cost of running dry.
	static constexpr float m_reloadSpeed = 1.40f; // -> 1.33 s
	static constexpr float m_emptySpeed  = 1.40f; // -> 1.90 s
	static constexpr float m_equipSpeed  = 1.30f; // the draw chambers a round

	// --- The round in the breech ---------------------------------------------
	// One 'bullet' mesh again: thrown as the spent case on firing and reappearing
	// seated as the next round. Hidden across the throw, per the rest of the pack,
	// and driven off MEASURED displacement rather than a frame number.
	MeshPart m_round;
	irr::core::vector3df m_roundRest;
	bool     m_roundRestValid = false;
	bool     m_caseHandedOff  = false;

	// The case is thrown 9.4 units on the frame after the shot and decays back to
	// seated over the next ten, so anything comfortably above the noise floor and
	// below that peak catches it on the way out.
	static constexpr float m_roundLooseEpsilon = 2.0f;

	// --- The magazine part ---------------------------------------------------
	// Hidden while it is away from the gun, and the frame it comes home is what
	// credits the ammunition — no frame constant, so the two reload clips share
	// one piece of logic despite the magazine leaving at f34 in one and f116 in
	// the other.
	MeshPart m_mag;
	irr::core::vector3df m_magRest;
	bool     m_magRestValid = false;
	bool     m_magWasAway   = false;
	bool     m_ammoCredited = false;

	static constexpr float m_magAwayEpsilon = 5.0f; // travels 44 at the peak

	// --- Audio ---------------------------------------------------------------
	// Seconds from the start of each .wav to the transient that should land on
	// the visual event. SECONDS, not frames — the clips run at their own speeds.
	static constexpr float m_removeMagLeadSec = 0.276f;
	static constexpr float m_insertMagLeadSec = 0.400f;
	static constexpr float m_boltLeadSec      = 0.047f;

	// Frames measured off the .glb (see init()). The two reload clips need their
	// own pairs because the same motion sits at different offsets in each.
	static const int m_magOutFrameEmpty = 34;
	static const int m_magInFrameEmpty  = 53;
	static const int m_boltFrameEmpty   = 68;  // bolt cycled after the swap
	static const int m_magOutFrame      = 116;
	static const int m_magInFrame       = 135;
	static const int m_equipBoltFrame   = 177; // the draw chambers a round here

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

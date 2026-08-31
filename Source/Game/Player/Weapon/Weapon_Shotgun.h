#pragma once

#include "../WeaponData.h"
#include "WeaponEffects.h"

class Weapon_Shotgun : public PlayerWeapon
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

	int displayAmmo() const override { return m_shells; }
	// Magazine contents for the save sidecar. See WeaponMagState — slot 0 is this
	// weapon's only counter.
	void saveMagState(WeaponMagState& out) const override { out.slots[0] = m_shells; }
	void loadMagState(const WeaponMagState& in) override  { if (in.slots[0] >= 0) m_shells = (in.slots[0] < m_magSize ? in.slots[0] : m_magSize); }

private:
	// Explicit state machine. A single m_isAnimating flag cannot express the
	// chains this weapon needs — fire ALWAYS hands off to the pump, and a reload
	// loops one clip per shell before running reload_end and, on an empty gun, a
	// closing rack. Each state knows only what it hands off to.
	enum class State
	{
		Idle,
		Equipping,
		Unequipping,
		Firing,      // "fire"        -> Pumping
		Pumping,     // "pump"        -> Idle
		Reloading,   // "reload"      -> itself while the tube has room, else ReloadEnd
		ReloadEnd,   // "reload_end"  -> Pumping when the chamber was run dry, else Idle
	};

	State m_state = State::Idle;

	// Fire rate and semi-auto tracking
	int m_lastFireTime = 0;
	float m_fireRate = 750.0f; // ms between shots (pump-action cadence)
	bool m_firedThisPress = false; // semi-auto: one shot per click

	// Tube magazine
	static const int m_magSize = 4;
	int  m_shells = m_magSize;

	// Set when a shot empties the tube. The post-fire pump then ejects the last
	// case and leaves the chamber empty, so a rack is owed once the reload
	// finishes — that is the second pump of an empty cycle.
	bool m_needsChamberRack = false;

	// The single 'slug' prop plays two parts: the case the pump flicks out, and
	// the fresh shell thumbed into the loading port. It has to be hidden and
	// shown per frame or it teleports between the two.
	MeshPart m_slug;
	bool     m_slugHandedOff = false; // a physics casing has replaced it this pump

	// Whether the running pump has a spent case to throw. True for the rack that
	// follows a shot; false for the one that closes an empty reload, where the
	// post-fire pump already cleared the chamber and this rack is only chambering
	// a fresh shell.
	bool     m_pumpEjects = false;

	// Set when fire is pressed while the gun is busy. From a reload it also cuts
	// the per-shell loop short; from the pump or the reload's closing move it is
	// just buffered input. Either way the shot is queued rather than immediate,
	// so the gun is never fired mid-cycle with the action open.
	bool     m_fireAfterReload = false;

	// Frames within the clips, measured off the .glb (see init())
	// The pump slides back over f21-23, holds to f29, then returns forward over
	// f31-34. Eject at the end of the REARWARD stroke: that is when a real pump
	// throws the case, and it was previously f32, which is on the way forward.
	// Hiding the slug here also means the artist's flick-up-and-snap-home at
	// f30-33 never renders, so nothing has to be done about that separately.
	static const int m_pumpEjectFrame   = 23;
	static const int m_reloadShowFrame  = 55; // the hand arrives with the fresh shell
	static const int m_reloadSeatFrame  = 69; // shell is home in the tube

	// Pump slides back over f21-23; the rack sound should start there.
	static const int m_rackBackFrame = 21;

	// The draw works the action too, racking over f111-124 of the equip clip —
	// the same gesture, at an absolute frame of its own.
	static const int m_equipRackFrame = 111;

	// Seconds from the start of each .wav to the transient that should land on the
	// visual event. SECONDS, not frames: these clips run at m_actionSpeed, so the
	// frame count they span differs from 1x. Convert with soundLeadFrames().
	static constexpr float m_pumpSoundLeadSec   = 0.064f; // Shotgun_Quick Pump_01.wav onset
	static constexpr float m_insertShellLeadSec = 0.104f; // insert_shell_shotgun.wav peak

	// Fire, pump and both halves of the reload run quicker than authored.
	// Applied in enterState() so no path can leave it stuck on.
	static constexpr float m_actionSpeed = 1.3f;

	// How much of the pump's screen-space swing to cancel. The clip moves the gun
	// root only 10.6 model units but rotates it 24.75 degrees and holds that from
	// f20 to f36 — and it is the rotation, swung out along an 85-unit barrel,
	// that throws the weapon up and left. Below 1.0 on purpose: fully pinned, the
	// arms look like they are doing all the moving.
	static constexpr float m_pumpStabilize = 1.0f;

	// Pellet stats
	int m_pelletCount = 8;
	float m_damagePerPellet = 30.0f;
	float m_spreadAngle = 3.0f; // degrees half-angle cone

	// Pump rack sound, triggered off the pump clip's own frame so it stays in
	// sync and also fires for the rack that closes an empty reload.
	bool m_pumpSoundPlayed   = false;
	bool m_equipRackPlayed   = false;
	bool m_insertShellPlayed = false; // re-armed per reload pass, so it plays once per shell

	// Recoil animation (delivered through the shared PlayerWeapon view-kick spring)
	float m_recoilAmount = 8.0f;
	float m_recoilRandomnessVertical = 1.0f;
	float m_recoilRandomnessHorizontal = 2.5f;
	float m_recoilPositionKick = 0.07f;

	// Shared muzzle flash / shell / impact VFX
	WeaponEffects m_effects;

	irr::video::ITexture* m_crosshair = nullptr;

	void enterState(State next);
	void finishAction();
	void updateSlug(float frame);
	void ejectSpentShell();
	void playPumpSound();

public:
	WeaponEffects* debugEffects() override { return &m_effects; }
};

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
	float m_fireRate = 900.0f; // ms between shots (pump-action cadence)
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

	// Frames within the clips, measured off the .glb (see init())
	static const int m_pumpEjectFrame   = 32; // top of the case's flick; it snaps home at 33
	static const int m_reloadShowFrame  = 55; // the hand arrives with the fresh shell
	static const int m_reloadSeatFrame  = 69; // shell is home in the tube

	// Pump slides back over f21-23. Shotgun_Quick Pump_01.wav has 1.9 frames of
	// lead-in before its onset, so triggering here lands that onset on the start
	// of the rack-back and its peak on the back stop.
	static const int m_pumpSoundFrame   = 19;

	// Pellet stats
	int m_pelletCount = 8;
	float m_damagePerPellet = 30.0f;
	float m_spreadAngle = 3.0f; // degrees half-angle cone

	// Pump rack sound, triggered off the pump clip's own frame so it stays in
	// sync and also fires for the rack that closes an empty reload.
	bool m_pumpSoundPlayed = false;

	// Recoil animation (delivered through the shared PlayerWeapon view-kick spring)
	float m_recoilAmount = 8.0f;
	float m_recoilRandomnessVertical = 1.0f;
	float m_recoilRandomnessHorizontal = 2.5f;
	float m_recoilPositionKick = 0.07f;

	// Shared muzzle flash / shell / impact VFX
	WeaponEffects m_effects;

	irr::video::ITexture* m_crosshair = nullptr;

	void enterState(State next);
	void updateSlug(float frame);
	void ejectSpentShell();

public:
	WeaponEffects* debugEffects() override { return &m_effects; }
};

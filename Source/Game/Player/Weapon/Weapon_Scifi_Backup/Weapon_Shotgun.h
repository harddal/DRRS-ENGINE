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
	bool isUnequipping() const override { return m_isUnequipping; }
	void idle();
	void move();
	void fire();
	void reload();

private:
	// Fire rate and semi-auto tracking
	int m_lastFireTime = 0;
	float m_fireRate = 900.0f; // ms between shots (pump-action cadence)
	bool m_firedThisPress = false; // semi-auto: one shot per click
	bool m_isEquipping = false;
	bool m_isUnequipping = false;
	bool m_isAnimating = false; // fire or reload anim in progress

	// Pellet stats
	int m_pelletCount = 8;
	float m_damagePerPellet = 30.0f;
	float m_spreadAngle = 3.0f; // degrees half-angle cone

	// Delayed pump-rack sound layer (checked in persist())
	bool  m_pumpPending = false;
	float m_pumpTime    = 0.0f;      // absolute ms timestamp
	const float m_pumpDelay = 400.0f; // ms after the blast

	// Recoil animation (delivered through the shared PlayerWeapon view-kick spring)
	float m_recoilAmount = 8.0f;
	float m_recoilRandomnessVertical = 1.0f;
	float m_recoilRandomnessHorizontal = 2.5f;
	float m_recoilPositionKick = 0.07f;

	// Shared muzzle flash / shell / impact VFX
	WeaponEffects m_effects;

	irr::video::ITexture* m_crosshair = nullptr;
};

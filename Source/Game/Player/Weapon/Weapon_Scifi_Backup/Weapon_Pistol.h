#pragma once

#include "../WeaponData.h"
#include "WeaponEffects.h"

class Weapon_Pistol : public PlayerWeapon
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
	// Semi-auto state
	bool m_firedThisPress = false;
	bool m_isFireAnim = false;    // fire animation in progress (does NOT block firing)
	bool m_isReloading = false;   // reload animation in progress (blocks firing)
	bool m_isEquipping = false;
	bool m_isUnequipping = false;
	float m_lastFireTime = 0.0f;
	const float m_minFireInterval = 150.0f; // ms — semi-auto cadence limiter
	int m_damage = 10;

	// Aim spread (tight for a precision sidearm)
	float m_spread = 0.008f;

	// Cached bone/scene nodes
	irr::scene::ISceneNode* m_muzzleNode = nullptr;

	// Shared muzzle flash / tracer / shell / impact VFX
	WeaponEffects m_effects;

	irr::video::ITexture *m_crosshair;
};

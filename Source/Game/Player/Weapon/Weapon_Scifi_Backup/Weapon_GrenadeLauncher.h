#pragma once

#include "../WeaponData.h"
#include "WeaponEffects.h"

#include <vector>

class Weapon_GrenadeLauncher : public PlayerWeapon
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
	float m_lastFireTime  = 0.0f;
	float m_fireRate      = 700.0f;
	bool  m_isEquipping   = false;
	bool  m_isUnequipping = false;

	float m_projectileSpeed = 50.0f;  // affects perceived arc — faster grenades travel farther before gravity pulls them down, making the arc wider and flatter even at the same lob angle.
	float m_lobAngle        = 0.15f;  // how steeply it launches upward. Higher values throw it higher and shorter; lower values make it flatter and longer-range. Typical useful range is 0.1f (nearly flat) to 0.8f (steep lob).
	float m_gravity         = 12.0f;  // how fast it falls. Higher values make the arc tighter and the grenade drop faster; lower values make it hang in the air longer and travel farther. Real gravity is ~9.8, but exaggerating it (12–20) usually feels better for game pacing.
	const float m_spawnOffset = 0.5f;

	float m_pointDamage  = 70.0f;
	float m_splashDamage = 60.0f;
	float m_splashRadius = 4.0f;
	float m_splashForce  = 0.8f;

	// Shared muzzle flash VFX (flash-only desc — trails/explosions stay bespoke)
	WeaponEffects m_effects;

	irr::video::E_MATERIAL_TYPE m_particleTrailMaterialType = irr::video::E_MATERIAL_TYPE::EMT_TRANSPARENT_ALPHA_CHANNEL;

	std::vector<WeaponProjectile> m_projectiles;

	irr::video::ITexture* m_crosshair = nullptr;

	void spawnProjectile(bool bounce = false);
	void updateProjectiles(float dt);
	// surfaceNormal: impact normal for contact detonations (scorch orientation);
	// zero vector for timer detonations (floor probe fallback)
	void detonateAt(const irr::core::vector3df& pos, entityid directHitID,
		const irr::core::vector3df& surfaceNormal = irr::core::vector3df(0.0f, 0.0f, 0.0f));
	void applySplashDamage(const irr::core::vector3df& epicentre, entityid directHitEntityID);
};

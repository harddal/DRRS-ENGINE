#pragma once

#include "../WeaponData.h"

#include <vector>

class Weapon_RocketLauncher : public PlayerWeapon
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
	std::vector<std::pair<entityid, float>> m_targetLockTimes;
	entityid m_currentPrimaryTarget = _entity_null_value;

	// Fire rate tracking
	int m_lastFireTime = 0;
	float m_fireRate = 150.0f;
	bool m_isEquipping = false;
	bool m_isUnequipping = false;
	float m_recoil = 0.0f;
	float m_pointDamage = 100.0f;
	float m_splashDamage = 80.0f;     // Maximum splash damage at the epicentre
	float m_splashRadius = 5.0f;      // Radius in world units for the splash damage falloff
	float m_splashForce  = 1.0f;      // Peak impulse magnitude applied to dynamic PhysX objects at the epicentre
	float m_projectileSpeed = 35.0f;

	// Muzzle flash
	irr::video::E_MATERIAL_TYPE m_muzzleFlashMaterialType = irr::video::E_MATERIAL_TYPE::EMT_TRANSPARENT_ALPHA_CHANNEL;
	irr::scene::IBillboardSceneNode* m_muzzleStarNode = nullptr;  // Billboard facing camera
	float m_muzzleFlashTime = 0.0f;
	const float m_muzzleFlashDuration = 50.0f; // 50ms flash

	// Particle trail shader
	irr::video::E_MATERIAL_TYPE m_particleTrailMaterialType = irr::video::E_MATERIAL_TYPE::EMT_TRANSPARENT_ALPHA_CHANNEL;

	// Projectile tracking
	std::vector<WeaponProjectile> m_projectiles;
	// Offset the spawn position of the rocket from the muzzle bone
	const float m_spawnOffset = 0.0f;

	irr::core::vector3df m_targetLockIndicatorOffset = irr::core::vector3df(0.0f, 2.0f, 0.0f);

	irr::video::ITexture
		*m_crosshair    = nullptr,
		*m_trackIcon    = nullptr,
		*m_lockIcon     = nullptr,
		*m_lockwaitIcon = nullptr;

	void renderNPCLockIndicators(irr::scene::ICameraSceneNode* cam);
	void spawnProjectile(bool useTracking = false);
	void updateProjectiles(float dt);
	void createMuzzleFlash();
	void updateMuzzleFlash(float dt);
	void applySplashDamage(const irr::core::vector3df& epicentre, entityid directHitEntityID);
};

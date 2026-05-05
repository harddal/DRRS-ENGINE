#pragma once

#include "../WeaponData.h"

#include <vector>

#include <anax/anax.hpp>

#include "Engine/Engine.h"
#include "Engine/Renderer/RenderManager.h"

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
	void idle();
	void move();
	void fire();
	void reload();

private:
	// Automatic fire variables
	float m_recoil = 0.01f;
	float m_lastFireTime = 0.0f;
	const float m_fireRate = 150.0f, m_fireRateAlt = 50.0f;
	bool m_isFiring = false;

	// SPARK plasma bolt + trail system (template cloned per projectile)
	SPK::SPK_ID m_projectileSparkBaseID = SPK::NO_ID;

	// SPARK plasma impact system
	SPK::SPK_ID m_impactSparkBaseID = SPK::NO_ID;
	std::list<SPK::System*> m_impactSystems;
	float m_impactUpdateRate = 500.0f;

	// Projectile tracking
	std::vector<WeaponProjectile> m_projectiles;

	void updateProjectiles(float dt);
	void spawnProjectile();
	void initPlasmaSparkSystem();
	void initImpactSparkSystem();
	void createImpactEffect(const irr::core::vector3df& pos);
	void createMuzzleFlash();
	void updateMuzzleFlash(float dt);

	// Muzzle flash
	irr::video::E_MATERIAL_TYPE m_muzzleFlashMaterialType;
	irr::scene::IBillboardSceneNode* m_muzzleStarNode = nullptr;
	irr::scene::ILightSceneNode* m_muzzleLightNode = nullptr;
	float m_muzzleFlashTime = 0.0f;
	const float m_muzzleFlashDuration = 50.0f;

	irr::video::ITexture* m_crosshair;
};

#pragma once

#include "../WeaponData.h"

#include <vector>

#include <anax/anax.hpp>

#include "Engine/Engine.h"
#include "Engine/Renderer/RenderManager.h"

class Weapon_PulseRifle : public PlayerWeapon
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
	float m_lastFireTime = 0.0f;
	bool m_isFiring = false;

	// Cached bone/scene nodes
	irr::scene::ISceneNode* m_muzzleNode = nullptr;

	void createMuzzleFlash();
	void updateMuzzleFlash(float dt);
	void createLaserBeam(const irr::core::vector3df& start, const irr::core::vector3df& end);
	void updateLaserBeam(float dt);
	void initImpactSparkSystem();
	void createImpactEffect(const irr::core::vector3df& pos);

	// SPARK plasma impact system
	SPK::SPK_ID m_impactSparkBaseID = SPK::NO_ID;
	std::list<SPK::System*> m_impactSystems;
	float m_impactUpdateRate = 500.0f;

	// Muzzle flash
	irr::video::E_MATERIAL_TYPE m_muzzleFlashMaterialType;
	irr::scene::IBillboardSceneNode* m_muzzleStarNode = nullptr;
	irr::scene::ILightSceneNode* m_muzzleLightNode = nullptr;
	float m_muzzleFlashTime = 0.0f;
	const float m_muzzleFlashDuration = 250.0f;

	// Continuous laser beam — single persistent node repositioned each shot
	irr::scene::IMeshSceneNode* m_laserNode = nullptr;
	float m_laserFireTime = -1.0f;
	const float m_laserFadeDuration = 50.0f; // ms; must exceed fire interval to look continuous

	irr::video::ITexture *m_crosshair;
};


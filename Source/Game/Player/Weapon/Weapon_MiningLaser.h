#pragma once

#include "../WeaponData.h"

#include <vector>

#include <anax/anax.hpp>

#include "Engine/Engine.h"
#include "Engine/Renderer/RenderManager.h"

class Weapon_MiningLaser : public PlayerWeapon
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
	float m_lastFireTime = 0.0f;
	bool m_isEquipping = false;
	bool m_isUnequipping = false;
	bool m_isReloadingAnim = false;
	SoundHandle m_fireLoopHandle;

	// Cached bone/scene nodes
	irr::scene::ISceneNode* m_muzzleNode = nullptr;

	void createMuzzleFlash();
	void updateMuzzleFlash(float dt);
	void createLaserBeam(const irr::core::vector3df& start, const irr::core::vector3df& end);
	void updateLaserBeam(float dt);

	// Muzzle flash
	irr::video::E_MATERIAL_TYPE m_muzzleFlashMaterialType;
	irr::scene::IBillboardSceneNode* m_muzzleStarNode = nullptr;
	irr::scene::ILightSceneNode* m_muzzleLightNode = nullptr;
	float m_muzzleFlashTime = 0.0f;
	const float m_muzzleFlashDuration = 50.0f;

	// Continuous laser beam — single persistent node repositioned each shot
	irr::scene::IMeshSceneNode* m_laserNode = nullptr;
	float m_laserFireTime = -1.0f;
	const float m_laserFadeDuration = 5.0f;

	irr::video::ITexture *m_crosshair;
};


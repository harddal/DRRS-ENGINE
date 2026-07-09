#pragma once

#include "../WeaponData.h"
#include "WeaponEffects.h"

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
	int m_damage = 5;

	// Cached bone/scene nodes
	irr::scene::ISceneNode* m_muzzleNode = nullptr;

	void createLaserBeam(const irr::core::vector3df& start, const irr::core::vector3df& end);
	void updateLaserBeam(float dt);

	// Shared muzzle flash VFX (flash-only desc — persistent laser beam stays bespoke)
	WeaponEffects m_effects;

	// Continuous laser beam — single persistent node repositioned each shot
	irr::scene::IMeshSceneNode* m_laserNode = nullptr;
	float m_laserFireTime = -1.0f;
	const float m_laserFadeDuration = 5.0f;

	irr::video::ITexture *m_crosshair;
};


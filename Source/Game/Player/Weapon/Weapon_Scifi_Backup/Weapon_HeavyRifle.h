#pragma once

#include "../WeaponData.h"
#include "WeaponEffects.h"

#include <anax/anax.hpp>

#include "Engine/Engine.h"
#include "Engine/Renderer/RenderManager.h"

class Weapon_BoltDriver : public PlayerWeapon
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
	float m_recoil = 0.01f;
	float m_lastFireTime = 0.0f;
	bool m_isEquipping = false;
	bool m_isUnequipping = false;
	bool m_isPlayingFireAnim = false;
	bool m_isReloadingAnim = false;

	// Cached bone/scene nodes
	irr::scene::ISceneNode* m_muzzleNode = nullptr;

	// Shared muzzle flash / tracer / shell / impact VFX
	WeaponEffects m_effects;

	irr::video::ITexture *m_crosshair;
};

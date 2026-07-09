#pragma once

#include "../WeaponData.h"
#include "WeaponEffects.h"

#include <anax/anax.hpp>

#include "Engine/Engine.h"
#include "Engine/Renderer/RenderManager.h"

class Weapon_Minigun : public PlayerWeapon
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
	bool m_isFiring = false;

	// Fire audio state
	SoundHandle m_spinUpHandle;
	SoundHandle m_fireLoopHandle;
	SoundHandle m_spinDownHandle;
	bool m_audioWasSpinning   = false;
	bool m_audioWasFiring     = false;
	bool m_audioButtonWasHeld = false;
	bool m_audioWasOverheated = false;

	// Barrel heat state — drives the barrel_heat shader each frame
	float m_barrelHeatLevel = 0.0f;
	bool  m_isOverheated    = false;   // true from white-hot until cooled to 50%
	const float m_heatRampRate       = 0.0010f; // per ms
	const float m_heatCoolRate       = 0.0001f; // per ms
	const float m_overheatCutoff     = 1.0f;     // heat level that triggers lockout
	const float m_overheatResetLevel = 0.5f;     // heat level that clears lockout

	// Barrel spin variables
	irr::scene::IBoneSceneNode* m_spinBone = nullptr;
	irr::scene::IBoneSceneNode* m_gearBone = nullptr;
	float m_currentSpinSpeed = 0.0f;
	float m_accumulatedRotation = 0.0f;
	bool m_isAligned = true; // Track if barrel is currently aligned/stopped
	const float m_maxSpinSpeed = 1.5f;       // Degrees per millisecond
	const float m_spinAcceleration = 0.001f; // Ramp up
	const float m_spinDeceleration = 0.001f; // Ramp down
	const float m_fireThreshold = 1.0f;      // When to start firing
	const float m_alignmentSpinSpeed = 0.1f; // Slow spin to align minigun
	const float m_alignmentSnap = 120.0f; // 360 / number of barrels = alignmentSnap

	// Shared muzzle flash / tracer / shell / impact VFX
	WeaponEffects m_effects;

	irr::video::ITexture *m_crosshair;
};

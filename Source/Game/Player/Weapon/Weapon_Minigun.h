#pragma once

#include "../WeaponData.h"

#include <vector>

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

	void createMuzzleFlash();
	void updateMuzzleFlash(float dt);
	void ejectShell();
	void updateShells(float dt);
	void createTracerBeam(const irr::core::vector3df& start, const irr::core::vector3df& end);
	void updateTracers(float dt);

	// Muzzle flash
	irr::video::E_MATERIAL_TYPE m_muzzleFlashMaterialType;
	irr::scene::IBillboardSceneNode* m_muzzleStarNode = nullptr;  // Billboard facing camera
	irr::scene::ILightSceneNode* m_muzzleLightNode = nullptr;  // Blue glow at muzzle
	float m_muzzleFlashTime = 0.0f;
	const float m_muzzleFlashDuration = 50.0f; // 50ms flash
	
	// Shell ejection system - pooled fake physics, no ECS/PhysX
	static constexpr int SHELL_POOL_SIZE = 240;
	struct ShellCasing {
		irr::scene::IMeshSceneNode* node = nullptr;
		irr::core::vector3df velocity;        // units/second
		irr::core::vector3df angularVelocity; // degrees/second
		irr::core::vector3df rotation;
		float spawnTime = 0.0f;
		bool active = false;
		bool physicsActive = true;
		int bounceCount = 0;
	};
	ShellCasing m_shellPool[SHELL_POOL_SIZE];
	const float m_shellEjectionSpeed = 8.0f;
	const float m_shellGravity = 9.81f;          // units/second^2
	const float m_shellBounceSoundInterval = 150.0f; // ms between bounce sounds
	float m_lastShellBounceSound = 0.0f;
	
	// Tracer round system
	int m_shotCounter = 0;
	const int m_tracerFrequency = 3;
	
	struct TracerBeam {
		irr::scene::IMeshSceneNode* node;
		float spawnTime;
		float lifetime = 50.0f;
	};
	std::vector<TracerBeam> m_tracerBeams;

	irr::video::ITexture *m_crosshair;
};


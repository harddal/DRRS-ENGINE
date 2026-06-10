#pragma once

#include "../WeaponData.h"

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
	bool m_isAnimating = false;   // fire or reload animation in progress
	bool m_isEquipping = false;
	bool m_isUnequipping = false;
	float m_lastFireTime = 0.0f;
	const float m_minFireInterval = 150.0f; // ms — semi-auto cadence limiter
	int m_damage = 10;

	// Aim spread (tight for a precision sidearm)
	float m_spread = 0.008f;

	// Programmatic recoil spring (layered on top of skeletal animation)
	irr::core::vector3df m_kickPos;
	irr::core::vector3df m_kickRot;

	// Cached bone/scene nodes
	irr::scene::ISceneNode* m_muzzleNode = nullptr;

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

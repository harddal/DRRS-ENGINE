#pragma once

#include <list>
#include "../WeaponData.h"

class Weapon_Shotgun : public PlayerWeapon
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
	// Fire rate and semi-auto tracking
	int m_lastFireTime = 0;
	float m_fireRate = 900.0f; // ms between shots (pump-action cadence)
	bool m_firedThisPress = false; // semi-auto: one shot per click

	// Pellet stats
	int m_pelletCount = 8;
	float m_damagePerPellet = 30.0f;
	float m_spreadAngle = 3.0f; // degrees half-angle cone

	// Recoil animation
	float m_recoilAmount = 8.0f;
	float m_recoilRandomnessVertical = 1.0f;
	float m_recoilRandomnessHorizontal = 2.5f;
	float m_currentRecoilRotation = 0.0f;
	float m_currentRecoilHorizontal = 0.0f;
	float m_currentRecoilPosition = 0.0f;
	float m_recoilPositionKick = 0.07f;
	float m_recoilRecoverySpeed = 20.0f;

	// Reload animation
	bool m_isReloading = false;
	int m_reloadStartTime = 0;
	int m_reloadDuration = 4000;
	int m_reloadDownDuration = 500;
	int m_reloadPauseDuration = 3000;
	int m_reloadUpDuration = 500;
	float m_reloadPositionOffset = -0.5f;
	float m_reloadRotationOffset = 45.0f;
	float m_reloadWiggleAmount = 2.0f;

	// Equip animation
	bool m_isEquipping = false;
	int m_equipStartTime = 0;
	int m_equipDuration = 500;
	float m_equipStartOffset = -1.0f;

	irr::video::ITexture* m_crosshair = nullptr;

	// Impact particle effects (SPARK)
	SPK::SPK_ID m_impactParticleBaseID = SPK::NO_ID;
	std::list<SPK::System*> m_impactParticleSystems;
	float m_impactUpdateRate = 500.0f;

	void initImpactParticleSystem();
	void createImpactParticleSystem(const SPK::Vector3D& pos);
	void destroyImpactParticleSystem(SPK::System*& system);

	// Shell ejection system - pooled fake physics, no ECS/PhysX
	static constexpr int SHELL_POOL_SIZE = 32;
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
	const float m_shellEjectionSpeed = 6.0f;
	const float m_shellGravity = 9.81f;
	const float m_shellBounceSoundInterval = 150.0f; // ms between bounce sounds
	float m_lastShellBounceSound = 0.0f;

	void ejectShell();
	void updateShells(float dt);
};

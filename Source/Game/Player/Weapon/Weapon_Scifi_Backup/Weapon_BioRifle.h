#pragma once

#include "../WeaponData.h"
#include "WeaponEffects.h"

#include <vector>

class Weapon_BioRifle : public PlayerWeapon
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
	// --- Primary fire (lob glob) ---
	float m_lastFireTime    = 0.0f;
	float m_fireRate        = 900.0f;
	float m_projectileSpeed = 45.0f;
	float m_lobAngle        = 0.18f;
	float m_gravity         = 14.0f;
	const float m_spawnOffset = 0.5f;

	float m_pointDamage  = 50.0f;
	float m_splashDamage = 40.0f;
	float m_splashRadius = 3.5f;
	float m_splashForce  = 0.5f;

	std::vector<WeaponProjectile> m_projectiles;

	// --- Alt-fire (mist spray) ---
	struct MistBurst
	{
		uint32_t                 handle;
		irr::core::vector3df     position;
		irr::core::vector3df     direction;
		float                    spawnTime;
	};

	bool        m_isSpraying      = false;
	float       m_lastAltDmgTime  = 0.0f;  // burst spawn timer
	float       m_altDmgRate      = 80.0f; // ms between burst spawns
	float       m_altDmgPerTick   = 3.0f;
	float       m_lastMistDmgTime = 0.0f;  // sphere damage timer
	float       m_mistDmgRate     = 100.0f; // ms between sphere damage ticks
	float       m_mistDmgRadius   = 2.0f;   // world-units, matches ~peak cloud spread
	SoundHandle              m_sprayLoopHandle = nullptr;
	std::vector<MistBurst>   m_mistBursts;

	// Shared muzzle flash VFX (flash-only desc — globs/mist stay bespoke)
	WeaponEffects m_effects;

	bool m_isEquipping   = false;
	bool m_isUnequipping = false;

	irr::video::ITexture* m_crosshair = nullptr;

	void spawnGlob();
	void updateGlobs(float dt);
	// surfaceNormal: impact normal for contact detonations (scorch orientation);
	// zero vector for timer detonations (floor probe fallback)
	void detonateAt(const irr::core::vector3df& pos, entityid directHitID,
		const irr::core::vector3df& surfaceNormal = irr::core::vector3df(0.0f, 0.0f, 0.0f));
	void applySplashDamage(const irr::core::vector3df& epicentre, entityid directHitID);
};

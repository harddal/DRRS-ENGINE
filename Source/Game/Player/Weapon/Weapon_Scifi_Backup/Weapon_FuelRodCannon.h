#pragma once

#include "../WeaponData.h"
#include "WeaponEffects.h"

#include <set>
#include <vector>

class Weapon_FuelRodCannon : public PlayerWeapon
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
	// --- Projectile ---
	float m_lastFireTime  = 0.0f;
	float m_fireRate      = 1200.0f;
	bool  m_isEquipping   = false;
	bool  m_isUnequipping = false;

	float m_projectileSpeed = 65.0f;
	float m_lobAngle        = 0.06f;
	float m_gravity         = 8.0f;
	const float m_spawnOffset = 0.5f;

	float m_pointDamage = 120.0f;

	// Shared muzzle flash VFX (flash-only desc — projectiles/zones stay bespoke)
	WeaponEffects m_effects;

	irr::video::E_MATERIAL_TYPE m_particleTrailMaterialType = irr::video::E_MATERIAL_TYPE::EMT_TRANSPARENT_ALPHA_CHANNEL;

	std::vector<WeaponProjectile> m_projectiles;

	irr::video::ITexture* m_crosshair = nullptr;

	// --- Radiation zones ---
	struct RadiationZone
	{
		irr::core::vector3df position;
		float radius;
		float lifetime         = 0.0f;
		float maxLifetime;
		float damageAccum      = 0.0f;
		float damageTickRate;
		float damagePerTick;
		bool  isSecondary      = false;

		irr::scene::IParticleSystemSceneNode* particles = nullptr;

		// Entity IDs that already triggered a secondary zone spawn from this zone
		std::set<entityid> secondarySpawned;
	};

	std::vector<RadiationZone> m_zones;

	void spawnProjectile();
	void updateProjectiles(float dt);
	// surfaceNormal: impact normal for contact detonations (scorch orientation);
	// zero vector for timer detonations (floor probe fallback)
	void detonateAt(const irr::core::vector3df& pos, entityid directHitID,
		const irr::core::vector3df& surfaceNormal = irr::core::vector3df(0.0f, 0.0f, 0.0f));
	void spawnZone(const irr::core::vector3df& pos, bool secondary);
	void updateZones(float dt);
};

#pragma once

#include "../WeaponData.h"

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

	irr::video::E_MATERIAL_TYPE      m_muzzleFlashMaterialType = irr::video::E_MATERIAL_TYPE::EMT_TRANSPARENT_ALPHA_CHANNEL;
	irr::scene::IBillboardSceneNode* m_muzzleStarNode          = nullptr;
	float                            m_muzzleFlashTime         = 0.0f;
	const float                      m_muzzleFlashDuration     = 60.0f;

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
	void detonateAt(const irr::core::vector3df& pos, entityid directHitID);
	void createMuzzleFlash();
	void updateMuzzleFlash(float dt);
	void spawnZone(const irr::core::vector3df& pos, bool secondary);
	void updateZones(float dt);
};

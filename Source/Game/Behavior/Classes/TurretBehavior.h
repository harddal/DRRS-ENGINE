#pragma once
#include "Engine/Types.h"
#include "Game/Behavior/EntityBehavior.h"
#include "Game/Components/DamageReceiverComponent.h"
#include <irrlicht.h>
#include <vector>

class TurretBehavior : public EntityBehavior
{
public:
    void init(anax::Entity& entity) override;
    void update(anax::Entity& entity, float dt) override;
    void destroy(anax::Entity& entity) override;

    std::vector<BehaviorProperty> getProperties() override
    {
        return {
            { "type",           BehaviorPropType::INT,   &m_type           },
            { "detectionRange", BehaviorPropType::FLOAT, &m_detectionRange },
            { "fireRate",       BehaviorPropType::FLOAT, &m_fireRate       },
            { "rotateSpeed",    BehaviorPropType::FLOAT, &m_rotateSpeed    },
            { "damage",         BehaviorPropType::INT,   &m_damage         },
            { "splashRadius",   BehaviorPropType::FLOAT, &m_splashRadius   },
        };
    }

private:
    float m_detectionRange = 20.0f;
    float m_fireRate       = 1.0f;    // shots per second
    float m_rotateSpeed    = 90.0f;   // degrees per second
    int   m_damage         = 10;
    int   m_type           = 0;       // 0=gun, 1=grenade, 2=rocket, 3=laser
    float m_splashRadius   = 4.0f;

    float m_fireCooldown   = 0.0f;
    float m_currentYaw     = 0.0f;
    float m_currentPitch   = 0.0f;
    bool  m_fireLeft       = true;

    irr::scene::IBoneSceneNode* m_bodyBone       = nullptr;
    irr::scene::IBoneSceneNode* m_leftBone       = nullptr;
    irr::scene::IBoneSceneNode* m_rightBone      = nullptr;
    irr::scene::IBoneSceneNode* m_leftFirespot   = nullptr;
    irr::scene::IBoneSceneNode* m_rightFirespot  = nullptr;

    struct TurretProjectile
    {
        anax::Entity entity;
        entityid     id        = _entity_null_value;
        irr::core::vector3df velocity;
        float lifetime         = 0.0f;
        float maxLifetime      = 4000.0f;
        bool  hasGravity       = true;
    };
    std::vector<TurretProjectile> m_projectiles;

    struct TracerBeam
    {
        irr::scene::ISceneNode* node = nullptr;
        float spawnTime = 0.0f;
        float lifetime  = 80.0f;  // ms
    };
    std::vector<TracerBeam> m_tracerBeams;
    irr::video::E_MATERIAL_TYPE m_tracerMaterial;

    void fireGun(const irr::core::vector3df& muzzlePos, const irr::core::vector3df& dir);
    void fireGrenade(const irr::core::vector3df& muzzlePos, const irr::core::vector3df& dir);
    void fireRocket(const irr::core::vector3df& muzzlePos, const irr::core::vector3df& dir);
    void fireLaser(const irr::core::vector3df& muzzlePos, const irr::core::vector3df& dir);
    void spawnProjectile(const irr::core::vector3df& pos, const irr::core::vector3df& velocity,
                         float maxLifetime, bool hasGravity);
    void createTracerBeam(const irr::core::vector3df& start, const irr::core::vector3df& end);
    void updateTracers(float dt);
    void updateProjectiles(float dt);
    void detonateAt(const irr::core::vector3df& pos);
};

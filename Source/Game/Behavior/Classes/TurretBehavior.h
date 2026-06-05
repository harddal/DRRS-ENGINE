#pragma once
#include "Engine/Types.h"
#include "Game/Behavior/EntityBehavior.h"
#include <irrlicht.h>
#include <vector>

class TurretBehavior : public EntityBehavior
{
public:
    void init(anax::Entity& entity) override;
    void update(anax::Entity& entity, float dt) override;
    void persist(anax::Entity& entity, float dt) override;
    void destroy(anax::Entity& entity) override;
    void onLogicSignal(anax::Entity& entity) override;

    std::vector<BehaviorProperty> getProperties() override
    {
        return {
            { "detectionRange", BehaviorPropType::FLOAT, &m_detectionRange },
            { "rotateSpeed",    BehaviorPropType::FLOAT, &m_rotateSpeed    },
            { "fireRate",       BehaviorPropType::FLOAT, &m_fireRate       },
            { "burstPause",     BehaviorPropType::FLOAT, &m_burstPause     },
            { "damage",         BehaviorPropType::INT,   &m_damage         },
        };
    }

private:
    float m_detectionRange = 20.0f;
    float m_rotateSpeed    = 90.0f;   // degrees per second
    float m_fireRate       = 8.0f;    // shots per second within a burst
    float m_burstPause     = 1000.0f; // ms pause between bursts
    int   m_damage         = 5;

    float    m_currentYaw   = 0.0f;
    float    m_currentPitch = 0.0f;
    bool     m_isFiring     = false;
    bool     m_active       = true;
    bool     m_destroyed    = false;
    float    m_fireCooldown = 0.0f;
    int      m_burstCount   = 0;
    uint32_t m_smokeHandle  = 0;
    float    m_entityScale  = 1.0f;

    irr::scene::IBoneSceneNode* m_boneRotY     = nullptr;
    irr::scene::IBoneSceneNode* m_boneRotX     = nullptr;
    irr::scene::IBoneSceneNode* m_boneFirespot = nullptr;
    irr::scene::IBoneSceneNode* m_boneEject    = nullptr;
    irr::scene::IBoneSceneNode* m_boneLight    = nullptr;

    irr::scene::IBillboardSceneNode* m_coronaNode = nullptr;

    // Muzzle flash
    irr::video::E_MATERIAL_TYPE          m_muzzleFlashMaterial;
    irr::scene::IBillboardSceneNode*     m_muzzleStarNode  = nullptr;
    irr::scene::ILightSceneNode*         m_muzzleLightNode = nullptr;
    float                                m_muzzleFlashTime = 0.0f;
    static constexpr float MUZZLE_FLASH_DURATION = 50.0f;

    // Tracer beams
    struct TracerBeam {
        irr::scene::IMeshSceneNode* node = nullptr;
        float spawnTime = 0.0f;
        float lifetime  = 50.0f;
    };
    std::vector<TracerBeam> m_tracerBeams;

    // Shell casing pool
    static constexpr int SHELL_POOL_SIZE = 32;
    struct ShellCasing {
        irr::scene::IMeshSceneNode* node = nullptr;
        irr::core::vector3df velocity;
        irr::core::vector3df angularVelocity;
        irr::core::vector3df rotation;
        float spawnTime     = 0.0f;
        bool  active        = false;
        bool  physicsActive = true;
        int   bounceCount   = 0;
    };
    ShellCasing          m_shellPool[SHELL_POOL_SIZE];
    float                m_lastShellBounceSound = 0.0f;
    irr::core::vector3df m_lastFiringDir;
    irr::core::vector3df m_modelUp = irr::core::vector3df(0.0f, 1.0f, 0.0f);

    void fire(const irr::core::vector3df& muzzlePos, const irr::core::vector3df& dir);
    void createMuzzleFlash();
    void updateMuzzleFlash(float dt);
    void ejectShell();
    void updateShells(float dt);
    void createTracerBeam(const irr::core::vector3df& start, const irr::core::vector3df& end);
    void updateTracers(float dt);
};

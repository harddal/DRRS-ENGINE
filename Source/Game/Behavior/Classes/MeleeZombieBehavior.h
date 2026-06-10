#pragma once
#include "Engine/Types.h"
#include "Game/Behavior/EntityBehavior.h"
#include <irrlicht.h>
#include <vector>
#include <string>

class MeleeZombieBehavior : public EntityBehavior
{
public:
    void init(anax::Entity& entity) override;
    void update(anax::Entity& entity, float dt) override;
    void persist(anax::Entity& entity, float dt) override;

    std::vector<BehaviorProperty> getProperties() override
    {
        return {
            { "Attack Damage",      BehaviorPropType::INT,   &m_attackDamage      },
            { "Attack Range",       BehaviorPropType::FLOAT, &m_attackRange       },
            { "Attack Delay",       BehaviorPropType::FLOAT, &m_attackDelay       },
            { "Detection Range",    BehaviorPropType::FLOAT, &m_detectionRange    },
            { "Chase Range",        BehaviorPropType::FLOAT, &m_chaseRange        },
            { "Move Speed",         BehaviorPropType::FLOAT, &m_moveSpeed         },
            { "Seperation Radius",  BehaviorPropType::FLOAT, &m_separationRadius  },
        };
    }

private:
    enum class State { IDLE, CHASE, ATTACK, DEAD };

    int   m_attackDamage      = 10;
    float m_attackRange       = 1.25f;
    float m_attackDelay       = 2000.0f; // ms
    float m_detectionRange    = 5.0f;
    float m_chaseRange        = 5.0f;
    float m_moveSpeed         = 3.0f;
    float m_separationRadius  = 1.5f;

    State m_state        = State::IDLE;
    bool  m_isDead       = false;
    bool  m_dmgToggle    = false;
    float m_attackTimer  = 0.0f;
    float m_repathTimer  = 0.0f;

    std::vector<irr::core::vector3df> m_path;
    int m_pathIndex = 0;

    void playAnim(const std::string& name, struct MeshComponent& mc);
    irr::core::vector3df calcSeparation(const irr::core::vector3df& myPos, anax::Entity& self);
};

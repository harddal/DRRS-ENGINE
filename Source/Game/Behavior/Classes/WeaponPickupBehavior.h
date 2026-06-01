#pragma once
#include "Game/Behavior/EntityBehavior.h"
#include "Game/Player/WeaponData.h"

class WeaponPickupBehavior : public EntityBehavior
{
public:
    void init(anax::Entity& entity) override;
    void update(anax::Entity& entity, float dt) override;

    std::vector<BehaviorProperty> getProperties() override
    {
        return {
            { "weaponType", BehaviorPropType::INT,   &m_weaponType },
            { "ammoAmount", BehaviorPropType::INT,   &m_ammoAmount },
            { "bobSpeed",   BehaviorPropType::FLOAT, &m_bobSpeed   },
        };
    }

private:
    int   m_weaponType = static_cast<int>(WEAP_PISTOL);
    int   m_ammoAmount = 30;
    float m_bobSpeed   = 1.5f;
    float m_bobTimer   = 0.0f;
    float m_baseY      = 0.0f;
};

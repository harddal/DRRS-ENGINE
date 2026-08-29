#pragma once
#include "Game/Behavior/EntityBehavior.h"
#include "Game/Player/WeaponData.h"

// ---------------------------------------------------------------------------
// WeaponPickupBehavior — a weapon lying in the world: bobs, turns, and arms the
// player who walks into it.
//
// The grant lives here rather than in an AngelScript pickup script because the
// data it needs is already here and TYPED (weaponType, ammoAmount), because this
// already ticks with the entity, and because it can call WeaponController
// directly — the script route would need a new binding and a module compiled per
// pickup to do the same three lines.
// ---------------------------------------------------------------------------
class WeaponPickupBehavior : public EntityBehavior
{
public:
    void init(anax::Entity& entity) override;
    void update(anax::Entity& entity, float dt) override;

    std::vector<BehaviorProperty> getProperties() override
    {
        return {
            { "weaponType",   BehaviorPropType::INT,   &m_weaponType   },
            { "ammoAmount",   BehaviorPropType::INT,   &m_ammoAmount   },
            { "ammoType",     BehaviorPropType::INT,   &m_ammoType     },
            { "bobSpeed",     BehaviorPropType::FLOAT, &m_bobSpeed     },
            { "spinSpeed",    BehaviorPropType::FLOAT, &m_spinSpeed    },
            { "pickupRadius", BehaviorPropType::FLOAT, &m_pickupRadius },
        };
    }

private:
    int   m_weaponType = static_cast<int>(WEAP_NONE);
    int   m_ammoAmount = 30;

    // AMMO_TYPE. Defaults to AMMO_NONE, which means "grant no reserve ammo" —
    // deliberately, because guessing a type would silently pour rounds into the
    // wrong pool. Every weapon in the glTF set keeps a self-contained magazine
    // and never reads the shared pool anyway, so this only matters once reserve
    // ammunition is wired up for them.
    int   m_ammoType = static_cast<int>(AMMO_NONE);

    float m_bobSpeed   = 1.5f;  // radians/sec through the bob cycle
    float m_spinSpeed  = 45.0f; // degrees/sec about Y

    // Deliberately its OWN property rather than being derived from the entity's
    // scale. The script pickup path builds its trigger box out of the transform
    // scale, which entangles how big a pickup LOOKS with how close you must get
    // to it — and these weapon models are scaled to 0.01, which would leave a
    // trigger a couple of centimetres wide.
    float m_pickupRadius = 1.0f;

    // Kills are queued to the end of the frame, so this entity keeps ticking
    // after it has been collected. Without this the pickup fires again on every
    // remaining frame — several sounds, and several giveWeapon() calls.
    bool  m_collected = false;

    float m_bobTimer = 0.0f;
    float m_baseY    = 0.0f;
};

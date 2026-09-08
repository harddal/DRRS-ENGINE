#pragma once
#include "Engine/Types.h"
#include "Game/Behavior/CharacterBehavior.h"
#include <irrlicht.h>
#include <vector>
#include <string>

// A fearless charger that closes on its target and detonates itself.
//
// Structurally a MeleeZombieBehavior with the bite loop replaced by a one-shot
// detonation, and one deliberate difference: there is no chase-range give-up.
// Once this thing has seen you it comes until it dies or reaches you, which is
// the entire read on the unit — you cannot back away from it, you have to kill
// it, and it has few enough hit points that you can.
//
// Clip names are the CAPITALISED ones from suicide_cultist.anim ("Idle",
// "Walk", "RunWithDynamite", "Die1", "Die2"), not the zombie's lowercase set.
// MeshComponent::findAnimation is an exact, case-sensitive compare.
//
// It takes CharacterBehavior's defaults for m_turnRate (200 deg/s) and
// m_yawOffset (180, the glTF Z-mirror) unchanged — this is the model the base
// defaults were chosen for.
class SuicideBomberBehavior : public CharacterBehavior
{
public:
    // Set here and not in init(): the factory runs applyPropertiesToBehavior()
    // BEFORE init(), so anything assigned in init() stomps a value loaded from
    // the .ent. Same reason MeleeZombieBehavior sets its overrides in its
    // constructor.
    SuicideBomberBehavior()
    {
        // APPROACH, never HOLD. The ring spreads the bearings a group comes in
        // on while they are still far out and is dropped the moment they are
        // inside it; nobody ever waits for a token. Holding this unit back at
        // the ring would contradict the entire read on it — see the class
        // comment above.
        m_squadMode = SQUAD_APPROACH;
        m_squadRing = 6.0f;

        // Unused in APPROACH (everyone is permanently a holder), but kept
        // sensible so flipping Squad Mode to 2 in the editor to try it out
        // behaves rather than granting nobody a token.
        m_squadTokens = 2;
    }

    // Cult unless a placed NPCComponent says otherwise. This is what makes the
    // cultists already saved into weapon_test.pak hostile without re-saving.
    FACTION defaultFaction() const override { return FACTION::CULT; }

    void init(anax::Entity& entity) override;
    void update(anax::Entity& entity, float dt) override;
    void persist(anax::Entity& entity, float dt) override;

    std::vector<BehaviorProperty> getProperties() override
    {
        // baseProperties() carries "Seperation Radius" and "Turn Rate", which
        // used to be listed here. Their key strings are unchanged, so saved
        // .ent values still resolve.
        std::vector<BehaviorProperty> props = baseProperties();
        props.push_back({ "Detection Range", BehaviorPropType::FLOAT,  &m_detectionRange });
        props.push_back({ "Trigger Radius",  BehaviorPropType::FLOAT,  &m_triggerRadius  });
        props.push_back({ "Blast Radius",    BehaviorPropType::FLOAT,  &m_blastRadius    });
        props.push_back({ "Blast Damage",    BehaviorPropType::INT,    &m_blastDamage    });
        props.push_back({ "Blast Force",     BehaviorPropType::FLOAT,  &m_blastForce     });
        props.push_back({ "Charge Speed",    BehaviorPropType::FLOAT,  &m_chargeSpeed    });
        props.push_back({ "Patrol Speed",    BehaviorPropType::FLOAT,  &m_patrolSpeed    });
        props.push_back({ "Chain Fuse",      BehaviorPropType::FLOAT,  &m_chainFuse      });
        props.push_back({ "Patrol Route",    BehaviorPropType::STRING, &m_patrolRoute    });
        return props;
    }

private:
    enum class State { IDLE, PATROL, CHARGE, DEAD };

    float m_detectionRange  = 12.0f;
    float m_triggerRadius   = 2.0f;
    float m_blastRadius     = 4.0f;
    int   m_blastDamage     = 75;
    float m_blastForce      = 900.0f;
    float m_chargeSpeed     = 4.5f;
    float m_patrolSpeed     = 1.5f;

    // Delay between being caught in a blast and cooking off, in ms. Jittered
    // +/-40% per bomber so a tight cluster does not sound like one explosion.
    float m_chainFuse       = 220.0f;

    // Comma-separated names of marker entities, walked in order and looped.
    // Empty (the default) means "stand still and play Idle" — no route, no
    // navmesh dependency, which is what makes a freshly dropped cultist do
    // something visible the moment it is placed.
    std::string m_patrolRoute;

    State m_state       = State::IDLE;
    bool  m_detonated   = false;

    // Cook-off timer, in ms. Negative means unarmed. Armed by any explosive hit
    // — another bomber's blast or a launcher round — so a cluster goes off as a
    // ripple of separate bangs rather than one simultaneous thud.
    float m_fuseTimer = -1.0f;

    void armFuse(anax::Entity& entity);

    // Resolved once on first use — marker entities may not exist yet at init().
    bool m_routeResolved = false;
    std::vector<irr::core::vector3df> m_routePoints;
    int m_routeIndex = 0;

    void resolveRoute();
    void detonate(anax::Entity& entity);
};

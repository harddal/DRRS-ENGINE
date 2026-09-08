#pragma once
#include "Engine/Types.h"
#include "Game/Behavior/CharacterBehavior.h"
#include <irrlicht.h>
#include <vector>
#include <string>

struct MeshComponent;
struct TransformComponent;

class MeleeZombieBehavior : public CharacterBehavior
{
public:
    // Base defaults are set for the glTF cast. This one is the old .b3d model,
    // and both overrides have to happen HERE rather than in init(): the factory
    // runs applyPropertiesToBehavior() before init(), so anything set in init()
    // would stomp a value loaded from the .ent.
    MeleeZombieBehavior()
    {
        // WAS 0 (snap to facing), which short-circuited followPath's slew
        // entirely and moved the body straight along the steering vector.
        //
        // That had to change when dtCrowd started supplying the steering, for
        // two reasons, and the second is structural:
        //
        //   1. An RVO avoidance velocity swings direction hard from frame to
        //      frame in a way a smooth waypoint vector never did. Snapping the
        //      yaw to it reads as jitter.
        //   2. At turn rate 0, forward == steer == the crowd's nvel, so the
        //      body moves EXACTLY along Detour's vector. The whole point of
        //      steering advisorily -- the behaviour keeping ownership of its own
        //      motion -- is vacuous at 0. A finite turn rate is what keeps this
        //      class in the loop at all.
        //
        // 360 deg/s: a half turn in half a second, so it still reads as the
        // hard-turning shambler it has always been, while low-passing the
        // avoidance vector.
        //
        // Knock-on: this also switches on followPath's cos(facingError) speed
        // scale, which the zombie has never had. A hard turn now costs it up to
        // 75% of its speed. If that reads as weak, raise Move Speed -- do not
        // put the turn rate back to 0.
        m_turnRate  = 360.0f;

        // Pack animal: hold the ring, close only with a token. This is the
        // doorway fix -- three zombies engage, the rest circle instead of
        // stacking up behind them.
        //
        // Its existing ranges cooperate with no new state: at a 4m ring it is
        // still inside the give-up distance (chaseRange * 2 == 10), so it stays
        // in CHASE and keeps playing 'move', and it never reaches attackRange
        // (1.25) so it cannot bite from out there.
        m_squadMode   = SQUAD_HOLD;
        m_squadTokens = 3;
        m_squadRing   = 4.0f;

        // No half-turn: the .b3d cast is not Z-mirrored the way the glTF
        // importer's output is. If the base's 180 leaks through, the zombie
        // moonwalks -- and it looks CORRECT running along X while staying
        // backwards along Z, which is a nasty thing to debug.
        m_yawOffset = 0.0f;

        // OMNIDIRECTIONAL, deliberately, against the base default of 120.
        //
        // This is a design choice and not a disabled feature: a zombie does not
        // have a face you can sneak up on, and giving it a cone would make
        // creeping past a shambling crowd trivially easy in exactly the
        // situation that is supposed to be tense. LOS still gates acquisition,
        // so it cannot see you through a wall -- it just does not care which
        // way it happens to be pointing.
        m_visionCone = 360.0f;
    }

    // Undead unless a placed NPCComponent says otherwise. This is what makes a
    // zombie already saved into a scene hostile without re-saving the scene.
    FACTION defaultFaction() const override { return FACTION::UNDEAD; }

    void init(anax::Entity& entity) override;
    void update(anax::Entity& entity, float dt) override;
    void persist(anax::Entity& entity, float dt) override;

    std::vector<BehaviorProperty> getProperties() override
    {
        // baseProperties() FIRST, then this behaviour's own rows. Forgetting the
        // append does not just hide the base rows from the panel -- editing any
        // other property rebuilds the whole serialized string from this vector
        // and would erase them from the save data.
        std::vector<BehaviorProperty> props = baseProperties();
        props.push_back({ "Attack Damage",   BehaviorPropType::INT,   &m_attackDamage   });
        props.push_back({ "Attack Range",    BehaviorPropType::FLOAT, &m_attackRange    });
        props.push_back({ "Attack Delay",    BehaviorPropType::FLOAT, &m_attackDelay    });
        props.push_back({ "Detection Range", BehaviorPropType::FLOAT, &m_detectionRange });
        props.push_back({ "Chase Range",     BehaviorPropType::FLOAT, &m_chaseRange     });
        props.push_back({ "Move Speed",      BehaviorPropType::FLOAT, &m_moveSpeed      });

        // Appended at the end. Key strings are matched by name, so a .ent saved
        // before these existed keeps the defaults rather than breaking.
        props.push_back({ "Search Delay",    BehaviorPropType::FLOAT, &m_searchDelay    });
        props.push_back({ "Search Look Time",BehaviorPropType::FLOAT, &m_searchLookTime });
        props.push_back({ "Stagger Time",    BehaviorPropType::FLOAT, &m_staggerTime    });
        props.push_back({ "Wander Radius",   BehaviorPropType::FLOAT, &m_wanderRadius   });
        props.push_back({ "Wander Delay",    BehaviorPropType::FLOAT, &m_wanderDelay    });
        props.push_back({ "Shuffle Amount",  BehaviorPropType::FLOAT, &m_shuffleAmount  });
        return props;
    }

private:
    // SEARCH sits between CHASE and IDLE: the target is still remembered, but it
    // has not been SEEN for m_searchDelay, so the zombie goes to where it last
    // was and has a look round before losing interest.
    //
    // It is the payoff for the base class's LOS gating. Without it, breaking
    // line of sight would just mean the zombie kept walking at your live
    // position through a wall -- the perception work would be invisible.
    enum class State { IDLE, CHASE, SEARCH, ATTACK, DEAD };

    int   m_attackDamage      = 10;
    float m_attackRange       = 1.25f;
    float m_attackDelay       = 2000.0f; // ms
    float m_detectionRange    = 5.0f;
    float m_chaseRange        = 5.0f;
    float m_moveSpeed         = 3.0f;

    // How long LOS must stay broken before CHASE becomes SEARCH. Short enough
    // that ducking behind a crate is answered, long enough that a pillar you
    // run past does not trigger it.
    float m_searchDelay       = 1500.0f; // ms

    // Time spent turning on the spot at the last known position before giving
    // up and dropping to IDLE.
    float m_searchLookTime    = 2500.0f; // ms

    // Flinch on taking a hit: movement stops and the legs slow, briefly. Driven
    // from the EXISTING didReceiveDamage() call site in persist() -- see there
    // for why a second call site would silently break the hit sounds.
    float m_staggerTime       = 350.0f;  // ms

    // Idle wander. 0 disables it and the zombie stands still exactly as before.
    // The destination comes from NavigationManager::randomPointNear, so it is
    // always somewhere actually reachable rather than through a wall.
    float m_wanderRadius      = 6.0f;
    float m_wanderDelay       = 5000.0f; // ms between legs, jittered +/-50%

    // Lateral drift around the target between bites, as a fraction of the
    // separation push. A ring of zombies waiting their turn on the attack
    // tokens otherwise reads as a ring of statues.
    float m_shuffleAmount     = 0.5f;

    State m_state        = State::IDLE;
    bool  m_dmgToggle    = false;
    float m_attackTimer  = 0.0f;

    float m_searchTimer  = 0.0f;   // ms spent looking round on arrival
    bool  m_searchWalked = false;  // false until the walk leg has finished

    float m_staggerTimer = 0.0f;   // ms of flinch left

    float m_shuffleTimer = 0.0f;   // ms until the drift direction flips
    float m_shuffleDir   = 1.0f;   // +1 / -1

    irr::core::vector3df m_wanderGoal;
    bool  m_hasWanderGoal = false;
    float m_wanderTimer   = 0.0f;

    // Stands still, or shambles to a random reachable point nearby. Called only
    // with no live target.
    void updateIdleWander(anax::Entity& entity, TransformComponent& tc,
                          MeshComponent& mc, float dt);
};

#include "MeleeZombieBehavior.h"

#include "Engine/Navigation/NavigationManager.h"
#include "Engine/World/WorldManager.h"
#include "Engine/World/Components/TransformComponent.h"
#include "Engine/World/Components/MeshComponent.h"
#include "Engine/World/Components/SoundComponent.h"
#include "Engine/World/Components/DescriptorComponent.h"
#include "Game/Components/DamageReceiverComponent.h"
#include "Utility/Utility.h"

#include <cmath>
#include <cstdlib>

using namespace irr::core;

namespace
{
    // How fast the zombie turns on the spot while searching, deg/sec. Slow
    // enough to read as looking around rather than as a spin.
    const float k_searchTurnRate = 70.0f;

    // Drift direction holds for this long, plus up to k_shuffleFlipJitter more,
    // before flipping. Randomised per zombie so a ring of them does not sway in
    // unison, which looks far more mechanical than not moving at all.
    const float k_shuffleFlipMs     = 600.0f;
    const float k_shuffleFlipJitter = 800.0f;

    // Playback rate for the legs during a flinch. Matches the floor followPath
    // clamps the walk cycle to, so the stagger reads as the same slowdown
    // rather than as a separate effect.
    const float k_staggerAnimRate = 0.35f;

    // followPath must report no movement for this long before the clip is
    // swapped to 'idle'. See m_stillTimer.
    const float k_stillClipSwapMs = 250.0f;
}

// ---------------------------------------------------------------------------

void MeleeZombieBehavior::init(anax::Entity& entity)
{
    // Randomised so a room full of zombies does not all take their first wander
    // step on the same frame. init() runs AFTER applyPropertiesToBehavior, so
    // m_wanderDelay is already the .ent's value here and not the default.
    m_wanderTimer = m_wanderDelay * (static_cast<float>(rand() % 101) * 0.01f);

    if (!entity.hasComponent<MeshComponent>()) return;
    playAnim(entity.getComponent<MeshComponent>(), "idle");
    m_state = State::IDLE;
}

// ---------------------------------------------------------------------------

void MeleeZombieBehavior::update(anax::Entity& entity, float dt)
{
    if (!entity.hasComponent<TransformComponent>()) return;
    auto& tc = entity.getComponent<TransformComponent>();

    // Gravity -- pin to floor every frame regardless of state.
    //
    // dt is passed now, which rate-limits the CLIMB. Without it this hard write
    // would undo followPath's own ground smoothing on the very next frame and
    // the body would keep popping up stair treads.
    snapToGround(tc, dt);

    if (m_isDead) return;
    if (!entity.hasComponent<MeshComponent>()) return;

    auto& mc = entity.getComponent<MeshComponent>();

    // Flinch. Armed in persist() from the existing didReceiveDamage() read.
    // Ticked here, before anything else, so every branch below sees it.
    if (m_staggerTimer > 0.0f)
    {
        m_staggerTimer -= dt;
        m_currentSpeed  = 0.0f;
        if (mc.node)
            mc.node->setAnimationSpeed(static_cast<irr::f32>(mc.fps) * k_staggerAnimRate);
    }
    const bool staggered = (m_staggerTimer > 0.0f);

    // Nearest hostile, not hardcoded "player". A zombie is UNDEAD, so the
    // hostility table sends it after the player, a cultist or a civilian alike.
    //
    // Acquisition is now LOS-gated in the base, so this only returns something
    // the zombie has actually seen. Retention is not gated, so it keeps the
    // target after losing sight of it -- what to DO about that is SEARCH below.
    const entityid targetId = findTarget(entity, dt);
    if (targetId == _entity_null_value)
    {
        // Nothing hostile left -- the player died, or everything in the level is
        // pacified. Drop to idle rather than freezing mid-stride in the chase
        // pose, which is what a bare early-return here would do.
        if (m_state != State::IDLE)
        {
            m_state = State::IDLE;
            resetMovement();
            m_hasWanderGoal = false;
            playAnim(mc, "idle");
        }

        if (!staggered) updateIdleWander(entity, tc, mc, dt);
        return;
    }

    const anax::Entity& target = this->target();

    const vector3df myPos     = tc.getPosition();
    const vector3df targetPos = target.getComponent<TransformComponent>().getPosition();

    vector3df delta = targetPos - myPos;
    delta.Y = 0.0f;
    const float dist = delta.getLength();

    // ---- State transitions ----
    if (m_state == State::IDLE)
    {
        // hasFreshTarget() as well as the range test. Distance alone fired
        // through solid walls: findTarget only ACQUIRES what it can see, but it
        // RETAINS a target indefinitely, so a zombie that had once seen you and
        // dropped back to IDLE would re-engage through a wall on range alone.
        if (dist <= m_detectionRange && hasFreshTarget())
        {
            m_state = State::CHASE;
            resetMovement();
            m_hasWanderGoal = false;
            m_stillTimer    = 0.0f;
            m_repathTimer   = 99999.0f;
            playAnim(mc, "move");
        }
        else
        {
            if (!staggered) updateIdleWander(entity, tc, mc, dt);
            return;
        }
    }

    if (m_state == State::CHASE)
    {
        if (dist <= m_attackRange)
        {
            m_state = State::ATTACK;
            m_attackTimer = m_attackDelay; // allow immediate first bite
            resetMovement();
        }
        else if (dist > m_chaseRange * 2.0f)
        {
            m_state = State::IDLE;
            resetMovement();
            playAnim(mc, "idle");
            return;
        }
    }

    if (m_state == State::SEARCH)
    {
        // Seeing it again at any point in the search cancels it outright --
        // including mid-turn, which is the moment that reads best.
        if (hasFreshTarget())
        {
            m_state      = State::CHASE;
            m_stillTimer = 0.0f;
            resetMovement();
            m_repathTimer = 99999.0f;
            playAnim(mc, "move");
        }
        else if (m_searchTimer >= m_searchLookTime)
        {
            m_state = State::IDLE;
            resetMovement();
            playAnim(mc, "idle");
            return;
        }
    }

    if (m_state == State::ATTACK)
    {
        if (dist > m_attackRange * 1.25f)
        {
            m_state      = State::CHASE;
            m_stillTimer = 0.0f;
            resetMovement();
            m_repathTimer = 99999.0f;
            playAnim(mc, "move");
        }
    }

    // ---- CHASE: pathfind and move ----
    if (m_state == State::CHASE)
    {
        if (staggered) return;

        // Pursue what it can actually SEE. With the sight line broken it heads
        // for where the target was, not where the target is -- same commitment,
        // no wallhack -- and snaps back the instant it sees it again.
        //
        // Losing sight does NOT change state. That is the whole lesson of the
        // maze test: a corner must not interrupt a chase.
        const bool      seen   = hasFreshTarget();
        const vector3df pursue = (seen || !hasLastKnown()) ? targetPos : lastKnownPos();

        // Where the squad wants this one -- the pursued point if it holds an
        // attack token, otherwise its slot on the ring. With Squad Mode set to
        // 0 this hands back 'pursue' unchanged and the behaviour is exactly
        // what it was before squads existed.
        //
        // The state machine deliberately keeps measuring 'dist' to the TARGET,
        // not to the goal: a zombie circling at the ring must still give up at
        // chase range and still transition to ATTACK when the target closes on
        // IT, neither of which is a fact about the slot.
        const SquadOrder order = requestSquadOrder(entity, targetId, pursue);

        const bool moved = followPath(entity, order.goal, m_moveSpeed, dt);

        m_stillTimer = moved ? 0.0f : (m_stillTimer + dt);

        // ARRIVED, and still cannot see it. Only now is the target really lost,
        // and only now does it stop and look round. m_searchDelay is a floor on
        // top of this so a momentary hold does not count as arriving.
        if (!moved && !seen && hasLastKnown() && timeSinceSeen() > m_searchDelay)
        {
            m_state       = State::SEARCH;
            m_searchTimer = 0.0f;
            resetMovement();
            playAnim(mc, "idle");
            return;
        }

        // Holding a ring slot, or boxed in behind another zombie. Standing
        // still with the walk cycle running IS the walking-in-place complaint,
        // and no animation rate can fix it -- it is the wrong clip.
        playAnim(mc, (m_stillTimer > k_stillClipSwapMs) ? "idle" : "move");
        return;
    }

    // ---- SEARCH: stand on the last known position and look round ----
    //
    // No walking here at all. CHASE did the walking; this state is only reached
    // by arriving. Re-acquiring at any point during it goes straight back to
    // CHASE, which is handled in the transition block above.
    if (m_state == State::SEARCH)
    {
        if (staggered) return;

        // Turn on the spot. m_heading is the base's own travel heading, so
        // writing it here (rather than the node rotation directly) keeps a
        // following followPath from snapping back the moment the search ends.
        m_searchTimer += dt;
        m_heading     += k_searchTurnRate * (dt * 0.001f);
        while (m_heading >  180.0f) m_heading -= 360.0f;
        while (m_heading < -180.0f) m_heading += 360.0f;
        tc.setRotation(vector3df(0.0f, m_heading + m_yawOffset, 0.0f));
        return;
    }

    // ---- ATTACK: face target and bite ----
    if (m_state == State::ATTACK)
    {
        if (dist > 0.01f)
            faceTowards(tc, delta / dist);

        if (!staggered)
        {
            vector3df drift = calcSeparation(myPos, entity);

            // Lateral shuffle between bites. Without it a ring of zombies
            // waiting on the attack tokens is a ring of statues, which is more
            // noticeable than the crowding it replaced.
            if (dist > 0.01f && m_shuffleAmount > 0.0f)
            {
                m_shuffleTimer -= dt;
                if (m_shuffleTimer <= 0.0f)
                {
                    m_shuffleDir   = (rand() % 2) ? 1.0f : -1.0f;
                    m_shuffleTimer = k_shuffleFlipMs +
                                     static_cast<float>(rand() % static_cast<int>(k_shuffleFlipJitter));
                }

                // Perpendicular to the bearing, in XZ. (fwd.Z, 0, -fwd.X) is the
                // right-hand normal of the flat facing.
                const vector3df fwd = delta / dist;
                drift += vector3df(fwd.Z, 0.0f, -fwd.X) * (m_shuffleDir * m_shuffleAmount);
            }

            if (drift.getLength() > 0.001f)
            {
                // Wall-slid: the drift is written straight to the transform and
                // nothing else stops it walking a zombie into geometry.
                const vector3df step = slideAlongWall(myPos, drift);
                tc.setPosition(myPos + step * m_moveSpeed * 0.5f * (dt * 0.001f));
                snapToGround(tc, dt);
            }
        }

        m_attackTimer += dt;
        if (m_attackTimer >= m_attackDelay)
        {
            m_attackTimer = 0.0f;

            WorldManager::Get()->gameplaySystem()->damageEntity(
                targetId, static_cast<unsigned int>(m_attackDamage));

            playAnim(mc, "melee");

            if (entity.hasComponent<SoundComponent>())
                entity.getComponent<SoundComponent>().play("attack");
        }
    }
}

// ---------------------------------------------------------------------------
// Idle wander
//
// Only ever called with no live target. A zombie that stands perfectly still
// until something walks into its detection range reads as a spawner prop; one
// that shuffles a few metres and stops reads as something that was already
// there.
// ---------------------------------------------------------------------------

void MeleeZombieBehavior::updateIdleWander(anax::Entity& entity, TransformComponent& tc,
                                           MeshComponent& mc, float dt)
{
    if (m_wanderRadius <= 0.0f)
    {
        playAnim(mc, "idle");
        return;
    }

    if (!m_hasWanderGoal)
    {
        playAnim(mc, "idle");

        m_wanderTimer -= dt;
        if (m_wanderTimer > 0.0f) return;

        vector3df goal;
        if (NavigationManager::Get() &&
            NavigationManager::Get()->randomPointNear(tc.getPosition(), m_wanderRadius, goal))
        {
            m_wanderGoal    = goal;
            m_hasWanderGoal = true;
            resetMovement();
            m_repathTimer   = 99999.0f;
            playAnim(mc, "move");
        }
        else
        {
            // No navmesh, or this zombie is standing off it. Re-arm the timer
            // rather than retrying the query every frame for the rest of the
            // level -- an unbaked scene is a supported configuration.
            m_wanderTimer = m_wanderDelay;
        }
        return;
    }

    // Arrival or an unwalkable leg; both end it. Jittered so a group placed
    // together does not step off in unison.
    if (!followPath(entity, m_wanderGoal, m_moveSpeed, dt))
    {
        m_hasWanderGoal = false;
        m_wanderTimer   = m_wanderDelay * (0.5f + static_cast<float>(rand() % 101) * 0.01f);
        resetMovement();
        playAnim(mc, "idle");
    }
}

// ---------------------------------------------------------------------------

void MeleeZombieBehavior::persist(anax::Entity& entity, float dt)
{
    if (m_isDead) return;

    if (!entity.hasComponent<DamageReceiverComponent>()) return;
    auto& drc = entity.getComponent<DamageReceiverComponent>();

    // Hit reaction. This stays HERE and not in the base: didReceiveDamage() is a
    // CONSUMING read (DamageReceiverComponent.h clears the flag as it returns
    // it), and a second caller would silently take the flag away from this one.
    //
    // The stagger is armed from this SAME call site for exactly that reason. A
    // second `if (drc.didReceiveDamage())` for the flinch would race this one
    // and the symptom would be "the hit sounds stopped working sometimes".
    if (drc.didReceiveDamage())
    {
        m_staggerTimer = m_staggerTime;

        if (entity.hasComponent<SoundComponent>())
        {
            m_dmgToggle = !m_dmgToggle;
            entity.getComponent<SoundComponent>().play(m_dmgToggle ? "damage_1" : "damage_2");
        }
    }

    if (handleDeath(entity, "die", "die"))
        m_state = State::DEAD;
}

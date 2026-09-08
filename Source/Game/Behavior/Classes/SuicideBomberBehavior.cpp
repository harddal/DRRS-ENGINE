#include "Game/Behavior/Classes/SuicideBomberBehavior.h"

#include "Engine/Engine.h"
#include "Engine/Navigation/NavigationManager.h"
#include "Engine/Physics/PhysicsManager.h"
#include "Engine/Renderer/DecalManager.h"
#include "Engine/Renderer/RenderManager.h"
#include "Engine/Renderer/Particle/ParticleManager.h"
#include "Engine/Resource/FilePaths.h"
#include "Engine/Sound/SoundManager.h"
#include "Engine/World/WorldManager.h"
#include "Engine/World/Components/DescriptorComponent.h"
#include "Engine/World/Components/MeshComponent.h"
#include "Engine/World/Components/PhysicsComponent.h"
#include "Engine/World/Components/SoundComponent.h"
#include "Engine/World/Components/TransformComponent.h"
#include "Game/Components/BehaviorComponent.h"
#include "Game/Components/DamageReceiverComponent.h"
#include "Game/Player/CameraFX.h"
#include "Utility/Utility.h"

#include <spdlog/spdlog.h>
#include <cmath>
#include <cstdlib>
#include <sstream>

#undef max
#undef min

using namespace irr::core;

// ---------------------------------------------------------------------------

void SuicideBomberBehavior::resolveRoute()
{
    m_routeResolved = true;
    m_routePoints.clear();
    m_routeIndex = 0;

    if (m_patrolRoute.empty()) return;

    std::stringstream ss(m_patrolRoute);
    std::string name;
    while (std::getline(ss, name, ','))
    {
        // Tolerate "a, b, c" as well as "a,b,c"
        const size_t b = name.find_first_not_of(" \t");
        const size_t e = name.find_last_not_of(" \t");
        if (b == std::string::npos) continue;
        name = name.substr(b, e - b + 1);

        auto& marker = WorldManager::Get()->managerSystem()->getEntityByName(name);
        if (marker.isValid() && marker.hasComponent<TransformComponent>())
            m_routePoints.push_back(marker.getComponent<TransformComponent>().getPosition());
        else
            spdlog::warn("SuicideBomberBehavior: patrol marker '{}' not found", name);
    }
}

// ---------------------------------------------------------------------------

void SuicideBomberBehavior::init(anax::Entity& entity)
{
    SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/effect/explosion1.wav", true);
    SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/effect/explosion2.wav", true);
    ParticleManager::Get()->precache("explosion", _asset_psys("explosion"));

    m_state     = State::IDLE;
    m_isDead    = false;
    m_detonated = false;

    if (!entity.hasComponent<MeshComponent>()) return;
    playAnim(entity.getComponent<MeshComponent>(), "Idle");
}

// ---------------------------------------------------------------------------

void SuicideBomberBehavior::update(anax::Entity& entity, float dt)
{
    if (!entity.hasComponent<TransformComponent>()) return;
    auto& tc = entity.getComponent<TransformComponent>();

    // Gravity — pin to floor every frame regardless of state.
    //
    // dt rate-limits the CLIMB. Without it this hard write would undo
    // followPath's ground smoothing on the very next frame.
    snapToGround(tc, dt);

    // Fuse ticks ahead of every other gate. A bomber that has been set off is
    // committed — it keeps running at you while it cooks, and it still goes off
    // if something kills it on the way in.
    if (m_fuseTimer >= 0.0f && !m_detonated)
    {
        m_fuseTimer -= dt;
        if (m_fuseTimer <= 0.0f)
        {
            detonate(entity);
            return;
        }
    }

    if (m_isDead || m_detonated) return;
    if (!entity.hasComponent<MeshComponent>()) return;

    auto& mc = entity.getComponent<MeshComponent>();

    // Markers may not have been spawned yet when init() ran, so the route is
    // resolved on first update instead.
    if (!m_routeResolved) resolveRoute();

    // Nearest hostile, not hardcoded "player". A cultist is CULT, so the
    // hostility table sends it after the player, a zombie or a civilian alike —
    // and a bomber that gets pacified mid-charge drops its target on the spot.
    //
    // No target is NOT an early return: this unit patrols, and a route should
    // keep being walked in an empty room.
    const entityid targetId = findTarget(entity, dt);
    const bool     hasTarget = (targetId != _entity_null_value);

    const vector3df myPos = tc.getPosition();

    vector3df targetPos = myPos;
    float     dist      = 1.0e18f;

    if (hasTarget)
    {
        targetPos = target().getComponent<TransformComponent>().getPosition();
        vector3df delta = targetPos - myPos;
        delta.Y = 0.0f;
        dist = delta.getLength();
    }

    // ---- State transitions --------------------------------------------------
    // The only way out of CHARGE is death or the target ceasing to exist. That
    // is the whole unit: it does not lose interest, so backing away is not an
    // answer to it — you have to kill it, which is why its health pool is small.
    if (!hasTarget && m_state == State::CHARGE)
    {
        // What it was running at is gone. Fall back rather than sprinting at a
        // corpse's last position forever.
        m_state = m_routePoints.empty() ? State::IDLE : State::PATROL;
        resetMovement();
        m_repathTimer = 99999.0f;
    }
    else if (hasTarget && m_state != State::CHARGE && dist <= m_detectionRange &&
             hasFreshTarget())
    {
        // hasFreshTarget() as well as the range test. findTarget only ACQUIRES
        // what it can see, but it RETAINS a target indefinitely -- so without
        // this, a cultist that had seen you once and fallen back to patrol
        // would start its charge through a solid wall on range alone.
        m_state = State::CHARGE;
        resetMovement();
        m_repathTimer = 99999.0f; // force an immediate repath
    }
    else if (m_state == State::IDLE && !m_routePoints.empty())
    {
        m_state = State::PATROL;
        resetMovement();
        m_repathTimer = 99999.0f;
    }

    // ---- IDLE ---------------------------------------------------------------
    if (m_state == State::IDLE)
    {
        playAnim(mc, "Idle");
        return;
    }

    // ---- PATROL -------------------------------------------------------------
    if (m_state == State::PATROL)
    {
        playAnim(mc, "Walk");

        const vector3df goal = m_routePoints[m_routeIndex];
        vector3df toGoal = goal - myPos;
        toGoal.Y = 0.0f;

        const bool arrived = (toGoal.getLength() <= m_arrivalRadius);

        // Skip an unreachable leg rather than standing on it forever
        if (arrived || !followPath(entity, goal, m_patrolSpeed, dt))
        {
            m_routeIndex = (m_routeIndex + 1) % static_cast<int>(m_routePoints.size());
            resetMovement();
            m_repathTimer = 99999.0f;
        }
        return;
    }

    // ---- CHARGE -------------------------------------------------------------
    if (m_state == State::CHARGE)
    {
        // NO GIVE-UP, AND THERE MUST NOT BE ONE. The zombie answers a broken
        // sight line with a SEARCH state that eventually drops to IDLE; this
        // unit deliberately does not get that. Its entire read is that backing
        // away is not an answer to it -- you have to kill it. Breaking line of
        // sight is backing away.
        //
        // What it loses instead is the WALLHACK: while it cannot see you it
        // runs at where you were, not at where you are, and snaps back to your
        // live position the instant the sight line returns. So you can make it
        // commit to the wrong doorway, which is a real play -- you just cannot
        // make it forget you.
        const bool      seen     = hasFreshTarget();
        const vector3df aimPos   = (seen || !hasLastKnown()) ? targetPos : lastKnownPos();

        // The trigger measures against what it is actually running at. Gated on
        // sight as well, so it cannot cook off through a wall it happens to be
        // standing against -- if it arrives at your last position and you are
        // gone, it waits there armed instead, and charges again the moment you
        // show yourself.
        if (seen && dist <= m_triggerRadius)
        {
            detonate(entity);
            return;
        }

        playAnim(mc, "RunWithDynamite");

        // SQUAD_APPROACH: the ring only picks which BEARING this one comes in
        // on while it is still outside the ring, and is abandoned the moment it
        // is inside. Nothing here ever holds back or circles -- a bomber that
        // waits at 6m for a token is a different enemy, and the whole read on
        // this unit is that you cannot back away from it.
        //
        // The squad is asked about aimPos, not targetPos: a ring projected
        // around your LIVE position while the bomber is committed to your last
        // one would hand it a slot it has no reason to walk to.
        const SquadOrder order = requestSquadOrder(entity, targetId, aimPos);

        followPath(entity, order.goal, m_chargeSpeed, dt);
    }
}

// ---------------------------------------------------------------------------

void SuicideBomberBehavior::armFuse(anax::Entity& entity)
{
    if (m_detonated || m_fuseTimer >= 0.0f) return;

    // A body that is already gone gets no fuse. GoreManager::removeBody() has
    // queued the entity for removal, so there is no later frame to tick in —
    // it is now or never.
    if (entity.hasComponent<DamageReceiverComponent>())
    {
        auto& drc = entity.getComponent<DamageReceiverComponent>();
        if (drc.gibbed || drc.health <= 0)
        {
            detonate(entity);
            return;
        }
    }

    // +/-40%, so a tight cluster reads as a ripple of separate bangs instead of
    // one simultaneous thud.
    const float jitter = 0.6f + static_cast<float>(rand() % 101) * 0.008f;
    m_fuseTimer = m_chainFuse * jitter;
}

// ---------------------------------------------------------------------------
// The detonation.
//
// Order matters. Everything else is damaged FIRST and the self-kill goes LAST,
// because the self-kill routes through GoreManager::kill() at TIER_GIB, which
// removes the body outright — running the splash loop after that would be
// iterating the world from inside a half-destroyed entity.
void SuicideBomberBehavior::detonate(anax::Entity& entity)
{
    if (m_detonated) return;

    // Set before anything else: damageEntity() below re-enters this object's
    // persist() in the same frame, and a second detonate() would double the
    // damage and spawn the theatre twice.
    m_detonated = true;
    m_state     = State::DEAD;
    resetMovement();

    // Chest height, not the feet — the blast should read as coming from the
    // charge he is carrying.
    const vector3df epicentre =
        entity.getComponent<TransformComponent>().getPosition() + vector3df(0.0f, 1.0f, 0.0f);

    const entityid selfId = entity.hasComponent<DescriptorComponent>()
        ? entity.getComponent<DescriptorComponent>().id
        : _entity_null_value;

    // --- Theatre -------------------------------------------------------------
    SoundManager::Get()->sound()->playRandomized3D("content/sound/effect/explosion", epicentre, 0.06f);
    ParticleManager::Get()->spawn("explosion", SPK::IRR::irr2spk(epicentre));

    {
        RaycastResultData ground = RenderManager::Get()->raycastWorldPosition(
            epicentre + vector3df(0.0f, 0.5f, 0.0f),
            epicentre - vector3df(0.0f, 2.5f, 0.0f),
            true);
        if (ground.hit)
            RenderManager::Get()->decals()->spawn(ground.point, ground.normal, 2.8f,
                "content/texture/decal/scorch/Burn Mark 4.png");
    }

    // --- Splash: everything but itself ---------------------------------------
    auto& entities = WorldManager::Get()->managerSystem()->getEntities();
    for (auto& other : entities)
    {
        if (!other.isValid()) continue;
        if (!other.hasComponent<DescriptorComponent>()) continue;
        if (!other.hasComponent<TransformComponent>()) continue;

        auto& desc = other.getComponent<DescriptorComponent>();
        if (desc.id == selfId) continue;
        if (!desc.isAlive) continue;

        const vector3df otherPos = other.getComponent<TransformComponent>().getPosition();
        const float d = (otherPos - epicentre).getLength();
        if (d >= m_blastRadius) continue;

        const float falloff = 1.0f - (d / m_blastRadius);
        const float damage  = m_blastDamage * falloff;

        if (damage >= 1.0f)
        {
            WorldManager::Get()->gameplaySystem()->damageEntity(
                desc.id, static_cast<unsigned int>(damage), DAMAGE_TYPE::DEFAULT,
                DamageContext::fromBlast(epicentre, otherPos));
        }

        if (m_blastForce > 0.0f && other.hasComponent<PhysicsComponent>())
        {
            auto& phys = other.getComponent<PhysicsComponent>();
            if (phys.actor && !phys.kinematic)
            {
                vector3df dir = otherPos - epicentre;
                const float len = dir.getLength();
                if (len > 0.001f) dir /= len;
                else              dir = vector3df(0.0f, 1.0f, 0.0f);

                phys.actor->addForce(
                    physx::PxVec3(dir.X, dir.Y, dir.Z) * (m_blastForce * falloff),
                    physx::PxForceMode::eIMPULSE);
            }
        }
    }

    // --- Camera feedback ------------------------------------------------------
    {
        auto& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
        if (player.isValid() && player.hasComponent<TransformComponent>())
        {
            const float d = (epicentre - player.getComponent<TransformComponent>().getPosition()).getLength();
            const float proximity = std::max(0.0f, 1.0f - d / (m_blastRadius * 2.0f));
            if (proximity > 0.05f)
            {
                g_CameraFX.addShake(proximity * 4.0f, 350.0f);
                g_CameraFX.addFovKick(-3.0f * proximity);
            }
        }
    }

    // --- Self-destruct, last --------------------------------------------------
    // threshold * 2 + 1 guarantees overkillRatio() >= GoreManager::gibRatio (1.0),
    // which routes the kill to TIER_GIB: the body is removed and there is no
    // corpse left to play a death animation on. persist() sees the 'gibbed' flag
    // and stays out of the way.
    if (selfId != _entity_null_value && entity.hasComponent<DamageReceiverComponent>())
    {
        const int threshold = entity.getComponent<DamageReceiverComponent>().threshold;
        WorldManager::Get()->gameplaySystem()->damageEntity(
            selfId, static_cast<unsigned int>(threshold * 2 + 1), DAMAGE_TYPE::DEFAULT,
            DamageContext::fromBlast(epicentre, epicentre));
    }

    m_isDead = true;
}

// ---------------------------------------------------------------------------

void SuicideBomberBehavior::persist(anax::Entity& entity, float dt)
{
    if (m_detonated) return;

    if (!entity.hasComponent<DamageReceiverComponent>()) return;
    auto& drc = entity.getComponent<DamageReceiverComponent>();

    // Caught in a blast — another bomber's, or an explosive weapon. Checked
    // before the death test on purpose: a charge cooks off whether or not the
    // blast that set it off also killed the man carrying it.
    if (drc.didReceiveExplosive())
    {
        armFuse(entity);
        if (m_detonated) return;
    }

    if (m_isDead) return;

    // Died with a lit fuse. The entity is queued for removal and will not get
    // another update(), so the charge has to go off now or never. Checked before
    // handleDeath() because detonating supersedes falling over.
    if (drc.health <= 0 && m_fuseTimer >= 0.0f)
    {
        detonate(entity);
        return;
    }

    if (handleDeath(entity, (rand() % 2) ? "Die1" : "Die2"))
        m_state = State::DEAD;
}

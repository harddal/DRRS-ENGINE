#include "MeleeZombieBehavior.h"

#include "Engine/Navigation/NavigationManager.h"
#include "Engine/Physics/PhysicsManager.h"
#include "Engine/World/WorldManager.h"
#include "Engine/World/Components/TransformComponent.h"
#include "Game/Components/BehaviorComponent.h"
#include "Game/Components/DamageReceiverComponent.h"
#include "Engine/World/Components/MeshComponent.h"
#include "Engine/World/Components/SoundComponent.h"
#include "Engine/World/Components/DescriptorComponent.h"
#include "Utility/Utility.h"

#include <cmath>

using namespace irr::core;

// ---------------------------------------------------------------------------

void MeleeZombieBehavior::playAnim(const std::string& name, MeshComponent& mc)
{
    if (!mc.node) return;
    for (const auto& a : mc.animationList)
    {
        if (a.name != name) continue;
        mc.node->setLoopMode(a.loop);
        mc.node->setFrameLoop(a.frames.X, a.frames.Y);
        mc.node->setAnimationSpeed(static_cast<irr::f32>(mc.fps));
        mc.lastPlayedAnimation = a;
        return;
    }
}

// ---------------------------------------------------------------------------

vector3df MeleeZombieBehavior::calcSeparation(const vector3df& myPos, anax::Entity& self)
{
    vector3df sep(0.0f, 0.0f, 0.0f);
    for (auto& other : WorldManager::Get()->world()->getEntities())
    {
        if (!other.isValid() || other == self)         continue;
        if (!other.hasComponent<BehaviorComponent>())  continue;
        if (!other.hasComponent<TransformComponent>()) continue;

        // Corpses can overlap — only push away from living NPCs
        if (other.hasComponent<DescriptorComponent>() &&
            !other.getComponent<DescriptorComponent>().isAlive)
            continue;

        vector3df diff = myPos - other.getComponent<TransformComponent>().getPosition();
        diff.Y = 0.0f;
        const float d = diff.getLength();
        if (d < m_separationRadius && d > 0.001f)
        {
            diff.normalize();
            sep += diff * ((m_separationRadius - d) / m_separationRadius);
        }
    }
    return sep;
}

// ---------------------------------------------------------------------------

void MeleeZombieBehavior::init(anax::Entity& entity)
{
    if (!entity.hasComponent<MeshComponent>()) return;
    playAnim("idle", entity.getComponent<MeshComponent>());
    m_state = State::IDLE;
}

// ---------------------------------------------------------------------------

void MeleeZombieBehavior::update(anax::Entity& entity, float dt)
{
    if (!entity.hasComponent<TransformComponent>()) return;
    auto& tc = entity.getComponent<TransformComponent>();

    // Gravity — pin to floor every frame regardless of state
    {
        const vector3df p = tc.getPosition();
        auto ray = PhysicsManager::Get()->raycast(
            p + vector3df(0.0f, 1.0f, 0.0f),
            vector3df(0.0f, -1.0f, 0.0f), 2.0f);
        if (ray.hit)
            tc.setPosition({ p.X, ray.data.getAnyHit(0).position.y, p.Z });
    }

    if (m_isDead) return;
    if (!entity.hasComponent<MeshComponent>()) return;

    auto& mc = entity.getComponent<MeshComponent>();

    auto& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
    if (!player.isValid()) return;

    const vector3df myPos     = tc.getPosition();
    const vector3df playerPos = player.getComponent<TransformComponent>().getPosition();

    vector3df delta = playerPos - myPos;
    delta.Y = 0.0f;
    const float dist = delta.getLength();

    // ---- State transitions ----
    if (m_state == State::IDLE)
    {
        if (dist <= m_detectionRange)
        {
            m_state = State::CHASE;
            m_path.clear();
            m_repathTimer = 99999.0f;
            playAnim("move", mc);
        }
        else
        {
            return;
        }
    }

    if (m_state == State::CHASE)
    {
        if (dist <= m_attackRange)
        {
            m_state = State::ATTACK;
            m_attackTimer = m_attackDelay; // allow immediate first bite
            m_path.clear();
        }
        else if (dist > m_chaseRange * 2.0f)
        {
            m_state = State::IDLE;
            m_path.clear();
            playAnim("idle", mc);
            return;
        }
    }

    if (m_state == State::ATTACK)
    {
        if (dist > m_attackRange * 1.25f)
        {
            m_state = State::CHASE;
            m_path.clear();
            m_repathTimer = 99999.0f;
            playAnim("move", mc);
        }
    }

    // ---- CHASE: pathfind and move ----
    if (m_state == State::CHASE)
    {
        m_repathTimer += dt;

        if (m_repathTimer >= 1500.0f || m_path.empty() || m_pathIndex >= static_cast<int>(m_path.size()))
        {
            if (NavigationManager::Get() && NavigationManager::Get()->isNavMeshBuilt())
                m_path = NavigationManager::Get()->findPath(myPos, playerPos);
            else
                m_path = { playerPos };

            m_pathIndex   = 0;
            m_repathTimer = 0.0f;
        }

        // Advance past reached waypoints (XZ only so slope height doesn't stall advancement)
        while (m_pathIndex < static_cast<int>(m_path.size()))
        {
            const vector3df toWp = m_path[m_pathIndex] - myPos;
            if (std::sqrtf(toWp.X * toWp.X + toWp.Z * toWp.Z) <= 0.5f)
                ++m_pathIndex;
            else
                break;
        }

        if (m_pathIndex < static_cast<int>(m_path.size()))
        {
            vector3df toWpXZ = m_path[m_pathIndex] - myPos;
            toWpXZ.Y = 0.0f;
            const float wpDistXZ = toWpXZ.getLength();

            if (wpDistXZ > 0.01f)
            {
                const vector3df dir = toWpXZ / wpDistXZ;
                const vector3df sep = calcSeparation(myPos, entity);
                vector3df move = dir + sep;
                const float moveLen = move.getLength();
                if (moveLen > 0.001f) move /= moveLen;

                vector3df newPos = myPos + move * m_moveSpeed * (dt * 0.001f);

                auto gndRay = PhysicsManager::Get()->raycast(
                    newPos + vector3df(0.0f, 1.0f, 0.0f),
                    vector3df(0.0f, -1.0f, 0.0f), 2.0f);
                newPos.Y = gndRay.hit ? gndRay.data.getAnyHit(0).position.y : myPos.Y;

                tc.setPosition(newPos);

                const vector3df flatMove(move.X, 0.0f, move.Z);
                if (flatMove.getLength() > 0.001f)
                    tc.setRotation(vector3df(0.0f, rad2deg(-atan2f(-flatMove.X, flatMove.Z)), 0.0f));
            }
        }

        return;
    }

    // ---- ATTACK: face player and bite ----
    if (m_state == State::ATTACK)
    {
        if (dist > 0.01f)
        {
            const vector3df dir = delta / dist;
            tc.setRotation(vector3df(0.0f, rad2deg(-atan2f(-dir.X, dir.Z)), 0.0f));
        }

        const vector3df sep = calcSeparation(myPos, entity);
        if (sep.getLength() > 0.001f)
        {
            vector3df newPos = myPos + sep * m_moveSpeed * 0.5f * (dt * 0.001f);
            auto gndRay = PhysicsManager::Get()->raycast(
                newPos + vector3df(0.0f, 1.0f, 0.0f),
                vector3df(0.0f, -1.0f, 0.0f), 2.0f);
            newPos.Y = gndRay.hit ? gndRay.data.getAnyHit(0).position.y : myPos.Y;
            tc.setPosition(newPos);
        }

        m_attackTimer += dt;
        if (m_attackTimer >= m_attackDelay)
        {
            m_attackTimer = 0.0f;

            entityid playerId = WorldManager::Get()->managerSystem()->getIDByName("player");
            if (playerId >= 0)
                WorldManager::Get()->gameplaySystem()->damageEntity(playerId, static_cast<unsigned int>(m_attackDamage));

            playAnim("melee", mc);

            if (entity.hasComponent<SoundComponent>())
                entity.getComponent<SoundComponent>().play("attack");
        }
    }
}

// ---------------------------------------------------------------------------

void MeleeZombieBehavior::persist(anax::Entity& entity, float dt)
{
    if (m_isDead) return;

    if (!entity.hasComponent<DamageReceiverComponent>()) return;
    auto& drc = entity.getComponent<DamageReceiverComponent>();

    // Hit reaction sounds
    if (drc.didReceiveDamage() && entity.hasComponent<SoundComponent>())
    {
        m_dmgToggle = !m_dmgToggle;
        entity.getComponent<SoundComponent>().play(m_dmgToggle ? "damage_1" : "damage_2");
    }

    // Death
    if (drc.health <= 0)
    {
        m_isDead = true;
        m_state  = State::DEAD;
        m_path.clear();

        if (entity.hasComponent<MeshComponent>())
            playAnim("die", entity.getComponent<MeshComponent>());

        if (entity.hasComponent<SoundComponent>())
            entity.getComponent<SoundComponent>().play("die");
    }
}

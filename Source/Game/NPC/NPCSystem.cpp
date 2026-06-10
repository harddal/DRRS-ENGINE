// --- DEPRECATED ---
// Replaced by the BehaviorSystem, each NPC will have its own, instanced state machine.

#include "NPCSystem.h"

#include <spdlog/spdlog.h>

#include "Engine/Engine.h"
#include "Engine/Navigation/NavigationManager.h"
#include "Engine/Physics/PhysicsManager.h"
#include "Engine/Script/ScriptManager.h"
#include "Engine/World/WorldManager.h"
#include "Game/Components.h"
#include "Utility/Utility.h"

void NPCSystem::init()
{
}

void NPCSystem::destroy()
{
}

void NPCSystem::update(irr::f32 dt)
{
    auto& entities = getEntities();

    // Phase 1: build a flat position cache for NPC-NPC separation.
    // Pre-building avoids redundant component fetches in the inner separation loop.
    struct SepEntry { irr::core::vector3df pos; entityid id; };
    std::vector<SepEntry> sepCache;
    sepCache.reserve(entities.size());
    for (auto& e : entities)
    {
        auto& desc = e.getComponent<DescriptorComponent>();
        if (desc.isAlive)
            sepCache.push_back({ e.getComponent<TransformComponent>().getPosition(), desc.id });
    }

    // Phase 2: per-NPC update.
    for (auto& entity : entities)
    {
        auto& desc      = entity.getComponent<DescriptorComponent>();
        auto& npc       = entity.getComponent<NPCComponent>();
        auto& transform = entity.getComponent<TransformComponent>();

        // ---- Reset movement intent ----
        npc.scriptWantsMovement = false;

        // ---- Gravity — runs even on dead NPCs so corpses don't float ----
        {
            auto ray = PhysicsManager::Get()->raycast(
                transform.getPosition() + irr::core::vector3df(0.0f, 0.05f, 0.0f),
                irr::core::vector3df(0.0f, -1.0f, 0.0f), 0.1);
            if (!ray.hit)
                transform.setPosition(transform.getPosition() + irr::core::vector3df(0.0f, -0.025f, 0.0f));
        }

        if (!desc.isAlive)
            continue;

        // ---- Dialog lock: face player and skip AI while in conversation ----
        if (entity.hasComponent<DialogComponent>() && entity.getComponent<DialogComponent>().active)
        {
            auto& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
            if (player.isValid())
            {
                irr::core::vector3df myPos  = transform.getPosition();
                irr::core::vector3df plrPos = player.getComponent<TransformComponent>().getPosition();
                float xdis = plrPos.X - myPos.X;
                float zdis = plrPos.Z - myPos.Z;
                float ydis = plrPos.Y - myPos.Y;
                float xzdis = sqrtf(xdis * xdis + zdis * zdis);
                float yaw = rad2deg(-atan2f(-xdis, zdis));
                transform.setRotation(irr::core::vector3df(0.0f, yaw, 0.0f));
            }
            continue;
        }

        // ---- Script npcUpdate dispatch ----
        if (entity.hasComponent<ScriptComponent>())
        {
            auto& script = entity.getComponent<ScriptComponent>();
            if (script.hasNpcUpdate)
                ScriptManager::Get()->execute(script, script.npcUpdateFunc, desc.id);
        }

        // ---- Movement execution ----
        if (!npc.scriptWantsMovement)
            continue;

        irr::core::vector3df pos    = transform.getPosition();
        irr::core::vector3df target = npc.move_target;

        // Separation: nudge move_target away from nearby NPCs (XZ only).
        // Applied after the script sets move_target so scripts never need to account for it.
        for (const auto& sep : sepCache)
        {
            if (sep.id == desc.id) continue;
            irr::core::vector3df delta = pos - sep.pos;
            delta.Y = 0.0f;
            float dist = delta.getLength();
            if (dist < 2.5f && dist > 0.001f)
            {
                delta.normalize();
                float push = (2.5f - dist) * 2.0f;
                if (push > 1.0f) push = 1.0f; // cap to prevent large spikes
                target += delta * push;
            }
        }

        // Direction toward (possibly separation-nudged) target, XZ plane only.
        irr::core::vector3df dir = target - pos;
        dir.Y = 0.0f;
        float dirLen = dir.getLength();
        if (dirLen < 0.01f)
        {
            npc.scriptWantsMovement = false;
            continue;
        }
        dir.normalize();

        // Look-at: match the pattern from the old GameplaySystem — atan2 on XZ.
        float yaw = rad2deg(-atan2f(-dir.X, dir.Z));
        transform.setRotation(irr::core::vector3df(0.0f, yaw, 0.0f));

        // Speed: NavAgentComponent.moveSpeed is now units/second (3.0 default after entity update).
        float speed = 3.0f;
        if (entity.hasComponent<NavAgentComponent>())
            speed = entity.getComponent<NavAgentComponent>().moveSpeed;
        speed *= npc.moveSpeedScale * dt;

        // Stair traversal: cast forward-then-down from head height to detect step-up opportunities.
        {
            irr::core::vector3df stairRayOrigin = pos + irr::core::vector3df(0.0f, 1.5f, 0.0f) + dir * 0.7f;
            auto stairRay = PhysicsManager::Get()->raycast(stairRayOrigin, irr::core::vector3df(0.0f, -1.0f, 0.0f), 1.6);
            if (stairRay.hit)
            {
                float groundY    = stairRay.data.getAnyHit(0).position.y;
                float currentY   = pos.Y;
                float heightDiff = groundY - currentY;
                if (heightDiff > 0.025f && heightDiff < 0.75f)
                    transform.setPosition(pos + irr::core::vector3df(0.0f, heightDiff * 0.4f, 0.0f));
            }
        }

        // Wall avoidance: forward raycast from waist height; slide along wall normal if blocked.
        {
            auto wallRay = PhysicsManager::Get()->raycast(
                pos + irr::core::vector3df(0.0f, 0.5f, 0.0f), dir, 1.5);
            if (wallRay.hit)
            {
                auto  pn     = wallRay.data.getAnyHit(0).normal;
                irr::core::vector3df wallNorm(pn.x, pn.y, pn.z);
                float dot = dir.dotProduct(wallNorm);
                if (dot < 0.0f)
                    dir -= wallNorm * dot; // slide along wall surface
            }
        }

        // Destination ground check: cancel horizontal movement if the step ahead has no ground.
        {
            irr::core::vector3df destCheck = pos + dir * (speed + 0.1f) + irr::core::vector3df(0.0f, 2.0f, 0.0f);
            auto destRay = PhysicsManager::Get()->raycast(destCheck, irr::core::vector3df(0.0f, -1.0f, 0.0f), 4.0);
            if (!destRay.hit)
            {
                dir.X = 0.0f;
                dir.Z = 0.0f;
            }
        }

        transform.setPosition(transform.getPosition() + dir * speed);
        npc.scriptWantsMovement = false;
    }
}

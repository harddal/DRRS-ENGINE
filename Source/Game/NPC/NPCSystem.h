// --- DEPRECATED ---
// Replaced by the BehaviorSystem, each NPC will have its own, instanced state machine.

#pragma once

#include <anax/anax.hpp>
#include <irrlicht.h>

#include "Engine/World/Components/DescriptorComponent.h"
#include "Engine/World/Components/TransformComponent.h"
#include "Game/Components/NPCComponent.h"

// NPCSystem owns all NPC movement infrastructure (gravity, stair traversal, wall avoidance,
// NPC-NPC separation) and dispatches npcUpdate script callbacks. It has no built-in AI state
// machine — scripts own the state machine entirely via npcUpdate.
class NPCSystem : public anax::System<anax::Requires<DescriptorComponent, NPCComponent, TransformComponent>>
{
public:
    void onEntityAdded(anax::Entity& entity) override {}
    void onEntityRemoved(anax::Entity& entity) override {}

    void init();
    void update(irr::f32 dt);
    void destroy();
};

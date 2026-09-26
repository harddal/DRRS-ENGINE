#pragma once

#include <anax/anax.hpp>
#include <irrlicht.h>

#include "Engine/World/Components/AnimationComponent.h"
#include "Engine/World/Components/MeshComponent.h"

// Evaluates every migrated character's animation and hands the result to its
// scene node.
//
// RUNS ONCE PER RENDERED FRAME, NOT IN THE FIXED-STEP LOOP.
// Engine::update() calls WorldManager::update() inside the substep while-loop,
// so it runs 0, 1 or 2+ times per rendered frame. Sampling a pose there gives
// the same class of artifact as the ImGui strobe
// (project_imgui_fixed_step_flicker): some frames would advance the cursor
// twice and some not at all. WorldManager::updateAnimation() is called from
// Engine::run() immediately before RenderManager::draw() instead.
//
// The split, for anything added later:
//
//   fixed step (update)                  once per frame (updateAnimation)
//   ------------------------------------ ------------------------------------
//   which state / which clip             advance every clip player's cursor
//   target blend weights, transitions    advance weights toward targets
//   grounded / air phase, turn direction sample, blend, layer, IK
//   speed -> desired blend coords        setExternalPose
class AnimationSystem
    : public anax::System<anax::Requires<MeshComponent, AnimationComponent>>
{
public:
    void onEntityRemoved(anax::Entity& entity) override;

    // 'dtMs' is ALREADY TIME-SCALED. A node on the external-pose path no longer
    // rides Irrlicht's virtual timer (reference_irrlicht_virtual_timer), so the
    // scale that ITimer::setSpeed() applies to every unmigrated animation has to
    // be applied to this delta by the caller instead - Engine::run() passes
    // variableDeltaMs * effectiveScale.
    void update(irr::f32 dtMs);
};

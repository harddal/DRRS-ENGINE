#pragma once

#include "anax/Component.hpp"

#include "Engine/Animation/AnimGraph.h"

// Marks an entity as driven by AnimationSystem rather than by
// IAnimatedMeshSceneNode's own frame playback.
//
// DELIBERATELY NOT SERIALIZED. Nothing in a .ent decides this: it is added at
// runtime by whatever code owns the character's animation (PlayerControllerTPS
// today, CharacterBehavior once phase 6 lands), and an entity without it keeps
// the old setFrameLoop path exactly as before. That is what lets migrated and
// unmigrated characters coexist while the port is in progress.
struct AnimationComponent : anax::Component
{
    AnimGraph graph;

    // Set false to hand the node back to setFrameLoop without removing the
    // component - the console bisect switch in phase 2 wants this.
    bool enabled = true;
};

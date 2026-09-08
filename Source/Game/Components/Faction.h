#pragma once

// Faction politics, split out from NPCComponent so weapons and the HUD can ask
// "is this thing hostile to me" without dragging the component into their
// headers.

#include <anax/Entity.hpp>

#include "Engine/World/Components/DescriptorComponent.h"
#include "Game/Components/BehaviorComponent.h"
#include "Game/Components/NPCComponent.h"

// [attacker][target]. Read as "does the row shoot the column".
//
// Deliberately NOT symmetric-by-construction: a one-way grudge is expressible.
// UNDEAD and CULT are mutually hostile -- that is the monster-vs-cult infighting
// the old 3-value disposition enum could not say.
//
// static constexpr, NOT inline constexpr. The project builds stdcpp14 and inline
// variables are a C++17 feature. (Every stdcpp17 in the vcxproj is a PER-FILE
// <ClCompile> override for the fastgltf/simdjson island and three TextEditor
// files, sitting in an <ItemGroup> -- not a project-wide setting.)
//
// static constexpr at namespace scope gives each TU its own copy. For a 25-byte
// bool array that is free, and there is no ODR problem.
static constexpr bool k_hostile[(int)FACTION::FACTION_COUNT][(int)FACTION::FACTION_COUNT] = {
/*              NEUTRAL PLAYER UNDEAD CULT  CIVILIAN */
/* NEUTRAL  */ {  false, false, false, false, false },
/* PLAYER   */ {  false, false, true,  true,  false },
/* UNDEAD   */ {  false, true,  false, true,  true  },
/* CULT     */ {  false, true,  true,  false, true  },
/* CIVILIAN */ {  false, false, false, false, false },
};

// The single place faction is resolved. Whether a given entity carries an
// NPCComponent at all stays an implementation detail of this function.
//
// ET_PLAYER is checked FIRST and wins outright. An entity that is somehow both
// ET_PLAYER and NPC-tagged resolves to PLAYER rather than silently picking one.
//
// A friendly squadmate is NOT ET_PLAYER -- it is an ordinary NPC entity carrying
// NPCComponent{ faction = PLAYER }, and lands on the second branch. That is what
// keeps "is an NPC" and "is on the player's side" separate questions.
inline FACTION factionOf(const anax::Entity& e)
{
    if (!e.isValid()) return FACTION::NEUTRAL;

    if (e.hasComponent<DescriptorComponent>() &&
        e.getComponent<DescriptorComponent>().type == ET_PLAYER)
        return FACTION::PLAYER;

    if (e.hasComponent<NPCComponent>())
    {
        const NPCComponent& npc = e.getComponent<NPCComponent>();
        if (npc.pacified) return FACTION::NEUTRAL;

        // Clamp: a hand-edited or stale .ent can carry an out-of-range value,
        // and k_hostile is indexed directly with no bounds check.
        const int f = (int)npc.faction;
        if (f < 0 || f >= (int)FACTION::FACTION_COUNT) return FACTION::NEUTRAL;
        return npc.faction;
    }

    // No explicit component: ask the behaviour what it is.
    //
    // Without this, "give the NPC an NPCComponent and set its faction" is a
    // setup step that fails SILENTLY -- the NPC resolves NEUTRAL, the NEUTRAL
    // row of the table is all false, and it never acquires a target. It idles
    // and patrols perfectly and simply never reacts to anything. Every NPC saved
    // into a scene before the faction system existed is in exactly that state,
    // and there is no way to spot it short of watching one ignore you.
    //
    // Mirrors resolveImpactSurface() in GameplaySystem.cpp: stored override
    // first, behaviour's own opinion second.
    if (e.hasComponent<BehaviorComponent>())
    {
        const BehaviorComponent& bc = e.getComponent<BehaviorComponent>();
        if (bc.behavior)
            return bc.behavior->defaultFaction();
    }

    // Props, pickups, brushes -- hostile to nothing. The NEUTRAL row and column
    // of the table are all false, so they reject in a single array lookup.
    return FACTION::NEUTRAL;
}

inline bool isHostile(const anax::Entity& a, const anax::Entity& b)
{
    return k_hostile[(int)factionOf(a)][(int)factionOf(b)];
}

// Editor / console display. Index-safe for out-of-range values.
inline const char* factionName(FACTION f)
{
    switch (f)
    {
    case FACTION::NEUTRAL:  return "NEUTRAL";
    case FACTION::PLAYER:   return "PLAYER";
    case FACTION::UNDEAD:   return "UNDEAD";
    case FACTION::CULT:     return "CULT";
    case FACTION::CIVILIAN: return "CIVILIAN";
    default:                return "NULL";
    }
}

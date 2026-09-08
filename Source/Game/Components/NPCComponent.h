#pragma once

#include "anax/Component.hpp"

#include "cereal/cereal.hpp"
#include "cereal/types/string.hpp"
#include "Engine/Types.h"

// Whose side an entity is on.
//
// APPEND-ONLY. Once a .ent carries <faction>2</faction>, this order is a FILE
// FORMAT -- inserting a value in the middle silently re-factions every saved NPC.
// Same trap as AMMO_TYPE. New factions go on the end, before FACTION_COUNT.
enum class FACTION
{
    NEUTRAL,    // attacks nobody, nobody attacks it (props, ambient critters)
    PLAYER,
    UNDEAD,
    CULT,
    CIVILIAN,
    FACTION_COUNT
};

// Identity, not AI.
//
// This used to be the state block for NPCSystem's pipeline (vision/chase/attack
// ranges, an 8-value AI state enum, waypoints, a nav path cache, a script
// movement contract). All of it is gone: every behaviour owns its own state
// machine, and its tunables are BehaviorPropertys on the behaviour itself.
//
// What is left is a TAG WITH DATA ON IT -- "this entity is an NPC, and here is
// whose side it is on". Two weapons already use hasComponent<NPCComponent>() as
// their "is this a valid target" predicate, and that predicate is only
// meaningful while the component stays a tag.
//
// The PLAYER deliberately does NOT carry one -- DescriptorComponent::type ==
// ET_PLAYER is the codebase's answer to "is this the player", and factionOf()
// in Faction.h checks that first. See Faction.h.
struct NPCComponent : anax::Component
{
    std::string displayName;   // HUD / dialog nameplate
    FACTION     faction = FACTION::NEUTRAL;

    // Per-entity override: this one NPC ignores the hostility table and is
    // friendly to all. The "tamed zombie" escape hatch, so the table stays
    // readable. factionOf() collapses it to NEUTRAL, which makes it work in
    // BOTH directions for free -- a pacified zombie neither attacks nor is
    // attacked -- without a second branch at every call site.
    bool pacified = false;

    template <class Archive>
    void serialize(Archive& archive)
    {
        archive(CEREAL_NVP(displayName), CEREAL_NVP(faction), CEREAL_NVP(pacified));
    }

    NPCComponent() {}
};

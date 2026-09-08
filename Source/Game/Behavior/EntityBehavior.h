#pragma once
#include <string>
#include <vector>
#include <anax/Entity.hpp>

#include "Game/Components/ImpactSurface.h"
#include "Game/Components/NPCComponent.h"   // FACTION

enum class BehaviorPropType { INT, FLOAT, BOOL, STRING, VECTOR3 };

struct BehaviorProperty
{
    std::string name;
    BehaviorPropType type;
    void* ptr;

    std::string toString() const;
    void applyFromString(const std::string& s);
};

class EntityBehavior
{
public:
    virtual ~EntityBehavior() = default;
    virtual void init(anax::Entity& entity)              {}
    virtual void update(anax::Entity& entity, float dt)  {}
    virtual void persist(anax::Entity& entity, float dt) {}
    virtual void destroy(anax::Entity& entity)           {}
    virtual void onLogicSignal(anax::Entity& entity)     {}

    virtual std::vector<BehaviorProperty> getProperties() { return {}; }

    // Whose side this behaviour's entity is on when the entity carries no
    // NPCComponent to say so.
    //
    // This exists because "place the component and set its faction" is a setup
    // step that is EASY TO FORGET AND FAILS SILENTLY: an NPC without it resolves
    // NEUTRAL, the NEUTRAL row of the hostility table is all false, and the NPC
    // simply never acquires a target. It idles convincingly and never reacts to
    // anything -- which is exactly what happened to ~43 cultists already saved
    // into weapon_test.pak before the faction system existed.
    //
    // A SuicideBomberBehavior IS a cultist; that is not scene data, it is what
    // the class is. So the class says so, old scenes heal themselves, and an
    // explicit NPCComponent still overrides this when a designer wants one
    // specific NPC on a different side.
    //
    // Same shape and same precedent as bloodType() below, and read the same way:
    // by an outside resolver (factionOf), never by the base class calling into
    // the subclass mid-update.
    virtual FACTION defaultFaction() const { return FACTION::NEUTRAL; }

    // What this entity is made of, for impact FX when a hit neither bleeds nor
    // shatters it. Consulted only when the DamageReceiverComponent is left on
    // IMPACT_AUTO. A living-creature behaviour returns IMPACT_FLESH; a metal
    // turret could return IMPACT_METAL. IMPACT_AUTO (the default) means "no
    // opinion — fall through to texture classification".
    virtual IMPACT_SURFACE bloodType() const { return IMPACT_AUTO; }
};

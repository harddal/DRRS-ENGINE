#pragma once
#include <string>
#include <vector>
#include <anax/Entity.hpp>

#include "Game/Components/ImpactSurface.h"

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

    // What this entity is made of, for impact FX when a hit neither bleeds nor
    // shatters it. Consulted only when the DamageReceiverComponent is left on
    // IMPACT_AUTO. A living-creature behaviour returns IMPACT_FLESH; a metal
    // turret could return IMPACT_METAL. IMPACT_AUTO (the default) means "no
    // opinion — fall through to texture classification".
    virtual IMPACT_SURFACE bloodType() const { return IMPACT_AUTO; }
};

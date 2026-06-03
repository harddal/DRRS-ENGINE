#pragma once
#include <string>
#include <vector>
#include <anax/Entity.hpp>

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

    virtual std::vector<BehaviorProperty> getProperties() { return {}; }
};

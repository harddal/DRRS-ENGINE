#include "Game/Behavior/BehaviorFactory.h"
#include <spdlog/spdlog.h>

BehaviorFactory& BehaviorFactory::Get()
{
    static BehaviorFactory instance;
    return instance;
}

void BehaviorFactory::registerBehavior(const std::string& name, BehaviorCreator creator)
{
    m_registry[name] = std::move(creator);
}

std::unique_ptr<EntityBehavior> BehaviorFactory::create(const std::string& name)
{
    auto it = m_registry.find(name);
    if (it == m_registry.end())
    {
        spdlog::warn("BehaviorFactory: unknown behavior type '{}'", name);
        return nullptr;
    }
    return it->second();
}

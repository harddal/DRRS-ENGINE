#include "Game/Behavior/BehaviorSystem.h"
#include "Game/Behavior/BehaviorFactory.h"
#include <spdlog/spdlog.h>

void BehaviorSystem::onEntityAdded(anax::Entity& entity)
{
    auto& bc = entity.getComponent<BehaviorComponent>();
    bc.behavior = BehaviorFactory::Get().create(bc.behaviorType);
    if (!bc.behavior) return;

    try
    {
        bc.applyPropertiesToBehavior();
        bc.behavior->init(entity);
    }
    catch (const std::exception& e)
    {
        spdlog::error("BehaviorSystem: exception in '{}' init: {}", bc.behaviorType, e.what());
        bc.behavior.reset();
    }
    catch (...)
    {
        spdlog::error("BehaviorSystem: unknown exception in '{}' init", bc.behaviorType);
        bc.behavior.reset();
    }
}

void BehaviorSystem::onEntityRemoved(anax::Entity& entity)
{
    auto& bc = entity.getComponent<BehaviorComponent>();
    if (bc.behavior)
    {
        bc.behavior->destroy(entity);
        bc.behavior.reset();
    }
}

void BehaviorSystem::update(irr::f32 dt)
{
    for (auto& entity : getEntities())
    {
        if (!entity.isValid()) continue;
		
        auto& bc = entity.getComponent<BehaviorComponent>();
        if (bc.behavior)
            bc.behavior->update(const_cast<anax::Entity&>(entity), dt);
    }
}

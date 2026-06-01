#pragma once
#include <memory>
#include <string>
#include <functional>
#include <unordered_map>
#include "Game/Behavior/EntityBehavior.h"

using BehaviorCreator = std::function<std::unique_ptr<EntityBehavior>()>;

class BehaviorFactory
{
public:
    static BehaviorFactory& Get();
    void registerBehavior(const std::string& name, BehaviorCreator creator);
    std::unique_ptr<EntityBehavior> create(const std::string& name);

private:
    std::unordered_map<std::string, BehaviorCreator> m_registry;
};

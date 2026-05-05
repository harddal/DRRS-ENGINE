#include "NavigationSystem.h"

#include <spdlog/spdlog.h>

#include "Engine/World/Components/NavAgentComponent.h"

using namespace anax;
using namespace irr::core;

void NavigationSystem::onEntityAdded(Entity& entity)
{
    auto& agent = entity.getComponent<NavAgentComponent>();
    agent.path.clear();
    agent.pathIndex = 0;
    agent.hasPath   = false;
}

void NavigationSystem::onEntityRemoved(Entity& entity)
{
    entity.getComponent<NavAgentComponent>().path.clear();
}

void NavigationSystem::update(irr::f32 dt)
{
    for (auto& entity : getEntities())
    {
        auto& agent     = entity.getComponent<NavAgentComponent>();
        auto& transform = entity.getComponent<TransformComponent>();

        if (!agent.hasPath || agent.path.empty())
            continue;

        if (agent.pathIndex >= static_cast<int>(agent.path.size()))
        {
            agent.hasPath = false;
            continue;
        }

        const vector3df& target = agent.path[agent.pathIndex];
        vector3df        pos    = transform.getPosition();

        vector3df delta = target - pos;
        delta.Y = 0.0f;  // Horizontal movement only — vertical is physics-owned

        const float dist = delta.getLength();

        if (dist <= agent.arrivalRadius)
        {
            ++agent.pathIndex;

            if (agent.pathIndex >= static_cast<int>(agent.path.size()))
                agent.hasPath = false;

            continue;
        }

        const vector3df dir   = delta / dist;
        const vector3df newPos = pos + dir * agent.moveSpeed * dt;

        transform.setPosition(newPos);

        // Face movement direction (Y-axis rotation only)
        const float yaw = std::atan2f(dir.X, dir.Z) * (180.0f / 3.14159265f);
        vector3df   rot = transform.getRotation();
        rot.Y = yaw;
        transform.setRotation(rot);
    }
}

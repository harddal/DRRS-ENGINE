#pragma once

#include <anax/anax.hpp>
#include <irrlicht.h>

#include "Engine/World/Components.h"
#include "Engine/World/Components/NavAgentComponent.h"

class NavigationSystem
    : public anax::System<anax::Requires<DescriptorComponent, TransformComponent, NavAgentComponent>>
{
public:
    void onEntityAdded(anax::Entity& entity) override;
    void onEntityRemoved(anax::Entity& entity) override;

    void update(irr::f32 dt);
};

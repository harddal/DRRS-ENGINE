#pragma once
#include <anax/anax.hpp>
#include <irrlicht.h>
#include "Engine/World/Components/DescriptorComponent.h"
#include "Game/Components/BehaviorComponent.h"

class BehaviorSystem : public anax::System<anax::Requires<DescriptorComponent, BehaviorComponent>>
{
public:
    void onEntityAdded(anax::Entity& entity) override;
    void onEntityRemoved(anax::Entity& entity) override;
    void update(irr::f32 dt);
};

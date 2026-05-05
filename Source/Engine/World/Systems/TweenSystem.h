#pragma once

#include <anax/anax.hpp>
#include <irrlicht.h>

#include "Engine/World/Components.h"

class TweenSystem
    : public anax::System<anax::Requires<DescriptorComponent, MeshComponent, TweenComponent>>
{
public:
    void onEntityAdded(anax::Entity& entity) override;
    void onEntityRemoved(anax::Entity& entity) override;

    void update(irr::f32 dt);
};

#pragma once

#include <anax/anax.hpp>
#include <irrlicht.h>

#include "Engine/World/Components/DescriptorComponent.h"
#include "Engine/World/Components/TransformComponent.h"
#include "Engine/World/Components/ParticleComponent.h"

class ParticleSystem : public anax::System<anax::Requires<
    DescriptorComponent,
    TransformComponent,
    ParticleComponent>>
{
public:
    void onEntityAdded(anax::Entity& entity) override;
    void onEntityRemoved(anax::Entity& entity) override;

    void update(irr::f32 dt);
};

#pragma once

#include <anax/Component.hpp>

#include <cereal/cereal.hpp>

// Tag component: marks an entity's mesh as belonging to the 3D skybox miniature.
// Nodes carrying this tag are rendered in RenderManager's dedicated sky pass
// (with a scaled sky camera, depth-cleared afterwards) instead of the main
// scene pass. It holds no data — presence alone is the flag.
struct SkyboxComponent : anax::Component
{
    template <class Archive>
    void serialize(Archive& /*archive*/) {}
};

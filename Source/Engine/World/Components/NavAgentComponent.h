#pragma once

#include <vector>

#include <anax/Component.hpp>
#include <irrlicht.h>

#include <cereal/cereal.hpp>

struct NavAgentComponent : anax::Component
{
    float moveSpeed;      // World units per second
    float arrivalRadius;  // Distance at which a waypoint is considered reached

    // Runtime — not serialized
    std::vector<irr::core::vector3df> path;   // Current waypoint list
    int                               pathIndex;  // Next waypoint to move toward
    bool                              hasPath;

    template <class Archive>
    void serialize(Archive& archive) { archive(CEREAL_NVP(moveSpeed), CEREAL_NVP(arrivalRadius)); }

    NavAgentComponent() :
        moveSpeed(0.05f), arrivalRadius(0.25f),
        pathIndex(0), hasPath(false) {}
};

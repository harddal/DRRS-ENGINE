#pragma once

#include <anax/anax.hpp>
#include <irrlicht.h>

struct TweenComponent : anax::Component
{
    // Serialized — configure in .ent file
    int  movingBufferIndex = 1;            // which mesh buffer is the moving part
    irr::core::vector3df startPosition;    // local position when inactive (default: origin)
    irr::core::vector3df startRotation;    // local rotation when inactive (default: none)
    irr::core::vector3df targetPosition;   // local position when active (e.g. button push-in)
    irr::core::vector3df targetRotation;   // local rotation when active (e.g. switch flip)
    float speed = 5.0f;                    // lerp speed (higher = snappier)

    // Runtime — not serialized
    irr::scene::ISceneNode* movingNode = nullptr;
    float t      = 0.0f;
    bool  active = false;

    template <class Archive>
    void serialize(Archive& archive)
    {
        archive(
            CEREAL_NVP(movingBufferIndex),
            CEREAL_NVP(startPosition.X),  CEREAL_NVP(startPosition.Y),  CEREAL_NVP(startPosition.Z),
            CEREAL_NVP(startRotation.X),  CEREAL_NVP(startRotation.Y),  CEREAL_NVP(startRotation.Z),
            CEREAL_NVP(targetPosition.X), CEREAL_NVP(targetPosition.Y), CEREAL_NVP(targetPosition.Z),
            CEREAL_NVP(targetRotation.X), CEREAL_NVP(targetRotation.Y), CEREAL_NVP(targetRotation.Z),
            CEREAL_NVP(speed)
        );
    }
};

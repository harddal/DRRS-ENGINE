#pragma once

#include <string>

#include <anax/Component.hpp>

#include "PxPhysicsAPI.h"

#include <cereal/cereal.hpp>

namespace irr { namespace scene { class ISceneNode; } }

struct CCTComponent : anax::Component
{
    bool active;

	physx::PxController* controller;

	physx::PxVec3 displacement;

    irr::scene::ISceneNode* hitboxNode = nullptr;

    template <class Archive>
    void serialize(Archive& archive) { archive(CEREAL_NVP(active)); }

    CCTComponent() : active(true), controller(nullptr) {};
};

#pragma once

#include "anax/Component.hpp"

#include "irrlicht.h"

#include "cereal/cereal.hpp"

struct CameraComponent : anax::Component
{
	bool hasInit = false;
	bool isWorldCamera = false;  // if true, this camera does not become active on spawn

	// OPT OUT OF THE RIGID FIRST-PERSON MOUNT.
	//
	// By default CameraSystem::update() copies the entity transform straight
	// onto the camera node every frame: position + offset, and rotation
	// verbatim. Camera rotation IS body rotation, which is only harmless
	// because the first-person player has no visible mesh.
	//
	// A third-person controller cannot live with that — pitch reaching the body
	// transform faceplants the character model, and 'offset' is a fixed
	// world-space vector with no boom length, no collision pullback and no
	// rotation of its own. Set this and the controller owns the camera pose
	// outright; everything else in the loop (target/near-target resolution and
	// the lookat vector) still runs off whatever pose the controller wrote.
	bool controllerDriven = false;

    irr::core::vector3df
        offset, target, lookat, neartarget;
	
    irr::scene::ICameraSceneNode* camera;
    irr::scene::ISceneNode* targetNode;
    irr::scene::ISceneNode* nearTargetNode;

	irr::core::vector3df getLookAt() const
	{
		return lookat;
	}
	irr::core::vector3df getLookAtNormalized() const
	{
		auto lan = lookat;
		return lan.normalize();
	}

    template <class Archive>
    void serialize(Archive& archive)
    {
        archive(
                CEREAL_NVP(offset.X), CEREAL_NVP(offset.Y), CEREAL_NVP(offset.Z),
                CEREAL_NVP(target.X), CEREAL_NVP(target.Y), CEREAL_NVP(target.Z), CEREAL_NVP(isWorldCamera));

        // Added later; guard so pre-existing .ent and scene files still load.
        // Append only — see MarkerComponent::serialize for why.
        try { archive(CEREAL_NVP(controllerDriven)); } catch (cereal::Exception&) {}
    }

    CameraComponent() : 
        offset(irr::core::vector3df(0, 0, 0)), target(irr::core::vector3df(0, 0, 100)), 
        lookat(irr::core::vector3df(0, 0, 0)), neartarget(irr::core::vector3df(0, 0, 1)),
        camera(nullptr), targetNode(nullptr), nearTargetNode(nullptr) {}
};

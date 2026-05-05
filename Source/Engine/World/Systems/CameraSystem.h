#pragma once

#include "anax/anax.hpp"
#include "Engine/World/Components.h"
#include "Engine/Types.h"

#define CAMERA_NEAR 0.1f
#define CAMERA_FAR 1000.0f

class CameraSystem
    : public anax::System<anax::Requires<DescriptorComponent, TransformComponent, CameraComponent>>
{
public:
    void update();

	void onEntityAdded(anax::Entity& entity) override;
	void onEntityRemoved(anax::Entity& entity) override;

	// Switch the active Irrlicht camera to a world camera entity, or back to the player camera.
	void switchToCamera(entityid id);
	void switchToPlayerCamera();

	bool hasOverride() const { return m_overrideCameraID != _entity_null_value; }
	entityid overrideCameraID() const { return m_overrideCameraID; }

private:
	entityid m_overrideCameraID = _entity_null_value;
};

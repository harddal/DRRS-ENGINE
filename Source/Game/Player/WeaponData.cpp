#include "WeaponData.h"

#include "Engine/World/WorldManager.h"

// Define the static member for ImpactBurnShaderCallback
float ImpactBurnShaderCallback::currentSpawnTime = 0.0f;

void PlayerWeapon::updateWeaponSway(float dt)
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	
	if (!player.isValid() || !player.hasComponent<CameraComponent>())
		return;
	
	auto& camera = player.getComponent<CameraComponent>();
	irr::core::vector3df currentRotation = camera.camera->getRotation();
	
	// Calculate rotation delta (how much camera moved this frame)
	irr::core::vector3df rotationDelta = currentRotation - m_lastCameraRotation;
	
	// Apply sway offset based on rotation delta
	// Negative values create lag effect (weapon moves opposite to camera initially)
	irr::core::vector3df targetOffset = m_viewPositionOffset;
	float swayX = -rotationDelta.Y * m_swayAmount; // Horizontal camera movement
	float swayY = rotationDelta.X * m_swayAmount;  // Vertical camera movement
	
	// Clamp sway to prevent extreme offsets during fast camera movement
	const float maxSwayOffset = 0.15f; // Maximum offset in any direction
	swayX = std::max(-maxSwayOffset, std::min(swayX, maxSwayOffset));
	swayY = std::max(-maxSwayOffset, std::min(swayY, maxSwayOffset));
	
	targetOffset.X += swayX;
	targetOffset.Y += swayY;
	
	// Smoothly interpolate current position towards target (creates lag effect)
	// Scale smoothing by delta time for framerate independence
	irr::core::vector3df currentPos = m_mesh.node->getPosition();
	float smoothingFactor = m_swaySmoothing * (dt / 1000.0f) * 60.0f; // Normalize to 60 FPS baseline
	smoothingFactor = std::min(smoothingFactor, 1.0f); // Clamp to prevent overshoot
	irr::core::vector3df newPos = currentPos + (targetOffset - currentPos) * smoothingFactor;
	
	m_mesh.node->setPosition(newPos);
	
	// Store current rotation for next frame
	m_lastCameraRotation = currentRotation;
}
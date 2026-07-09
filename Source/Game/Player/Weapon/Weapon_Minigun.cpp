#include "Weapon_Minigun.h"

#include "Engine/Engine.h"
#include "Engine/Renderer/DecalManager.h"
#include "Utility/Utility.h"

#include "../CameraFX.h"

#include <random>

#undef MB_RIGHT

using namespace SPK;
using namespace SPK::IRR;

void Weapon_Minigun::precache()
{
	ParticleManager::Get()->precache("spark", _asset_psys("spark"));
}

void Weapon_Minigun::init()
{
	m_descriptor.name = "Player_Weapon_Minigun";
	m_descriptor.id = _entity_null_value;

	m_viewPositionOffset = irr::core::vector3df(0.1450f, -0.1800f, 0.2650f);
	m_viewRotationOffset = irr::core::vector3df(0.00f, 0.50f, 0.00f);
	m_viewScaleOffset = irr::core::vector3df(0.3, 0.3, 0.3);

	m_mesh.mesh = "content/mesh/player/weapon/minigun/HUD.b3d";

	m_mesh.trimesh = RenderManager::Get()->sceneManager()->getMesh(m_mesh.mesh.c_str());
	if (!m_mesh.trimesh)
	{
		spdlog::warn("In function PlayerWeapon::init() -> RenderManager::Get()->sceneManager()->getMesh() : Mesh does not exist, stand-in mesh loaded");

		m_mesh.trimesh = RenderManager::Get()->sceneManager()->getMesh("content/mesh/primitive/double_tetrahedron.obj");
		m_mesh.node = RenderManager::Get()->sceneManager()->addAnimatedMeshSceneNode(m_mesh.trimesh, nullptr, m_descriptor.id);

		auto* t = RenderManager::Get()->driver()->getTexture("content/texture/color/magenta.png");
		m_mesh.node->setMaterialTexture(0, t);
	}

	m_mesh.node = RenderManager::Get()->sceneManager()->addAnimatedMeshSceneNode(m_mesh.trimesh, nullptr, m_descriptor.id);

	//auto* t = RenderManager::Get()->driver()->getTexture("content/mesh/player/weapon/minigun/body.png");
	//m_mesh.node->setMaterialTexture(0, t);

	// Set nearest-neighbor filtering for pixelated effect
	m_mesh.node->setMaterialFlag(irr::video::EMF_BILINEAR_FILTER, false);
	m_mesh.node->setMaterialFlag(irr::video::EMF_TRILINEAR_FILTER, false);

	//RenderManager::Get()->renderer()->getMaterialSwapper()->swapMaterials(m_mesh.node);

	m_mesh.fps = 20;
	m_mesh.node->setAnimationSpeed(static_cast<irr::f32>(m_mesh.fps));
	m_mesh.node->setLoopMode(false);
	m_mesh.node->setFrameLoop(0, 0);

	// Allow manual bone control
	m_mesh.node->setJointMode(irr::scene::EJUOR_CONTROL);

	m_mesh.animation_call_back = std::make_shared<AnimationCallback>();
	m_mesh.node->setAnimationEndCallback(m_mesh.animation_call_back.get());

	m_mesh.node->setScale(m_viewScaleOffset);

	// Apply the standard PBR shader to every buffer as the baseline (body, GEAR, etc.)
	auto perpixelMat = ShaderMaterialManager::get("phong_perpixel");
	if (perpixelMat != irr::video::EMT_SOLID)
		m_mesh.node->setMaterialType(perpixelMat);

	// Override only the mesh buffers that belong to the BARREL.
	// We detect them by inspecting which buffers have vertices weighted to the
	// "BARREL" joint in the skinned mesh — no hard-coded buffer index required.
	auto barrelHeatMat = ShaderMaterialManager::get("barrel_heat");
	if (barrelHeatMat != irr::video::EMT_SOLID)
	{
		auto* skinnedMesh = (m_mesh.trimesh->getMeshType() == irr::scene::EAMT_SKINNED)
			? static_cast<irr::scene::ISkinnedMesh*>(m_mesh.trimesh)
			: nullptr;
		if (skinnedMesh)
		{
			const auto& joints = skinnedMesh->getAllJoints();
			bool found = false;
			for (irr::u32 j = 0; j < joints.size(); ++j)
			{
				if (joints[j]->Name == "BARREL")
				{
					// Apply the heat material to every buffer weighted to this joint,
					// and collect the Z bounds of those buffers so the shader knows
					// where the rear (Z-) and MUZZLE (Z+) of the BARREL are.
					const auto& meshBuffers = skinnedMesh->getMeshBuffers();
					float barrelZMin = -1e9f;
					float barrelZMax = 1e9f;

					for (irr::u32 w = 0; w < joints[j]->Weights.size(); ++w)
					{
						irr::u32 bufIdx = joints[j]->Weights[w].buffer_id;
						m_mesh.node->getMaterial(bufIdx).MaterialType =
							static_cast<irr::video::E_MATERIAL_TYPE>(barrelHeatMat);

						// Accumulate Z extents from this buffer's bounding box.
						// Z spin-rotation doesn't change Z values, so the bind-pose
						// box is accurate for all spin angles.
						const auto& bbox = meshBuffers[bufIdx]->getBoundingBox();
						barrelZMin = std::min(barrelZMin, bbox.MinEdge.Z);
						barrelZMax = std::max(barrelZMax, bbox.MaxEdge.Z);
					}

					RenderManager::Get()->barrelHeatCallback()->setBarrelZRange(barrelZMin, barrelZMax);
					found = true;
					spdlog::info("Weapon_Minigun: barrel_heat applied — Z range [{:.2f}, {:.2f}]", barrelZMin, barrelZMax);
					break;
				}
			}
			if (!found)
				spdlog::warn("Weapon_Minigun: 'BARREL' joint not found in skinned mesh — barrel_heat not applied");
		}
		else
		{
			// Mesh is not skinned (e.g. stand-in geometry) — fall back to whole-mesh heat.
			spdlog::warn("Weapon_Minigun: mesh is not ISkinnedMesh — barrel_heat applied to all buffers");
			m_mesh.node->setMaterialType(barrelHeatMat);
		}
	}

	for (auto i = 0; i < m_mesh.node->getMaterialCount(); i++)
	{
		m_mesh.node->getMaterial(i).Shininess = 0.f;
		m_mesh.node->getMaterial(i).SpecularColor.setAlpha(0);
	}

	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");

	if (!player.isValid())
	{
		spdlog::error("In function PlayerWeapon::init() -> WorldManager::Get()->managerSystem()->getEntityByName(\"player\") : Entity \'player\' does not exist");

		return;
	}

	if (player.hasComponent<CameraComponent>())
	{
		m_mesh.node->setParent(player.getComponent<CameraComponent>().camera);

		m_mesh.node->setPosition(m_viewPositionOffset);
		m_mesh.node->setRotation(m_viewRotationOffset);
	}
	else
	{
		spdlog::error("In function PlayerWeapon::init() -> player.getComponent<CameraComponent>() : Entity \'player\' does not have specified component");
	}

	RenderManager::Get()->registerViewmodelNode(m_mesh.node);
	m_mesh.node->setVisible(false);

	// Get the spin bone 
	m_spinBone = m_mesh.node->getJointNode("BARREL");
	if (!m_spinBone)
	{
		spdlog::warn("Weapon_Minigun: 'BARREL' bone not found!");
	}
	m_gearBone = m_mesh.node->getJointNode("GEAR");
	if (!m_spinBone)
	{
		spdlog::warn("Weapon_Minigun: 'GEAR' bone not found!");
	}

	m_crosshair = RenderManager::Get()->driver()->getTexture("content/texture/ui/crosshair/crosshair038.png");

	// Character sheet: big violent flash on the spinning MUZZLE bone, heavy brass torrent
	WeaponEffectsDesc fx;
	fx.muzzleJointName = "MUZZLE";
	fx.flashColor      = irr::video::SColor(255, 255, 204, 76);
	fx.flashSize       = 1.2f;
	fx.lightColor      = irr::video::SColorf(1.0f, 0.8f, 0.2f);
	fx.tracerFrequency = 3;
	fx.tracerPoolSize  = 24;
	fx.shellMesh       = "content/mesh/prop/shells/shelllarge.obj";
	fx.shellEjectJoint = "EJECT";
	fx.shellPoolSize   = 240;
	m_effects.init(m_mesh.node, fx);
}

void Weapon_Minigun::destroy()
{
	if (m_spinUpHandle)   { m_spinUpHandle->stop();   m_spinUpHandle   = nullptr; }
	if (m_fireLoopHandle) { m_fireLoopHandle->stop(); m_fireLoopHandle = nullptr; }
	if (m_spinDownHandle) { m_spinDownHandle->stop(); m_spinDownHandle = nullptr; }

	m_effects.destroy();

	RenderManager::Get()->unregisterViewmodelNode(m_mesh.node);
	m_mesh.node->remove();

	WorldManager::Get()->freeEntityID(m_descriptor.id);
}

void Weapon_Minigun::update()
{
	if (!m_mesh.node || !m_mesh.node->isVisible())
		return;

	float currentTime = Engine::Get()->getCurrentTime();
	float dt = Engine::Get()->getDeltaTime();
	m_isFiring = false;  // reset each frame; set true below if fire() is called

	// Check if fire button is held
	bool fireButtonPressed = InputManager::Get()->isMouseButtonPressed(MB_LEFT);
	bool altFireButtonPressed = InputManager::Get()->isMouseButtonPressed(MB_RIGHT);
	
	// Handle BARREL spinning
	if (fireButtonPressed)
	{
		// Ramp up
		m_currentSpinSpeed += m_spinAcceleration * dt;
		
		// Mark as no longer aligned when we start moving
		if (m_currentSpinSpeed > 0.0f)
		{
			m_isAligned = false;
		}
	}
	else
	{
		// Only decelerate if we are moving
		if (m_currentSpinSpeed > 0.0f)
		{
			// Ramp down
			m_currentSpinSpeed -= m_spinDeceleration * dt;

			// If we slowed down below alignment speed, clamp to alignment speed.
			// The actual stop happens when we cross a BARREL boundary.
			if (m_currentSpinSpeed < m_alignmentSpinSpeed)
			{
				m_currentSpinSpeed = m_alignmentSpinSpeed;
			}
		}
	}

	// Clamp speed
	if (m_currentSpinSpeed > m_maxSpinSpeed) m_currentSpinSpeed = m_maxSpinSpeed;
	if (m_currentSpinSpeed < 0.0f) m_currentSpinSpeed = 0.0f;

	// Apply rotation to bone
	if (m_spinBone)
	{
		// Skip rotation updates if BARREL is fully aligned and stopped
		if (m_isAligned && m_currentSpinSpeed == 0.0f)
		{
			// Barrel is locked in place, skip all rotation logic
		}
		else
		{
			// CRITICAL: Update weapon mesh absolute position first to sync with camera
			// This must happen before animateJoints() so bones get correct parent transform
			m_mesh.node->updateAbsolutePosition();
			
			// Force update of all joints to bind pose/current frame (since EJUOR_CONTROL disabled auto-update)
			// This ensures the MUZZLE bone and others follow the weapon correctly
			m_mesh.node->animateJoints();

			float previousRotation = m_accumulatedRotation;
			m_accumulatedRotation += m_currentSpinSpeed * dt;
			
			bool wrapped = false;
			if (m_accumulatedRotation >= 360.0f)
			{
				m_accumulatedRotation -= 360.0f;
				wrapped = true;
			}

			// Use epsilon tolerance for boundary detection to prevent floating-point oscillation
			const float epsilon = 0.01f;
			
			// Check if we crossed a 60 degree boundary
			// Add small tolerance to prevent flickering at exact boundaries
			int prevStep = (int)((previousRotation + epsilon) / m_alignmentSnap);
			int currStep = (int)((m_accumulatedRotation + epsilon) / m_alignmentSnap);

			// Check for crossing. 
			// If wrapped, we crossed 360 (which is a multiple of 60).
			// If not wrapped, we check if integer division changed.
			if (wrapped || prevStep != currStep)
			{
				// Firing Logic: Fire whenever a BARREL passes the firing point (every 60 degrees).
				// Blocked while overheated — BARREL can still spin, just won't fire.
				if (fireButtonPressed && !m_isOverheated)
				{
					m_recoil = 0.01f;
					m_isFiring = true;
					fire();
				}
				// Alignment Logic: If not firing and moving slowly, snap to the nearest BARREL
				else if (m_currentSpinSpeed <= m_alignmentSpinSpeed && !m_isAligned)
				{
					// Play alignment sound
					SoundManager::Get()->sound()->play2D("content/sound/weapon/minigun/align.wav", false);

					m_currentSpinSpeed = 0.0f;
					// Snap to the multiple (use the step calculated with epsilon)
					m_accumulatedRotation = (float)currStep * m_alignmentSnap;
					m_isAligned = true; // Mark as aligned to prevent re-triggering
				}
			}

			m_spinBone->setRotation(irr::core::vector3df(0.0f, 0.0f, -m_accumulatedRotation));
			m_gearBone->setRotation(irr::core::vector3df(0.0f, 0.0f, m_accumulatedRotation));

			// Force update of the spin bone and its children (MUZZLE) immediately
			// so that getAbsolutePosition() returns the correct value for this frame
			m_spinBone->updateAbsolutePosition();
			m_gearBone->updateAbsolutePosition();
		}
	}

	// --- Fire audio state machine (runs after rotation so m_isFiring reflects this frame's fire() calls) ---
	{
		bool buttonHeld = fireButtonPressed;
		bool isSpinning = m_currentSpinSpeed > 0.0f;

		// Once the loop is running keep it going while button held; only start on the first actual bullet.
		// Overheat drops us out of the firing state so the transitions below can handle the audio swap.
		bool isFiring = m_audioWasFiring
			? (fireButtonPressed && isSpinning && !m_isOverheated)
			: m_isFiring;

		// Spin just started — loop spin_up, pre-load fire_loop paused so it unpauses with zero latency
		if (isSpinning && !m_audioWasSpinning)
		{
			m_spinUpHandle   = SoundManager::Get()->sound()->play2D("content/sound/weapon/minigun/spin_up.wav", true, 0, 0.5f);
			m_fireLoopHandle = SoundManager::Get()->sound()->play2D("content/sound/weapon/minigun/fire_loop.wav", true, 0, 1.0f, nullptr, true);
		}

		// Button released while still in spin-up (before first bullet) — swap spin_up for spin_down
		if (!buttonHeld && m_audioButtonWasHeld && m_spinUpHandle)
		{
			m_spinUpHandle->stop();   m_spinUpHandle   = nullptr;
			m_fireLoopHandle->stop(); m_fireLoopHandle = nullptr;
			m_spinDownHandle = SoundManager::Get()->sound()->play2D("content/sound/weapon/minigun/spin_down.wav", true, 0, 0.5f);
		}

		// First bullet fired — stop spin_up/spin_down loops, unpause the already-buffered fire_loop
		if (isFiring && !m_audioWasFiring)
		{
			if (m_spinUpHandle)   { m_spinUpHandle->stop();   m_spinUpHandle   = nullptr; }
			if (m_spinDownHandle) { m_spinDownHandle->stop(); m_spinDownHandle = nullptr; }
			m_fireLoopHandle->setPaused(false);
		}

		// Firing stopped normally (button released, not overheat) — pause fire_loop, spin_down
		if (!isFiring && m_audioWasFiring && !m_isOverheated)
		{
			m_fireLoopHandle->setPaused(true);
			m_spinDownHandle = SoundManager::Get()->sound()->play2D("content/sound/weapon/minigun/spin_down.wav", true);
		}

		// Overheat just engaged while fire loop was running — swap to spin_up
		if (m_isOverheated && !m_audioWasOverheated && m_audioWasFiring)
		{
			if (m_fireLoopHandle) m_fireLoopHandle->setPaused(true);
			m_spinUpHandle = SoundManager::Get()->sound()->play2D("content/sound/weapon/minigun/spin_up.wav", true);
		}

		// Overheat cleared, button still held — swap back to fire_loop
		if (!m_isOverheated && m_audioWasOverheated && buttonHeld && isSpinning)
		{
			if (m_spinUpHandle) { m_spinUpHandle->stop(); m_spinUpHandle = nullptr; }
			if (m_fireLoopHandle) m_fireLoopHandle->setPaused(false);
		}

		// Barrel aligned and fully stopped — stop everything
		if (!isSpinning && m_audioWasSpinning)
		{
			if (m_spinUpHandle)   { m_spinUpHandle->stop();   m_spinUpHandle   = nullptr; }
			if (m_spinDownHandle) { m_spinDownHandle->stop(); m_spinDownHandle = nullptr; }
			if (m_fireLoopHandle) { m_fireLoopHandle->stop(); m_fireLoopHandle = nullptr; }
		}

		m_audioButtonWasHeld  = buttonHeld;
		m_audioWasSpinning    = isSpinning;
		m_audioWasFiring      = isFiring;
		m_audioWasOverheated  = m_isOverheated;
	}

	// --- Barrel heat (ramp only — cooling runs in persist() so it ticks while unequipped) ---
	if (m_isFiring)
		m_barrelHeatLevel = std::min(1.0f, m_barrelHeatLevel + m_heatRampRate * dt);

	// Overheat lockout: engage at white-hot, release at 50%.
	if (!m_isOverheated && m_barrelHeatLevel >= m_overheatCutoff)
		m_isOverheated = true;
	else if (m_isOverheated && m_barrelHeatLevel <= m_overheatResetLevel)
		m_isOverheated = false;

	RenderManager::Get()->barrelHeatCallback()->setHeatLevel(m_barrelHeatLevel);

	RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
}

void Weapon_Minigun::persist()
{
	float dt = Engine::Get()->getDeltaTime();

	// Cool BARREL heat while unequipped (also runs when equipped — update() only ramps up)
	if (!m_isFiring)
	{
		m_barrelHeatLevel = std::max(0.0f, m_barrelHeatLevel - m_heatCoolRate * dt);

		if (!m_isOverheated && m_barrelHeatLevel >= m_overheatCutoff)
			m_isOverheated = true;
		else if (m_isOverheated && m_barrelHeatLevel <= m_overheatResetLevel)
			m_isOverheated = false;
	}

	m_effects.update(dt);
}

void Weapon_Minigun::equip()
{
	m_mesh.node->setVisible(true);

	m_currentSpinSpeed = 0.0f;
	m_accumulatedRotation = 0.0f;
	m_isAligned = true;
}

void Weapon_Minigun::unequip()
{
	m_mesh.node->setVisible(false);

	if (m_spinUpHandle)   { m_spinUpHandle->stop();   m_spinUpHandle   = nullptr; }
	if (m_fireLoopHandle) { m_fireLoopHandle->stop(); m_fireLoopHandle = nullptr; }
	if (m_spinDownHandle) { m_spinDownHandle->stop(); m_spinDownHandle = nullptr; }
	m_audioButtonWasHeld = false;
	m_audioWasSpinning   = false;
	m_audioWasFiring     = false;
}

void Weapon_Minigun::idle()
{

}

void Weapon_Minigun::move()
{

}

void Weapon_Minigun::fire()
{
	// Raycast-based instant hit
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>())
		return;

	auto& camera = player.getComponent<CameraComponent>();

	// Get the Muzzle bone scene node from the weapon
	if (!m_mesh.node)
		return;

	irr::scene::IBoneSceneNode* muzzleBone = m_mesh.node->getJointNode("MUZZLE");
	if (!muzzleBone)
	{
		spdlog::warn("Muzzle bone not found on weapon model - cannot fire");
		return;
	}

	// Force full hierarchy update: camera → weapon → bones
	camera.camera->updateAbsolutePosition();
	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();
	muzzleBone->updateAbsolutePosition();
	irr::core::vector3df muzzlePos = muzzleBone->getAbsolutePosition();

	// Get camera target for aiming direction
	irr::core::vector3df target = camera.camera->getTarget();
	irr::core::vector3df cameraPos = camera.camera->getAbsolutePosition();
	irr::core::vector3df forward = (target - cameraPos).normalize();

	// Calculate right and down vectors relative to camera orientation for spread
	irr::core::vector3df up(0, 1, 0);
	irr::core::vector3df right = forward.crossProduct(up).normalize();
	irr::core::vector3df down = right.crossProduct(forward).normalize();

	// Converge on the crosshair aim point so muzzle-origin shots land on centre
	irr::core::vector3df direction = getAimDirection(muzzlePos);

	// Apply random offset in right and down directions
	float spreadRight = Engine::Get()->rng()->getFloat(-m_recoil, m_recoil);
	float spreadDown  = Engine::Get()->rng()->getFloat(-m_recoil, m_recoil);
	direction = (direction + right * spreadRight + down * spreadDown).normalize();

	// Perform raycast from MUZZLE position in spread direction
	// Cast ray a long distance (1000 units)
	irr::core::vector3df rayEnd = muzzlePos + direction * 1000.0f;

	RaycastResultData raycastResult = RenderManager::Get()->raycastWorldPosition(
		muzzlePos,
		rayEnd,
		true  // Exclude debug nodes
	);

	// Check if we hit something
	if (raycastResult.hit && raycastResult.node)
	{
		auto& hitEntity = WorldManager::Get()->managerSystem()->getEntityByID(raycastResult.node->getID());

		// Check if hit entity is valid and has correct type
		if (hitEntity.isValid() && hitEntity.hasComponent<DescriptorComponent>())
		{
			auto& hitDescriptor = hitEntity.getComponent<DescriptorComponent>();

			// Only register collision with static or dynamic entities
			if (hitDescriptor.type == ET_STATIC || hitDescriptor.type == ET_DYNAMIC)
			{
				// Damage through the gameplay chokepoint; drives hitmarker/kill feedback
				registerHitFeedback(
					WorldManager::Get()->gameplaySystem()->damageEntity(hitDescriptor.id, 25));

				// Sparks fanned off the surface + bullet-hole decal
				m_effects.impact(raycastResult.point, raycastResult.normal);
			}
		}
	}

	// Tracer segment toward the hit point (module fires every Nth shot)
	irr::core::vector3df tracerEnd = (raycastResult.hit && raycastResult.node) ?
		raycastResult.point : (muzzlePos + direction * 1000.0f);
	m_effects.spawnTracer(muzzlePos, tracerEnd);

	m_effects.muzzleFlash();
	m_effects.ejectShell();

	// Camera recoil kick — random yaw drift for a natural, unsteady feel.
	// Per-shot kick stays light; sustained-fire violence comes from the shake
	// ramp below, which grows with barrel heat.
	auto recoilYaw = Engine::Get()->rng()->getFloat(-0.1f, 0.1f);
	g_CameraFX.addRecoil(-0.5f, recoilYaw);
	g_CameraFX.addShake(0.15f + m_barrelHeatLevel * 0.5f, 100.0f);
	addViewKick(
		irr::core::vector3df(0.0f, 0.0f, -0.008f),
		irr::core::vector3df(0.4f,
			Engine::Get()->rng()->getFloat(-0.15f, 0.15f),
			Engine::Get()->rng()->getFloat(-0.3f, 0.3f)));
}

void Weapon_Minigun::reload()
{

}



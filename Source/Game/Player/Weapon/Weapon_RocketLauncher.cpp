#include "Weapon_RocketLauncher.h"

#include "Engine/Engine.h"

#include "Game/Components/NPCComponent.h"
#include "../CameraFX.h"

#undef MB_RIGHT

using namespace irr;
using namespace SPK;
using namespace SPK::IRR;

void Weapon_RocketLauncher::precache()
{

}

void Weapon_RocketLauncher::init()
{
	m_descriptor.name = "Player_Weapon_RocketLauncher";
	m_descriptor.id = _entity_null_value;

	m_viewPositionOffset = irr::core::vector3df(0.0f, 0.0f, -0.1f);
	m_viewRotationOffset = irr::core::vector3df(0.0f, 0.0f, 0.0f);
	m_viewScaleOffset    = irr::core::vector3df(1.0f, 1.0f, 1.0f);

	m_mesh.mesh = "content/mesh/player/weapon/launcher/hud.b3d";

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

	m_mesh.node->setMaterialFlag(irr::video::EMF_BILINEAR_FILTER, true);
	m_mesh.node->setMaterialFlag(irr::video::EMF_TRILINEAR_FILTER, true);
	m_mesh.node->setMaterialFlag(irr::video::EMF_ANISOTROPIC_FILTER, true);
	m_mesh.node->setMaterialFlag(irr::video::EMF_ANTI_ALIASING, true);
	m_mesh.node->setMaterialFlag(irr::video::EMF_USE_MIP_MAPS, true);

	m_mesh.fps = 20;
	m_mesh.node->setAnimationSpeed(static_cast<irr::f32>(m_mesh.fps));
	m_mesh.node->setLoopMode(true);
	m_mesh.node->setFrameLoop(18, 58);

	m_mesh.animation_call_back = std::make_shared<AnimationCallback>();
	m_mesh.node->setAnimationEndCallback(m_mesh.animation_call_back.get());

	m_mesh.node->setScale(m_viewScaleOffset);

	auto perpixelMat = ShaderMaterialManager::get("phong_perpixel");
	if (perpixelMat != irr::video::EMT_SOLID) // only assign if shader compiled successfully
	{
		m_mesh.node->setMaterialType(perpixelMat);
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

	m_crosshair    = RenderManager::Get()->driver()->getTexture("content/texture/ui/crosshair/2x/crosshair131.png");
	m_trackIcon    = RenderManager::Get()->driver()->getTexture("content/texture/ui/crosshair/rocket_launcher/trackpip.png");
	m_lockwaitIcon = RenderManager::Get()->driver()->getTexture("content/texture/ui/crosshair/rocket_launcher/lockwaitpip.png");
	m_lockIcon     = RenderManager::Get()->driver()->getTexture("content/texture/ui/crosshair/rocket_launcher/lockpip.png");

	initExplosionParticleSystem();
}

void Weapon_RocketLauncher::destroy()
{
	// Clean up all projectiles and their trail particle systems
	for (auto& proj : m_projectiles)
	{
		// Clean up trail particle system
		if (proj.trailParticles)
		{
			proj.trailParticles->remove();
			proj.trailParticles = nullptr;
		}

		// Stop and release the looping 3D flight sound
		if (proj.flyingSound)
		{
			proj.flyingSound->stop();
			proj.flyingSound->drop();
			proj.flyingSound = nullptr;
		}

		// Mark entity for cleanup
		if (proj.entity.isValid())
		{
			if (proj.entity.hasComponent<DescriptorComponent>())
			{
				proj.entity.getComponent<DescriptorComponent>().isAlive = false;
			}
		}
	}
	m_projectiles.clear();

	RenderManager::Get()->unregisterViewmodelNode(m_mesh.node);
	m_mesh.node->remove();

	WorldManager::Get()->freeEntityID(m_descriptor.id);
}

void Weapon_RocketLauncher::update()
{
	if (!m_mesh.node || !m_mesh.node->isVisible())
		return;

	float currentTime = Engine::Get()->getCurrentTime();
	float dt = Engine::Get()->getDeltaTime();

	bool fireButtonPressed = InputManager::Get()->isMouseButtonPressed(MB_LEFT);
	bool altFireButtonPressed = InputManager::Get()->isMouseButtonPressed(MB_RIGHT);

	static bool playError = true;
	if (fireButtonPressed)
	{
		if (currentTime - m_lastFireTime >= m_fireRate)
		{
			if (m_currentPrimaryTarget < _entity_null_value)
			{
				spawnProjectile(true);
				m_lastFireTime = currentTime;
			}
			else if (playError)
			{
				SoundManager::Get()->sound()->play2D("content/sound/weapon/rocket_launcher/error.wav");
				playError = false;
			}
		}
	}
	else
	{
		playError = true;
	}

	if (altFireButtonPressed)
	{
		if (currentTime - m_lastFireTime >= m_fireRate)
		{ 
			spawnProjectile(false);
			m_lastFireTime = currentTime;
		}
	}
	
	RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair2x_center_position);
	renderNPCLockIndicators(WorldManager::Get()->managerSystem()->getEntityByName("player").getComponent<CameraComponent>().camera);
}

void Weapon_RocketLauncher::persist()
{
	float dt = Engine::Get()->getDeltaTime();

	// Update all active projectiles
	updateProjectiles(dt);

	// Update muzzle flash effect
	updateMuzzleFlash(dt);

	// Update explosion particles
	std::list<System*>::iterator it = m_particleSystems.begin();
	while (it != m_particleSystems.end())
	{
		// Updates the particle systems (dt is in milliseconds, so divide by 1000)
		if (!(*it)->update(dt / m_particleExplosionUpdateRate))
		{
			// If a system is sleeping, destroys it
			destroyParticleSystem(*it);

			// And erases its entry in the container
			it = m_particleSystems.erase(it);
		}
		else
			++it;
	}
}

void Weapon_RocketLauncher::equip()
{
	m_mesh.node->setVisible(true);
}

void Weapon_RocketLauncher::unequip()
{
	m_mesh.node->setVisible(false);
}

void Weapon_RocketLauncher::idle()
{

}

void Weapon_RocketLauncher::move()
{

}

void Weapon_RocketLauncher::fire()
{

}

void Weapon_RocketLauncher::reload()
{
	
}

void Weapon_RocketLauncher::renderNPCLockIndicators(irr::scene::ICameraSceneNode* cam)
{
	if (!m_lockIcon || !m_lockwaitIcon || !cam) return;

	const int screenW = RenderManager::Get()->getConfiguration().width;
	const int screenH = RenderManager::Get()->getConfiguration().height;
	const irr::core::vector2di screenCentre(screenW / 2, screenH / 2);

	float dt = Engine::Get()->getDeltaTime();
	std::vector<entityid> visibleEntitiesThisFrame;

	struct TargetData {
		entityid id;
		irr::core::vector2d<irr::s32> screenPos;
		float lockTime;
	};
	std::vector<TargetData> renderTargets;

	// Crosshair circle radius in pixels
	const float crosshairRadius = _weapon_crosshair2x_size / 2.0f;

	// Icon size (e.g. 32x32)
	const int iconW = m_lockIcon->getSize().Width;
	const int iconH = m_lockIcon->getSize().Height;

	// Camera forward direction (to cull behind-camera NPCs)
	irr::core::vector3df camForward = cam->getTarget() - cam->getAbsolutePosition();
	camForward.normalize();

	auto& entities = WorldManager::Get()->world()->getEntities();
	for (auto& entity : entities)
	{
		if (!entity.isValid()) continue;
		if (!entity.hasComponent<NPCComponent>()) continue;
		if (!entity.hasComponent<MeshComponent>())  continue;

		auto& npc = entity.getComponent<NPCComponent>();
		auto& desc = entity.getComponent<DescriptorComponent>();

		if (!desc.isAlive) continue;

		auto& mesh = entity.getComponent<MeshComponent>();

		irr::core::vector3df worldPos = entity.getComponent<TransformComponent>().getPosition() + m_targetLockIndicatorOffset;

		// Cull if NPC is behind camera
		irr::core::vector3df toNPC = worldPos - cam->getAbsolutePosition();
		if (camForward.dotProduct(toNPC) <= 0.0f) continue;

		// Raycast to check for line of sight
		RaycastResultData hit = RenderManager::Get()->raycastWorldPosition(cam->getAbsolutePosition(), worldPos, true);
		if (hit.hit && hit.node)
		{
			// If we hit geometry that does NOT belong to this entity, it's occluded!
			if (hit.node->getID() != desc.id)
			{
				continue;
			}
		}

		// Project to 2D screen coordinates
		irr::core::vector2d<irr::s32> screenPos = 
			RenderManager::Get()->sceneManager()->getSceneCollisionManager()->getScreenCoordinatesFrom3DPosition(worldPos, cam);

		// Check if within crosshair circle
		float dx = (float)(screenPos.X - screenCentre.X);
		float dy = (float)(screenPos.Y - screenCentre.Y);
		if (sqrtf(dx * dx + dy * dy) > crosshairRadius) continue;

		visibleEntitiesThisFrame.push_back(desc.id);

		// Find or add to tracking times
		float lockTime = 0.0f;
		bool found = false;
		for (auto& pair : m_targetLockTimes)
		{
			if (pair.first == desc.id)
			{
				pair.second += dt;
				lockTime = pair.second;
				found = true;
				break;
			}
		}

		if (!found)
		{
			m_targetLockTimes.push_back(std::pair<entityid, float>(desc.id, dt));
			lockTime = dt;
		}

		renderTargets.push_back({desc.id, screenPos, lockTime});
	}

	// Clean up targets that are no longer in the crosshair view
	for (auto it = m_targetLockTimes.begin(); it != m_targetLockTimes.end();)
	{
		bool stillVisible = false;
		for (const auto& visibleEntityID : visibleEntitiesThisFrame)
		{
			if (it->first == visibleEntityID)
			{
				stillVisible = true;
				break;
			}
		}

		if (!stillVisible)
		{
			it = m_targetLockTimes.erase(it);
		}
		else
		{
			++it;
		}
	}

	// Determine primary target (the one with the longest lock time >= 2000.0f)
	float maxLockTime = 0.0f;
	entityid primaryTarget = 0;
	bool hasPrimary = false;

	for (const auto& target : renderTargets)
	{
		if (target.lockTime >= 2000.0f && target.lockTime > maxLockTime)
		{
			maxLockTime = target.lockTime;
			primaryTarget = target.id;
			hasPrimary = true;
		}
	}

	if (hasPrimary && primaryTarget != m_currentPrimaryTarget)
	{
		SoundManager::Get()->sound()->play2D("content/sound/weapon/rocket_launcher/beep.wav", false);
	}

	m_currentPrimaryTarget = hasPrimary ? primaryTarget : _entity_null_value;

	// Render tracking icons
	for (const auto& target : renderTargets)
	{
		if (target.lockTime >= 2000.0f)
		{
			if (hasPrimary && target.id == primaryTarget)
			{
				RenderManager::Get()->renderImage2D(
					m_lockIcon,
					irr::core::vector2di(target.screenPos.X - iconW / 2, target.screenPos.Y - iconH / 2),
					irr::video::SColor(255, 255, 80, 80) // Red tint
				);
			}
			else
			{
				RenderManager::Get()->renderImage2D(
					m_lockwaitIcon,
					irr::core::vector2di(target.screenPos.X - iconW / 2, target.screenPos.Y - iconH / 2),
					irr::video::SColor(255, 255, 255, 80) // Yellow tint
				);
			}
		}
		else
		{
			RenderManager::Get()->renderImage2D(
				m_trackIcon,
				irr::core::vector2di(target.screenPos.X - iconW / 2, target.screenPos.Y - iconH / 2),
				irr::video::SColor(255, 80, 255, 80) // Green tint
			);
		}
	}
}

void Weapon_RocketLauncher::createMuzzleFlash()
{
	if (!m_mesh.node)
		return;

	// CRITICAL: Force immediate update of weapon hierarchy to prevent lag
	m_mesh.node->updateAbsolutePosition();

	// Get the Muzzle bone scene node from the weapon
	irr::scene::IBoneSceneNode* muzzleBone = m_mesh.node->getJointNode("firespot");
	if (!muzzleBone)
	{
		spdlog::warn("Muzzle bone not found on weapon model");
		return;
	}

	// Force update muzzle bone to get current frame position
	muzzleBone->updateAbsolutePosition();

	// Position offset from bone (adjust this to move flash forward/back/up/down)
	irr::core::vector3df flashOffset(0.0f, 0.0f, 0.0f);

	if (!m_muzzleStarNode)
	{
		m_muzzleStarNode = RenderManager::Get()->sceneManager()->addBillboardSceneNode(
			muzzleBone,
			irr::core::dimension2df(1.2f, 1.2f),
			flashOffset
		);

		m_muzzleStarNode->setMaterialFlag(irr::video::EMF_LIGHTING, false);
		m_muzzleStarNode->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE, false);
		m_muzzleStarNode->setMaterialFlag(irr::video::EMF_BLEND_OPERATION, true);
		m_muzzleStarNode->setMaterialType(m_muzzleFlashMaterialType);
	}

	std::string starPath = "content/texture/particle/star_05.png";
	auto* starTex = RenderManager::Get()->driver()->getTexture(starPath.c_str());
	m_muzzleStarNode->setMaterialTexture(0, starTex);
	m_muzzleStarNode->setVisible(true);

	// Force immediate position update to prevent lag during fast camera movement
	m_muzzleStarNode->updateAbsolutePosition();

	m_muzzleStarNode->getMaterial(0).AmbientColor = irr::video::SColor(255, 100, 150, 255);
	m_muzzleStarNode->getMaterial(0).DiffuseColor = irr::video::SColor(255, 100, 150, 255);

	m_muzzleFlashTime = 0.0f;
}

void Weapon_RocketLauncher::updateMuzzleFlash(float dt)
{
	if (!m_muzzleStarNode)
		return;

	m_muzzleFlashTime += dt;

	if (m_muzzleFlashTime >= m_muzzleFlashDuration)
	{
		if (m_muzzleStarNode)
			m_muzzleStarNode->setVisible(false);
	}
	else
	{
		float fadeProgress = m_muzzleFlashTime / m_muzzleFlashDuration;
		irr::u32 alpha = (irr::u32)((1.0f - fadeProgress) * 255.0f);

		if (m_muzzleStarNode)
		{
			m_muzzleStarNode->getMaterial(0).AmbientColor.setAlpha(alpha);
			m_muzzleStarNode->getMaterial(0).DiffuseColor.setAlpha(alpha);
		}
	}
}

void Weapon_RocketLauncher::spawnProjectile(bool useTracking)
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>())
		return;

	auto& camera = player.getComponent<CameraComponent>();

	// Get the Muzzle bone scene node from the weapon
	if (!m_mesh.node)
		return;

	irr::scene::IBoneSceneNode* muzzleBone = m_mesh.node->getJointNode("firespot");
	if (!muzzleBone)
	{
		spdlog::warn("Muzzle bone not found on weapon model - cannot spawn projectile");
		return;
	}

	// Force full hierarchy update: camera → weapon → bones
	camera.camera->updateAbsolutePosition();
	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();
	muzzleBone->updateAbsolutePosition();
	irr::core::vector3df spawnPos = muzzleBone->getAbsolutePosition();

	// Get camera target for aiming direction
	irr::core::vector3df target = camera.camera->getTarget();
	irr::core::vector3df cameraPos = camera.camera->getAbsolutePosition();
	irr::core::vector3df forward = (target - cameraPos).normalize();

	// Calculate right and down vectors relative to camera orientation for spread
	irr::core::vector3df up(0, 1, 0);
	irr::core::vector3df right = forward.crossProduct(up).normalize();
	irr::core::vector3df down = right.crossProduct(forward).normalize();

	// Calculate direction from muzzle position to crosshair (screen center)
	irr::core::vector3df direction = (target - spawnPos).normalize();

	auto spreadDist = Engine::Get()->rng()->getFloat(-m_recoil, m_recoil);

	// Apply random offset in right and down directions
	float spreadRight = spreadDist;
	float spreadDown = spreadDist;
	direction = (direction + right * spreadRight + down * spreadDown).normalize();

	// Push spawn position forward so the missile clears the player and nearby geometry
	// (prevents self-collision when the player's back is against a wall)
	spawnPos += direction * m_spawnOffset;

	// Create entity for the missile projectile
	anax::Entity projectileEntity = WorldManager::Get()->managerSystem()->getWorld().createEntity();

	// Add Descriptor Component
	projectileEntity.addComponent<DescriptorComponent>();
	auto& descriptor = projectileEntity.getComponent<DescriptorComponent>();
	descriptor.id = WorldManager::Get()->getNewID();
	descriptor.name = "rocket_projectile_" + std::to_string(descriptor.id);
	descriptor.type = ET_DYNAMIC;
	descriptor.isSerializable = false;

	// Add Transform Component
	projectileEntity.addComponent<TransformComponent>();
	auto& transform = projectileEntity.getComponent<TransformComponent>();
	transform.position = spawnPos;  // Set position directly (node doesn't exist yet)
	transform.initialPosition = spawnPos;

	irr::core::vector3df initialRotation = direction.getHorizontalAngle();
	transform.rotation = initialRotation;
	transform.initialRotation = initialRotation;

	// Add Render Component
	projectileEntity.addComponent<RenderComponent>();
	projectileEntity.getComponent<RenderComponent>().isVisible = true;

	// Add Mesh Component (sphere primitive)
	projectileEntity.addComponent<MeshComponent>();
	auto& mesh = projectileEntity.getComponent<MeshComponent>();
	mesh.mesh = "content/mesh/prop/missile.obj";
	mesh.textures.emplace_back<std::string>("content/mesh/prop/missile.png");
	mesh.isPrimitive = false;
	mesh.isVisible = true;
	mesh.castShadows = false;
	mesh.receiveShadows = false;

	// Add Light Component to make the plasma ball glow
	projectileEntity.addComponent<LightComponent>();
	auto& light = projectileEntity.getComponent<LightComponent>();
	light.type = LT_POINT;
	light.visible = true;
	light.radius = 3.0f; // Light affects 2 units around the projectile
	light.color_diffuse = irr::video::SColorf(0.7f, 0.7f, 0.5f);
	light.offset = irr::core::vector3df(0.0f, 0.0f, 0.0f); // No offset from projectile center

	// Activate the entity
	projectileEntity.activate();
	// Entity will be processed by systems on the next frame


	// Create projectile data
	WeaponProjectile proj;
	proj.speed = m_projectileSpeed;
	proj.useTracking = useTracking;
	proj.targetId = _entity_null_value;
	if (useTracking) {
		proj.targetId = m_currentPrimaryTarget;
	}
	proj.distanceTraveled = 0.0f;
	proj.isTrackingActive = false;
	
	proj.entity = projectileEntity;  // Store entity handle
	proj.velocity = direction * proj.speed; // Projectile speed (units per second)
	proj.previousPosition = spawnPos;  // Initialize to spawn position
	proj.trailParticles = nullptr; // Will be created once transform node is ready
	proj.maxLifetime = 10000.0f;

	// Play looping 3D flight sound at spawn position; update position each frame
	proj.flyingSound = SoundManager::Get()->sound()->play3D(
		"content/sound/effect/rocket_flying.wav",
		spawnPos,
		true,   // loop
		false,  // start paused
		true    // track (keep ISound* to control later)
	);

	m_projectiles.emplace_back(proj);

	// Play fire sound
	SoundManager::Get()->sound()->play2D("content/sound/weapon/rocket_launcher/fire.wav", false);

	// Create muzzle flash effect
	createMuzzleFlash();

	// Camera recoil kick — random yaw drift for a natural, unsteady feel
	//auto recoilYaw = Engine::Get()->rng()->getFloat(-0.3f, 0.3f);
	//g_CameraFX.addRecoil(-0.5f, recoilYaw);
}

void Weapon_RocketLauncher::updateProjectiles(float dt)
{
	// Update and remove expired projectiles
	for (auto it = m_projectiles.begin(); it != m_projectiles.end();)
	{
		// Check if entity is valid and has components
		if (!it->entity.isValid() || !it->entity.hasComponent<TransformComponent>())
		{
			++it;  // Skip this frame - entity might not be processed yet
			continue;
		}

		// Get current and next position from transform
		auto& transformComp = it->entity.getComponent<TransformComponent>();

		// Skip if transform node hasn't been created yet (waiting for TransformSystem to process)
		if (!transformComp.node)
		{
			++it;
			continue;
		}

		// Create and attach particle trail once transform node is ready
		if (!it->trailParticles && transformComp.node)
		{
			// Create particle system parented directly to transform node
			auto* particleSystem = RenderManager::Get()->sceneManager()->addParticleSystemSceneNode(false, transformComp.node);

			// Create point emitter for trail particles
			auto* emitter = particleSystem->createPointEmitter(
				irr::core::vector3df(0, 0, 0),           // Direction - particles stay at spawn point
				35,                                       // Min particles per second (increased from 10)
				50,                                       // Max particles per second (increased from 20)
				irr::video::SColor(255, 250, 250, 250),  // Min start color 
				irr::video::SColor(255, 150, 150, 150),  // Max start color 
				400,                                      // Min lifetime (ms)
				800,                                      // Max lifetime (ms)
				1,                                        // Max angle degrees (no spread)
				irr::core::dimension2df(0.15f, 0.15f),   // Min size (increased from 0.08)
				irr::core::dimension2df(0.2f, 0.2f)    // Max size (increased from 0.12)
			);

			particleSystem->setEmitter(emitter);
			emitter->drop();

			// Add fade-out affector for smooth trail dissipation
			auto* fadeAffector = particleSystem->createFadeOutParticleAffector();
			particleSystem->addAffector(fadeAffector);
			fadeAffector->drop();

			// Configure particle material with custom shader that reads depth
			particleSystem->setMaterialFlag(irr::video::EMF_LIGHTING, false);
			particleSystem->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE, false); // Disable depth WRITE for blending
			particleSystem->setMaterialFlag(irr::video::EMF_BLEND_OPERATION, true); // Enable blend operations
			particleSystem->setMaterialType(m_particleTrailMaterialType); // Custom shader with depth testing

			// Use a simple white texture for particle glow
			auto* particleTexture = RenderManager::Get()->driver()->getTexture("content/texture/particle/smoke_04.png");
			if (!particleTexture)
			{
				// Fallback to a fireball texture or any circular texture if particle.png doesn't exist
				particleTexture = RenderManager::Get()->driver()->getTexture("content/texture/color/magenta.png");
			}
			if (particleTexture)
			{
				particleSystem->setMaterialTexture(0, particleTexture);
			}

			// Bind depth MRT texture for manual depth testing in shader
			//auto* depthMRT = RenderManager::Get()->renderer()->getMRT(2);
			//particleSystem->setMaterialTexture(1, depthMRT);

			// Center particle system on projectile
			particleSystem->setPosition(irr::core::vector3df(0, 0, 0));

			it->trailParticles = particleSystem;
		}

		irr::core::vector3df currentPos = transformComp.getPosition();

		// Use swept raycast FROM PREVIOUS POSITION to current position
		// This catches collisions during the TransformSystem's update (which already moved the projectile)
		const float sphereRadius = 0.15f; // Projectile collision radius

		// Raycast from where projectile WAS (previous frame) to where it IS (current frame)
		irr::core::vector3df rayStart = it->previousPosition;
		irr::core::vector3df rayEnd = currentPos;

		// Extend ray slightly in both directions for safety margin
		irr::core::vector3df rayDirection = (rayEnd - rayStart);
		float rayLength = rayDirection.getLength();
		if (rayLength > 0.001f) {
			rayDirection.normalize();
			rayStart = rayStart - rayDirection * sphereRadius;
			rayEnd = rayEnd + rayDirection * sphereRadius;
		}

		// Perform raycast along movement path (with extended range)
		RaycastResultData raycastResult = RenderManager::Get()->raycastWorldPosition(
			rayStart,
			rayEnd,
			true  // Exclude debug nodes
		);

		bool hitSomething = false;
		irr::scene::ISceneNode* hitNode = nullptr;
		irr::core::vector3df hitPoint = currentPos;  // Default to current position
		irr::core::vector3df hitNormal(0, 1, 0); // Default upward normal

		// Check if raycast hit something
		if (raycastResult.hit && raycastResult.node)
		{

			// Calculate distance from previous position to hit point
			float hitDistance = (raycastResult.point - it->previousPosition).getLength();
			float movementDistance = rayLength;  // Distance projectile moved this frame

			// Hit is valid if within movement distance + sphere radius tolerance
			if (hitDistance <= movementDistance + sphereRadius)
			{
				// Filter by entity type (only collide with static/dynamic entities)
				auto& hitEntity = WorldManager::Get()->managerSystem()->getEntityByID(raycastResult.node->getID());

				// Check if hit entity is valid and has correct type
				if (hitEntity.isValid())
				{
					if (hitEntity.hasComponent<DescriptorComponent>())
					{
						auto& hitDescriptor = hitEntity.getComponent<DescriptorComponent>();

						// Only register collision with static or dynamic entities
						if (hitDescriptor.type == ET_STATIC || hitDescriptor.type == ET_DYNAMIC)
						{
							// Don't collide with self
							if (it->entity.isValid() && it->entity.hasComponent<DescriptorComponent>() &&
								hitDescriptor.id != it->entity.getComponent<DescriptorComponent>().id)
							{
								hitSomething = true;
								hitNode = raycastResult.node;
								hitPoint = raycastResult.point;
								hitNormal = raycastResult.normal;
							}
						}
					}
				}
			}
		}

		bool shouldRemove = false;
		bool hitSomethingReal = false;

		// Check if we hit something
		if (hitSomething && hitNode)
		{
			entityid hitEntityID = hitNode->getID();

			// Ignore if we hit ourselves
			if (it->entity.isValid() && it->entity.hasComponent<DescriptorComponent>() &&
				hitEntityID == it->entity.getComponent<DescriptorComponent>().id)
			{
				// Hitting our own mesh, not a real collision

			}
			else
			{
				// Hit something other than ourselves
				hitSomethingReal = true;

				SoundManager::Get()->sound()->play3D("content/sound/effect/explosion2.wav", hitPoint);

				// Always spawn the explosion and apply splash damage at the impact point,
				// regardless of whether the hit surface is a tracked entity or static geometry
				createParticleSystem(SPK::IRR::irr2spk(hitPoint));
				applySplashDamage(hitPoint, hitEntityID);

				// Screen shake — scaled by proximity to the blast
				{
					anax::Entity& playerEnt = WorldManager::Get()->managerSystem()->getEntityByName("player");
					if (playerEnt.isValid() && playerEnt.hasComponent<TransformComponent>())
					{
						const float maxShakeDist = 5.0f; // units — beyond this, no shake felt
						const float peakShake    = 4.5f;  // degrees at point-blank
						const float shakeDurMs   = 350.0f;

						irr::core::vector3df playerPos = playerEnt.getComponent<TransformComponent>().getPosition();
						float dist = (hitPoint - playerPos).getLength();
						float intensity = std::max(0.0f, 1.0f - dist / maxShakeDist) * peakShake;

						if (intensity > 0.05f)
							g_CameraFX.addShake(intensity, shakeDurMs);
					}
				}

				// Additionally deal direct point damage to the entity that was directly struck
				auto entities = WorldManager::Get()->managerSystem()->getEntities();
				for (auto& entity : entities)
				{
					if (entity.hasComponent<DescriptorComponent>() &&
						entity.getComponent<DescriptorComponent>().id == hitEntityID)
					{
						if (entity.hasComponent<DamageReceiverComponent>())
						{
							auto& damageComp = entity.getComponent<DamageReceiverComponent>();
							damageComp.damageReceived += m_pointDamage;
						}
						break;
					}
				}

				// Mark for removal on hit
				shouldRemove = true;
			}
		}

		// Calculate next position based on velocity
		// Convert dt from milliseconds to seconds
		float dtSeconds = dt / 1000.0f;
		
		// Update distance traveled
		it->distanceTraveled += it->velocity.getLength() * dtSeconds;
		
		if (!it->isTrackingActive && it->distanceTraveled >= it->trackingStartDistance)
		{
			it->isTrackingActive = true;
		}

		// Homing logic
		if (it->useTracking && it->isTrackingActive && it->targetId != _entity_null_value)
		{
			auto& targetEntity = WorldManager::Get()->managerSystem()->getEntityByID(it->targetId);
			if (targetEntity.isValid() && targetEntity.hasComponent<TransformComponent>() && targetEntity.hasComponent<DescriptorComponent>() && targetEntity.getComponent<DescriptorComponent>().isAlive)
			{
				irr::core::vector3df targetPos = targetEntity.getComponent<TransformComponent>().getPosition() + m_targetLockIndicatorOffset;
				
				irr::core::vector3df toTarget = (targetPos - currentPos);
				toTarget.normalize();
				
				irr::core::vector3df currentDir = it->velocity;
				currentDir.normalize();
				
				float dot = currentDir.dotProduct(toTarget);
				
				// If target requires a very sharp turn (e.g. dot < 0.25 is > 75 degrees away), lose lock
				if (dot < 0.25f)
				{
					it->targetId = _entity_null_value; // Lost track
				}
				else
				{
					// Smooth turn towards target
					float maxTurnRate = 1.25f; // Radians per second
					float angle = acos(std::max(-1.0f, std::min(1.0f, dot)));
					float maxAngleTurn = maxTurnRate * dtSeconds;
					
					if (angle > 0.001f)
					{
						if (angle > maxAngleTurn)
						{
							// Interpolate direction
							float t = maxAngleTurn / angle;
							irr::core::vector3df newDir = currentDir + (toTarget - currentDir) * t;
							newDir.normalize();
							it->velocity = newDir * it->speed;
						}
						else
						{
							it->velocity = toTarget * it->speed;
						}
					}
					else 
					{
						// Ensure it maintains its speed even if angle <= 0.001f
						it->velocity = currentDir * it->speed;
					}
				}
			}
			else
			{
				// Target destroyed or removed
				// Auto-track the next available lock target
				entityid nextTarget = _entity_null_value;
				float maxLockTime = 0.0f;

				for (const auto& pair : m_targetLockTimes)
				{
					if (pair.first == it->targetId)
						continue;

					auto& potentialTarget = WorldManager::Get()->managerSystem()->getEntityByID(pair.first);
					if (potentialTarget.isValid() && 
						potentialTarget.hasComponent<TransformComponent>() && 
						potentialTarget.hasComponent<DescriptorComponent>() && 
						potentialTarget.getComponent<DescriptorComponent>().isAlive)
					{
						if (pair.second >= 2000.0f && pair.second > maxLockTime)
						{
							maxLockTime = pair.second;
							nextTarget = pair.first;
						}
					}
				}

				it->targetId = nextTarget;
			}
		}

		irr::core::vector3df nextPos = currentPos + it->velocity * dtSeconds;

		// Update position if we didn't hit anything
		if (!hitSomethingReal)
		{
			transformComp.position = nextPos;

			irr::core::vector3df dir = it->velocity;
			dir.normalize();
			irr::core::vector3df rot = dir.getHorizontalAngle();
			transformComp.rotation = rot;

			if (transformComp.node)
			{
				transformComp.node->setPosition(nextPos);
				transformComp.node->setRotation(rot);
				transformComp.node->updateAbsolutePosition();
			}
		}

		// Update 3D flight sound position to follow the rocket
		if (it->flyingSound)
		{
			irr::core::vector3df soundPos = hitSomethingReal ? currentPos : nextPos;
			it->flyingSound->setPosition(soundPos);
		}

		// Store current position for next frame's collision check
		it->previousPosition = currentPos;

		// Update lifetime
		it->lifetime += dt;

		// Remove if hit something or expired
		if (shouldRemove || it->lifetime >= it->maxLifetime)
		{
			// Clean up trail particle system
			if (it->trailParticles)
			{
				it->trailParticles->remove();
				it->trailParticles = nullptr;
			}

			// Stop and release the looping 3D flight sound
			if (it->flyingSound)
			{
				it->flyingSound->stop();
				it->flyingSound->drop();
				it->flyingSound = nullptr;
			}

			// Kill the entity (this will handle all cleanup automatically)
			if (it->entity.isValid() && it->entity.hasComponent<DescriptorComponent>())
			{
				WorldManager::Get()->killEntityByID(it->entity.getComponent<DescriptorComponent>().id);
			}

			it = m_projectiles.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void Weapon_RocketLauncher::applySplashDamage(const irr::core::vector3df& epicentre, entityid directHitEntityID)
{
	if (m_splashRadius <= 0.0f || m_splashDamage <= 0.0f)
		return;

	auto& entities = WorldManager::Get()->managerSystem()->getEntities();
	for (auto& entity : entities)
	{
		if (!entity.isValid()) continue;
		if (!entity.hasComponent<DescriptorComponent>()) continue;
		if (!entity.hasComponent<TransformComponent>()) continue;

		auto& desc = entity.getComponent<DescriptorComponent>();

		// Skip the entity that was directly hit — it already received m_pointDamage
		if (desc.id == directHitEntityID) continue;

		// Skip dead entities
		if (!desc.isAlive) continue;

		// Measure distance from the impact point to this entity's position
		irr::core::vector3df entityPos = entity.getComponent<TransformComponent>().getPosition();
		float dist = (entityPos - epicentre).getLength();

		if (dist >= m_splashRadius) continue; // Outside the blast radius

		// Linear falloff: 1.0 at the epicentre, 0.0 at the edge of the radius
		float falloff = 1.0f - (dist / m_splashRadius);
		float damage  = m_splashDamage * falloff;

		if (damage > 0.0f && entity.hasComponent<DamageReceiverComponent>())
		{
			entity.getComponent<DamageReceiverComponent>().damageReceived += damage;
		}

		// Apply outward explosion impulse to dynamic (non-kinematic) PhysX actors
		if (m_splashForce > 0.0f && entity.hasComponent<PhysicsComponent>())
		{
			auto& phys = entity.getComponent<PhysicsComponent>();
			if (phys.actor && !phys.kinematic)
			{
				irr::core::vector3df dir = entityPos - epicentre;
				float len = dir.getLength();
				if (len > 0.001f)
					dir /= len;  // normalise
				else
					dir = irr::core::vector3df(0.0f, 1.0f, 0.0f); // straight up if exactly on epicentre

				float impulse = m_splashForce * falloff;
				phys.actor->addForce(
					physx::PxVec3(dir.X, dir.Y, dir.Z) * impulse,
					physx::PxForceMode::eIMPULSE);
			}
		}
	}
}

void Weapon_RocketLauncher::initExplosionParticleSystem()
{
	// smoke renderer
	IRRQuadRenderer* smokeRenderer = IRRQuadRenderer::create(RenderManager::Get()->device());
	smokeRenderer->setTexturingMode(TEXTURE_2D);
	smokeRenderer->setTexture(RenderManager::Get()->driver()->getTexture("content/texture/particle/explosion.png"));
	smokeRenderer->setAtlasDimensions(2, 2); // uses 4 different patterns in the texture
	smokeRenderer->setBlending(BLENDING_ALPHA);
	smokeRenderer->enableRenderingHint(DEPTH_WRITE, false);
	smokeRenderer->setShared(true);

	// flame renderer
	IRRQuadRenderer* flameRenderer = IRRQuadRenderer::create(RenderManager::Get()->device());
	flameRenderer->setTexturingMode(TEXTURE_2D);
	flameRenderer->setTexture(RenderManager::Get()->driver()->getTexture("content/texture/particle/explosion.png"));
	flameRenderer->setAtlasDimensions(2, 2);
	flameRenderer->setBlending(BLENDING_ADD);
	flameRenderer->enableRenderingHint(DEPTH_WRITE, false);
	flameRenderer->setShared(true);

	// flash renderer
	IRRQuadRenderer* flashRenderer = IRRQuadRenderer::create(RenderManager::Get()->device());
	flashRenderer->setTexturingMode(TEXTURE_2D);
	flashRenderer->setTexture(RenderManager::Get()->driver()->getTexture("content/texture/particle/star_09.png"));
	flashRenderer->setBlending(BLENDING_ADD);
	flashRenderer->enableRenderingHint(DEPTH_WRITE, false);
	flashRenderer->setShared(true);

	// spark 1 renderer
	IRRQuadRenderer* spark1Renderer = IRRQuadRenderer::create(RenderManager::Get()->device());
	spark1Renderer->setTexturingMode(TEXTURE_2D);
	spark1Renderer->setTexture(RenderManager::Get()->driver()->getTexture("content/texture/particle/spark1.bmp"));
	spark1Renderer->setBlending(BLENDING_ADD);
	spark1Renderer->enableRenderingHint(DEPTH_WRITE, false);
	spark1Renderer->setOrientation(DIRECTION_ALIGNED); // sparks are oriented function of their velocity
	spark1Renderer->setScale(0.05f, 1.0f); // thin rectangles
	spark1Renderer->setShared(true);

	IRRQuadRenderer* spark2Renderer = IRRQuadRenderer::create(RenderManager::Get()->device());
	spark2Renderer->setTexturingMode(TEXTURE_2D);
	spark2Renderer->setTexture(RenderManager::Get()->driver()->getTexture("content/texture/particle/star_05.png"));
	spark2Renderer->setScale(0.02f, 0.02f);
	spark2Renderer->setBlending(BLENDING_ADD);
	spark2Renderer->enableRenderingHint(DEPTH_WRITE, false);
	spark2Renderer->setShared(true);

	// wave renderer
	IRRQuadRenderer* waveRenderer = IRRQuadRenderer::create(RenderManager::Get()->device());
	waveRenderer->setTexturingMode(TEXTURE_2D);
	waveRenderer->setTexture(RenderManager::Get()->driver()->getTexture("content/texture/particle/circle_03.png"));
	waveRenderer->setBlending(BLENDING_ALPHA);
	waveRenderer->enableRenderingHint(DEPTH_WRITE, false);
	waveRenderer->enableRenderingHint(ALPHA_TEST, true); // uses the alpha test
	waveRenderer->setAlphaTestThreshold(0.0f);
	waveRenderer->setOrientation(FIXED_ORIENTATION); // the orientatin is fixed
	waveRenderer->lookVector.set(0.0f, 1.0f, 0.0f);
	waveRenderer->upVector.set(1.0f, 0.0f, 0.0f); // we dont really care about the up axis
	waveRenderer->setShared(true);

	Interpolator* interpolator = NULL; // pointer to an interpolator that is used to retrieve interpolators	

	// smoke model
	Model* smokeModel = Model::create(FLAG_RED | FLAG_GREEN | FLAG_BLUE | FLAG_ALPHA | FLAG_SIZE | FLAG_ANGLE | FLAG_TEXTURE_INDEX,
		FLAG_SIZE | FLAG_ANGLE,
		FLAG_SIZE | FLAG_ANGLE | FLAG_TEXTURE_INDEX,
		FLAG_ALPHA);
	smokeModel->setParam(PARAM_RED, 0.2f);
	smokeModel->setParam(PARAM_GREEN, 0.2f);
	smokeModel->setParam(PARAM_BLUE, 0.2f);
	smokeModel->setParam(PARAM_SIZE, 0.6f, 0.8f, 1.0f, 1.4f);
	smokeModel->setParam(PARAM_TEXTURE_INDEX, 0.0f, 4.0f);
	smokeModel->setParam(PARAM_ANGLE, 0.0f, _pi * 0.5f, 0.0f, _pi * 0.5f);
	smokeModel->setLifeTime(2.5f, 3.0f);
	smokeModel->setShared(true);

	interpolator = smokeModel->getInterpolator(PARAM_ALPHA);
	interpolator->addEntry(0.0f, 0.0f);
	interpolator->addEntry(0.4f, 0.4f, 0.6f);
	interpolator->addEntry(0.6f, 0.4f, 0.6f);
	interpolator->addEntry(1.0f, 0.0f);

	// flame model
	Model* flameModel = Model::create(FLAG_RED | FLAG_GREEN | FLAG_BLUE | FLAG_ALPHA | FLAG_SIZE | FLAG_ANGLE | FLAG_TEXTURE_INDEX,
		FLAG_ANGLE | FLAG_RED | FLAG_GREEN | FLAG_BLUE,
		FLAG_ANGLE | FLAG_TEXTURE_INDEX,
		FLAG_SIZE | FLAG_ALPHA);
	flameModel->setParam(PARAM_RED, 1.0f, 0.2f);
	flameModel->setParam(PARAM_GREEN, 0.5f, 0.2f);
	flameModel->setParam(PARAM_BLUE, 0.2f, 0.2f);
	flameModel->setParam(PARAM_TEXTURE_INDEX, 0.0f, 4.0f);
	flameModel->setParam(PARAM_ANGLE, 0.0f, _pi * 0.5f, 0.0f, _pi * 0.5f);
	flameModel->setLifeTime(1.5f, 2.0f);
	flameModel->setShared(true);

	interpolator = flameModel->getInterpolator(PARAM_SIZE);
	interpolator->addEntry(0.0f * particle_size_mult, 0.25f * particle_size_mult);
	interpolator->addEntry(0.02f * particle_size_mult, 0.6f * particle_size_mult, 0.8f * particle_size_mult);
	interpolator->addEntry(1.0f * particle_size_mult, 1.0f * particle_size_mult, 1.4f * particle_size_mult);

	interpolator = flameModel->getInterpolator(PARAM_ALPHA);
	interpolator->addEntry(0.5f, 1.0f);
	interpolator->addEntry(1.0f, 0.0f);

	// flash model
	Model* flashModel = Model::create(FLAG_ALPHA | FLAG_SIZE | FLAG_ANGLE,
		FLAG_NONE,
		FLAG_ANGLE,
		FLAG_ALPHA | FLAG_SIZE);
	flashModel->setParam(PARAM_ANGLE, 0.0f, 2.0f * _pi);
	flashModel->setLifeTime(0.5f, 0.5f);
	flashModel->setShared(true);

	interpolator = flashModel->getInterpolator(PARAM_SIZE);
	interpolator->addEntry(0.0f * particle_size_mult, 0.25f * particle_size_mult);
	interpolator->addEntry(0.1f * particle_size_mult, 1.0f * particle_size_mult, 2.0f * particle_size_mult);

	interpolator = flashModel->getInterpolator(PARAM_ALPHA);
	interpolator->addEntry(0.0f, 1.0f);
	interpolator->addEntry(0.4f, 0.0f);

	// spark 1 model
	Model* spark1Model = Model::create(FLAG_SIZE | FLAG_ALPHA,
		FLAG_ALPHA,
		FLAG_SIZE);
	spark1Model->setParam(PARAM_ALPHA, 1.0f, 0.0f);
	spark1Model->setParam(PARAM_SIZE, 0.2f * particle_size_mult, 0.4f * particle_size_mult);
	spark1Model->setLifeTime(0.2f, 1.0f);
	spark1Model->setShared(true);

	// spark 2 model
	Model* spark2Model = Model::create(FLAG_RED | FLAG_GREEN | FLAG_BLUE | FLAG_ALPHA,
		FLAG_GREEN | FLAG_BLUE | FLAG_ALPHA,
		FLAG_GREEN);
	spark2Model->setParam(PARAM_ALPHA, 1.0f, 0.0f);
	spark2Model->setParam(PARAM_RED, 1.0f);
	spark2Model->setParam(PARAM_GREEN, 1.0f, 1.0f, 0.3f, 1.0f);
	spark2Model->setParam(PARAM_BLUE, 0.7f, 0.3f);
	spark2Model->setLifeTime(1.0f, 3.0f);
	spark2Model->setShared(true);

	// wave model
	Model* waveModel = Model::create(FLAG_ALPHA | FLAG_SIZE,
		FLAG_SIZE | FLAG_ALPHA);
	waveModel->setParam(PARAM_SIZE, 0.0f * particle_size_mult, 4.0f * particle_size_mult);
	waveModel->setParam(PARAM_ALPHA, 0.2f, 0.0f);
	waveModel->setLifeTime(0.8f, 0.8f);
	waveModel->setShared(true);

	// This zone will be used by several emitters
	Sphere* explosionSphere = Sphere::create(Vector3D(0.0f, 0.0f, 0.0f), 0.4f);

	// smoke emitter
	RandomEmitter* smokeEmitter = RandomEmitter::create();
	smokeEmitter->setZone(Sphere::create(Vector3D(0.0f, 0.0f, 0.0f), 0.6f), false);
	smokeEmitter->setFlow(-1);
	smokeEmitter->setTank(15);
	smokeEmitter->setForce(0.02f, 0.04f);

	// flame emitter
	NormalEmitter* flameEmitter = NormalEmitter::create();
	flameEmitter->setZone(explosionSphere);
	flameEmitter->setFlow(-1);
	flameEmitter->setTank(15);
	flameEmitter->setForce(0.06f, 0.1f);

	// flash emitter
	StaticEmitter* flashEmitter = StaticEmitter::create();
	flashEmitter->setZone(Sphere::create(Vector3D(0.0f, 0.0f, 0.0f), 0.1f));
	flashEmitter->setFlow(-1);
	flashEmitter->setTank(3);

	// spark 1 emitter
	NormalEmitter* spark1Emitter = NormalEmitter::create();
	spark1Emitter->setZone(explosionSphere);
	spark1Emitter->setFlow(-1);
	spark1Emitter->setTank(20);
	spark1Emitter->setForce(2.0f, 3.0f);

	// spark 2 emitter
	NormalEmitter* spark2Emitter = NormalEmitter::create();
	spark2Emitter->setZone(explosionSphere);
	spark2Emitter->setFlow(-1);
	spark2Emitter->setTank(400);
	spark2Emitter->setForce(0.4f, 0.8f);

	// wave emitter
	StaticEmitter* waveEmitter = StaticEmitter::create();
	waveEmitter->setZone(Point::create());
	waveEmitter->setFlow(-1);
	waveEmitter->setTank(1);

	// smoke group
	Group* smokeGroup = Group::create(smokeModel, 15);
	smokeGroup->addEmitter(smokeEmitter);
	smokeGroup->setRenderer(smokeRenderer);
	smokeGroup->setGravity(Vector3D(0.0f, 0.05f, 0.0f));

	// flame group
	Group* flameGroup = Group::create(flameModel, 15);
	flameGroup->addEmitter(flameEmitter);
	flameGroup->setRenderer(flameRenderer);

	// flash group
	Group* flashGroup = Group::create(flashModel, 3);
	flashGroup->addEmitter(flashEmitter);
	flashGroup->setRenderer(flashRenderer);
	
	// spark 1 group
	Group* spark1Group = Group::create(spark1Model, 20);
	spark1Group->addEmitter(spark1Emitter);
	spark1Group->setRenderer(spark1Renderer);
	spark1Group->setGravity(Vector3D(0.0f, -1.5f, 0.0f));

	// spark 2 group
	Group* spark2Group = Group::create(spark2Model, 400);
	spark2Group->addEmitter(spark2Emitter);
	spark2Group->setRenderer(spark2Renderer);
	spark2Group->setGravity(Vector3D(0.0f, -0.3f, 0.0f));
	spark2Group->setFriction(0.4f);

	// wave group
	Group* waveGroup = Group::create(waveModel, 1);
	waveGroup->addEmitter(waveEmitter);
	waveGroup->setRenderer(waveRenderer);
	
	auto* particleSystem = IRRSystem::create(RenderManager::Get()->sceneManager()->getRootSceneNode(), RenderManager::Get()->sceneManager());
	particleSystem->addGroup(waveGroup);
	particleSystem->addGroup(smokeGroup);
	particleSystem->addGroup(flameGroup);
	particleSystem->addGroup(flashGroup);
	particleSystem->addGroup(spark1Group);
	particleSystem->addGroup(spark2Group);
	
	// Prevent base system from rendering or auto-updating, preserving it as a pristine template
	particleSystem->setAutoUpdateEnabled(false, false);
	particleSystem->setVisible(false);

	m_particleSystemBaseID = particleSystem->getSPKID();
}
#include "Weapon_Rifle.h"

#include "Engine/Engine.h"
#include "Utility/Utility.h"

#include "../CameraFX.h"

#undef MB_RIGHT

using namespace SPK;
using namespace SPK::IRR;

void Weapon_Rifle::precache()
{

}

void Weapon_Rifle::init()
{
	m_descriptor.name = "Player_Weapon_Rifle";
	m_descriptor.id = _entity_null_value;

	m_viewPositionOffset = irr::core::vector3df(0.25f, 0.0f, 0.4f);
	m_viewRotationOffset = irr::core::vector3df(0.0f, 0.0f, 0.0f);
	m_viewScaleOffset = irr::core::vector3df(1.0f, 1.0f, 1.0f);

	m_mesh.mesh = "content/mesh/player/weapon/bolt_driver/hud.b3d";

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

	//RenderManager::Get()->renderer()->getMaterialSwapper()->swapMaterials(m_mesh.node);

	m_mesh.fps = 30;
	m_mesh.node->setAnimationSpeed(30.0f);
	m_mesh.node->setLoopMode(true);
	m_mesh.node->setFrameLoop(20, 50);

	m_mesh.animationList.emplace_back(sAnimationData("equip",   1,   20,  false));
	m_mesh.animationList.emplace_back(sAnimationData("idle",    20,  50,  true));
	m_mesh.animationList.emplace_back(sAnimationData("move",    50,  79,  false));
	m_mesh.animationList.emplace_back(sAnimationData("fire",    81,  95,  false));
	m_mesh.animationList.emplace_back(sAnimationData("reload",  96,  179, false));
	m_mesh.animationList.emplace_back(sAnimationData("unequip", 179, 190, false));

	m_mesh.node->setJointMode(irr::scene::EJUOR_READ);

	m_mesh.animation_call_back = std::make_shared<AnimationCallback>();
	m_mesh.node->setAnimationEndCallback(m_mesh.animation_call_back.get());

	m_mesh.node->setScale(m_viewScaleOffset);

	// Apply the standard PBR shader to every buffer as the baseline (body, gear, etc.)
	auto perpixelMat = ShaderMaterialManager::get("phong_perpixel");
	if (perpixelMat != irr::video::EMT_SOLID)
		m_mesh.node->setMaterialType(perpixelMat);

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

	m_muzzleNode = m_mesh.node->getJointNode("FIRESPOT");
	if (!m_muzzleNode)
		spdlog::warn("Weapon_Rifle: 'FIRESPOT' joint not found");

	m_muzzleFlashMaterialType = irr::video::EMT_TRANSPARENT_ADD_COLOR;

	m_crosshair = RenderManager::Get()->driver()->getTexture("content/texture/ui/crosshair/crosshair038.png");

	// Pre-build shell casing pool (no per-shot alloc/PhysX cost)
	auto* shellMesh = RenderManager::Get()->sceneManager()->getMesh("content/mesh/prop/shells/shellmedium.obj");
	auto* shellTex = RenderManager::Get()->driver()->getTexture("content/mesh/prop/shells/shellsColor.png");
	for (int i = 0; i < SHELL_POOL_SIZE; i++)
	{
		m_shellPool[i].node = RenderManager::Get()->sceneManager()->addMeshSceneNode(shellMesh);
		if (m_shellPool[i].node)
		{
			m_shellPool[i].node->setMaterialTexture(0, shellTex);
			m_shellPool[i].node->setMaterialFlag(irr::video::EMF_BILINEAR_FILTER, true);
			m_shellPool[i].node->setMaterialFlag(irr::video::EMF_TRILINEAR_FILTER, true);
			m_shellPool[i].node->setRotation(irr::core::vector3df(0.0f, 180.0f, 0.0f));
			m_shellPool[i].node->setScale(irr::core::vector3df(1.0f, 1.0f, 1.0f));
			m_shellPool[i].node->setMaterialType(perpixelMat);
			m_shellPool[i].node->setVisible(false);
		}
		m_shellPool[i].active = false;
	}

	initImpactSparkSystem();
}

void Weapon_Rifle::destroy()
{
	// Clean up shell casing pool
	for (int i = 0; i < SHELL_POOL_SIZE; i++)
	{
		if (m_shellPool[i].node)
		{
			m_shellPool[i].node->remove();
			m_shellPool[i].node = nullptr;
		}
	}

	// Clean up all tracer beams
	for (auto& tracer : m_tracerBeams)
	{
		if (tracer.node)
		{
			tracer.node->remove();
		}
	}
	m_tracerBeams.clear();

	// Destroy active impact systems
	for (auto* sys : m_impactSystems)
		SPK_Destroy(sys);
	m_impactSystems.clear();

	if (m_impactSparkBaseID != SPK::NO_ID)
	{
		SPK_Destroy(SPK_Get(SPK::System, m_impactSparkBaseID));
		m_impactSparkBaseID = SPK::NO_ID;
	}

	RenderManager::Get()->unregisterViewmodelNode(m_mesh.node);
	m_mesh.node->remove();

	WorldManager::Get()->freeEntityID(m_descriptor.id);
}

void Weapon_Rifle::update()
{
	if (!m_mesh.node || !m_mesh.node->isVisible())
		return;

	float currentTime = Engine::Get()->getCurrentTime();

	bool animEnded = m_mesh.animation_call_back->hasAnimationEnded();

	if (m_isUnequipping)
	{
		if (animEnded) { m_isUnequipping = false; m_mesh.node->setVisible(false); }
		return;
	}

	if (m_isEquipping)
	{
		if (animEnded)
		{
			m_isEquipping = false;
			m_mesh.node->setLoopMode(true);
			m_mesh.node->setFrameLoop(20, 50);
		}
		return;
	}

	if (m_isReloadingAnim)
	{
		if (animEnded)
		{
			m_isReloadingAnim = false;
			m_mesh.node->setLoopMode(true);
			m_mesh.node->setFrameLoop(20, 50);
		}
		RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
		return;
	}

	bool fireButtonPressed = InputManager::Get()->isMouseButtonPressed(MB_LEFT);
	const float fireRate = 250.0f;
	if (fireButtonPressed && (currentTime - m_lastFireTime) >= fireRate)
	{
		m_lastFireTime = currentTime;
		fire();
		m_isPlayingFireAnim = true;
	}

	if (!fireButtonPressed && m_isPlayingFireAnim && animEnded)
	{
		m_isPlayingFireAnim = false;
		m_mesh.node->setLoopMode(true);
		m_mesh.node->setFrameLoop(20, 50);
	}

	RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
}

void Weapon_Rifle::persist()
{
	float dt = Engine::Get()->getDeltaTime();

	// Update muzzle flash effect
	updateMuzzleFlash(dt);

	// Update tracer beams
	updateTracers(dt);

	// Update shell casings
	updateShells(dt);

	// Tick impact particle systems, remove finished ones
	for (auto it = m_impactSystems.begin(); it != m_impactSystems.end();)
	{
		if (!(*it)->update(dt / m_impactUpdateRate))
		{
			SPK_Destroy(*it);
			it = m_impactSystems.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void Weapon_Rifle::equip()
{
	m_mesh.node->setVisible(true);
	m_mesh.animation_call_back->hasAnimationEnded();
	m_mesh.node->setLoopMode(false);
	m_mesh.node->setFrameLoop(1, 20);
	m_isEquipping = true;
	m_isUnequipping = false;
	m_isPlayingFireAnim = false;
	m_isReloadingAnim = false;
}

void Weapon_Rifle::unequip()
{
	m_isEquipping = false;
	m_isUnequipping = false;
	m_isPlayingFireAnim = false;
	m_isReloadingAnim = false;
	m_mesh.node->setVisible(false);
}

void Weapon_Rifle::startUnequip()
{
	m_isUnequipping = true;
	m_isEquipping = false;
	m_isPlayingFireAnim = false;
	m_isReloadingAnim = false;
	m_mesh.animation_call_back->hasAnimationEnded();
	m_mesh.node->setLoopMode(false);
	m_mesh.node->setFrameLoop(179, 190);
}

void Weapon_Rifle::idle()
{

}

void Weapon_Rifle::move()
{

}

void Weapon_Rifle::fire()
{
	m_mesh.node->setLoopMode(false);
	m_mesh.node->setFrameLoop(81, 95);

	// Raycast-based instant hit
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>())
		return;

	auto& camera = player.getComponent<CameraComponent>();

	if (!m_mesh.node || !m_muzzleNode)
	{
		if (!m_muzzleNode) spdlog::warn("Weapon_Rifle: muzzle node not found - cannot fire");
		return;
	}

	// Force full hierarchy update: camera → weapon → bones
	camera.camera->updateAbsolutePosition();
	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();
	m_muzzleNode->updateAbsolutePosition();
	irr::core::vector3df muzzlePos = m_muzzleNode->getAbsolutePosition();

	// Get camera target for aiming direction
	irr::core::vector3df target = camera.camera->getTarget();
	irr::core::vector3df cameraPos = camera.camera->getAbsolutePosition();
	irr::core::vector3df forward = (target - cameraPos).normalize();

	// Calculate right and down vectors relative to camera orientation for spread
	irr::core::vector3df up(0, 1, 0);
	irr::core::vector3df right = forward.crossProduct(up).normalize();
	irr::core::vector3df down = right.crossProduct(forward).normalize();

	// Calculate direction from camera to crosshair (screen center)
	irr::core::vector3df direction = forward;

	// Apply random offset in right and down directions
	float spreadRight = Engine::Get()->rng()->getFloat(-m_recoil, m_recoil);
	float spreadDown = Engine::Get()->rng()->getFloat(-m_recoil, m_recoil);
	direction = (direction + right * spreadRight + down * spreadDown).normalize();

	// Perform raycast from muzzle position in spread direction
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
				// Deal damage if entity can receive it
				if (hitEntity.hasComponent<DamageReceiverComponent>())
				{
					auto& damageComp = hitEntity.getComponent<DamageReceiverComponent>();
					damageComp.damageReceived += 25; // Minigun damage
				}

				// Create impact spark particles at hit position with surface normal
				createImpactEffect(raycastResult.point);
			}
		}
	}

	// Increment shot counter for tracer logic
	m_shotCounter++;
	bool isTracerRound = (m_shotCounter % m_tracerFrequency == 0);

	// Create tracer beam for every Nth shot
	if (isTracerRound)
	{
		// Determine tracer end point (hit point or max range)
		irr::core::vector3df tracerEnd = (raycastResult.hit && raycastResult.node) ?
			raycastResult.point : (muzzlePos + direction * 1000.0f);

		createTracerBeam(muzzlePos, tracerEnd);
	}

	// Create muzzle flash effect
	createMuzzleFlash();

	// Eject shell casing
	ejectShell();

	// Camera recoil kick — random yaw drift for a natural, unsteady feel
	auto recoilYaw = Engine::Get()->rng()->getFloat(-0.1f, 0.1f);
	g_CameraFX.addRecoil(-0.5f, recoilYaw);

	SoundManager::Get()->sound()->play2D("content/sound/weapon/rifle/fire.wav", false);
}

void Weapon_Rifle::reload()
{
	if (!m_isReloadingAnim)
	{
		m_mesh.node->setLoopMode(false);
		m_mesh.node->setFrameLoop(96, 179);
		m_isReloadingAnim = true;
		m_isPlayingFireAnim = false;
	}
}


void Weapon_Rifle::createMuzzleFlash()
{
	if (!m_mesh.node)
		return;

	m_mesh.node->updateAbsolutePosition();

	if (!m_muzzleNode)
		return;

	m_muzzleNode->updateAbsolutePosition();

	irr::core::vector3df flashOffset(0, 0, 0.0f);

	if (!m_muzzleStarNode)
	{
		m_muzzleStarNode = RenderManager::Get()->sceneManager()->addBillboardSceneNode(
			m_muzzleNode,
			irr::core::dimension2df(1.2f, 1.2f),
			flashOffset
		);

		m_muzzleStarNode->setMaterialFlag(irr::video::EMF_LIGHTING, false);
		m_muzzleStarNode->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE, false);
		m_muzzleStarNode->setMaterialFlag(irr::video::EMF_BLEND_OPERATION, true);
		m_muzzleStarNode->setMaterialType(m_muzzleFlashMaterialType);

		// Bind depth texture (texture unit 1)
		//m_muzzleStarNode->getMaterial(0).setTexture(1, RenderManager::Get()->renderer()->getMRT(2));
	}

	std::string starPath = "content/texture/particle/star_05.png";
	auto* starTex = RenderManager::Get()->driver()->getTexture(starPath.c_str());
	m_muzzleStarNode->setMaterialTexture(0, starTex);
	m_muzzleStarNode->setVisible(true);

	// Force immediate position update to prevent lag during fast camera movement
	m_muzzleStarNode->updateAbsolutePosition();

	// Tint star yellow-orange for minigun effect
	m_muzzleStarNode->getMaterial(0).AmbientColor = irr::video::SColor(255, 255, 204, 76);
	m_muzzleStarNode->getMaterial(0).DiffuseColor = irr::video::SColor(255, 255, 204, 76);
	m_muzzleStarNode->getMaterial(0).EmissiveColor = irr::video::SColor(255, 255, 204, 76);

	// Create/show blue point light at muzzle if not exists
	if (!m_muzzleLightNode)
	{
		m_muzzleLightNode = RenderManager::Get()->sceneManager()->addLightSceneNode(
			m_muzzleNode,
			flashOffset,  // Same position as flash
			irr::video::SColorf(1.0f, 1.0f, 0.0f),  // Blue light
			3.0f  // Small radius
		);
	}
	if (m_muzzleLightNode)
	{
		m_muzzleLightNode->setVisible(true);
		// Force immediate position update for light as well
		m_muzzleLightNode->updateAbsolutePosition();
	}

	m_muzzleFlashTime = 0.0f;
}

void Weapon_Rifle::updateMuzzleFlash(float dt)
{
	if (!m_muzzleStarNode)
		return;

	m_muzzleFlashTime += dt;

	if (m_muzzleFlashTime >= m_muzzleFlashDuration)
	{
		if (m_muzzleStarNode)
			m_muzzleStarNode->setVisible(false);
		if (m_muzzleLightNode)
			m_muzzleLightNode->setVisible(false);
	}
	else
	{
		float fadeProgress = m_muzzleFlashTime / m_muzzleFlashDuration;
		irr::u32 alpha = (irr::u32)((1.0f - fadeProgress) * 255.0f);

		if (m_muzzleStarNode)
		{
			m_muzzleStarNode->getMaterial(0).AmbientColor.setAlpha(alpha);
			m_muzzleStarNode->getMaterial(0).DiffuseColor.setAlpha(alpha);
			m_muzzleStarNode->getMaterial(0).EmissiveColor.setAlpha(alpha);
		}
	}
}

void Weapon_Rifle::createTracerBeam(const irr::core::vector3df& start, const irr::core::vector3df& end)
{
	// Calculate beam properties
	irr::core::vector3df direction = (end - start);
	float distance = direction.getLength();

	if (distance < 0.1f)
		return; // Too short to render

	direction.normalize();

	// Create a plane mesh for the tracer beam
	irr::scene::IMesh* planeMesh = RenderManager::Get()->sceneManager()->getGeometryCreator()->createPlaneMesh(
		irr::core::dimension2df(0.1f, distance), // Width x Length
		irr::core::dimension2du(1, 1)
	);

	irr::scene::IMeshSceneNode* tracerNode = RenderManager::Get()->sceneManager()->addMeshSceneNode(planeMesh);

	if (!tracerNode)
	{
		spdlog::warn("Failed to create tracer beam mesh");
		return;
	}

	// Position at start point
	tracerNode->setPosition(start);

	// Rotate to align with beam direction
	// The plane is created facing +Y, we need to align it with the beam direction
	irr::core::vector3df targetDirection = direction;

	// Calculate rotation to align plane with beam
	// Default plane normal is (0, 1, 0), we want it to point along the beam
	irr::core::vector3df rotation;
	rotation.Y = atan2f(targetDirection.X, targetDirection.Z) * (180.0f / 3.14159265f);
	rotation.X = -asinf(targetDirection.Y) * (180.0f / 3.14159265f);
	rotation.Z = 0;

	tracerNode->setRotation(rotation);

	// Offset position to center the beam (since plane starts at one end)
	irr::core::vector3df offset = direction * (distance * 0.5f);
	tracerNode->setPosition(start + offset);

	// Load tracer texture (glowing line)
	auto* tracerTexture = RenderManager::Get()->driver()->getTexture("content/texture/particle/trace_07.png");
	if (!tracerTexture)
	{
		// Fallback to a simple particle texture
		tracerTexture = RenderManager::Get()->driver()->getTexture("content/texture/color/magenta.png");
	}

	tracerNode->setMaterialTexture(0, tracerTexture);

	// Bind depth texture (texture unit 1) for depth-aware rendering
	//tracerNode->setMaterialTexture(1, RenderManager::Get()->renderer()->getMRT(2));

	// Configure material for glowing effect
	tracerNode->setMaterialFlag(irr::video::EMF_LIGHTING, false);
	tracerNode->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE, false);
	tracerNode->setMaterialFlag(irr::video::EMF_BACK_FACE_CULLING, false); // Render both sides
	tracerNode->setMaterialFlag(irr::video::EMF_BLEND_OPERATION, true);

	// Use the same muzzle flash shader for depth-aware transparency
	tracerNode->setMaterialType(m_muzzleFlashMaterialType);

	// Set bright yellow/orange color for tracer
	tracerNode->getMaterial(0).AmbientColor = irr::video::SColor(255, 255, 200, 100);
	tracerNode->getMaterial(0).DiffuseColor = irr::video::SColor(255, 255, 200, 100);
	tracerNode->getMaterial(0).EmissiveColor = irr::video::SColor(255, 255, 200, 100);

	// Store tracer for update/cleanup
	TracerBeam tracer;
	tracer.node = tracerNode;
	tracer.spawnTime = Engine::Get()->getCurrentTime();
	m_tracerBeams.push_back(tracer);
}

void Weapon_Rifle::ejectShell()
{
	if (!m_mesh.node)
		return;

	// Find a free slot in the pool
	ShellCasing* shell = nullptr;
	for (int i = 0; i < SHELL_POOL_SIZE; i++)
	{
		if (!m_shellPool[i].active)
		{
			shell = &m_shellPool[i];
			break;
		}
	}
	if (!shell || !shell->node)
		return; // Pool exhausted, skip

	// Sync weapon hierarchy so eject bone is at the correct world position
	{
		anax::Entity& pl = WorldManager::Get()->managerSystem()->getEntityByName("player");
		if (pl.isValid() && pl.hasComponent<CameraComponent>())
			pl.getComponent<CameraComponent>().camera->updateAbsolutePosition();
	}
	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();

	irr::scene::IBoneSceneNode* ejectBone = m_mesh.node->getJointNode("BRASS");
	if (!ejectBone)
	{
		spdlog::warn("BRASS bone not found on minigun model - cannot eject shell");
		return;
	}
	ejectBone->updateAbsolutePosition();

	irr::core::vector3df ejectPosition = ejectBone->getAbsolutePosition()
		+ irr::core::vector3df(0.0f, Engine::Get()->rng()->getFloat(-0.1f, 0.1f), 0.0f);

	// Derive ejection vectors from current camera orientation
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>())
		return;

	auto& camera = player.getComponent<CameraComponent>();
	irr::core::vector3df target = camera.camera->getTarget();
	irr::core::vector3df camPos = camera.camera->getAbsolutePosition();
	irr::core::vector3df forward = (target - camPos).normalize();

	irr::core::vector3df worldUp(0, 1, 0);
	irr::core::vector3df right = forward.crossProduct(worldUp).normalize();
	irr::core::vector3df localUp = right.crossProduct(forward).normalize();

	irr::core::vector3df randomOffset(
		Engine::Get()->rng()->getFloat(-0.3f, 0.3f),
		Engine::Get()->rng()->getFloat(-0.3f, 0.3f),
		Engine::Get()->rng()->getFloat(-0.3f, 0.3f)
	);
	irr::core::vector3df ejectionDir = (-right + localUp + randomOffset).normalize();
	float randomSpeed = m_shellEjectionSpeed * Engine::Get()->rng()->getFloat(0.75f, 1.25f);

	// Orient shell to match camera yaw/pitch
	float yaw = atan2f(forward.X, forward.Z) * (180.0f / 3.14159265f);
	float pitch = asinf(irr::core::clamp(forward.Y, -1.0f, 1.0f)) * (180.0f / 3.14159265f);

	// Activate this pool slot — immediate, no frame delay
	shell->node->setPosition(ejectPosition);
	shell->rotation = irr::core::vector3df(-pitch, yaw, 0.0f);
	shell->node->setRotation(shell->rotation);
	shell->velocity = ejectionDir * randomSpeed;
	shell->angularVelocity = irr::core::vector3df(
		Engine::Get()->rng()->getFloat(-300.0f, 300.0f),
		Engine::Get()->rng()->getFloat(-300.0f, 300.0f),
		Engine::Get()->rng()->getFloat(-300.0f, 300.0f)
	);
	shell->spawnTime = static_cast<float>(Engine::Get()->getCurrentTime());
	shell->active = true;
	shell->physicsActive = true;
	shell->bounceCount = 0;
	shell->node->setVisible(true);
}

void Weapon_Rifle::updateTracers(float dt)
{
	float currentTime = Engine::Get()->getCurrentTime();

	// Update and remove expired tracers
	for (auto it = m_tracerBeams.begin(); it != m_tracerBeams.end();)
	{
		float elapsed = currentTime - it->spawnTime;

		if (elapsed >= it->lifetime)
		{
			// Remove expired tracer
			if (it->node)
			{
				it->node->remove();
			}
			it = m_tracerBeams.erase(it);
		}
		else
		{
			// Update fade based on lifetime
			float fadeProgress = elapsed / it->lifetime;
			irr::u32 alpha = (irr::u32)((1.0f - fadeProgress) * 255.0f);

			if (it->node)
			{
				it->node->getMaterial(0).AmbientColor.setAlpha(alpha);
				it->node->getMaterial(0).DiffuseColor.setAlpha(alpha);
			}

			++it;
		}
	}
}

void Weapon_Rifle::updateShells(float dt)
{
	const float dt_s = dt * 0.001f; // ms -> seconds
	const float currentTime = static_cast<float>(Engine::Get()->getCurrentTime());
	const float shellLifetime = 10000.0f; // ms

	for (int i = 0; i < SHELL_POOL_SIZE; i++)
	{
		ShellCasing& shell = m_shellPool[i];
		if (!shell.active)
			continue;

		// Lifetime expiry — return slot to pool
		if (currentTime - shell.spawnTime >= shellLifetime)
		{
			shell.active = false;
			shell.node->setVisible(false);
			continue;
		}

		if (!shell.physicsActive)
			continue;

		// Gravity
		shell.velocity.Y -= m_shellGravity * dt_s;

		// Candidate new position
		irr::core::vector3df pos = shell.node->getPosition();
		irr::core::vector3df newPos = pos + shell.velocity * dt_s;

		// Cast ray along direction of travel — detects floors, walls, ceilings, ramps
		float speed = shell.velocity.getLength();
		if (speed > 0.001f)
		{
			irr::core::vector3df travelDir = shell.velocity / speed; // manual normalize (avoid mutate-in-place)
			irr::core::vector3df rayStart = pos;
			irr::core::vector3df rayEnd = newPos + travelDir * 0.1f; // small look-ahead past new pos
			RaycastResultData hit = RenderManager::Get()->raycastWorldPosition(rayStart, rayEnd, true);

			if (hit.hit)
			{
				// Reflect velocity off surface normal: v' = v - 2(v·n)n
				irr::core::vector3df n = hit.normal;
				float dot = shell.velocity.dotProduct(n);
				shell.velocity = (shell.velocity - n * (2.0f * dot)) * 0.45f;
				shell.angularVelocity *= 0.5f;

				// Place shell just off the surface so it doesn't tunnel next frame
				newPos = hit.point + n * 0.05f;

				// Bounce sound — throttled so rapid shell cascades don't stack up
				if (shell.bounceCount < 2 && (currentTime - m_lastShellBounceSound) >= m_shellBounceSoundInterval)
				{
					std::string soundFile = "content/sound/prop/shell"
						+ std::to_string(Engine::Get()->rng()->getInt(1, 2)) + ".wav";
					SoundManager::Get()->sound()->play3D(soundFile.c_str(), shell.node->getPosition(), false, false, true, 0, 0.5f);
					m_lastShellBounceSound = currentTime;
				}

				shell.bounceCount++;

				// After 3 bounces the shell has settled — stop simulating
				if (shell.bounceCount >= 3)
				{
					shell.physicsActive = false;
					shell.velocity = irr::core::vector3df(0, 0, 0);
				}
			}
		}

		shell.node->setPosition(newPos);

		// Spin
		shell.rotation += shell.angularVelocity * dt_s;
		shell.node->setRotation(shell.rotation);
	}
}

// ---------------------------------------------------------------------------
// initImpactSparkSystem
// ---------------------------------------------------------------------------
// Groups:
//   1. Flash    – large, very short-lived white-blue billboard burst at impact
//   2. Sparks   – thin blue sparks shooting outward, fade with gravity
//   3. Glow     – soft expanding glow that lingers briefly
// ---------------------------------------------------------------------------
void Weapon_Rifle::initImpactSparkSystem()
{
	auto* driver = RenderManager::Get()->driver();
	auto* smgr = RenderManager::Get()->sceneManager();

	// -----------------------------------------------------------------------
	// Renderers
	// -----------------------------------------------------------------------

	IRRQuadRenderer* flashRenderer = IRRQuadRenderer::create(RenderManager::Get()->device());
	flashRenderer->setTexturingMode(TEXTURE_2D);
	flashRenderer->setTexture(driver->getTexture("content/texture/particle/star_04.png"));
	flashRenderer->setBlending(BLENDING_ADD);
	flashRenderer->enableRenderingHint(DEPTH_WRITE, false);
	flashRenderer->setShared(true);

	IRRQuadRenderer* sparkRenderer = IRRQuadRenderer::create(RenderManager::Get()->device());
	sparkRenderer->setTexturingMode(TEXTURE_2D);
	sparkRenderer->setTexture(driver->getTexture("content/texture/particle/spark1.bmp"));
	sparkRenderer->setBlending(BLENDING_ADD);
	sparkRenderer->enableRenderingHint(DEPTH_WRITE, false);
	sparkRenderer->setOrientation(DIRECTION_ALIGNED);
	sparkRenderer->setScale(0.03f, 1.0f);
	sparkRenderer->setShared(true);

	IRRQuadRenderer* glowRenderer = IRRQuadRenderer::create(RenderManager::Get()->device());
	glowRenderer->setTexturingMode(TEXTURE_2D);
	glowRenderer->setTexture(driver->getTexture("content/texture/particle/flame_04.png"));
	glowRenderer->setBlending(BLENDING_ADD);
	glowRenderer->enableRenderingHint(DEPTH_WRITE, false);
	glowRenderer->setShared(true);

	// -----------------------------------------------------------------------
	// Models
	// -----------------------------------------------------------------------

	// Flash: white-blue, fades very fast, large
	Model* flashModel = Model::create(
		FLAG_RED | FLAG_GREEN | FLAG_BLUE | FLAG_ALPHA | FLAG_SIZE,
		FLAG_ALPHA | FLAG_SIZE,
		FLAG_NONE);
	flashModel->setParam(PARAM_RED, 1.0f);
	flashModel->setParam(PARAM_GREEN, 1.0f);
	flashModel->setParam(PARAM_BLUE, 1.0f);
	flashModel->setParam(PARAM_ALPHA, 1.0f, 0.0f);
	flashModel->setParam(PARAM_SIZE, 0.6f, 0.05f);
	flashModel->setLifeTime(0.08f, 0.14f);
	flashModel->setShared(true);

	// Sparks: bright blue-white streaks, fly out then fade
	Model* sparkModel = Model::create(
		FLAG_RED | FLAG_GREEN | FLAG_BLUE | FLAG_ALPHA | FLAG_SIZE,
		FLAG_ALPHA | FLAG_RED | FLAG_GREEN | FLAG_BLUE,
		FLAG_SIZE);
	sparkModel->setParam(PARAM_RED, 1.0f, 0.3f);
	sparkModel->setParam(PARAM_GREEN, 1.0f, 0.4f);
	sparkModel->setParam(PARAM_BLUE, 1.0f, 1.0f);
	sparkModel->setParam(PARAM_ALPHA, 1.0f, 0.0f);
	sparkModel->setParam(PARAM_SIZE, 0.08f, 0.18f);
	sparkModel->setLifeTime(0.25f, 0.6f);
	sparkModel->setShared(true);

	// Glow: soft yellow, expands and fades
	Model* glowModel = Model::create(
		FLAG_RED | FLAG_GREEN | FLAG_BLUE | FLAG_ALPHA | FLAG_SIZE,
		FLAG_ALPHA | FLAG_SIZE,
		FLAG_NONE);
	glowModel->setParam(PARAM_RED, 1.0f);
	glowModel->setParam(PARAM_GREEN, 0.99f);
	glowModel->setParam(PARAM_BLUE, 0.77f);
	glowModel->setParam(PARAM_ALPHA, 0.6f, 0.0f);
	glowModel->setParam(PARAM_SIZE, 0.1f, 0.55f);
	glowModel->setLifeTime(0.15f, 0.3f);
	glowModel->setShared(true);

	// -----------------------------------------------------------------------
	// Zones & Emitters  (all one-shot: flow = -1, tank = N)
	// -----------------------------------------------------------------------

	Sphere* impactSphere = Sphere::create(Vector3D(0.0f, 0.0f, 0.0f), 0.05f);

	// Flash: single burst at centre
	NormalEmitter* flashEmitter = NormalEmitter::create();
	flashEmitter->setZone(Sphere::create(Vector3D(0.0f, 0.0f, 0.0f), 0.01f), false);
	flashEmitter->setFlow(-1);
	flashEmitter->setTank(1);
	flashEmitter->setForce(0.0f, 0.1f);

	// Sparks: burst outward from impact sphere
	NormalEmitter* sparkEmitter = NormalEmitter::create();
	sparkEmitter->setZone(impactSphere);
	sparkEmitter->setFlow(-1);
	sparkEmitter->setTank(18);
	sparkEmitter->setForce(2.0f, 5.0f);

	// Glow: small burst of soft billboards
	NormalEmitter* glowEmitter = NormalEmitter::create();
	glowEmitter->setZone(Sphere::create(Vector3D(0.0f, 0.0f, 0.0f), 0.04f), false);
	glowEmitter->setFlow(-1);
	glowEmitter->setTank(4);
	glowEmitter->setForce(0.1f, 0.4f);

	// -----------------------------------------------------------------------
	// Groups
	// -----------------------------------------------------------------------

	Group* flashGroup = Group::create(flashModel, 1);
	flashGroup->addEmitter(flashEmitter);
	flashGroup->setRenderer(flashRenderer);

	Group* sparkGroup = Group::create(sparkModel, 18);
	sparkGroup->addEmitter(sparkEmitter);
	sparkGroup->setRenderer(sparkRenderer);
	sparkGroup->setGravity(Vector3D(0.0f, -4.0f, 0.0f));

	Group* glowGroup = Group::create(glowModel, 4);
	glowGroup->addEmitter(glowEmitter);
	glowGroup->setRenderer(glowRenderer);

	// -----------------------------------------------------------------------
	// System
	// -----------------------------------------------------------------------

	IRRSystem* system = IRRSystem::create(smgr->getRootSceneNode(), smgr);
	system->addGroup(flashGroup);
	system->addGroup(sparkGroup);
	system->addGroup(glowGroup);
	system->setAutoUpdateEnabled(false, false);
	static_cast<irr::scene::ISceneNode*>(system)->setVisible(false);

	m_impactSparkBaseID = system->getSPKID();
}

void Weapon_Rifle::createImpactEffect(const irr::core::vector3df& pos)
{
	if (m_impactSparkBaseID == SPK::NO_ID)
		return;

	SPK::System* system = SPK_Copy(SPK::System, m_impactSparkBaseID);
	IRRSystem* irrSys = static_cast<IRRSystem*>(system);
	if (irrSys)
	{
		irrSys->setVisible(true);
		irrSys->setPosition(pos);
		irrSys->updateAbsolutePosition();
	}

	m_impactSystems.push_back(system);
}
#include "Weapon_PulseRifle.h"

#include "Engine/Engine.h"
#include "Utility/Utility.h"

#include "../CameraFX.h"

#undef MB_RIGHT

using namespace SPK;
using namespace SPK::IRR;

// MaterialTypeParam carries burnTime (0..1). Read directly in OnSetConstants — it is
// set by the callback's own OnSetMaterial every time the material is applied, and by
// updateBurnDecals every frame, so it is always current when the shader runs.
class BurnDecalShaderCallback : public irr::video::IShaderConstantSetCallBack
{
	float m_burnTime = 0.0f;
public:
	void OnSetMaterial(const irr::video::SMaterial& mat) override
	{
		m_burnTime = mat.MaterialTypeParam;
	}
	void OnSetConstants(irr::video::IMaterialRendererServices* services, irr::s32) override
	{
		services->setPixelShaderConstant("uBurnTime", &m_burnTime, 1);
		irr::s32 tex0 = 0;
		services->setPixelShaderConstant("texture1", &tex0, 1);
		irr::scene::ICameraSceneNode* cam = RenderManager::Get()->sceneManager()->getActiveCamera();
		if (cam)
		{
			irr::f32 farDist = cam->getFarValue();
			services->setVertexShaderConstant("CamFar", &farDist, 1);
		}
	}
};

void Weapon_PulseRifle::precache()
{

}

void Weapon_PulseRifle::init()
{
	m_descriptor.name = "Player_Weapon_PulseRifle";
	m_descriptor.id = _entity_null_value;

	m_viewPositionOffset = irr::core::vector3df(0.25f, -0.1f, 0.3f);
	m_viewRotationOffset = irr::core::vector3df(0.0f, 0.0f, 0.0f);
	m_viewScaleOffset = irr::core::vector3df(1.0f, 1.0f, 1.0f);

	m_mesh.mesh = "content/mesh/player/weapon/beam_cutter/hud.b3d";

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
		spdlog::warn("Weapon_PulseRifle: 'FIRESPOT' joint not found");

	m_muzzleFlashMaterialType = ShaderMaterialManager::get("additive_color");

	m_crosshair = RenderManager::Get()->driver()->getTexture("content/texture/ui/crosshair/crosshair038.png");

	// Create persistent laser beam node (unit plane stretched per-shot in createLaserBeam)
	{
		auto* geo = RenderManager::Get()->sceneManager()->getGeometryCreator();
		irr::scene::IMesh* planeMesh = geo->createPlaneMesh(
			irr::core::dimension2df(0.05f, 1.0f), irr::core::dimension2du(1, 1));
		m_laserNode = RenderManager::Get()->sceneManager()->addMeshSceneNode(planeMesh);
		planeMesh->drop();
		if (m_laserNode)
		{
			auto* laserTex = RenderManager::Get()->driver()->getTexture("content/texture/particle/trace_07.png");
			m_laserNode->setMaterialTexture(0, laserTex);
			m_laserNode->setMaterialFlag(irr::video::EMF_LIGHTING, false);
			m_laserNode->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE, false);
			m_laserNode->setMaterialFlag(irr::video::EMF_BACK_FACE_CULLING, false);
			m_laserNode->setMaterialFlag(irr::video::EMF_BLEND_OPERATION, true);
			m_laserNode->setMaterialType(m_muzzleFlashMaterialType);
			m_laserNode->getMaterial(0).AmbientColor  = irr::video::SColor(255, 255, 30, 30);
			m_laserNode->getMaterial(0).DiffuseColor  = irr::video::SColor(255, 255, 30, 30);
			m_laserNode->getMaterial(0).EmissiveColor = irr::video::SColor(255, 255, 30, 30);
			m_laserNode->setVisible(false);
		}
	}

	initImpactSparkSystem();

	// Register impact_burn shader with a shared per-decal callback
	auto* gpu = RenderManager::Get()->driver()->getGPUProgrammingServices();
	if (gpu)
	{
		auto* burnCb = new BurnDecalShaderCallback();
		irr::s32 burnMat = gpu->addHighLevelShaderMaterialFromFiles(
			"content/shader/impact_burn.vert", "main", irr::video::EVST_VS_2_0,
			"content/shader/impact_burn.frag", "main", irr::video::EPST_PS_2_0,
			burnCb,
			irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL,
			0, irr::video::EGSL_DEFAULT
		);
		burnCb->drop();
		m_burnDecalMat = static_cast<irr::video::E_MATERIAL_TYPE>(burnMat);
	}
	m_burnTexture = RenderManager::Get()->driver()->getTexture("content/texture/particle/smoke_04.png");
}

void Weapon_PulseRifle::destroy()
{
	if (m_fireLoopHandle) { m_fireLoopHandle->stop(); m_fireLoopHandle->drop(); m_fireLoopHandle = nullptr; }

	for (auto& d : m_burnDecals)
	{
		if (d.node)
		{
			RenderManager::Get()->unregisterLDREffectNode(d.node);
			d.node->remove();
		}
	}
	m_burnDecals.clear();

	// Clean up laser beam node
	if (m_laserNode)
	{
		m_laserNode->remove();
		m_laserNode = nullptr;
	}

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

void Weapon_PulseRifle::update()
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
		return;
	}

	bool fireButtonPressed = InputManager::Get()->isMouseButtonPressed(MB_LEFT);
	const float fireRate = 100.0f;
	if (fireButtonPressed && (currentTime - m_lastFireTime) >= fireRate)
	{
		m_lastFireTime = currentTime;
		fire();
	}

	if (fireButtonPressed && !m_fireLoopHandle)
		m_fireLoopHandle = SoundManager::Get()->sound()->play2D("content/sound/weapon/pulse_rifle/fire.wav", true);

	if (!fireButtonPressed && m_fireLoopHandle)
	{
		m_fireLoopHandle->stop();
		m_fireLoopHandle->drop();
		m_fireLoopHandle = nullptr;
	}
}

void Weapon_PulseRifle::persist()
{
	float dt = Engine::Get()->getDeltaTime();
	float currentTime = Engine::Get()->getCurrentTime();

	bool fireHeld = InputManager::Get()->isMouseButtonPressed(MB_LEFT);

	// While firing, reset the flash timer so updateMuzzleFlash keeps it at full brightness
	if (fireHeld && m_muzzleStarNode && m_muzzleStarNode->isVisible())
		m_muzzleFlashTime = 0.0f;

	// Update muzzle flash effect (fades when timer is not being reset)
	updateMuzzleFlash(dt);

	// Drive the laser beam every frame while the fire button is held
	if (fireHeld && m_mesh.node && m_mesh.node->isVisible() && m_muzzleNode)
	{
		anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
		if (player.isValid() && player.hasComponent<CameraComponent>())
		{
			auto& camera = player.getComponent<CameraComponent>();

			// Force full hierarchy update: camera → weapon → bones.
			// Without this, getAbsolutePosition() returns last frame's value and
			// the beam lags behind the viewmodel during camera movement.
			camera.camera->updateAbsolutePosition();
			m_mesh.node->updateAbsolutePosition();
			m_mesh.node->animateJoints();
			m_muzzleNode->updateAbsolutePosition();
			irr::core::vector3df muzzlePos = m_muzzleNode->getAbsolutePosition();

			irr::core::vector3df forward = (camera.camera->getTarget() - camera.camera->getAbsolutePosition()).normalize();
			irr::core::vector3df rayEnd = muzzlePos + forward * 1000.0f;

			RaycastResultData hit = RenderManager::Get()->raycastWorldPosition(muzzlePos, rayEnd, true);
			createLaserBeam(muzzlePos, hit.hit ? hit.point : rayEnd);

			if (hit.hit && (currentTime - m_lastBurnDecalTime) >= m_burnDecalInterval)
			{
				createBurnDecal(hit.point, hit.normal);
				m_lastBurnDecalTime = currentTime;
			}
		}
	}
	else
	{
		updateLaserBeam(dt);
	}

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

	updateBurnDecals(currentTime);
}

void Weapon_PulseRifle::equip()
{
	m_mesh.node->setVisible(true);
	m_mesh.animation_call_back->hasAnimationEnded();
	m_mesh.node->setLoopMode(false);
	m_mesh.node->setFrameLoop(1, 20);
	m_isEquipping = true;
	m_isUnequipping = false;

	m_isReloadingAnim = false;
}

void Weapon_PulseRifle::unequip()
{
	m_isEquipping = false;
	m_isUnequipping = false;
	m_isReloadingAnim = false;
	if (m_fireLoopHandle) { m_fireLoopHandle->stop(); m_fireLoopHandle->drop(); m_fireLoopHandle = nullptr; }
	if (m_laserNode) m_laserNode->setVisible(false);
	m_mesh.node->setVisible(false);
}

void Weapon_PulseRifle::startUnequip()
{
	m_isUnequipping = true;
	m_isEquipping = false;
	m_isReloadingAnim = false;
	if (m_fireLoopHandle) { m_fireLoopHandle->stop(); m_fireLoopHandle->drop(); m_fireLoopHandle = nullptr; }
	if (m_laserNode) m_laserNode->setVisible(false);
	m_mesh.animation_call_back->hasAnimationEnded();
	m_mesh.node->setLoopMode(false);
	m_mesh.node->setFrameLoop(179, 190);
}

void Weapon_PulseRifle::idle()
{

}

void Weapon_PulseRifle::move()
{

}

void Weapon_PulseRifle::fire()
{
	// Raycast-based instant hit
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>())
		return;

	auto& camera = player.getComponent<CameraComponent>();

	if (!m_mesh.node || !m_muzzleNode)
	{
		if (!m_muzzleNode) spdlog::warn("Weapon_PulseRifle: muzzle node not found - cannot fire");
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
	irr::core::vector3df direction = (target - cameraPos).normalize();

	// Perform raycast from muzzle position in aim direction
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

	// Create muzzle flash effect
	createMuzzleFlash();
}

void Weapon_PulseRifle::reload()
{
	if (!m_isReloadingAnim)
	{
		m_mesh.node->setLoopMode(false);
		m_mesh.node->setFrameLoop(81, 95);
		m_isReloadingAnim = true;
	
	}
}


void Weapon_PulseRifle::createMuzzleFlash()
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
			irr::core::dimension2df(0.5f, 0.5f),
			flashOffset
		);

		m_muzzleStarNode->setMaterialFlag(irr::video::EMF_LIGHTING, false);
		m_muzzleStarNode->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE, false);
		m_muzzleStarNode->setMaterialFlag(irr::video::EMF_BLEND_OPERATION, true);
		m_muzzleStarNode->setMaterialType(m_muzzleFlashMaterialType);

		// Bind depth texture (texture unit 1)
		//m_muzzleStarNode->getMaterial(0).setTexture(1, RenderManager::Get()->renderer()->getMRT(2));
	}

	std::string starPath = "content/texture/particle/star_06.png";
	auto* starTex = RenderManager::Get()->driver()->getTexture(starPath.c_str());
	m_muzzleStarNode->setMaterialTexture(0, starTex);
	m_muzzleStarNode->setVisible(true);
	
	// Force immediate position update to prevent lag during fast camera movement
	m_muzzleStarNode->updateAbsolutePosition();

	// Tint star red for laser effect
	m_muzzleStarNode->getMaterial(0).AmbientColor  = irr::video::SColor(255, 255, 30, 30);
	m_muzzleStarNode->getMaterial(0).DiffuseColor  = irr::video::SColor(255, 255, 30, 30);
	m_muzzleStarNode->getMaterial(0).EmissiveColor = irr::video::SColor(255, 255, 30, 30);

	// Create/show red point light at muzzle if not exists
	if (!m_muzzleLightNode)
	{
		m_muzzleLightNode = RenderManager::Get()->sceneManager()->addLightSceneNode(
			m_muzzleNode,
			flashOffset,
			irr::video::SColorf(1.0f, 0.0f, 0.0f),  // Red light
			3.0f
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

void Weapon_PulseRifle::updateMuzzleFlash(float dt)
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

void Weapon_PulseRifle::createLaserBeam(const irr::core::vector3df& start, const irr::core::vector3df& end)
{
	if (!m_laserNode)
		return;

	irr::core::vector3df direction = end - start;
	float distance = direction.getLength();
	if (distance < 0.1f)
		return;

	direction.normalize();

	// Stretch the unit-length plane to match the shot distance
	m_laserNode->setScale(irr::core::vector3df(5.0f, 5.0f, distance));

	// Position at beam midpoint
	m_laserNode->setPosition(start + direction * (distance * 0.5f));

	// Align plane's Z axis with beam direction
	irr::core::vector3df rotation;
	rotation.Y = atan2f(direction.X, direction.Z) * (180.0f / 3.14159265f);
	rotation.X = -asinf(irr::core::clamp(direction.Y, -1.0f, 1.0f)) * (180.0f / 3.14159265f);
	rotation.Z = 0.0f;
	m_laserNode->setRotation(rotation);

	// Reset alpha and restart fade timer
	m_laserNode->getMaterial(0).AmbientColor.setAlpha(255);
	m_laserNode->getMaterial(0).DiffuseColor.setAlpha(255);
	m_laserFireTime = Engine::Get()->getCurrentTime();
	m_laserNode->setVisible(true);
}

void Weapon_PulseRifle::updateLaserBeam(float dt)
{
	if (!m_laserNode || !m_laserNode->isVisible())
		return;

	float elapsed = Engine::Get()->getCurrentTime() - m_laserFireTime;
	if (elapsed >= m_laserFadeDuration)
	{
		m_laserNode->setVisible(false);
		return;
	}

	float fadeProgress = elapsed / m_laserFadeDuration;
	irr::u32 alpha = static_cast<irr::u32>((1.0f - fadeProgress) * 255.0f);
	m_laserNode->getMaterial(0).AmbientColor.setAlpha(alpha);
	m_laserNode->getMaterial(0).DiffuseColor.setAlpha(alpha);
}

// ---------------------------------------------------------------------------
// initImpactSparkSystem
// ---------------------------------------------------------------------------
// Groups:
//   1. Flash    – large, very short-lived white-blue billboard burst at impact
//   2. Sparks   – thin blue sparks shooting outward, fade with gravity
//   3. Glow     – soft expanding glow that lingers briefly
// ---------------------------------------------------------------------------
void Weapon_PulseRifle::initImpactSparkSystem()
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

void Weapon_PulseRifle::createImpactEffect(const irr::core::vector3df& pos)
{
	if (m_impactSparkBaseID == SPK::NO_ID)
		return;

	SPK::System* system = SPK_Copy(SPK::System, m_impactSparkBaseID);
	IRRSystem* irrSys = static_cast<IRRSystem*>(system);
	if (irrSys)
	{
		irrSys->setVisible(true);
		irrSys->setPosition(pos);
		irrSys->setScale(irr::core::vector3df(3.0f, 3.0f, 3.0f));
		irrSys->updateAbsolutePosition();
	}

	m_impactSystems.push_back(system);
}

void Weapon_PulseRifle::createBurnDecal(const irr::core::vector3df& pos, const irr::core::vector3df& normal)
{
	// Drop oldest decal when pool is full
	if (static_cast<int>(m_burnDecals.size()) >= BURN_DECAL_MAX)
	{
		if (m_burnDecals.front().node)
		{
			RenderManager::Get()->unregisterLDREffectNode(m_burnDecals.front().node);
			m_burnDecals.front().node->remove();
		}
		m_burnDecals.pop_front();
	}

	auto* geo = RenderManager::Get()->sceneManager()->getGeometryCreator();
	irr::scene::IMesh* planeMesh = geo->createPlaneMesh(
		irr::core::dimension2df(0.25f, 0.25f), irr::core::dimension2du(1, 1));
	auto* node = RenderManager::Get()->sceneManager()->addMeshSceneNode(planeMesh);
	planeMesh->drop();
	if (!node) return;

	// Normalize and orient toward the camera — a backface ray hit (muzzle inside/near geometry)
	// returns a normal pointing into the surface, which would push the decal behind the mesh.
	irr::core::vector3df n = normal;
	n.normalize();
	auto* cam = RenderManager::Get()->sceneManager()->getActiveCamera();
	if (cam && n.dotProduct(cam->getAbsolutePosition() - pos) < 0.0f)
		n = -n;

	node->setPosition(pos + n * 0.02f);

	// Map the plane's +Y axis to the surface normal.
	// With Irrlicht setRotationDegrees(X,Y,Z), the Y row of the rotation matrix is
	// (sin(X)*sin(Y), cos(X), sin(X)*cos(Y)) — so X=acos(nY), Y=atan2(nX,nZ) is exact.
	// Z is left at 0: adding any roll to euler.Z distorts the surface normal direction.
	float alpha = acosf(irr::core::clamp(n.Y, -1.0f, 1.0f)) * irr::core::RADTODEG;
	float beta  = atan2f(n.X, n.Z) * irr::core::RADTODEG;
	node->setRotation(irr::core::vector3df(alpha, beta, 0.0f));

	// The plane mesh has zero local Y extent (all verts lie on Y=0). After rotation
	// onto a wall the world AABB collapses in one axis, failing Irrlicht's frustum
	// test — all decals on the same surface blink out simultaneously. Disable culling.
	node->setAutomaticCulling(irr::scene::EAC_OFF);

	auto& mat = node->getMaterial(0);
	mat.MaterialType          = m_burnDecalMat;
	mat.MaterialTypeParam     = 0.0f;
	mat.setTexture(0, m_burnTexture);
	mat.Lighting        = false;
	mat.ZWriteEnable    = false;
	mat.ZBuffer         = irr::video::ECFN_LESSEQUAL;
	mat.BackfaceCulling = false;

	BurnDecal decal;
	decal.node        = node;
	decal.spawnTime   = Engine::Get()->getCurrentTime();
	decal.maxLifetime = 8000.0f;
	m_burnDecals.push_back(decal);
	RenderManager::Get()->registerLDREffectNode(node);
}

void Weapon_PulseRifle::updateBurnDecals(float currentTime)
{
	for (auto it = m_burnDecals.begin(); it != m_burnDecals.end(); )
	{
		float burnTime = (currentTime - it->spawnTime) / it->maxLifetime;
		if (burnTime >= 1.0f)
		{
			if (it->node)
			{
				RenderManager::Get()->unregisterLDREffectNode(it->node);
				it->node->remove();
			}
			it = m_burnDecals.erase(it);
		}
		else
		{
			it->node->getMaterial(0).MaterialTypeParam = burnTime;
			++it;
		}
	}
}
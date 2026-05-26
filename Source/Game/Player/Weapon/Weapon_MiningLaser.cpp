#include "Weapon_MiningLaser.h"

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

void Weapon_MiningLaser::precache()
{
	ParticleManager::Get()->precache("laser_impact", _asset_psys("laser_impact"));
}

void Weapon_MiningLaser::init()
{
	m_descriptor.name = "Player_Weapon_MiningLaser";
	m_descriptor.id = _entity_null_value;

	m_viewPositionOffset = irr::core::vector3df(0.2850f, -0.0950f, 0.4250f);
	m_viewRotationOffset = irr::core::vector3df(0.00f, -1.00f, -3.50f);
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
		spdlog::warn("Weapon_MiningLaser: 'FIRESPOT' joint not found");

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
}

void Weapon_MiningLaser::destroy()
{
	if (m_fireLoopHandle) { m_fireLoopHandle->stop(); m_fireLoopHandle->drop(); m_fireLoopHandle = nullptr; }

	// Clean up laser beam node
	if (m_laserNode)
	{
		m_laserNode->remove();
		m_laserNode = nullptr;
	}

	RenderManager::Get()->unregisterViewmodelNode(m_mesh.node);
	m_mesh.node->remove();

	WorldManager::Get()->freeEntityID(m_descriptor.id);
}

void Weapon_MiningLaser::update()
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
	const float fireRate = 50.0f;
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

void Weapon_MiningLaser::persist()
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
		}
	}
	else
	{
		updateLaserBeam(dt);
	}
}

void Weapon_MiningLaser::equip()
{
	m_mesh.node->setVisible(true);
	m_mesh.animation_call_back->hasAnimationEnded();
	m_mesh.node->setLoopMode(false);
	m_mesh.node->setFrameLoop(1, 20);
	m_isEquipping = true;
	m_isUnequipping = false;

	m_isReloadingAnim = false;
}

void Weapon_MiningLaser::unequip()
{
	m_isEquipping = false;
	m_isUnequipping = false;
	m_isReloadingAnim = false;
	if (m_fireLoopHandle) { m_fireLoopHandle->stop(); m_fireLoopHandle->drop(); m_fireLoopHandle = nullptr; }
	if (m_laserNode) m_laserNode->setVisible(false);
	m_mesh.node->setVisible(false);
}

void Weapon_MiningLaser::startUnequip()
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

void Weapon_MiningLaser::idle()
{

}

void Weapon_MiningLaser::move()
{

}

void Weapon_MiningLaser::fire()
{
	// Raycast-based instant hit
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>())
		return;

	auto& camera = player.getComponent<CameraComponent>();

	if (!m_mesh.node || !m_muzzleNode)
	{
		if (!m_muzzleNode) spdlog::warn("Weapon_MiningLaser: muzzle node not found - cannot fire");
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
					damageComp.damageReceived += 1;
				}

				// Create impact spark particles at hit position with surface normal
				ParticleManager::Get()->spawn("laser_impact", SPK::IRR::irr2spk(raycastResult.point));
			}
		}
	}

	// Create muzzle flash effect
	createMuzzleFlash();
}

void Weapon_MiningLaser::reload()
{
	if (!m_isReloadingAnim)
	{
		m_mesh.node->setLoopMode(false);
		m_mesh.node->setFrameLoop(81, 95);
		m_isReloadingAnim = true;
	
	}
}


void Weapon_MiningLaser::createMuzzleFlash()
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

void Weapon_MiningLaser::updateMuzzleFlash(float dt)
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

void Weapon_MiningLaser::createLaserBeam(const irr::core::vector3df& start, const irr::core::vector3df& end)
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

void Weapon_MiningLaser::updateLaserBeam(float dt)
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

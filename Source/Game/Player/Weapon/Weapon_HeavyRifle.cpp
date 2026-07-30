#include "Weapon_HeavyRifle.h"

#include "Engine/Engine.h"
#include "Engine/Renderer/DecalManager.h"
#include "Utility/Utility.h"

#include "../CameraFX.h"

#undef MB_RIGHT

using namespace SPK;
using namespace SPK::IRR;

void Weapon_HeavyRifle::precache()
{
	ParticleManager::Get()->precache("spark", _asset_psys("spark"));
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/rifle/fire.wav", true);
}

void Weapon_HeavyRifle::init()
{
	m_descriptor.name = "Player_Weapon_HeavyRifle";
	m_descriptor.id = _entity_null_value;

	m_viewPositionOffset = irr::core::vector3df(0.1100f, -0.1450f, 0.2200f);
	m_viewRotationOffset = irr::core::vector3df(0.0f, 180.0f, 0.0f);
	m_viewScaleOffset = irr::core::vector3df(0.01f, 0.01f, 0.01f);

	m_mesh.mesh = _asset_glb("player/weapon/heavyrifle_animated");

	m_mesh.trimesh = RenderManager::Get()->loadMesh(m_mesh.mesh);
	if (!m_mesh.trimesh)
	{
		spdlog::warn("PlayerWeapon::init(): failed to load mesh \"{}\", stand-in mesh loaded", m_mesh.mesh);

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

	m_mesh.animationList.emplace_back(sAnimationData("equip",   163,   208,  false));
	m_mesh.animationList.emplace_back(sAnimationData("idle",    209,  209,  true));
	m_mesh.animationList.emplace_back(sAnimationData("move",    50,  79,  false));
	m_mesh.animationList.emplace_back(sAnimationData("fire",    0,  9,  false));
	m_mesh.animationList.emplace_back(sAnimationData("reload",  94,  152, false));
	m_mesh.animationList.emplace_back(sAnimationData("reload_empty", 10, 93, false));
	m_mesh.animationList.emplace_back(sAnimationData("unequip", 153, 163, false));

	playAnimation("idle"); // safe default until equip() runs

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
		spdlog::warn("Weapon_HeavyRifle: 'FIRESPOT' joint not found");

	m_crosshair = RenderManager::Get()->driver()->getTexture("content/texture/ui/crosshair/crosshair038.png");

	// Character sheet: medium rifle flash, oversized bolt casings, every-3rd tracer
	WeaponEffectsDesc fx;
	fx.muzzleJointName = "FIRESPOT";
	fx.flashColor      = irr::video::SColor(255, 255, 204, 76);
	fx.flashSize       = 0.8f;
	fx.lightColor      = irr::video::SColorf(1.0f, 0.8f, 0.2f);
	fx.tracerFrequency = 3;
	fx.shellMesh       = "content/mesh/prop/shells/shellmedium.obj";
	fx.shellEjectJoint = "BRASS";
	fx.shellScale      = 2.0f;
	fx.shellPoolSize   = 50;
	m_effects.init(m_mesh.node, fx);
}

void Weapon_HeavyRifle::destroy()
{
	m_effects.destroy();

	RenderManager::Get()->unregisterViewmodelNode(m_mesh.node);
	m_mesh.node->remove();

	WorldManager::Get()->freeEntityID(m_descriptor.id);
}

void Weapon_HeavyRifle::update()
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
			playAnimation("idle");
		}
		return;
	}

	if (m_isReloadingAnim)
	{
		if (animEnded)
		{
			m_isReloadingAnim = false;
			playAnimation("idle");
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
		playAnimation("idle");
	}

	RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
}

void Weapon_HeavyRifle::persist()
{
	m_effects.update(Engine::Get()->getDeltaTime());
}

void Weapon_HeavyRifle::equip()
{
	m_mesh.node->setVisible(true);
	m_mesh.animation_call_back->hasAnimationEnded();
	playAnimation("equip");
	m_isEquipping = true;
	m_isUnequipping = false;
	m_isPlayingFireAnim = false;
	m_isReloadingAnim = false;
}

void Weapon_HeavyRifle::unequip()
{
	m_isEquipping = false;
	m_isUnequipping = false;
	m_isPlayingFireAnim = false;
	m_isReloadingAnim = false;
	m_mesh.node->setVisible(false);
}

void Weapon_HeavyRifle::startUnequip()
{
	m_isUnequipping = true;
	m_isEquipping = false;
	m_isPlayingFireAnim = false;
	m_isReloadingAnim = false;
	m_mesh.animation_call_back->hasAnimationEnded();
	playAnimation("unequip");
}

void Weapon_HeavyRifle::idle()
{

}

void Weapon_HeavyRifle::move()
{

}

void Weapon_HeavyRifle::fire()
{
	playAnimation("fire");

	// Raycast-based instant hit
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>())
		return;

	auto& camera = player.getComponent<CameraComponent>();

	if (!m_mesh.node || !m_muzzleNode)
	{
		if (!m_muzzleNode) spdlog::warn("Weapon_HeavyRifle: muzzle node not found - cannot fire");
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

	// Converge on the crosshair aim point so muzzle-origin shots land on centre
	irr::core::vector3df direction = getAimDirection(muzzlePos);

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
				// Damage through the gameplay chokepoint; drives hitmarker/kill feedback
				registerHitFeedback(
					WorldManager::Get()->gameplaySystem()->damageEntity(hitDescriptor.id, 25));

				// Sparks fanned off the surface + bullet-hole decal
				m_effects.impact(raycastResult.point, raycastResult.normal);
			}
		}
		else if (RenderManager::isWorldGeometryNode(raycastResult.node))
		{
			// Brush chunks / props carry no ECS id — solid surface hit, nothing to damage
			m_effects.impact(raycastResult.point, raycastResult.normal);
		}
	}

	// Tracer segment toward the hit point (module fires every Nth shot)
	irr::core::vector3df tracerEnd = (raycastResult.hit && raycastResult.node) ?
		raycastResult.point : (muzzlePos + direction * 1000.0f);
	m_effects.spawnTracer(muzzlePos, tracerEnd);

	m_effects.muzzleFlash();
	m_effects.ejectShell();

	// Heavy-rifle kick — noticeably harder than the pistol, slower cadence earns it
	g_CameraFX.addRecoil(-1.8f, Engine::Get()->rng()->getFloat(-0.2f, 0.2f));
	addViewKick(
		irr::core::vector3df(0.0f, 0.02f, -0.07f),
		irr::core::vector3df(3.5f,
			Engine::Get()->rng()->getFloat(-0.4f, 0.4f),
			Engine::Get()->rng()->getFloat(-0.9f, 0.9f)));

	SoundManager::Get()->sound()->playRandomized2D("content/sound/weapon/rifle/fire", 0.05f, 3, 0.6f, "rifle_fire");
}

void Weapon_HeavyRifle::reload()
{
	if (!m_isReloadingAnim)
	{
		playAnimation("reload");
		m_isReloadingAnim = true;
		m_isPlayingFireAnim = false;
	}
}



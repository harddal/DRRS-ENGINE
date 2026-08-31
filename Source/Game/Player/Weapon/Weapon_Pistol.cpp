#include "Weapon_Pistol.h"

#include "Engine/Engine.h"
#include "Engine/Renderer/DecalManager.h"

#undef MB_RIGHT

#include "Engine/Engine.h"
#include "Utility/Utility.h"

#include "../CameraFX.h"

#undef MB_RIGHT

using namespace SPK;
using namespace SPK::IRR;

void Weapon_Pistol::precache()
{
	ParticleManager::Get()->precache("spark", _asset_psys("spark"));
}

void Weapon_Pistol::init()
{
	m_descriptor.name = "Player_Weapon_Pistol";
	m_descriptor.id = _entity_null_value;

	m_viewPositionOffset = irr::core::vector3df(0.3300f, -0.0100f, 0.6800f);
	m_viewRotationOffset = irr::core::vector3df(0.0f, 0.0f, 0.0f);
	m_viewScaleOffset = irr::core::vector3df(1.0f, 1.0f, 1.0f);

	m_mesh.mesh = "content/mesh/player/weapon/pistol/hud.b3d";

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

	//	RenderManager::Get()->renderer()->getMaterialSwapper()->swapMaterials(m_mesh.node);

	m_mesh.node->setMaterialFlag(irr::video::EMF_BILINEAR_FILTER, true);
	m_mesh.node->setMaterialFlag(irr::video::EMF_TRILINEAR_FILTER, true);
	m_mesh.node->setMaterialFlag(irr::video::EMF_ANISOTROPIC_FILTER, true);
	m_mesh.node->setMaterialFlag(irr::video::EMF_ANTI_ALIASING, true);
	m_mesh.node->setMaterialFlag(irr::video::EMF_USE_MIP_MAPS, true);

	m_mesh.fps = 30;
	m_mesh.node->setAnimationSpeed(30.0f);

	m_mesh.animationList.emplace_back(sAnimationData("equip",   1,   20,   false));
	m_mesh.animationList.emplace_back(sAnimationData("idle",    20,  50,  true));
	m_mesh.animationList.emplace_back(sAnimationData("move",    50,  79,  false));
	m_mesh.animationList.emplace_back(sAnimationData("fire",    81,  89,  false));
	m_mesh.animationList.emplace_back(sAnimationData("reload",  96,  179, false));
	m_mesh.animationList.emplace_back(sAnimationData("unequip", 179, 190, false));

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
		spdlog::warn("Weapon_Pistol: 'FIRESPOT' joint not found");

	m_crosshair = RenderManager::Get()->driver()->getTexture("content/texture/ui/crosshair/crosshair038.png");

	// Character sheet: small sharp flash, brass shells right off the slide, every-3rd tracer
	WeaponEffectsDesc fx;
	fx.muzzleJointName = "FIRESPOT";
	fx.flashColor      = irr::video::SColor(255, 255, 204, 76);
	fx.flashSize       = 0.3f;
	fx.lightColor      = irr::video::SColorf(1.0f, 0.8f, 0.2f);
	fx.tracerFrequency = 3;
	fx.shellMesh       = "content/mesh/prop/shells/shellsmall.obj";
	fx.shellEjectJoint = "BRASS";
	fx.shellPoolSize   = 80;
	m_effects.init(m_mesh.node, fx);
}

void Weapon_Pistol::destroy()
{
	m_effects.destroy();

	RenderManager::Get()->unregisterViewmodelNode(m_mesh.node);
	m_mesh.node->remove();

	WorldManager::Get()->freeEntityID(m_descriptor.id);
}

void Weapon_Pistol::update()
{
	if (!m_mesh.node || !m_mesh.node->isVisible())
		return;

	float currentTime = Engine::Get()->getCurrentTime();

	// Consume animation-end signal once per frame to avoid double-reads
	bool animEnded = m_mesh.animation_call_back->hasAnimationEnded();

	// State: unequip animation → hide (WeaponController completes switch next frame)
	// Kick spring recovery + transform are handled by updateWeaponSway().
	if (m_isUnequipping)
	{
		if (animEnded)
		{
			m_isUnequipping = false;
			m_mesh.node->setVisible(false);
		}
		return;
	}

	// State: equip animation → idle
	if (m_isEquipping)
	{
		if (animEnded)
		{
			m_isEquipping = false;
			playAnimation("idle");
		}
	}
	// State: reload animation → idle
	else if (m_isReloading)
	{
		if (animEnded)
		{
			m_isReloading = false;
			playAnimation("idle");
		}
	}
	// State: fire animation → idle. Firing is NOT blocked while this plays —
	// fire() restarts the frame loop so fast taps land at m_minFireInterval.
	else if (m_isFireAnim)
	{
		if (animEnded)
		{
			m_isFireAnim = false;
			playAnimation("idle");
		}
	}

	// Semi-auto: clear fire lock when mouse is released
	bool lmbPressed = InputManager::Get()->isMouseButtonPressed(MB_LEFT);
	if (!lmbPressed)
		m_firedThisPress = false;

	bool canFire = lmbPressed
		&& !m_firedThisPress
		&& !m_isReloading
		&& !m_isEquipping
		&& (currentTime - m_lastFireTime) >= m_minFireInterval;

	if (canFire)
	{
		m_lastFireTime = currentTime;
		m_firedThisPress = true;
		fire();
	}

	// Reload
	static bool r = false;
	if (InputManager::Get()->getKeyPressOnce(KEYBOARD_KEY::KEY_R, &r))
	{
		if (!m_isReloading && !m_isEquipping)
		{
			playAnimation("reload");
			m_isReloading = true;
			m_isFireAnim = false;
		}
	}

	RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
}

void Weapon_Pistol::persist()
{
	m_effects.update(Engine::Get()->getDeltaTime());
}

void Weapon_Pistol::equip()
{
	m_mesh.node->setVisible(true);

	// Consume any stale animation-end flag from before the weapon was hidden
	m_mesh.animation_call_back->hasAnimationEnded();

	playEquipSound();

	playAnimation("equip");
	m_isEquipping = true;
	m_isFireAnim = false;
	m_isReloading = false;
	m_firedThisPress = false;
	resetViewKick();
}

void Weapon_Pistol::unequip()
{
	m_isUnequipping = false;
	m_isFireAnim = false;
	m_isReloading = false;
	m_isEquipping = false;
	m_mesh.node->setVisible(false);
}

void Weapon_Pistol::startUnequip()
{
	m_isUnequipping = true;
	m_isFireAnim = false;
	m_isReloading = false;
	m_isEquipping = false;
	m_firedThisPress = true; // block fire input during unequip

	playUnequipSound();

	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag
	playAnimation("unequip");
}

void Weapon_Pistol::idle()
{

}

void Weapon_Pistol::move()
{

}

void Weapon_Pistol::fire()
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>() || !m_mesh.node || !m_muzzleNode)
	{
		if (!m_muzzleNode) spdlog::warn("Weapon_Pistol: muzzle node not found - cannot fire");
		return;
	}

	// Trigger skeletal fire animation (restarts if already playing — keeps taps snappy)
	playAnimation("fire");
	m_isFireAnim = true;

	// Programmatic kick layered on top — instant snap, spring recovers in updateWeaponSway()
	addViewKick(
		irr::core::vector3df(0.0f, 0.02f, -0.05f),                    // subtle rise, push back into screen
		irr::core::vector3df(5.0f, 0.0f,                              // muzzle pitches up
			Engine::Get()->rng()->getFloat(-0.8f, 0.8f)));            // micro roll variation

	auto& camera = player.getComponent<CameraComponent>();

	// Force full hierarchy update so bone world positions are current
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

	// Converge on the crosshair aim point — the muzzle sits below-right of screen
	// centre, so a plain camera-forward ray would land offset from the crosshair
	irr::core::vector3df direction = getAimDirection(muzzlePos);

	// Apply random offset in right and down directions
	float spreadRight = Engine::Get()->rng()->getFloat(-m_spread, m_spread);
	float spreadDown = Engine::Get()->rng()->getFloat(-m_spread, m_spread);
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
					WorldManager::Get()->gameplaySystem()->damageEntity(hitDescriptor.id, m_damage, DAMAGE_TYPE::DEFAULT,
						DamageContext::fromImpact(raycastResult.point, raycastResult.normal,
							raycastResult.ray.getVector())));

				// Sparks and a bullet hole are for hard surfaces. Anything carrying a
				// damage receiver is flesh as far as feedback goes, and GoreManager has
				// already covered it.
				if (!hitEntity.hasComponent<DamageReceiverComponent>())
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

	// Hard camera kick -- conveys weight on each shot
	float recoilYaw = Engine::Get()->rng()->getFloat(-0.12f, 0.12f);
	g_CameraFX.addRecoil(-1.5f, recoilYaw);

	SoundManager::Get()->sound()->playRandomized2D("content/sound/weapon/pistol/fire", 0.06f, 4, -1.0f, "pistol_fire");
}

void Weapon_Pistol::reload()
{

}



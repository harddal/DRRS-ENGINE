#include "Weapon_Shotgun.h"

#include "Engine/Engine.h"
#include "../CameraFX.h"

#undef MB_RIGHT

using namespace irr;
using namespace SPK;
using namespace SPK::IRR;

void Weapon_Shotgun::precache()
{
	ParticleManager::Get()->precache("spark_smoke", _asset_psys("spark_smoke"));
}

void Weapon_Shotgun::init()
{
	m_descriptor.name = "Player_Weapon_Shotgun";
	m_descriptor.id = _entity_null_value;

	m_viewPositionOffset = irr::core::vector3df(0.2700f, -0.0700f, 0.4700f);
	m_viewRotationOffset = irr::core::vector3df(0.0f, 0.0f, 0.0f);
	m_viewScaleOffset    = irr::core::vector3df(1.0f, 1.0f, 1.0f);

	m_mesh.mesh = "content/mesh/player/weapon/shotgun/hud.b3d";

	m_mesh.trimesh = RenderManager::Get()->sceneManager()->getMesh(m_mesh.mesh.c_str());
	if (!m_mesh.trimesh)
	{
		spdlog::warn("In function Weapon_Shotgun::init() -> getMesh() : Shotgun mesh not found, stand-in loaded");
		m_mesh.trimesh = RenderManager::Get()->sceneManager()->getMesh("content/mesh/primitive/double_tetrahedron.obj");
	}

	m_mesh.node = RenderManager::Get()->sceneManager()->addAnimatedMeshSceneNode(m_mesh.trimesh, nullptr, m_descriptor.id);

	if (!m_mesh.trimesh)
	{
		auto* t = RenderManager::Get()->driver()->getTexture("content/texture/color/magenta.png");
		m_mesh.node->setMaterialTexture(0, t);
	}

	m_mesh.fps = 30;
	m_mesh.node->setAnimationSpeed(static_cast<irr::f32>(m_mesh.fps));
	m_mesh.node->setLoopMode(true);
	m_mesh.node->setFrameLoop(20, 50);
	m_mesh.node->setJointMode(irr::scene::EJUOR_READ);

	m_mesh.animation_call_back = std::make_shared<AnimationCallback>();
	m_mesh.node->setAnimationEndCallback(m_mesh.animation_call_back.get());

	m_mesh.animationList.emplace_back(sAnimationData("equip",   1,   20,  false));
	m_mesh.animationList.emplace_back(sAnimationData("idle",    20,  50,  true));
	m_mesh.animationList.emplace_back(sAnimationData("move",    50,  79,  false));
	m_mesh.animationList.emplace_back(sAnimationData("fire",    81,  95,  false));
	m_mesh.animationList.emplace_back(sAnimationData("reload",  96,  179, false));
	m_mesh.animationList.emplace_back(sAnimationData("unequip", 179, 190, false));

	m_mesh.node->setScale(m_viewScaleOffset);

	auto perpixelMat = ShaderMaterialManager::get("phong_perpixel");
	if (perpixelMat != irr::video::EMT_SOLID)
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
		spdlog::error("In function Weapon_Shotgun::init() -> getEntityByName(\"player\") : Entity 'player' does not exist");
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
		spdlog::error("In function Weapon_Shotgun::init() -> player.getComponent<CameraComponent>() : Entity 'player' does not have CameraComponent");
	}

	RenderManager::Get()->registerViewmodelNode(m_mesh.node);
	m_mesh.node->setVisible(false);

	m_crosshair = RenderManager::Get()->driver()->getTexture("content/texture/ui/crosshair/crosshair001.png");

	// Character sheet: big smoky blast (first muzzle flash this weapon has ever had),
	// slug shells out the side port, smoke-heavy pellet impacts, no tracers
	WeaponEffectsDesc fx;
	fx.muzzleJointName      = "FIRESPOT"; // falls back to weapon node + offset if the model lacks it
	fx.muzzleFallbackOffset = irr::core::vector3df(0.0f, 0.05f, 0.9f);
	fx.flashColor      = irr::video::SColor(255, 255, 214, 110);
	fx.flashSize       = 1.0f;
	fx.flashDuration   = 60.0f;
	fx.lightColor      = irr::video::SColorf(1.0f, 0.8f, 0.3f);
	fx.lightRadius     = 4.0f;
	fx.tracerPoolSize  = 0;
	fx.shellMesh       = "content/mesh/prop/shells/slug.obj";
	fx.shellEjectJoint = nullptr; // offset-mode eject port (right, up, forward)
	fx.shellEjectOffset = irr::core::vector3df(0.15f, 0.05f, -0.1f);
	fx.shellSpeed      = 6.0f;
	fx.shellPoolSize   = 32;
	fx.shellBounceSoundBase = "content/sound/prop/shotgunshell";
	fx.impactParticle  = "spark_smoke";
	fx.impactDecalSize = 0.15f; // smaller per-pellet holes, clustered
	m_effects.init(m_mesh.node, fx);
}

void Weapon_Shotgun::destroy()
{
	m_effects.destroy();

	RenderManager::Get()->unregisterViewmodelNode(m_mesh.node);
	m_mesh.node->remove();
	WorldManager::Get()->freeEntityID(m_descriptor.id);
}

void Weapon_Shotgun::update()
{
	bool animEnded = m_mesh.animation_call_back->hasAnimationEnded();

	// Unequip: wait for anim to finish, then hide.
	// Kick recovery keeps ticking via updateWeaponSway() in WeaponController.
	if (m_isUnequipping)
	{
		if (animEnded)
		{
			m_isUnequipping = false;
			m_mesh.node->setVisible(false);
		}
		return;
	}

	// Equip: wait for anim to finish, then loop idle
	if (m_isEquipping)
	{
		if (animEnded)
		{
			m_isEquipping = false;
			m_mesh.node->setLoopMode(true);
			m_mesh.node->setFrameLoop(20, 50);
		}
		RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
		return;
	}

	// Fire or reload anim playing: wait for completion
	if (m_isAnimating)
	{
		if (animEnded)
		{
			m_isAnimating = false;
			m_mesh.node->setLoopMode(true);
			m_mesh.node->setFrameLoop(20, 50);
		}
		RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
		return;
	}

	// Semi-auto: reset fire flag when mouse released
	if (!InputManager::Get()->isMouseButtonPressed(MB_LEFT))
		m_firedThisPress = false;

	if (InputManager::Get()->isMouseButtonPressed(MB_LEFT) && !m_firedThisPress)
		fire();

	// Reload via R key
	static bool r = false;
	if (InputManager::Get()->getKeyPressOnce(KEYBOARD_KEY::KEY_R, &r))
		reload();

	// Recoil recovery + node transform are handled by updateWeaponSway()

	RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
}

void Weapon_Shotgun::persist()
{
	m_effects.update(Engine::Get()->getDeltaTime());

	// Pump-rack mechanical layer — the second half of the fire sound
	if (m_pumpPending && Engine::Get()->getCurrentTime() >= m_pumpTime)
	{
		m_pumpPending = false;
		SoundManager::Get()->sound()->play2D(
			"content/sound/weapon/shotgun/Shotgun_Quick Pump_01.wav",
			false, 0, -1.0f, nullptr, false,
			1.0f + Engine::Get()->rng()->getFloat(-0.03f, 0.03f));
	}
}

void Weapon_Shotgun::equip()
{
	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag
	m_isEquipping   = true;
	m_isUnequipping = false;
	m_isAnimating   = false;
	m_firedThisPress = false;
	resetViewKick();

	m_mesh.node->setLoopMode(false);
	m_mesh.node->setFrameLoop(1, 20);
	m_mesh.node->setPosition(m_viewPositionOffset);
	m_mesh.node->setRotation(m_viewRotationOffset);
	m_mesh.node->setVisible(true);
}

void Weapon_Shotgun::unequip()
{
	m_isEquipping   = false;
	m_isUnequipping = false;
	m_isAnimating   = false;
	m_pumpPending   = false;
	m_mesh.node->setVisible(false);
}

void Weapon_Shotgun::startUnequip()
{
	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag
	m_isUnequipping  = true;
	m_isAnimating    = false;
	m_pumpPending    = false;
	m_firedThisPress = true; // block fire during transition

	m_mesh.node->setLoopMode(false);
	m_mesh.node->setFrameLoop(179, 190);
}

void Weapon_Shotgun::idle()
{
}

void Weapon_Shotgun::move()
{
}

void Weapon_Shotgun::fire()
{
	if (m_isAnimating)
		return;

	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");

	if (!player.isValid() || !player.hasComponent<CameraComponent>())
		return;

	auto& camera = player.getComponent<CameraComponent>();

	int currentTime = static_cast<int>(Engine::Get()->getCurrentTime());
	if (currentTime - m_lastFireTime < static_cast<int>(m_fireRate))
		return;

	m_lastFireTime = currentTime;
	m_firedThisPress = true;

	// Build camera basis vectors for spread calculation
	irr::core::vector3df camPos    = camera.camera->getAbsolutePosition();
	irr::core::vector3df camTarget = camera.camera->getTarget();
	irr::core::vector3df forward   = camTarget - camPos;
	forward.normalize();

	irr::core::vector3df worldUp(0.0f, 1.0f, 0.0f);
	irr::core::vector3df right = forward.crossProduct(worldUp);
	right.normalize();
	irr::core::vector3df up = right.crossProduct(forward);
	up.normalize();

	const float spreadRad = m_spreadAngle * 3.14159f / 180.0f;

	// Aggregate pellet results so a blast produces ONE feedback event, not eight
	HIT_RESULT bestResult = HIT_RESULT::NONE;

	for (int i = 0; i < m_pelletCount; i++)
	{
		float rx = ((rand() / (float)RAND_MAX) * 2.0f - 1.0f) * spreadRad;
		float ry = ((rand() / (float)RAND_MAX) * 2.0f - 1.0f) * spreadRad;

		irr::core::vector3df pelletDir = forward + right * rx + up * ry;
		pelletDir.normalize();

		irr::core::vector3df pelletTarget = camPos + pelletDir * 1000.0f;

		auto raycastResult = RenderManager::Get()->raycastWorldPosition(camPos, pelletTarget, true);

		if (raycastResult.hit && raycastResult.node)
		{
			entityid hitID = raycastResult.node->getID();

			HIT_RESULT r = WorldManager::Get()->gameplaySystem()->damageEntity(hitID, static_cast<unsigned int>(m_damagePerPellet));
			if (static_cast<int>(r) > static_cast<int>(bestResult))
				bestResult = r;

			// Smoke-heavy impact fanned off the surface + per-pellet bullet hole
			m_effects.impact(raycastResult.point, raycastResult.normal);
		}
	}

	registerHitFeedback(bestResult);

	m_effects.muzzleFlash();
	m_effects.ejectShell();

	// Heavy recoil kick
	float verticalRecoil   = m_recoilAmount + Engine::Get()->rng()->getFloat(-m_recoilRandomnessVertical, m_recoilRandomnessVertical);
	float horizontalRecoil = Engine::Get()->rng()->getFloat(-m_recoilRandomnessHorizontal, m_recoilRandomnessHorizontal);

	addViewKick(
		irr::core::vector3df(0.0f, m_recoilPositionKick, 0.0f),
		irr::core::vector3df(-verticalRecoil, horizontalRecoil, 0.0f));

	// Camera shake + FOV punch for the big kick
	g_CameraFX.addRecoil(verticalRecoil * 0.4f, horizontalRecoil * 0.2f);
	g_CameraFX.addShake(1.5f, 120.0f);
	g_CameraFX.addFovKick(2.0f);

	SoundManager::Get()->sound()->playRandomized2D("content/sound/weapon/shotgun/fire", 0.04f);

	// Queue the pump-rack mechanical layer for ~400ms after the blast (mid fire-anim)
	m_pumpPending = true;
	m_pumpTime    = currentTime + m_pumpDelay;
}

void Weapon_Shotgun::reload()
{
	if (m_isAnimating)
		return;

	m_mesh.node->setLoopMode(false);
	m_mesh.node->setFrameLoop(96, 179);
	m_isAnimating = true;
}


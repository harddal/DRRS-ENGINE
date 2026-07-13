#include "Weapon_Melee.h"

#include "Engine/Engine.h"

#include "../CameraFX.h"

#undef MB_RIGHT

using namespace SPK;
using namespace SPK::IRR;

void Weapon_Melee::precache()
{
	ParticleManager::Get()->precache("spark", _asset_psys("spark"));
}

void Weapon_Melee::init()
{
	m_descriptor.name = "Player_Weapon_Melee";
	m_descriptor.id = _entity_null_value;

	m_viewPositionOffset = irr::core::vector3df(0.0f, 0.0f, 0.0f);
	m_viewRotationOffset = irr::core::vector3df(0.0f, 0.0f, 0.0f);
	m_viewScaleOffset    = irr::core::vector3df(1.0f, 1.0f, 1.0f);

	m_mesh.mesh = _asset_b3d("player/weapon/sword/HUD");

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

//	RenderManager::Get()->renderer()->getMaterialSwapper()->swapMaterials(m_mesh.node);

	m_mesh.animationList.emplace_back(sAnimationData("equip",   205, 211, false));
	m_mesh.animationList.emplace_back(sAnimationData("idle",    211, 229, false));
	m_mesh.animationList.emplace_back(sAnimationData("move",    232, 256, false));
	m_mesh.animationList.emplace_back(sAnimationData("fire1",   266, 273, false));
	m_mesh.animationList.emplace_back(sAnimationData("fire2",   276, 282, false));
	m_mesh.animationList.emplace_back(sAnimationData("fire3",   296, 308, false));
	m_mesh.animationList.emplace_back(sAnimationData("unequip", 393, 399, false));

	m_mesh.fps = 20;
	m_mesh.node->setAnimationSpeed(static_cast<irr::f32>(m_mesh.fps));
	m_mesh.node->setLoopMode(false);
	m_mesh.node->setFrameLoop(0, 0);

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
}

void Weapon_Melee::destroy()
{
	RenderManager::Get()->unregisterViewmodelNode(m_mesh.node);
	m_mesh.node->remove();

	WorldManager::Get()->freeEntityID(m_descriptor.id);
}

void Weapon_Melee::update()
{
	if (!m_mesh.node || !m_mesh.node->isVisible())
		return;

	bool animEnded = m_mesh.animation_call_back->hasAnimationEnded();

	if (m_isSwinging)
	{
		// The strike lands when the blade visually reaches the target
		if (!m_damageDone && m_mesh.node->getFrameNr() >= static_cast<irr::f32>(m_contactFrame))
		{
			m_damageDone = true;
			performStrike();
		}

		if (animEnded)
		{
			m_isSwinging = false;
			idle();
		}

		// Committed to the swing — no new attacks until it finishes
		return;
	}

	// Re-loop the idle animation when it runs out
	if (animEnded)
		idle();

	// Attack input
	static bool ml = false, mr = false;
	if (InputManager::Get()->getMousePressOnce(MOUSE_BUTTON::MB_LEFT, &ml))
	{
		// Light attack — two swing variants
		if (Engine::Get()->rng()->getInt(0, 1) == 0)
			startSwing(266, 273, 270, 10);
		else
			startSwing(276, 282, 279, 10);
	}
	else if (InputManager::Get()->getMousePressOnce(MOUSE_BUTTON::MB_RIGHT, &mr))
	{
		// Heavy attack — longer wind-up, heavier hit
		startSwing(296, 308, 303, 25);
	}
}

void Weapon_Melee::equip()
{
	m_mesh.node->setVisible(true);

	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag
	m_isSwinging = false;
	m_damageDone = false;
	idle();
}

void Weapon_Melee::unequip()
{
	m_mesh.node->setVisible(false);
}

void Weapon_Melee::idle()
{
	m_mesh.node->setLoopMode(false);
	m_mesh.node->setFrameLoop(211, 229);
}

void Weapon_Melee::move()
{
	
}

void Weapon_Melee::fire()
{

}

void Weapon_Melee::startSwing(int startFrame, int endFrame, int contactFrame, unsigned int damage)
{
	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag

	m_mesh.node->setLoopMode(false);
	m_mesh.node->setFrameLoop(startFrame, endFrame);

	m_isSwinging   = true;
	m_damageDone   = false;
	m_contactFrame = contactFrame;
	m_attackDamage = damage;

	// Anticipation: whoosh + forward lunge at swing START — the payoff
	// (damage/impact) comes later, on the contact frame
	SoundManager::Get()->sound()->playRandomized2D("content/sound/weapon/sword/fire", 0.08f);
	g_CameraFX.addRecoil(0.8f, Engine::Get()->rng()->getFloat(-0.2f, 0.2f));
	addViewKick(
		irr::core::vector3df(0.0f, -0.01f, 0.05f),
		irr::core::vector3df(-2.0f, 0.0f, Engine::Get()->rng()->getFloat(-1.0f, 1.0f)));
}

void Weapon_Melee::performStrike()
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>())
		return;

	auto& camera = player.getComponent<CameraComponent>();
	irr::core::vector3df cameraPos = camera.camera->getAbsolutePosition();
	irr::core::vector3df forward = (camera.camera->getTarget() - cameraPos).normalize();

	irr::core::vector3df rayEnd = cameraPos + forward * 2.0f;

	RaycastResultData raycastResult = RenderManager::Get()->raycastWorldPosition(cameraPos, rayEnd, true);

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
				HIT_RESULT result = WorldManager::Get()->gameplaySystem()->damageEntity(
					hitDescriptor.id, m_attackDamage);
				registerHitFeedback(result);

				// Create impact spark particles at hit position
				ParticleManager::Get()->spawn("spark", SPK::IRR::irr2spk(raycastResult.point));

				if (result != HIT_RESULT::NONE)
				{
					// Connect crunch: impact thunk (placeholder — pitched-down
					// whoosh until sword/impact1..N.wav exists), camera bite,
					// and the hit-stop payoff
					SoundManager::Get()->sound()->play2D(
						"content/sound/weapon/sword/fire.wav",
						false, 0, 0.5f, nullptr, false, 0.7f);
					g_CameraFX.addShake(0.8f, 100.0f);
					g_CameraFX.addRecoil(0.6f, 0.0f);

					Engine::Get()->requestHitStop(result == HIT_RESULT::KILL ? 70.0f : 35.0f);
				}
			}
		}
		else if (RenderManager::isWorldGeometryNode(raycastResult.node))
		{
			// Brush chunks / props carry no ECS id — clang off the surface
			ParticleManager::Get()->spawn("spark", SPK::IRR::irr2spk(raycastResult.point));
		}
	}
}

void Weapon_Melee::reload()
{

}
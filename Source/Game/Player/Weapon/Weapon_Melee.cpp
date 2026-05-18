#include "Weapon_Melee.h"

#include "Engine/Engine.h"

#undef MB_RIGHT

void Weapon_Melee::precache()
{

}

void Weapon_Melee::init()
{
	m_descriptor.name = "Player_Weapon_Melee";
	m_descriptor.id = _entity_null_value;

	m_viewPositionOffset = irr::core::vector3df(0.3f, 0.0f, 0.5f);
	m_viewRotationOffset = irr::core::vector3df(0.0f, 0.0f, 0.0f);
	m_viewScaleOffset    = irr::core::vector3df(0.25f, 0.25f, 0.25f);

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
	if (m_mesh.animation_call_back->hasAnimationEnded())
	{
		this->idle();
	}

	this->fire();
}

void Weapon_Melee::equip()
{
	m_mesh.node->setVisible(true);
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
	m_mesh.node->setLoopMode(false);

	bool alt = false;
	static bool ml = false, mr = false;
	if (InputManager::Get()->getMousePressOnce(MOUSE_BUTTON::MB_LEFT, &ml))
	{
		switch (rand() % 2)
		{
		case 0:
			m_mesh.node->setFrameLoop(266, 273);
			break;
		case 1:
			m_mesh.node->setFrameLoop(276, 282);
			break;
		default:
			m_mesh.node->setFrameLoop(266, 273);
			break;
		}
	}
	if (InputManager::Get()->getMousePressOnce(MOUSE_BUTTON::MB_RIGHT, &mr))
	{
		m_mesh.node->setFrameLoop(296, 308);

		alt = true;
	}
}

void Weapon_Melee::reload()
{

}
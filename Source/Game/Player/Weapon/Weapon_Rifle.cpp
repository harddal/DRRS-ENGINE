#include "Weapon_Rifle.h"

#include "Engine/Engine.h"

#include <random>

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

	m_viewPositionOffset = irr::core::vector3df(0.25f, -0.3f, 0.6f);
	m_viewRotationOffset = irr::core::vector3df(0.0f, 180.0f, 0.0f);
	m_viewScaleOffset = irr::core::vector3df(0.75f, 0.75f, 0.75f);

	m_mesh.mesh = "content/mesh/player/weapon/plasma_rifle/plasma_rifle.b3d";

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

	auto* t = RenderManager::Get()->driver()->getTexture("content/mesh/player/weapon/plasma_rifle/plasma_rifle.png");
	m_mesh.node->setMaterialTexture(0, t);

	// Set nearest-neighbor filtering for pixelated effect
	m_mesh.node->setMaterialFlag(irr::video::EMF_BILINEAR_FILTER, false);
	m_mesh.node->setMaterialFlag(irr::video::EMF_TRILINEAR_FILTER, false);

//	RenderManager::Get()->renderer()->getMaterialSwapper()->swapMaterials(m_mesh.node);

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

	// Build SPARK templates
	initPlasmaSparkSystem();
	initImpactSparkSystem();

	m_muzzleFlashMaterialType = irr::video::EMT_TRANSPARENT_ADD_COLOR;

	m_crosshair = RenderManager::Get()->driver()->getTexture("content/texture/ui/crosshair/crosshair039.png");
}

void Weapon_Rifle::destroy()
{
	// Clean up all projectiles and their SPARK systems
	for (auto& proj : m_projectiles)
	{
		if (proj.sparkSystem)
		{
			SPK_Destroy(proj.sparkSystem);
			proj.sparkSystem = nullptr;
		}

		if (proj.entity.isValid() && proj.entity.hasComponent<DescriptorComponent>())
		{
			proj.entity.getComponent<DescriptorComponent>().isAlive = false;
		}
	}
	m_projectiles.clear();

	// Destroy active impact systems
	for (auto* sys : m_impactSystems)
		SPK_Destroy(sys);
	m_impactSystems.clear();

	if (m_impactSparkBaseID != SPK::NO_ID)
	{
		SPK_Destroy(SPK_Get(SPK::System, m_impactSparkBaseID));
		m_impactSparkBaseID = SPK::NO_ID;
	}

	// Destroy the template SPARK system
	if (m_projectileSparkBaseID != SPK::NO_ID)
	{
		SPK_Destroy(SPK_Get(SPK::System, m_projectileSparkBaseID));
		m_projectileSparkBaseID = SPK::NO_ID;
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
	float dt = Engine::Get()->getDeltaTime();

	// Check if fire button is held
	bool fireButtonPressed = InputManager::Get()->isMouseButtonPressed(MB_LEFT);
	bool altFireButtonPressed = InputManager::Get()->isMouseButtonPressed(MB_RIGHT);

	// Automatic fire with rate limiting
	if (fireButtonPressed && !altFireButtonPressed)
	{
		if (currentTime - m_lastFireTime >= m_fireRate)
		{
			fire();
			m_lastFireTime = currentTime;
		}
	}

	// Automatic fire with rate limiting
	if (!fireButtonPressed && altFireButtonPressed)
	{
		if (currentTime - m_lastFireTime >= m_fireRateAlt)
		{
			fire();
			m_lastFireTime = currentTime;
		}
	}

	RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
}

void Weapon_Rifle::persist()
{
	float dt = Engine::Get()->getDeltaTime();

	// Update all active projectiles
	updateProjectiles(dt);

	// Update muzzle flash effect
	updateMuzzleFlash(dt);

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
}

void Weapon_Rifle::unequip()
{
	m_mesh.node->setVisible(false);
}

void Weapon_Rifle::idle()
{

}

void Weapon_Rifle::move()
{

}

void Weapon_Rifle::fire()
{
	spawnProjectile();
}

void Weapon_Rifle::spawnProjectile()
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>())
		return;

	auto& camera = player.getComponent<CameraComponent>();

	if (!m_mesh.node)
		return;

	irr::scene::IBoneSceneNode* muzzleBone = m_mesh.node->getJointNode("Muzzle");
	if (!muzzleBone)
	{
		spdlog::warn("Muzzle bone not found on weapon model - cannot spawn projectile");
		return;
	}

	irr::core::vector3df spawnPos = muzzleBone->getAbsolutePosition();

	irr::core::vector3df target    = camera.camera->getTarget();
	irr::core::vector3df cameraPos = camera.camera->getAbsolutePosition();
	irr::core::vector3df forward   = (target - cameraPos).normalize();

	irr::core::vector3df up(0, 1, 0);
	irr::core::vector3df right = forward.crossProduct(up).normalize();
	irr::core::vector3df down  = right.crossProduct(forward).normalize();

	irr::core::vector3df direction = (target - spawnPos).normalize();

	static std::random_device rd;
	static std::mt19937 gen(rd());
	std::uniform_real_distribution<float> spreadDist(-m_recoil, m_recoil);

	float spreadRight = spreadDist(gen);
	float spreadDown  = spreadDist(gen);
	direction = (direction + right * spreadRight + down * spreadDown).normalize();

	// Create projectile entity (no mesh - SPARK provides the visual)
	anax::Entity projectileEntity = WorldManager::Get()->managerSystem()->getWorld().createEntity();

	projectileEntity.addComponent<DescriptorComponent>();
	auto& descriptor   = projectileEntity.getComponent<DescriptorComponent>();
	descriptor.id      = WorldManager::Get()->getNewID();
	descriptor.name    = "plasma_projectile_" + std::to_string(descriptor.id);
	descriptor.type    = ET_DYNAMIC;
	descriptor.isSerializable = false;

	projectileEntity.addComponent<TransformComponent>();
	auto& transform          = projectileEntity.getComponent<TransformComponent>();
	transform.position       = spawnPos;
	transform.initialPosition = spawnPos;
	transform.scale          = irr::core::vector3df(0.05f, 0.05f, 0.05f);

	projectileEntity.addComponent<RenderComponent>();
	projectileEntity.getComponent<RenderComponent>().isVisible = true;

	// Point light for environment glow
	projectileEntity.addComponent<LightComponent>();
	auto& light         = projectileEntity.getComponent<LightComponent>();
	light.type          = LT_POINT;
	light.visible       = true;
	light.radius        = 2.0f;
	light.color_diffuse = irr::video::SColorf(0.2f, 0.3f, 1.0f);
	light.offset        = irr::core::vector3df(0.0f, 0.0f, 0.0f);

	projectileEntity.activate();

	// Create projectile data and immediately attach a SPARK system
	WeaponProjectile proj;
	proj.entity          = projectileEntity;
	proj.velocity        = direction * proj.speed;
	proj.previousPosition = spawnPos;

	// Clone the SPARK template and position it at the muzzle
	if (m_projectileSparkBaseID != SPK::NO_ID)
	{
		SPK::System* system = SPK_Copy(SPK::System, m_projectileSparkBaseID);
		IRRSystem* irrSys   = static_cast<IRRSystem*>(system);
		if (irrSys)
		{
			irrSys->setVisible(true);
			irrSys->setPosition(spawnPos);
			irrSys->updateAbsolutePosition();
		}
		proj.sparkSystem = system;
	}

	m_projectiles.emplace_back(proj);

	SoundManager::Get()->sound()->play2D("content/sound/weapon/plasma_rifle/fire.wav", false);

	createMuzzleFlash();
}

void Weapon_Rifle::reload()
{

}

// ---------------------------------------------------------------------------
// initPlasmaSparkSystem
// ---------------------------------------------------------------------------
// Builds the shared SPARK template that is cloned once per projectile.
//
// Groups:
//   1. Core glow  – tight cluster of bright white-blue billboards at the bolt
//                   head; very short lifetime so they stay centred on the bolt.
//   2. Trail sparks – direction-aligned streaks emitted at the current bolt
//                   position with near-zero velocity; they linger behind as
//                   the bolt moves forward.
// ---------------------------------------------------------------------------
void Weapon_Rifle::initPlasmaSparkSystem()
{
	auto* driver = RenderManager::Get()->driver();
	auto* smgr   = RenderManager::Get()->sceneManager();

	// -----------------------------------------------------------------------
	// Renderers
	// -----------------------------------------------------------------------

	// Core: circular glow billboard, additive
	IRRQuadRenderer* coreRenderer = IRRQuadRenderer::create(RenderManager::Get()->device());
	coreRenderer->setTexturingMode(TEXTURE_2D);
	coreRenderer->setTexture(driver->getTexture("content/texture/particle/star_09.png"));
	coreRenderer->setBlending(BLENDING_ADD);
	coreRenderer->enableRenderingHint(DEPTH_WRITE, false);
	coreRenderer->setShared(true);

	// Trail: billboard sparks, additive
	IRRQuadRenderer* trailRenderer = IRRQuadRenderer::create(RenderManager::Get()->device());
	trailRenderer->setTexturingMode(TEXTURE_2D);
	trailRenderer->setTexture(driver->getTexture("content/texture/particle/star_09.png"));
	trailRenderer->setBlending(BLENDING_ADD);
	trailRenderer->enableRenderingHint(DEPTH_WRITE, false);
	trailRenderer->setShared(true);

	// -----------------------------------------------------------------------
	// Models
	// -----------------------------------------------------------------------

	// Core: white-blue, fades from 1 → 0, random size in [0.18, 0.32]
	Model* coreModel = Model::create(
		FLAG_RED | FLAG_GREEN | FLAG_BLUE | FLAG_ALPHA | FLAG_SIZE,
		FLAG_ALPHA,
		FLAG_SIZE | FLAG_RED | FLAG_GREEN);

	coreModel->setParam(PARAM_RED,   0.75f, 1.0f);
	coreModel->setParam(PARAM_GREEN, 0.80f, 1.0f);
	coreModel->setParam(PARAM_BLUE,  1.0f,  1.0f);
	coreModel->setParam(PARAM_ALPHA, 1.0f,  0.0f);
	coreModel->setParam(PARAM_SIZE,  0.18f, 0.32f);
	coreModel->setLifeTime(0.05f, 0.09f);
	coreModel->setShared(true);

	// Trail: deep blue fading in colour and alpha, random size in [0.05, 0.12]
	Model* trailModel = Model::create(
		FLAG_RED | FLAG_GREEN | FLAG_BLUE | FLAG_ALPHA | FLAG_SIZE,
		FLAG_ALPHA | FLAG_RED | FLAG_GREEN,
		FLAG_SIZE);

	trailModel->setParam(PARAM_RED,   0.25f, 0.05f);
	trailModel->setParam(PARAM_GREEN, 0.50f, 0.10f);
	trailModel->setParam(PARAM_BLUE,  1.0f,  0.80f);
	trailModel->setParam(PARAM_ALPHA, 0.85f, 0.0f);
	trailModel->setParam(PARAM_SIZE,  0.05f, 0.12f);
	trailModel->setLifeTime(0.18f, 0.35f);
	trailModel->setShared(true);

	// -----------------------------------------------------------------------
	// Zones
	// -----------------------------------------------------------------------

	Sphere* coreZone  = Sphere::create(Vector3D(0.0f, 0.0f, 0.0f), 0.025f);
	Point*  trailZone = Point::create(Vector3D(0.0f, 0.0f, 0.0f));

	// -----------------------------------------------------------------------
	// Emitters
	// -----------------------------------------------------------------------

	// Core: continuous, isotropic burst from tiny sphere
	StaticEmitter* coreEmitter = StaticEmitter::create();
	coreEmitter->setZone(coreZone);
	coreEmitter->setFlow(120);
	coreEmitter->setForce(0.05f, 0.20f);

	// Trail: continuous from bolt centre, near-zero velocity so particles lag
	StaticEmitter* trailEmitter = StaticEmitter::create();
	trailEmitter->setZone(trailZone);
	trailEmitter->setFlow(90);
	trailEmitter->setForce(0.0f, 0.05f);

	// -----------------------------------------------------------------------
	// Groups
	// -----------------------------------------------------------------------

	Group* coreGroup = Group::create(coreModel, 30);
	coreGroup->addEmitter(coreEmitter);
	coreGroup->setRenderer(coreRenderer);

	Group* trailGroup = Group::create(trailModel, 60);
	trailGroup->addEmitter(trailEmitter);
	trailGroup->setRenderer(trailRenderer);
	trailGroup->setGravity(Vector3D(0.0f, -0.25f, 0.0f)); // gentle downward drift
	trailGroup->setFriction(0.6f);

	// -----------------------------------------------------------------------
	// System
	// -----------------------------------------------------------------------

	IRRSystem* system = IRRSystem::create(smgr->getRootSceneNode(), smgr);
	system->addGroup(coreGroup);
	system->addGroup(trailGroup);
	system->setAutoUpdateEnabled(false, false); // we drive updates manually
	static_cast<irr::scene::ISceneNode*>(system)->setVisible(false);

	m_projectileSparkBaseID = system->getSPKID();
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
	auto* smgr   = RenderManager::Get()->sceneManager();

	// -----------------------------------------------------------------------
	// Renderers
	// -----------------------------------------------------------------------

	IRRQuadRenderer* flashRenderer = IRRQuadRenderer::create(RenderManager::Get()->device());
	flashRenderer->setTexturingMode(TEXTURE_2D);
	flashRenderer->setTexture(driver->getTexture("content/texture/particle/star_09.png"));
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
	glowRenderer->setTexture(driver->getTexture("content/texture/particle/star_09.png"));
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
	flashModel->setParam(PARAM_RED,   1.0f);
	flashModel->setParam(PARAM_GREEN, 0.9f);
	flashModel->setParam(PARAM_BLUE,  1.0f);
	flashModel->setParam(PARAM_ALPHA, 1.0f, 0.0f);
	flashModel->setParam(PARAM_SIZE,  0.6f, 0.05f);
	flashModel->setLifeTime(0.08f, 0.14f);
	flashModel->setShared(true);

	// Sparks: bright blue-white streaks, fly out then fade
	Model* sparkModel = Model::create(
		FLAG_RED | FLAG_GREEN | FLAG_BLUE | FLAG_ALPHA | FLAG_SIZE,
		FLAG_ALPHA | FLAG_RED | FLAG_GREEN | FLAG_BLUE,
		FLAG_SIZE);
	sparkModel->setParam(PARAM_RED,   1.0f, 0.3f);
	sparkModel->setParam(PARAM_GREEN, 0.9f, 0.4f);
	sparkModel->setParam(PARAM_BLUE,  1.0f, 1.0f);
	sparkModel->setParam(PARAM_ALPHA, 1.0f, 0.0f);
	sparkModel->setParam(PARAM_SIZE,  0.08f, 0.18f);
	sparkModel->setLifeTime(0.25f, 0.6f);
	sparkModel->setShared(true);

	// Glow: soft blue, expands and fades
	Model* glowModel = Model::create(
		FLAG_RED | FLAG_GREEN | FLAG_BLUE | FLAG_ALPHA | FLAG_SIZE,
		FLAG_ALPHA | FLAG_SIZE,
		FLAG_NONE);
	glowModel->setParam(PARAM_RED,   0.3f);
	glowModel->setParam(PARAM_GREEN, 0.5f);
	glowModel->setParam(PARAM_BLUE,  1.0f);
	glowModel->setParam(PARAM_ALPHA, 0.6f, 0.0f);
	glowModel->setParam(PARAM_SIZE,  0.1f, 0.55f);
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
	IRRSystem* irrSys   = static_cast<IRRSystem*>(system);
	if (irrSys)
	{
		irrSys->setVisible(true);
		irrSys->setPosition(pos);
		irrSys->updateAbsolutePosition();
	}

	m_impactSystems.push_back(system);
}

void Weapon_Rifle::createMuzzleFlash()
{
	if (!m_mesh.node)
		return;

	irr::scene::IBoneSceneNode* muzzleBone = m_mesh.node->getJointNode("Muzzle");
	if (!muzzleBone)
	{
		spdlog::warn("Muzzle bone not found on weapon model");
		return;
	}

	static std::random_device rd;
	static std::mt19937 gen(rd());
	std::uniform_int_distribution<int> starDist(1, 9);

	irr::core::vector3df flashOffset(0, 0, 0.0f);

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

	int starIndex = starDist(gen);
	std::string starPath = "content/texture/particle/star_0" + std::to_string(starIndex) + ".png";
	auto* starTex = RenderManager::Get()->driver()->getTexture(starPath.c_str());
	m_muzzleStarNode->setMaterialTexture(0, starTex);
	m_muzzleStarNode->setVisible(true);

	m_muzzleStarNode->getMaterial(0).AmbientColor  = irr::video::SColor(255, 100, 150, 255);
	m_muzzleStarNode->getMaterial(0).DiffuseColor  = irr::video::SColor(255, 100, 150, 255);
	m_muzzleStarNode->getMaterial(0).EmissiveColor = irr::video::SColor(255, 100, 150, 255);

	if (!m_muzzleLightNode)
	{
		m_muzzleLightNode = RenderManager::Get()->sceneManager()->addLightSceneNode(
			muzzleBone,
			flashOffset,
			irr::video::SColorf(0.3f, 0.5f, 1.0f),
			3.0f
		);
	}
	if (m_muzzleLightNode)
	{
		m_muzzleLightNode->setVisible(true);
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

void Weapon_Rifle::updateProjectiles(float dt)
{
	const float dtSeconds = dt / 1000.0f;

	for (auto it = m_projectiles.begin(); it != m_projectiles.end();)
	{
		if (!it->entity.isValid() || !it->entity.hasComponent<TransformComponent>())
		{
			++it;
			continue;
		}

		auto& transformComp = it->entity.getComponent<TransformComponent>();
		irr::core::vector3df currentPos = transformComp.position; // read component field directly — node may not exist yet on frame 1

		// -----------------------------------------------------------------------
		// Move the SPARK system to follow the bolt this frame
		// -----------------------------------------------------------------------
		if (it->sparkSystem)
		{
			IRRSystem* irrSys = static_cast<IRRSystem*>(it->sparkSystem);
			if (irrSys)
			{
				irrSys->setPosition(currentPos);
				irrSys->updateAbsolutePosition();
			}
			it->sparkSystem->update(dtSeconds);
		}

		// -----------------------------------------------------------------------
		// Swept raycast collision
		// -----------------------------------------------------------------------
		const float sphereRadius = 0.15f;

		irr::core::vector3df rayStart = it->previousPosition;
		irr::core::vector3df rayEnd   = currentPos;

		irr::core::vector3df rayDirection = (rayEnd - rayStart);
		float rayLength = rayDirection.getLength();
		if (rayLength > 0.001f)
		{
			rayDirection.normalize();
			rayStart = rayStart - rayDirection * sphereRadius;
			rayEnd   = rayEnd   + rayDirection * sphereRadius;
		}

		RaycastResultData raycastResult = RenderManager::Get()->raycastWorldPosition(rayStart, rayEnd, true);

		bool hitSomething = false;
		irr::scene::ISceneNode* hitNode = nullptr;
		irr::core::vector3df hitPoint   = currentPos;
		irr::core::vector3df hitNormal(0, 1, 0);

		if (raycastResult.hit && raycastResult.node)
		{
			float hitDistance      = (raycastResult.point - it->previousPosition).getLength();
			float movementDistance = rayLength;

			if (hitDistance <= movementDistance + sphereRadius)
			{
				auto& hitEntity = WorldManager::Get()->managerSystem()->getEntityByID(raycastResult.node->getID());

				if (hitEntity.isValid() && hitEntity.hasComponent<DescriptorComponent>())
				{
					auto& hitDescriptor = hitEntity.getComponent<DescriptorComponent>();

					if (hitDescriptor.type == ET_STATIC || hitDescriptor.type == ET_DYNAMIC)
					{
						if (it->entity.isValid() && it->entity.hasComponent<DescriptorComponent>() &&
							hitDescriptor.id != it->entity.getComponent<DescriptorComponent>().id)
						{
							hitSomething = true;
							hitNode  = raycastResult.node;
							hitPoint = raycastResult.point;
							hitNormal = raycastResult.normal;
						}
					}
				}
			}
		}

		bool shouldRemove      = false;
		bool hitSomethingReal  = false;

		if (hitSomething && hitNode)
		{
			entityid hitEntityID = hitNode->getID();

			if (it->entity.isValid() && it->entity.hasComponent<DescriptorComponent>() &&
				hitEntityID == it->entity.getComponent<DescriptorComponent>().id)
			{
				// Self-hit, ignore
			}
			else
			{
				hitSomethingReal = true;

				auto entities = WorldManager::Get()->managerSystem()->getEntities();
				for (auto& entity : entities)
				{
					if (entity.hasComponent<DescriptorComponent>() &&
						entity.getComponent<DescriptorComponent>().id == hitEntityID)
					{
						if (entity.hasComponent<DamageReceiverComponent>())
						{
							entity.getComponent<DamageReceiverComponent>().damageReceived += 25;
						}
						break;
					}
				}

				createImpactEffect(hitPoint);
				shouldRemove = true;
			}
		}

		// -----------------------------------------------------------------------
		// Advance position
		// -----------------------------------------------------------------------
		irr::core::vector3df nextPos = currentPos + it->velocity * dtSeconds;

		if (!hitSomethingReal)
		{
			transformComp.position = nextPos;
			if (transformComp.node)
			{
				transformComp.node->setPosition(nextPos);
				transformComp.node->updateAbsolutePosition();
			}
		}

		it->previousPosition = currentPos;
		it->lifetime += dt;

		// -----------------------------------------------------------------------
		// Remove expired / hit projectiles
		// -----------------------------------------------------------------------
		if (shouldRemove || it->lifetime >= it->maxLifetime)
		{
			if (it->sparkSystem)
			{
				SPK_Destroy(it->sparkSystem);
				it->sparkSystem = nullptr;
			}

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

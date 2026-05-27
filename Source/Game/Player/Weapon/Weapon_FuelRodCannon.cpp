#include "Weapon_FuelRodCannon.h"

#include "Engine/Engine.h"

#include "../CameraFX.h"

#include "Engine/Renderer/Particle/ParticleManager.h"
#include "Engine/Resource/FilePaths.h"

#undef MB_RIGHT
#undef max
#undef min

using namespace irr;
using namespace SPK;
using namespace SPK::IRR;

void Weapon_FuelRodCannon::precache()
{
	ParticleManager::Get()->precache("explosion", _asset_psys("explosion"));
}

void Weapon_FuelRodCannon::init()
{
	m_descriptor.name = "Player_Weapon_FuelRodCannon";
	m_descriptor.id   = _entity_null_value;

	m_viewPositionOffset = irr::core::vector3df(0.3600f, -0.0850f, 0.3550f);
	m_viewRotationOffset = irr::core::vector3df(0.50f, -1.50f, -6.50f);
	m_viewScaleOffset    = irr::core::vector3df(1.0f, 1.0f, 1.0f);

	m_mesh.mesh = "content/mesh/player/weapon/fuelrodcannon/hud.b3d";

	m_mesh.trimesh = RenderManager::Get()->sceneManager()->getMesh(m_mesh.mesh.c_str());
	if (!m_mesh.trimesh)
	{
		spdlog::warn("In function PlayerWeapon::init() -> RenderManager::Get()->sceneManager()->getMesh() : Mesh does not exist, stand-in mesh loaded");

		m_mesh.trimesh = RenderManager::Get()->sceneManager()->getMesh("content/mesh/primitive/double_tetrahedron.obj");
		m_mesh.node    = RenderManager::Get()->sceneManager()->addAnimatedMeshSceneNode(m_mesh.trimesh, nullptr, m_descriptor.id);

		auto* t = RenderManager::Get()->driver()->getTexture("content/texture/color/magenta.png");
		m_mesh.node->setMaterialTexture(0, t);
	}

	m_mesh.node = RenderManager::Get()->sceneManager()->addAnimatedMeshSceneNode(m_mesh.trimesh, nullptr, m_descriptor.id);

	m_mesh.node->setMaterialFlag(irr::video::EMF_BILINEAR_FILTER,    true);
	m_mesh.node->setMaterialFlag(irr::video::EMF_TRILINEAR_FILTER,   true);
	m_mesh.node->setMaterialFlag(irr::video::EMF_ANISOTROPIC_FILTER, true);
	m_mesh.node->setMaterialFlag(irr::video::EMF_ANTI_ALIASING,      true);
	m_mesh.node->setMaterialFlag(irr::video::EMF_USE_MIP_MAPS,       true);

	m_mesh.fps = 30;
	m_mesh.node->setAnimationSpeed(30.0f);
	m_mesh.node->setLoopMode(true);
	m_mesh.node->setFrameLoop(20, 50);

	m_mesh.animationList.emplace_back(sAnimationData("equip",   1,   20,  false));
	m_mesh.animationList.emplace_back(sAnimationData("idle",    20,  50,  true));
	m_mesh.animationList.emplace_back(sAnimationData("move",    50,  79,  false));
	m_mesh.animationList.emplace_back(sAnimationData("fire",    81,  89,  false));
	m_mesh.animationList.emplace_back(sAnimationData("reload",  96,  179, false));
	m_mesh.animationList.emplace_back(sAnimationData("unequip", 179, 190, false));

	m_mesh.node->setJointMode(irr::scene::EJUOR_READ);

	m_mesh.animation_call_back = std::make_shared<AnimationCallback>();
	m_mesh.node->setAnimationEndCallback(m_mesh.animation_call_back.get());

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
		spdlog::error("In function PlayerWeapon::init() -> WorldManager::Get()->managerSystem()->getEntityByName(\"player\") : Entity 'player' does not exist");
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
		spdlog::error("In function PlayerWeapon::init() -> player.getComponent<CameraComponent>() : Entity 'player' does not have specified component");
	}

	RenderManager::Get()->registerViewmodelNode(m_mesh.node);
	m_mesh.node->setVisible(false);

	m_crosshair = RenderManager::Get()->driver()->getTexture("content/texture/ui/crosshair/2x/crosshair131.png");
}

void Weapon_FuelRodCannon::destroy()
{
	for (auto& proj : m_projectiles)
	{
		if (proj.trailParticles)
		{
			proj.trailParticles->remove();
			proj.trailParticles = nullptr;
		}

		if (proj.flyingSound)
		{
			proj.flyingSound->stop();
			proj.flyingSound->drop();
			proj.flyingSound = nullptr;
		}

		if (proj.entity.isValid() && proj.entity.hasComponent<DescriptorComponent>())
		{
			proj.entity.getComponent<DescriptorComponent>().isAlive = false;
		}
	}
	m_projectiles.clear();

	RenderManager::Get()->unregisterViewmodelNode(m_mesh.node);
	m_mesh.node->remove();

	WorldManager::Get()->freeEntityID(m_descriptor.id);
}

void Weapon_FuelRodCannon::update()
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

	bool fireButtonPressed = InputManager::Get()->isMouseButtonPressed(MB_LEFT);

	if (fireButtonPressed)
	{
		if (currentTime - m_lastFireTime >= m_fireRate)
		{
			spawnProjectile();
			m_lastFireTime = currentTime;
		}
	}

	RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair2x_center_position);
}

void Weapon_FuelRodCannon::persist()
{
	float dt = Engine::Get()->getDeltaTime();

	updateProjectiles(dt);
	updateMuzzleFlash(dt);
}

void Weapon_FuelRodCannon::equip()
{
	m_mesh.node->setVisible(true);
	m_mesh.animation_call_back->hasAnimationEnded();
	m_mesh.node->setLoopMode(false);
	m_mesh.node->setFrameLoop(1, 20);
	m_isEquipping   = true;
	m_isUnequipping = false;
}

void Weapon_FuelRodCannon::unequip()
{
	m_isEquipping   = false;
	m_isUnequipping = false;
	m_mesh.node->setVisible(false);
}

void Weapon_FuelRodCannon::startUnequip()
{
	m_isUnequipping = true;
	m_isEquipping   = false;
	m_mesh.animation_call_back->hasAnimationEnded();
	m_mesh.node->setLoopMode(false);
	m_mesh.node->setFrameLoop(179, 190);
}

void Weapon_FuelRodCannon::idle()
{
}

void Weapon_FuelRodCannon::move()
{
}

void Weapon_FuelRodCannon::fire()
{
}

void Weapon_FuelRodCannon::reload()
{
}

void Weapon_FuelRodCannon::spawnProjectile()
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>())
		return;

	auto& camera = player.getComponent<CameraComponent>();

	if (!m_mesh.node)
		return;

	irr::scene::IBoneSceneNode* muzzleBone = m_mesh.node->getJointNode("FIRESPOT");
	if (!muzzleBone)
	{
		spdlog::warn("Muzzle bone not found on weapon model - cannot spawn projectile");
		return;
	}

	camera.camera->updateAbsolutePosition();
	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();
	muzzleBone->updateAbsolutePosition();
	irr::core::vector3df spawnPos = muzzleBone->getAbsolutePosition();

	irr::core::vector3df target    = camera.camera->getTarget();
	irr::core::vector3df direction = (target - spawnPos).normalize();

	direction.Y += m_lobAngle;
	direction.normalize();

	spawnPos += direction * m_spawnOffset;

	anax::Entity projectileEntity = WorldManager::Get()->managerSystem()->getWorld().createEntity();

	projectileEntity.addComponent<DescriptorComponent>();
	auto& descriptor          = projectileEntity.getComponent<DescriptorComponent>();
	descriptor.id             = WorldManager::Get()->getNewID();
	descriptor.name           = "fuelrod_projectile_" + std::to_string(descriptor.id);
	descriptor.type           = ET_DYNAMIC;
	descriptor.isSerializable = false;

	projectileEntity.addComponent<TransformComponent>();
	auto& transform           = projectileEntity.getComponent<TransformComponent>();
	transform.position        = spawnPos;
	transform.initialPosition = spawnPos;

	irr::core::vector3df initialRotation = direction.getHorizontalAngle();
	transform.rotation        = initialRotation;
	transform.initialRotation = initialRotation;

	projectileEntity.addComponent<RenderComponent>();
	projectileEntity.getComponent<RenderComponent>().isVisible = true;

	projectileEntity.addComponent<MeshComponent>();
	auto& mesh          = projectileEntity.getComponent<MeshComponent>();
	mesh.mesh           = "content/mesh/prop/missile.obj";
	mesh.textures.emplace_back<std::string>("content/mesh/prop/missile.png");
	mesh.isPrimitive    = false;
	mesh.isVisible      = true;
	mesh.castShadows    = false;
	mesh.receiveShadows = false;

	// Green point light to sell the glowing fuel rod
	projectileEntity.addComponent<LightComponent>();
	auto& light         = projectileEntity.getComponent<LightComponent>();
	light.type          = LT_POINT;
	light.visible       = true;
	light.radius        = 3.0f;
	light.color_diffuse = irr::video::SColorf(0.2f, 0.9f, 0.2f);
	light.offset        = irr::core::vector3df(0.0f, 0.0f, 0.0f);

	projectileEntity.activate();

	WeaponProjectile proj;
	proj.speed            = m_projectileSpeed;
	proj.useTracking      = false;
	proj.targetId         = _entity_null_value;
	proj.distanceTraveled = 0.0f;
	proj.isTrackingActive = false;
	proj.entity           = projectileEntity;
	proj.velocity         = direction * proj.speed;
	proj.previousPosition = spawnPos;
	proj.trailParticles   = nullptr;
	proj.isBouncing       = false;
	proj.maxLifetime      = 5000.0f;

	m_projectiles.emplace_back(proj);

	SoundManager::Get()->sound()->play2D("content/sound/weapon/fuel_rod_cannon/fire.wav", false);

	m_mesh.node->setLoopMode(false);
	m_mesh.node->setFrameLoop(81, 89);

	createMuzzleFlash();
}

void Weapon_FuelRodCannon::updateProjectiles(float dt)
{
	for (auto it = m_projectiles.begin(); it != m_projectiles.end();)
	{
		if (!it->entity.isValid() || !it->entity.hasComponent<TransformComponent>())
		{
			++it;
			continue;
		}

		auto& transformComp = it->entity.getComponent<TransformComponent>();

		if (!transformComp.node)
		{
			++it;
			continue;
		}

		// Create green plasma trail once transform node is ready
		if (!it->trailParticles)
		{
			auto* particleSystem = RenderManager::Get()->sceneManager()->addParticleSystemSceneNode(false, transformComp.node);

			auto* emitter = particleSystem->createPointEmitter(
				irr::core::vector3df(0, 0, 0),
				20,
				35,
				irr::video::SColor(255, 80, 220, 80),
				irr::video::SColor(255, 20, 120, 20),
				200,
				450,
				1,
				irr::core::dimension2df(0.12f, 0.12f),
				irr::core::dimension2df(0.18f, 0.18f)
			);

			particleSystem->setEmitter(emitter);
			emitter->drop();

			auto* fadeAffector = particleSystem->createFadeOutParticleAffector();
			particleSystem->addAffector(fadeAffector);
			fadeAffector->drop();

			particleSystem->setMaterialFlag(irr::video::EMF_LIGHTING,       false);
			particleSystem->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE,  false);
			particleSystem->setMaterialFlag(irr::video::EMF_BLEND_OPERATION, true);
			particleSystem->setMaterialType(m_particleTrailMaterialType);

			auto* particleTexture = RenderManager::Get()->driver()->getTexture("content/texture/particle/smoke_04.png");
			if (!particleTexture)
				particleTexture = RenderManager::Get()->driver()->getTexture("content/texture/color/magenta.png");
			if (particleTexture)
				particleSystem->setMaterialTexture(0, particleTexture);

			particleSystem->setPosition(irr::core::vector3df(0, 0, 0));
			it->trailParticles = particleSystem;
		}

		irr::core::vector3df currentPos = transformComp.getPosition();

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

		bool hitSomething               = false;
		irr::scene::ISceneNode* hitNode = nullptr;
		irr::core::vector3df hitPoint   = currentPos;

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
							hitNode      = raycastResult.node;
							hitPoint     = raycastResult.point;
						}
					}
				}
			}
		}

		bool shouldRemove = false;

		if (hitSomething && hitNode)
		{
			entityid hitEntityID = hitNode->getID();

			if (it->entity.isValid() && it->entity.hasComponent<DescriptorComponent>() &&
				hitEntityID != it->entity.getComponent<DescriptorComponent>().id)
			{
				detonateAt(hitPoint, hitEntityID);
				shouldRemove = true;
			}
		}

		float dtSeconds = dt / 1000.0f;

		it->velocity.Y -= m_gravity * dtSeconds;

		irr::core::vector3df nextPos = currentPos + it->velocity * dtSeconds;

		if (!shouldRemove)
		{
			transformComp.position = nextPos;

			irr::core::vector3df dir = it->velocity;
			dir.normalize();
			transformComp.rotation = dir.getHorizontalAngle();

			if (transformComp.node)
			{
				transformComp.node->setPosition(nextPos);
				transformComp.node->setRotation(transformComp.rotation);
				transformComp.node->updateAbsolutePosition();
			}
		}

		it->previousPosition = currentPos;
		it->lifetime += dt;

		if (shouldRemove || it->lifetime >= it->maxLifetime)
		{
			if (it->lifetime >= it->maxLifetime && !shouldRemove)
			{
				irr::core::vector3df detonPos = transformComp.node
					? transformComp.node->getAbsolutePosition()
					: nextPos;
				detonateAt(detonPos, _entity_null_value);
			}

			if (it->trailParticles)
			{
				it->trailParticles->remove();
				it->trailParticles = nullptr;
			}

			if (it->flyingSound)
			{
				it->flyingSound->stop();
				it->flyingSound->drop();
				it->flyingSound = nullptr;
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

void Weapon_FuelRodCannon::detonateAt(const irr::core::vector3df& pos, entityid directHitID)
{
	SoundManager::Get()->sound()->play3D("content/sound/effect/explosion2.wav", pos);
	ParticleManager::Get()->spawn("explosion", irr2spk(pos));
	applySplashDamage(pos, directHitID);

	anax::Entity& playerEnt = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (playerEnt.isValid() && playerEnt.hasComponent<TransformComponent>())
	{
		const float maxShakeDist = 7.0f;
		const float peakShake    = 5.0f;
		const float shakeDurMs   = 400.0f;

		float dist      = (pos - playerEnt.getComponent<TransformComponent>().getPosition()).getLength();
		float intensity = std::max(0.0f, 1.0f - dist / maxShakeDist) * peakShake;

		if (intensity > 0.05f)
			g_CameraFX.addShake(intensity, shakeDurMs);
	}

	if (directHitID != _entity_null_value)
	{
		auto entities = WorldManager::Get()->managerSystem()->getEntities();
		for (auto& entity : entities)
		{
			if (entity.hasComponent<DescriptorComponent>() &&
				entity.getComponent<DescriptorComponent>().id == directHitID)
			{
				if (entity.hasComponent<DamageReceiverComponent>())
					entity.getComponent<DamageReceiverComponent>().damageReceived += m_pointDamage;
				break;
			}
		}
	}
}

void Weapon_FuelRodCannon::createMuzzleFlash()
{
	if (!m_mesh.node)
		return;

	m_mesh.node->updateAbsolutePosition();

	irr::scene::IBoneSceneNode* muzzleBone = m_mesh.node->getJointNode("FIRESPOT");
	if (!muzzleBone)
	{
		spdlog::warn("Muzzle bone not found on weapon model");
		return;
	}

	muzzleBone->updateAbsolutePosition();

	if (!m_muzzleStarNode)
	{
		m_muzzleStarNode = RenderManager::Get()->sceneManager()->addBillboardSceneNode(
			muzzleBone,
			irr::core::dimension2df(0.9f, 0.9f),
			irr::core::vector3df(0.0f, 0.0f, 0.0f)
		);

		m_muzzleStarNode->setMaterialFlag(irr::video::EMF_LIGHTING,       false);
		m_muzzleStarNode->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE,  false);
		m_muzzleStarNode->setMaterialFlag(irr::video::EMF_BLEND_OPERATION, true);
		m_muzzleStarNode->setMaterialType(m_muzzleFlashMaterialType);
	}

	auto* starTex = RenderManager::Get()->driver()->getTexture("content/texture/particle/star_05.png");
	m_muzzleStarNode->setMaterialTexture(0, starTex);
	m_muzzleStarNode->setVisible(true);
	m_muzzleStarNode->updateAbsolutePosition();

	// Green tint for the fuel rod muzzle flash
	m_muzzleStarNode->getMaterial(0).AmbientColor = irr::video::SColor(255, 80, 220, 80);
	m_muzzleStarNode->getMaterial(0).DiffuseColor = irr::video::SColor(255, 80, 220, 80);

	m_muzzleFlashTime = 0.0f;
}

void Weapon_FuelRodCannon::updateMuzzleFlash(float dt)
{
	if (!m_muzzleStarNode)
		return;

	m_muzzleFlashTime += dt;

	if (m_muzzleFlashTime >= m_muzzleFlashDuration)
	{
		m_muzzleStarNode->setVisible(false);
	}
	else
	{
		float fadeProgress = m_muzzleFlashTime / m_muzzleFlashDuration;
		irr::u32 alpha = (irr::u32)((1.0f - fadeProgress) * 255.0f);

		m_muzzleStarNode->getMaterial(0).AmbientColor.setAlpha(alpha);
		m_muzzleStarNode->getMaterial(0).DiffuseColor.setAlpha(alpha);
	}
}

void Weapon_FuelRodCannon::applySplashDamage(const irr::core::vector3df& epicentre, entityid directHitEntityID)
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

		if (desc.id == directHitEntityID) continue;
		if (!desc.isAlive) continue;

		irr::core::vector3df entityPos = entity.getComponent<TransformComponent>().getPosition();
		float dist = (entityPos - epicentre).getLength();

		if (dist >= m_splashRadius) continue;

		float falloff = 1.0f - (dist / m_splashRadius);
		float damage  = m_splashDamage * falloff;

		if (damage > 0.0f && entity.hasComponent<DamageReceiverComponent>())
		{
			entity.getComponent<DamageReceiverComponent>().damageReceived += damage;
		}

		if (m_splashForce > 0.0f && entity.hasComponent<PhysicsComponent>())
		{
			auto& phys = entity.getComponent<PhysicsComponent>();
			if (phys.actor && !phys.kinematic)
			{
				irr::core::vector3df dir = entityPos - epicentre;
				float len = dir.getLength();
				if (len > 0.001f)
					dir /= len;
				else
					dir = irr::core::vector3df(0.0f, 1.0f, 0.0f);

				float impulse = m_splashForce * falloff;
				phys.actor->addForce(
					physx::PxVec3(dir.X, dir.Y, dir.Z) * impulse,
					physx::PxForceMode::eIMPULSE);
			}
		}
	}
}

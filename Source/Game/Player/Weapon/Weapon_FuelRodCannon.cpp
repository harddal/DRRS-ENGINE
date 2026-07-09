#include "Weapon_FuelRodCannon.h"

#include "Engine/Engine.h"

#include "../CameraFX.h"

#include "Engine/Renderer/Particle/ParticleManager.h"
#include "Engine/Resource/FilePaths.h"
#include "Engine/Sound/GeigerEffect.h"

#include "Game/Components/NPCComponent.h"

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

	// Character sheet: radioactive green blast flash, no tracers/shells/impacts (projectile weapon)
	WeaponEffectsDesc fx;
	fx.muzzleJointName = "FIRESPOT";
	fx.flashColor      = irr::video::SColor(255, 80, 220, 80);
	fx.flashSize       = 0.9f;
	fx.flashDuration   = 60.0f;
	fx.flashLight      = false;
	fx.tracerPoolSize  = 0;
	fx.shellPoolSize   = 0;
	fx.impactParticle  = nullptr;
	fx.impactDecal     = nullptr;
	m_effects.init(m_mesh.node, fx);
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

	for (auto& zone : m_zones)
	{
		if (zone.particles)
		{
			zone.particles->remove();
			zone.particles = nullptr;
		}
	}
	m_zones.clear();

	GeigerEffect::Get()->setEnabled(false);
	GeigerEffect::Get()->setStrength(0.0f);

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
	m_effects.update(dt);
	updateZones(dt);
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

	// Converge on the crosshair's world hit point, then apply the lob on top
	irr::core::vector3df direction = getAimDirection(spawnPos);

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

	SoundManager::Get()->sound()->playRandomized2D("content/sound/weapon/fuel_rod_cannon/fire", 0.05f);

	m_mesh.node->setLoopMode(false);
	m_mesh.node->setFrameLoop(81, 89);

	m_effects.muzzleFlash();

	// Heaviest kick in the arsenal — slow fire rate earns a violent shove
	g_CameraFX.addRecoil(-4.0f, Engine::Get()->rng()->getFloat(-0.4f, 0.4f));
	g_CameraFX.addShake(1.2f, 200.0f);
	g_CameraFX.addFovKick(2.0f);
	addViewKick(
		irr::core::vector3df(0.0f, 0.04f, -0.18f),
		irr::core::vector3df(7.0f,
			Engine::Get()->rng()->getFloat(-1.0f, 1.0f),
			Engine::Get()->rng()->getFloat(-1.8f, 1.8f)));
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

		if (!it->trailParticles)
		{
			auto* particleSystem = RenderManager::Get()->sceneManager()->addParticleSystemSceneNode(false, transformComp.node);

			auto* emitter = particleSystem->createPointEmitter(
				irr::core::vector3df(0, 0, 0),
				20, 35,
				irr::video::SColor(255, 80, 220, 80),
				irr::video::SColor(255, 20, 120, 20),
				200, 450,
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
		irr::core::vector3df hitNormal(0.0f, 0.0f, 0.0f);

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
							hitNormal    = raycastResult.normal;
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
				detonateAt(hitPoint, hitEntityID, hitNormal);
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

void Weapon_FuelRodCannon::detonateAt(const irr::core::vector3df& pos, entityid directHitID,
	const irr::core::vector3df& surfaceNormal)
{
	SoundManager::Get()->sound()->playRandomized3D("content/sound/effect/explosion", pos, 0.05f);
	ParticleManager::Get()->spawn("explosion", irr2spk(pos));

	// Radioactive green flash + scorch (oriented to the hit surface) + proximity
	// feedback — the biggest blast in the arsenal
	m_effects.explosionAt(pos,
		irr::video::SColorf(0.3f, 1.0f, 0.3f), 11.0f, 5.0f, 7.0f, 400.0f,
		surfaceNormal);

	// Point damage on direct hit — through the chokepoint for hitmarker/kill feedback.
	// Radiation-zone DoT ticks intentionally do NOT feed the hitmarker (spam).
	if (directHitID != _entity_null_value)
	{
		registerHitFeedback(WorldManager::Get()->gameplaySystem()->damageEntity(
			directHitID, static_cast<unsigned int>(m_pointDamage)));
	}

	// The zone does all ongoing damage; no instant splash
	spawnZone(pos, false);
}

void Weapon_FuelRodCannon::spawnZone(const irr::core::vector3df& pos, bool secondary)
{
	RadiationZone zone;
	zone.position     = pos;
	zone.isSecondary  = secondary;
	zone.damageAccum  = 0.0f;

	if (secondary)
	{
		zone.radius        = 3.5f;
		zone.maxLifetime   = 5000.0f;
		zone.damageTickRate = 600.0f;
		zone.damagePerTick  = 10.0f;
	}
	else
	{
		zone.radius        = 6.0f;
		zone.maxLifetime   = 8000.0f;
		zone.damageTickRate = 500.0f;
		zone.damagePerTick  = 15.0f;
	}

	// Build a standalone particle cloud at the zone position
	auto* psys = RenderManager::Get()->sceneManager()->addParticleSystemSceneNode(
		false, nullptr, -1, pos);

	auto* emitter = psys->createSphereEmitter(
		irr::core::vector3df(0, 0, 0),
		zone.radius * 0.4f,          // emit from inner sphere so cloud spreads naturally
		irr::core::vector3df(0.0f, 0.002f, 0.0f),
		secondary ? 8 : 18,
		secondary ? 16 : 32,
		irr::video::SColor(180, 40, 180, 40),
		irr::video::SColor(120, 10, 100, 10),
		1500, 3000,
		30,
		irr::core::dimension2df(0.3f, 0.3f),
		irr::core::dimension2df(0.6f, 0.6f)
	);

	psys->setEmitter(emitter);
	emitter->drop();

	auto* fade = psys->createFadeOutParticleAffector(irr::video::SColor(0, 0, 0, 0), 800);
	psys->addAffector(fade);
	fade->drop();

	psys->setMaterialFlag(irr::video::EMF_LIGHTING,       false);
	psys->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE,  false);
	psys->setMaterialFlag(irr::video::EMF_BLEND_OPERATION, true);
	psys->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);

	auto* tex = RenderManager::Get()->driver()->getTexture("content/texture/particle/smoke_04.png");
	if (!tex)
		tex = RenderManager::Get()->driver()->getTexture("content/texture/color/magenta.png");
	if (tex)
		psys->setMaterialTexture(0, tex);

	zone.particles = psys;

	m_zones.emplace_back(std::move(zone));

	SoundManager::Get()->sound()->play3D(
		secondary ? "content/sound/effect/radiation_hiss.wav"
		          : "content/sound/effect/radiation_impact.wav",
		pos);
}

void Weapon_FuelRodCannon::updateZones(float dt)
{
	if (m_zones.empty())
		return;

	anax::Entity& playerEnt = WorldManager::Get()->managerSystem()->getEntityByName("player");

	irr::core::vector3df playerPos(0, 0, 0);
	bool playerValid = playerEnt.isValid() && playerEnt.hasComponent<TransformComponent>();
	if (playerValid)
		playerPos = playerEnt.getComponent<TransformComponent>().getPosition();

	// Buffer secondary zones to append after the loop (can't modify m_zones while iterating)
	std::vector<irr::core::vector3df> pendingSecondaries;

	float maxRadiationContrib = 0.0f;

	for (auto it = m_zones.begin(); it != m_zones.end();)
	{
		it->lifetime  += dt;
		it->damageAccum += dt;

		// Radiation contribution to postprocess + geiger — linear falloff from zone edge to centre
		if (playerValid)
		{
			float dist = (playerPos - it->position).getLength();
			float t    = 1.0f - std::min(dist / it->radius, 1.0f);
			maxRadiationContrib = std::max(maxRadiationContrib, t);
		}

		// Damage tick
		if (it->damageAccum >= it->damageTickRate)
		{
			it->damageAccum -= it->damageTickRate;

			auto& allEntities = WorldManager::Get()->managerSystem()->getEntities();
			for (auto& entity : allEntities)
			{
				if (!entity.isValid()) continue;
				if (!entity.hasComponent<DescriptorComponent>()) continue;
				if (!entity.hasComponent<TransformComponent>()) continue;

				auto& desc = entity.getComponent<DescriptorComponent>();

				irr::core::vector3df entPos = entity.getComponent<TransformComponent>().getPosition();
				float dist = (entPos - it->position).getLength();
				if (dist >= it->radius) continue;

				// Damage alive entities (player gets half — self-irradiation is punishing but not instant)
				if (desc.isAlive && entity.hasComponent<DamageReceiverComponent>())
				{
					float falloff   = 1.0f - (dist / it->radius);
					bool  isPlayer  = playerValid && (desc.id == playerEnt.getComponent<DescriptorComponent>().id);
					float scale     = isPlayer ? 0.5f : 1.0f;
					int   damage    = (int)(it->damagePerTick * falloff * scale);
					if (damage > 0)
						entity.getComponent<DamageReceiverComponent>().damageReceived += damage;
				}

				// Contamination: primaries only — watch for NPC death inside the zone.
				// Dead entities are NOT skipped here so we catch the tick they die.
				if (!it->isSecondary && entity.hasComponent<NPCComponent>())
				{
					if (!desc.isAlive && it->secondarySpawned.find(desc.id) == it->secondarySpawned.end())
					{
						pendingSecondaries.emplace_back(entPos);
						it->secondarySpawned.insert(desc.id);
					}
				}
			}
		}

		// Fade emitter rate as zone ages so cloud visually dissipates before disappearing
		if (it->particles)
		{
			float lifeRatio = it->lifetime / it->maxLifetime;
			if (lifeRatio > 0.6f)
			{
				// Cut emission in the last 40% of lifetime
				auto* emitter = it->particles->getEmitter();
				if (emitter)
				{
					float fadeRatio = 1.0f - ((lifeRatio - 0.6f) / 0.4f);
					int maxParticles = (int)(( it->isSecondary ? 16 : 32) * fadeRatio);
					emitter->setMaxParticlesPerSecond(std::max(0, maxParticles));
				}
			}
		}

		if (it->lifetime >= it->maxLifetime)
		{
			if (it->particles)
			{
				it->particles->remove();
				it->particles = nullptr;
			}
			it = m_zones.erase(it);
		}
		else
		{
			++it;
		}
	}

	// Spawn secondaries outside the iteration loop
	for (const auto& pos : pendingSecondaries)
		spawnZone(pos, true);

	// Contribute radiation to postprocess + geiger — uses std::max so we cooperate with
	// any script-driven radiation sources that already contributed this frame
	if (maxRadiationContrib > 0.0f)
	{
		auto* rm = RenderManager::Get();
#undef max
		float combined = std::max(rm->radiationCallback()->intensity, maxRadiationContrib);
		rm->radiationCallback()->intensity = combined;
		rm->setRadiationEnabled(true);

		GeigerEffect::Get()->setEnabled(true);
		GeigerEffect::Get()->setStrength(std::max(GeigerEffect::Get()->strength, maxRadiationContrib));
	}
}


#include "Weapon_GrenadeLauncher.h"

#include "Engine/Engine.h"

#include "../CameraFX.h"

#include "Engine/Renderer/Particle/ParticleManager.h"
#include "Engine/Resource/FilePaths.h"

#include <cmath>

#undef MB_RIGHT
#undef max
#undef min

using namespace irr;
using namespace SPK;
using namespace SPK::IRR;

void Weapon_GrenadeLauncher::precache()
{
	ParticleManager::Get()->precache("explosion", _asset_psys("explosion"));
}

void Weapon_GrenadeLauncher::init()
{
	m_descriptor.name = "Player_Weapon_GrenadeLauncher";
	m_descriptor.id   = _entity_null_value;

	m_viewPositionOffset = irr::core::vector3df(0.3800f, -0.2650f, 0.4550f);
	m_viewRotationOffset = irr::core::vector3df(0.50f, -0.50f, -4.00f);
	m_viewScaleOffset    = irr::core::vector3df(1.0f, 1.0f, 1.0f);

	m_mesh.mesh = "content/mesh/player/weapon/grenadelauncher/hud.b3d";

	m_mesh.trimesh = RenderManager::Get()->loadMesh(m_mesh.mesh);
	if (!m_mesh.trimesh)
	{
		spdlog::warn("PlayerWeapon::init(): failed to load mesh \"{}\", stand-in mesh loaded", m_mesh.mesh);

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

	m_mesh.animationList.emplace_back(sAnimationData("equip",   1,   20,  false));
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

	// Character sheet: warm amber thump flash, no tracers/shells/impacts (projectile weapon)
	WeaponEffectsDesc fx;
	fx.muzzleJointName = "FIRESPOT";
	fx.flashColor      = irr::video::SColor(255, 200, 160, 80);
	fx.flashSize       = 0.9f;
	fx.flashLight      = false;
	fx.tracerPoolSize  = 0;
	fx.shellPoolSize   = 0;
	fx.impactParticle  = nullptr;
	fx.impactDecal     = nullptr;
	m_effects.init(m_mesh.node, fx);
}

void Weapon_GrenadeLauncher::destroy()
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

void Weapon_GrenadeLauncher::update()
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

	bool fireButtonPressed    = InputManager::Get()->isMouseButtonPressed(MB_LEFT);
	bool altFireButtonPressed = InputManager::Get()->isMouseButtonPressed(MB_RIGHT);

	if (fireButtonPressed || altFireButtonPressed)
	{
		if (currentTime - m_lastFireTime >= m_fireRate)
		{
			spawnProjectile(altFireButtonPressed);
			m_lastFireTime = currentTime;
		}
	}

	RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair2x_center_position);
}

void Weapon_GrenadeLauncher::persist()
{
	float dt = Engine::Get()->getDeltaTime();

	updateProjectiles(dt);
	m_effects.update(dt);
}

void Weapon_GrenadeLauncher::equip()
{
	m_mesh.node->setVisible(true);
	m_mesh.animation_call_back->hasAnimationEnded();
	playAnimation("equip");
	m_isEquipping   = true;
	m_isUnequipping = false;
}

void Weapon_GrenadeLauncher::unequip()
{
	m_isEquipping   = false;
	m_isUnequipping = false;
	m_mesh.node->setVisible(false);
}

void Weapon_GrenadeLauncher::startUnequip()
{
	m_isUnequipping = true;
	m_isEquipping   = false;
	m_mesh.animation_call_back->hasAnimationEnded();
	playAnimation("unequip");
}

void Weapon_GrenadeLauncher::idle()
{
}

void Weapon_GrenadeLauncher::move()
{
}

void Weapon_GrenadeLauncher::fire()
{
}

void Weapon_GrenadeLauncher::reload()
{
}

void Weapon_GrenadeLauncher::spawnProjectile(bool bounce)
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

	// Raycast from camera to find where the crosshair intersects world geometry
	irr::core::vector3df camPos = camera.camera->getAbsolutePosition();
	irr::core::vector3df aimFar = camera.targetNode
		? camera.targetNode->getAbsolutePosition()
		: camPos + (camera.camera->getTarget() - camPos).normalize() * 1000.0f;

	RaycastResultData aimHit = RenderManager::Get()->raycastWorldPosition(camPos, aimFar, true);
	irr::core::vector3df aimTarget = aimHit.hit ? aimHit.point : aimFar;

	// Solve for the low-arc launch velocity that lands on aimTarget
	irr::core::vector3df launchVelocity;
	{
		irr::core::vector3df toTarget(aimTarget.X - spawnPos.X, 0.0f, aimTarget.Z - spawnPos.Z);
		float d = toTarget.getLength();
		float h = aimTarget.Y - spawnPos.Y;

		bool solved = false;
		if (d > 0.01f)
		{
			float v2   = m_projectileSpeed * m_projectileSpeed;
			float disc = v2 * v2 - m_gravity * (m_gravity * d * d + 2.0f * h * v2);
			if (disc >= 0.0f)
			{
				float tanTheta = (v2 - std::sqrt(disc)) / (m_gravity * d);
				float theta    = std::atan(tanTheta);
				irr::core::vector3df horizDir = toTarget;
				horizDir.normalize();
				launchVelocity    = horizDir * (m_projectileSpeed * std::cos(theta));
				launchVelocity.Y += m_projectileSpeed * std::sin(theta);
				solved = true;
			}
		}

		if (!solved)
		{
			// Fallback: static lob angle (target out of range or directly above)
			irr::core::vector3df fallDir = (aimTarget - spawnPos).normalize();
			fallDir.Y += m_lobAngle;
			fallDir.normalize();
			launchVelocity = fallDir * m_projectileSpeed;
		}
	}

	irr::core::vector3df launchDir = launchVelocity;
	launchDir.normalize();

	spawnPos += launchDir * m_spawnOffset;

	anax::Entity projectileEntity = WorldManager::Get()->managerSystem()->getWorld().createEntity();

	projectileEntity.addComponent<DescriptorComponent>();
	auto& descriptor        = projectileEntity.getComponent<DescriptorComponent>();
	descriptor.id           = WorldManager::Get()->getNewID();
	descriptor.name         = "grenade_projectile_" + std::to_string(descriptor.id);
	descriptor.type         = ET_DYNAMIC;
	descriptor.isSerializable = false;

	projectileEntity.addComponent<TransformComponent>();
	auto& transform           = projectileEntity.getComponent<TransformComponent>();
	transform.position        = spawnPos;
	transform.initialPosition = spawnPos;

	irr::core::vector3df initialRotation = launchDir.getHorizontalAngle();
	transform.rotation        = initialRotation;
	transform.initialRotation = initialRotation;

	projectileEntity.addComponent<RenderComponent>();
	projectileEntity.getComponent<RenderComponent>().isVisible = true;

	projectileEntity.addComponent<MeshComponent>();
	auto& mesh            = projectileEntity.getComponent<MeshComponent>();
	mesh.mesh             = "content/mesh/prop/missile.obj";  // placeholder until grenade model is available
	mesh.textures.emplace_back<std::string>("content/mesh/prop/missile.png");
	mesh.isPrimitive      = false;
	mesh.isVisible        = true;
	mesh.castShadows      = false;
	mesh.receiveShadows   = false;

	projectileEntity.addComponent<LightComponent>();
	auto& light           = projectileEntity.getComponent<LightComponent>();
	light.type            = LT_POINT;
	light.visible         = true;
	light.radius          = 2.0f;
	light.color_diffuse   = irr::video::SColorf(0.8f, 0.6f, 0.2f);
	light.offset          = irr::core::vector3df(0.0f, 0.0f, 0.0f);

	projectileEntity.activate();

	WeaponProjectile proj;
	proj.speed            = m_projectileSpeed;
	proj.useTracking      = false;
	proj.targetId         = _entity_null_value;
	proj.distanceTraveled = 0.0f;
	proj.isTrackingActive = false;
	proj.entity           = projectileEntity;
	proj.velocity         = launchVelocity;
	proj.previousPosition = spawnPos;
	proj.trailParticles   = nullptr;
	proj.isBouncing       = bounce;
	proj.maxLifetime      = bounce ? 2500.0f : 5000.0f;

	m_projectiles.emplace_back(proj);

	SoundManager::Get()->sound()->playRandomized2D("content/sound/weapon/grenade_launcher/fire", 0.05f);

	playAnimation("fire");

	m_effects.muzzleFlash();

	// Thumpy single-shot kick — lighter than the rocket, heavier than a rifle
	g_CameraFX.addRecoil(-2.0f, Engine::Get()->rng()->getFloat(-0.25f, 0.25f));
	addViewKick(
		irr::core::vector3df(0.0f, 0.02f, -0.09f),
		irr::core::vector3df(4.0f,
			Engine::Get()->rng()->getFloat(-0.6f, 0.6f),
			Engine::Get()->rng()->getFloat(-1.0f, 1.0f)));
}

void Weapon_GrenadeLauncher::updateProjectiles(float dt)
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

		// Create smoke trail once transform node is ready
		if (!it->trailParticles)
		{
			auto* particleSystem = RenderManager::Get()->sceneManager()->addParticleSystemSceneNode(false, transformComp.node);

			auto* emitter = particleSystem->createPointEmitter(
				irr::core::vector3df(0, 0, 0),
				15,
				25,
				irr::video::SColor(255, 180, 180, 180),
				irr::video::SColor(255, 100, 100, 100),
				300,
				600,
				1,
				irr::core::dimension2df(0.10f, 0.10f),
				irr::core::dimension2df(0.15f, 0.15f)
			);

			particleSystem->setEmitter(emitter);
			emitter->drop();

			auto* fadeAffector = particleSystem->createFadeOutParticleAffector();
			particleSystem->addAffector(fadeAffector);
			fadeAffector->drop();

			particleSystem->setMaterialFlag(irr::video::EMF_LIGHTING,        false);
			particleSystem->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE,   false);
			particleSystem->setMaterialFlag(irr::video::EMF_BLEND_OPERATION,  true);
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

		// Swept raycast for frame-rate-independent collision
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
							hitNode      = raycastResult.node;
							hitPoint     = raycastResult.point;
							hitNormal    = raycastResult.normal;
						}
					}
				}
				else if (RenderManager::isWorldGeometryNode(raycastResult.node))
				{
					// Brush chunks / props carry no ECS id — solid surface hit
					hitSomething = true;
					hitNode      = raycastResult.node;
					hitPoint     = raycastResult.point;
					hitNormal    = raycastResult.normal;
				}
			}
		}

		bool shouldRemove = false;
		bool bounced      = false;

		// Where the next frame's swept raycast originates — updated on bounce to sit off the surface
		irr::core::vector3df sweepOrigin = currentPos;

		if (hitSomething && hitNode)
		{
			entityid hitEntityID = hitNode->getID();

			if (it->entity.isValid() && it->entity.hasComponent<DescriptorComponent>() &&
				hitEntityID == it->entity.getComponent<DescriptorComponent>().id)
			{
				// Hit own mesh — ignore
			}
			else if (it->isBouncing)
			{
				if (it->bounceCount >= 1)
				{
					// Second contact — detonate on the surface we struck
					detonateAt(hitPoint, hitEntityID, hitNormal);
					shouldRemove = true;
				}
				else
				{
					// First bounce — reflect velocity with energy loss
					float dot = it->velocity.dotProduct(hitNormal);
					it->velocity -= hitNormal * (2.0f * dot);
					it->velocity *= 0.6f;

					// Push the grenade off the surface so the next sweep doesn't immediately re-detect it
					irr::core::vector3df safePoint = hitPoint + hitNormal * 0.3f;
					sweepOrigin = safePoint;
					transformComp.position = safePoint;
					if (transformComp.node)
						transformComp.node->setPosition(safePoint);

					it->bounceCount++;
					bounced = true;
					SoundManager::Get()->sound()->play3D("content/sound/weapon/grenade_launcher/bounce.wav", hitPoint);
				}
			}
			else
			{
				detonateAt(hitPoint, hitEntityID, hitNormal);
				shouldRemove = true;
			}
		}

		float dtSeconds = dt / 1000.0f;

		// Apply gravity before computing next position so orientation reflects the arc
		it->velocity.Y -= m_gravity * dtSeconds;

		irr::core::vector3df nextPos = sweepOrigin + it->velocity * dtSeconds;

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

		it->previousPosition = sweepOrigin;
		it->lifetime += dt;

		// Bounce grenade timer detonation
		if (it->isBouncing && !shouldRemove && it->lifetime >= it->maxLifetime)
		{
			irr::core::vector3df detonPos = transformComp.node
				? transformComp.node->getAbsolutePosition()
				: nextPos;
			detonateAt(detonPos, _entity_null_value);
			shouldRemove = true;
		}

		if (shouldRemove || (!it->isBouncing && it->lifetime >= it->maxLifetime))
		{
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

void Weapon_GrenadeLauncher::detonateAt(const irr::core::vector3df& pos, entityid directHitID,
	const irr::core::vector3df& surfaceNormal)
{
	SoundManager::Get()->sound()->playRandomized3D("content/sound/effect/explosion", pos, 0.06f);
	ParticleManager::Get()->spawn("explosion", irr2spk(pos));
	applySplashDamage(pos, directHitID);

	// Light flash + scorch (oriented to the hit surface) + smoke + proximity feedback
	m_effects.explosionAt(pos,
		irr::video::SColorf(1.0f, 0.75f, 0.35f), 9.0f, 3.5f, 5.0f, 300.0f,
		surfaceNormal);

	if (directHitID != _entity_null_value)
	{
		registerHitFeedback(WorldManager::Get()->gameplaySystem()->damageEntity(
			directHitID, static_cast<unsigned int>(m_pointDamage)));
	}
}

void Weapon_GrenadeLauncher::applySplashDamage(const irr::core::vector3df& epicentre, entityid directHitEntityID)
{
	if (m_splashRadius <= 0.0f || m_splashDamage <= 0.0f)
		return;

	// One feedback event per detonation regardless of how many entities it caught
	HIT_RESULT bestResult = HIT_RESULT::NONE;

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

		if (damage >= 1.0f)
		{
			HIT_RESULT r = WorldManager::Get()->gameplaySystem()->damageEntity(
				desc.id, static_cast<unsigned int>(damage));

			// Splash-damaging yourself is not a hit confirm
			if (desc.name != "player" && static_cast<int>(r) > static_cast<int>(bestResult))
				bestResult = r;
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

	registerHitFeedback(bestResult);
}

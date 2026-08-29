#include "WeaponEffects.h"

#include "Engine/Engine.h"
#include "Engine/Renderer/RenderManager.h"
#include "Engine/Renderer/DecalManager.h"
#include "Engine/Renderer/Particle/ParticleManager.h"
#include "Engine/Resource/FilePaths.h"
#include "Engine/World/WorldManager.h"

#include "../CameraFX.h"

#include <algorithm>
#include <cstdio>

// Windows.h defines min/max macros; project does not use NOMINMAX
#undef min
#undef max

static const float _shell_gravity = 9.81f;            // units/second^2
static const float _shell_lifetime = 10000.0f;         // ms
static const float _shell_bounce_sound_interval = 150.0f; // ms

void WeaponEffects::init(irr::scene::IAnimatedMeshSceneNode* weaponNode, const WeaponEffectsDesc& desc)
{
	destroy(); // idempotent re-init (editor↔game mode switches)

	m_desc = desc;
	m_weaponNode = weaponNode;

	if (!m_weaponNode)
		return;

	// Resolve the muzzle attachment point once
	if (m_desc.muzzleJointName)
	{
		m_muzzleNode = m_weaponNode->getJointNode(m_desc.muzzleJointName);
		if (!m_muzzleNode)
		{
			spdlog::warn("WeaponEffects: muzzle joint '{}' not found — flash parented to weapon node + offset",
				m_desc.muzzleJointName);
			m_muzzleNode = m_weaponNode;
		}
		createFlashNodes();
	}

	// Pre-build tracer pool (unit-length planes, Z-scaled per frame)
	if (m_desc.tracerPoolSize > 0)
	{
		auto* geo = RenderManager::Get()->sceneManager()->getGeometryCreator();
		auto* tracerTex = RenderManager::Get()->driver()->getTexture(m_desc.tracerTexture);

		m_tracers.resize(m_desc.tracerPoolSize);
		for (auto& tracer : m_tracers)
		{
			irr::scene::IMesh* planeMesh = geo->createPlaneMesh(
				irr::core::dimension2df(m_desc.tracerWidth, 1.0f), irr::core::dimension2du(1, 1));
			tracer.node = RenderManager::Get()->sceneManager()->addMeshSceneNode(planeMesh);
			planeMesh->drop();
			if (!tracer.node)
				continue;

			tracer.node->setMaterialTexture(0, tracerTex);
			tracer.node->setMaterialFlag(irr::video::EMF_LIGHTING, false);
			tracer.node->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE, false);
			tracer.node->setMaterialFlag(irr::video::EMF_BACK_FACE_CULLING, false);
			tracer.node->setMaterialFlag(irr::video::EMF_BLEND_OPERATION, true);
			tracer.node->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);

			// Vertex color drives additive tint (material colors are ignored)
			irr::scene::IMesh* mesh = tracer.node->getMesh();
			for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); b++)
			{
				irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(b);
				for (irr::u32 v = 0; v < buf->getVertexCount(); v++)
					static_cast<irr::video::S3DVertex*>(buf->getVertices())[v].Color = m_desc.tracerColor;
				buf->setDirty(irr::scene::EBT_VERTEX);
			}

			tracer.node->setVisible(false);
		}
	}

	// Pre-build shell pool
	if (m_desc.shellPoolSize > 0 && m_desc.shellMesh)
	{
		auto* shellMesh = RenderManager::Get()->sceneManager()->getMesh(m_desc.shellMesh);
		auto* shellTex  = RenderManager::Get()->driver()->getTexture(m_desc.shellTexture);
		auto perpixelMat = ShaderMaterialManager::get("phong_perpixel");

		// Cached so callers can scale a casing to match geometry it stands in for
		if (shellMesh)
			m_shellMeshExtent = shellMesh->getBoundingBox().getExtent();

		m_shells.resize(m_desc.shellPoolSize);
		for (auto& shell : m_shells)
		{
			shell.node = RenderManager::Get()->sceneManager()->addMeshSceneNode(shellMesh);
			if (!shell.node)
				continue;

			shell.node->setMaterialTexture(0, shellTex);
			shell.node->setMaterialFlag(irr::video::EMF_BILINEAR_FILTER, true);
			shell.node->setMaterialFlag(irr::video::EMF_TRILINEAR_FILTER, true);
			shell.node->setScale(irr::core::vector3df(m_desc.shellScale, m_desc.shellScale, m_desc.shellScale));
			if (perpixelMat != irr::video::EMT_SOLID)
				shell.node->setMaterialType(perpixelMat);
			shell.node->setVisible(false);
		}
	}
}

void WeaponEffects::createFlashNodes()
{
	if (m_flashNode || !m_muzzleNode)
		return;

	// Local to whichever node the flash is parented to. Note the billboard's SIZE
	// is world space regardless — CBillboardSceneNode builds its quad from
	// getAbsolutePosition() and Size against the camera basis, never the parent
	// scale — so only the position here inherits the viewmodel's scale.
	const irr::core::vector3df offset = activeMuzzleOffset();

	m_flashNode = RenderManager::Get()->sceneManager()->addBillboardSceneNode(
		m_muzzleNode,
		irr::core::dimension2df(m_desc.flashSize, m_desc.flashSize),
		offset);

	if (m_flashNode)
	{
		m_flashNode->setMaterialFlag(irr::video::EMF_LIGHTING, false);
		m_flashNode->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE, false);
		m_flashNode->setMaterialFlag(irr::video::EMF_BLEND_OPERATION, true);
		m_flashNode->setMaterialType(m_desc.flashMaterial);

		auto* starTex = RenderManager::Get()->driver()->getTexture(m_desc.flashTexture);
		m_flashNode->setMaterialTexture(0, starTex);
		m_flashNode->setColor(m_desc.flashColor);
		m_flashNode->setVisible(false);

		// Composite onto the tonemapped LDR backbuffer so additive blending is
		// not crushed by the tonemap S-curve (previously only the pistol did this).
		RenderManager::Get()->registerLDREffectNode(m_flashNode);
	}

	if (m_desc.flashLight)
	{
		m_flashLight = RenderManager::Get()->sceneManager()->addLightSceneNode(
			m_muzzleNode, offset, m_desc.lightColor, m_desc.lightRadius);
		if (m_flashLight)
			m_flashLight->setVisible(false);
	}
}

void WeaponEffects::destroy()
{
	m_shellMeshExtent = irr::core::vector3df(0.0f, 0.0f, 0.0f);

	if (m_flashNode)
	{
		RenderManager::Get()->unregisterLDREffectNode(m_flashNode);
		m_flashNode->remove();
		m_flashNode = nullptr;
	}
	if (m_flashLight)
	{
		m_flashLight->remove();
		m_flashLight = nullptr;
	}

	for (auto& tracer : m_tracers)
		if (tracer.node) { tracer.node->remove(); tracer.node = nullptr; }
	m_tracers.clear();

	for (auto& shell : m_shells)
		if (shell.node) { shell.node->remove(); shell.node = nullptr; }
	m_shells.clear();

	for (auto& light : m_explosionLights)
		if (light.node) { light.node->remove(); light.node = nullptr; }
	m_explosionLights.clear();

	m_weaponNode = nullptr;
	m_muzzleNode = nullptr;
	m_flashTime = 1.0e9f;
	m_shotCounter = 0;
}

void WeaponEffects::update(float dt)
{
	updateFlash(dt);
	updateTracers(dt);
	updateShells(dt);
	updateExplosionLights(dt);
}

// --- Muzzle flash -----------------------------------------------------------

void WeaponEffects::muzzleFlash()
{
	if (!m_flashNode || !m_weaponNode)
		return;

	// Sync hierarchy so the flash appears at this frame's muzzle position
	m_weaponNode->updateAbsolutePosition();
	m_muzzleNode->updateAbsolutePosition();

	// Per-shot variation: size ±variance, texture rolled to a random angle
	float size = m_desc.flashSize *
		(1.0f + Engine::Get()->rng()->getFloat(-m_desc.flashSizeVariance, m_desc.flashSizeVariance));
	m_flashNode->setSize(irr::core::dimension2df(size, size));

	float roll = Engine::Get()->rng()->getFloat(0.0f, 2.0f * 3.14159265f);
	m_flashNode->getMaterial(0).getTextureMatrix(0).setTextureRotationCenter(roll);

	m_flashNode->setColor(m_desc.flashColor);
	m_flashNode->setVisible(true);
	m_flashNode->updateAbsolutePosition();

	if (m_flashLight)
	{
		m_flashLight->setVisible(true);
		m_flashLight->updateAbsolutePosition();
	}

	m_flashTime = 0.0f;
}

void WeaponEffects::updateFlash(float dt)
{
	if (!m_flashNode || m_flashTime >= m_desc.flashDuration)
		return;

	m_flashTime += dt;

	if (m_flashTime >= m_desc.flashDuration)
	{
		m_flashNode->setVisible(false);
		if (m_flashLight)
			m_flashLight->setVisible(false);
		return;
	}

	// Two-stage fade: hot and full-bright for the first 30%, quadratic falloff after
	float t = m_flashTime / m_desc.flashDuration;
	float brightness = (t < 0.3f) ? 1.0f : 1.0f - ((t - 0.3f) / 0.7f);
	brightness *= brightness;

	irr::video::SColor c = m_desc.flashColor;
	c.setAlpha((irr::u32)(brightness * 255.0f));
	m_flashNode->setColor(c);
}

// --- Tracers ------------------------------------------------------------------

void WeaponEffects::spawnTracer(const irr::core::vector3df& start, const irr::core::vector3df& end)
{
	if (m_tracers.empty())
		return;

	m_shotCounter++;
	if (m_desc.tracerFrequency > 1 && (m_shotCounter % m_desc.tracerFrequency) != 0)
		return;

	irr::core::vector3df dir = end - start;
	float distance = dir.getLength();
	if (distance < 0.1f)
		return;
	dir /= distance;

	// Find a free pool slot
	Tracer* tracer = nullptr;
	for (auto& t : m_tracers)
		if (!t.active && t.node) { tracer = &t; break; }
	if (!tracer)
		return; // pool exhausted, skip

	tracer->start    = start;
	tracer->dir      = dir;
	tracer->distance = distance;
	tracer->traveled = 0.0f;
	tracer->active   = true;

	// Align the plane's Z axis with the flight direction
	irr::core::vector3df rotation;
	rotation.Y = atan2f(dir.X, dir.Z) * (180.0f / 3.14159265f);
	rotation.X = -asinf(irr::core::clamp(dir.Y, -1.0f, 1.0f)) * (180.0f / 3.14159265f);
	rotation.Z = 0.0f;
	tracer->node->setRotation(rotation);

	// Position/scale set in updateTracers this frame
	tracer->node->setVisible(true);
}

void WeaponEffects::updateTracers(float dt)
{
	float dt_s = dt * 0.001f;

	for (auto& tracer : m_tracers)
	{
		if (!tracer.active || !tracer.node)
			continue;

		tracer.traveled += m_desc.tracerSpeed * dt_s;

		if (tracer.traveled >= tracer.distance)
		{
			tracer.active = false;
			tracer.node->setVisible(false);
			continue;
		}

		// Clip the visible segment so it never pokes past the impact point
		float visibleLen = std::min(m_desc.tracerSegmentLength, tracer.distance - tracer.traveled);
		tracer.node->setScale(irr::core::vector3df(1.0f, 1.0f, visibleLen));
		tracer.node->setPosition(tracer.start + tracer.dir * (tracer.traveled + visibleLen * 0.5f));
	}
}

// --- Shell casings ------------------------------------------------------------

irr::core::vector3df& WeaponEffects::activeMuzzleOffset()
{
	return (m_muzzleNode && m_muzzleNode == m_weaponNode)
		? m_desc.muzzleFallbackOffset
		: m_desc.muzzleJointOffset;
}

irr::core::vector3df WeaponEffects::muzzleWorldPosition()
{
	if (!m_muzzleNode)
		return irr::core::vector3df(0.0f, 0.0f, 0.0f);

	m_muzzleNode->updateAbsolutePosition();

	irr::core::vector3df world = activeMuzzleOffset();
	m_muzzleNode->getAbsoluteTransformation().transformVect(world);

	return world;
}

void WeaponEffects::applyMuzzleOffset()
{
	const irr::core::vector3df& offset = activeMuzzleOffset();

	if (m_flashNode)
		m_flashNode->setPosition(offset);
	if (m_flashLight)
		m_flashLight->setPosition(offset);
}

const irr::core::vector3df& WeaponEffects::shellMeshExtent() const
{
	return m_shellMeshExtent;
}

WeaponEffects::Shell* WeaponEffects::acquireShell()
{
	for (auto& s : m_shells)
		if (!s.active && s.node)
			return &s;

	return nullptr; // pool exhausted
}

bool WeaponEffects::spawnShellAt(const irr::core::vector3df& position,
                                 const irr::core::vector3df& rotation,
                                 const irr::core::vector3df& velocity,
                                 const irr::core::vector3df& scale)
{
	Shell* shell = acquireShell();
	if (!shell)
		return false;

	shell->node->setScale(scale);
	shell->node->setPosition(position);
	shell->rotation = rotation;
	shell->node->setRotation(rotation);
	shell->velocity = velocity;
	shell->angularVelocity = irr::core::vector3df(
		Engine::Get()->rng()->getFloat(-300.0f, 300.0f),
		Engine::Get()->rng()->getFloat(-300.0f, 300.0f),
		Engine::Get()->rng()->getFloat(-300.0f, 300.0f));
	shell->spawnTime     = static_cast<float>(Engine::Get()->getCurrentTime());
	shell->active        = true;
	shell->physicsActive = true;
	shell->bounceCount   = 0;
	shell->node->setVisible(true);

	return true;
}

void WeaponEffects::ejectShell()
{
	if (m_shells.empty() || !m_weaponNode)
		return;

	// Find a free pool slot
	Shell* shell = acquireShell();
	if (!shell)
		return; // pool exhausted, skip

	// Camera basis for eject direction + shell orientation
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>())
		return;

	auto& camera = player.getComponent<CameraComponent>();
	camera.camera->updateAbsolutePosition();
	m_weaponNode->updateAbsolutePosition();
	m_weaponNode->animateJoints();

	irr::core::vector3df target  = camera.camera->getTarget();
	irr::core::vector3df camPos  = camera.camera->getAbsolutePosition();
	irr::core::vector3df forward = (target - camPos).normalize();

	irr::core::vector3df worldUp(0, 1, 0);
	irr::core::vector3df right   = forward.crossProduct(worldUp).normalize();
	irr::core::vector3df localUp = right.crossProduct(forward).normalize();

	// Eject position: named joint, or an offset port relative to the weapon.
	// Note: forward.cross(worldUp) points LEFT in Irrlicht's left-handed system,
	// so -right is the actual rightward direction.
	irr::core::vector3df ejectPosition;
	if (m_desc.shellEjectJoint)
	{
		irr::scene::IBoneSceneNode* ejectBone = m_weaponNode->getJointNode(m_desc.shellEjectJoint);
		if (!ejectBone)
		{
			spdlog::warn("WeaponEffects: eject joint '{}' not found — cannot eject shell", m_desc.shellEjectJoint);
			return;
		}
		ejectBone->updateAbsolutePosition();
		ejectPosition = ejectBone->getAbsolutePosition()
			+ irr::core::vector3df(0.0f, Engine::Get()->rng()->getFloat(-0.1f, 0.1f), 0.0f);
	}
	else
	{
		ejectPosition = m_weaponNode->getAbsolutePosition()
			+ (-right)  * m_desc.shellEjectOffset.X
			+ localUp   * m_desc.shellEjectOffset.Y
			+ forward   * m_desc.shellEjectOffset.Z;
	}

	irr::core::vector3df randomOffset(
		Engine::Get()->rng()->getFloat(-0.3f, 0.3f),
		Engine::Get()->rng()->getFloat(-0.3f, 0.3f),
		Engine::Get()->rng()->getFloat(-0.3f, 0.3f));
	irr::core::vector3df ejectionDir = (-right + localUp + randomOffset).normalize();
	float randomSpeed = m_desc.shellSpeed * Engine::Get()->rng()->getFloat(0.75f, 1.25f);

	// Orient shell to match camera yaw/pitch
	float yaw   = atan2f(forward.X, forward.Z) * (180.0f / 3.14159265f);
	float pitch = asinf(irr::core::clamp(forward.Y, -1.0f, 1.0f)) * (180.0f / 3.14159265f);

	shell->node->setPosition(ejectPosition);
	shell->rotation = irr::core::vector3df(-pitch, yaw, 0.0f);
	shell->node->setRotation(shell->rotation);
	shell->velocity = ejectionDir * randomSpeed;
	shell->angularVelocity = irr::core::vector3df(
		Engine::Get()->rng()->getFloat(-300.0f, 300.0f),
		Engine::Get()->rng()->getFloat(-300.0f, 300.0f),
		Engine::Get()->rng()->getFloat(-300.0f, 300.0f));
	shell->spawnTime     = static_cast<float>(Engine::Get()->getCurrentTime());
	shell->active        = true;
	shell->physicsActive = true;
	shell->bounceCount   = 0;
	shell->node->setVisible(true);
}

void WeaponEffects::updateShells(float dt)
{
	if (m_shells.empty())
		return;

	const float dt_s = dt * 0.001f;
	const float currentTime = static_cast<float>(Engine::Get()->getCurrentTime());

	for (auto& shell : m_shells)
	{
		if (!shell.active || !shell.node)
			continue;

		// Lifetime expiry — return slot to pool
		if (currentTime - shell.spawnTime >= _shell_lifetime)
		{
			shell.active = false;
			shell.node->setVisible(false);
			continue;
		}

		if (!shell.physicsActive)
			continue;

		// Gravity
		shell.velocity.Y -= _shell_gravity * dt_s;

		irr::core::vector3df pos    = shell.node->getPosition();
		irr::core::vector3df newPos = pos + shell.velocity * dt_s;

		// Cast ray along direction of travel — detects floors, walls, ceilings, ramps
		float speed = shell.velocity.getLength();
		if (speed > 0.001f)
		{
			irr::core::vector3df travelDir = shell.velocity / speed;
			irr::core::vector3df rayEnd    = newPos + travelDir * 0.1f;
			RaycastResultData hit = RenderManager::Get()->raycastWorldPosition(pos, rayEnd, true);

			if (hit.hit)
			{
				// Reflect velocity off surface normal: v' = v - 2(v·n)n
				irr::core::vector3df n = hit.normal;
				float dot = shell.velocity.dotProduct(n);
				shell.velocity = (shell.velocity - n * (2.0f * dot)) * 0.45f;
				shell.angularVelocity *= 0.5f;

				// Place shell just off the surface so it doesn't tunnel next frame
				newPos = hit.point + n * 0.05f;

				// Bounce sound — throttled so rapid shell cascades don't stack up
				if (shell.bounceCount < 2 && (currentTime - m_lastShellBounceSound) >= _shell_bounce_sound_interval)
				{
					SoundManager::Get()->sound()->playRandomized3D(
						m_desc.shellBounceSoundBase, shell.node->getPosition(), 0.08f, 0, 0.5f);
					m_lastShellBounceSound = currentTime;
				}

				shell.bounceCount++;

				// After 3 bounces the shell has settled — stop simulating
				if (shell.bounceCount >= 3)
				{
					shell.physicsActive = false;
					shell.velocity = irr::core::vector3df(0, 0, 0);
				}
			}
		}

		shell.node->setPosition(newPos);

		// Spin
		shell.rotation += shell.angularVelocity * dt_s;
		shell.node->setRotation(shell.rotation);
	}
}

// --- Explosions ------------------------------------------------------------------

void WeaponEffects::explosionAt(const irr::core::vector3df& pos,
	irr::video::SColorf lightColor, float lightRadius,
	float shakePeak, float shakeMaxDist, float shakeDurMs,
	const irr::core::vector3df& surfaceNormal)
{
	// --- Light flash: lazily grown pool (explosions can overlap) -------------
	ExplosionLight* slot = nullptr;
	for (auto& l : m_explosionLights)
		if (!l.active) { slot = &l; break; }
	if (!slot && m_explosionLights.size() < 4)
	{
		m_explosionLights.emplace_back();
		slot = &m_explosionLights.back();
	}
	if (slot)
	{
		if (!slot->node)
			slot->node = RenderManager::Get()->sceneManager()->addLightSceneNode(
				nullptr, pos, lightColor, lightRadius);
		if (slot->node)
		{
			// Lift slightly so a ground burst lights the floor around the crater
			slot->node->setPosition(pos + irr::core::vector3df(0.0f, 0.5f, 0.0f));
			slot->node->setRadius(lightRadius);
			slot->node->getLightData().DiffuseColor = lightColor;
			slot->node->setVisible(true);
			slot->color    = lightColor;
			slot->time     = 0.0f;
			slot->duration = 150.0f;
			slot->active   = true;
		}
	}

	// --- Scorch decal ---------------------------------------------------------
	{
		const char* scorchTexture = "content/texture/decal/scorch/Burn Mark 4.png";
		const float scorchSize = 2.8f;

		if (surfaceNormal.getLengthSQ() > 0.5f)
		{
			// Contact detonation: 'pos' is on the impact surface — project the
			// scorch along its normal (works on walls and ceilings too)
			RenderManager::Get()->decals()->spawn(pos, surfaceNormal, scorchSize, scorchTexture);
		}
		else
		{
			// Timer/airburst detonation: probe for a floor below (may find nothing)
			RaycastResultData ground = RenderManager::Get()->raycastWorldPosition(
				pos + irr::core::vector3df(0.0f, 0.5f, 0.0f),
				pos - irr::core::vector3df(0.0f, 2.5f, 0.0f),
				true);
			if (ground.hit)
				RenderManager::Get()->decals()->spawn(ground.point, ground.normal, scorchSize, scorchTexture);
		}
	}

	// --- Lingering smoke (asset-gated: drop in explosion_smoke.psys to enable) ---
	{
		static int s_smokeAvailable = -1; // -1 unknown, 0 missing, 1 ok
		if (s_smokeAvailable < 0)
			s_smokeAvailable = ParticleManager::Get()->precache("explosion_smoke", _asset_psys("explosion_smoke")) ? 1 : 0;
		if (s_smokeAvailable == 1)
			ParticleManager::Get()->spawn("explosion_smoke", SPK::IRR::irr2spk(pos));
	}

	// --- Proximity camera feedback: shake + directional shove + FOV crunch ----
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<TransformComponent>() || !player.hasComponent<CameraComponent>())
		return;

	irr::core::vector3df playerPos = player.getComponent<TransformComponent>().getPosition();
	float dist = (pos - playerPos).getLength();
	float proximity = std::max(0.0f, 1.0f - dist / shakeMaxDist);
	if (proximity <= 0.05f)
		return;

	g_CameraFX.addShake(proximity * shakePeak, shakeDurMs);
	g_CameraFX.addFovKick(-3.0f * proximity); // crunch-in

	// Directional shove — head snaps away from the blast
	auto& camera = player.getComponent<CameraComponent>();
	irr::core::vector3df camPos  = camera.camera->getAbsolutePosition();
	irr::core::vector3df forward = (camera.camera->getTarget() - camPos).normalize();
	irr::core::vector3df toBlast = pos - camPos;
	if (toBlast.getLength() > 0.001f)
	{
		toBlast.normalize();
		// forward × worldUp points LEFT in Irrlicht's left-handed system
		irr::core::vector3df left = forward.crossProduct(irr::core::vector3df(0, 1, 0)).normalize();
		float lateralRight = -toBlast.dotProduct(left);          // + = blast to the right
		float yawShove = -lateralRight * 1.5f * proximity;       // yaw away from it
		g_CameraFX.addRecoil(-2.0f * proximity, yawShove);       // pitch shoved up
	}
}

void WeaponEffects::updateExplosionLights(float dt)
{
	for (auto& light : m_explosionLights)
	{
		if (!light.active || !light.node)
			continue;

		light.time += dt;

		if (light.time >= light.duration)
		{
			light.node->setVisible(false);
			light.active = false;
			continue;
		}

		// Quadratic burn-down: hot flash that dies fast
		float fade = 1.0f - (light.time / light.duration);
		fade *= fade;
		light.node->getLightData().DiffuseColor = irr::video::SColorf(
			light.color.r * fade, light.color.g * fade, light.color.b * fade);
	}
}

// --- Impacts -------------------------------------------------------------------

void WeaponEffects::impact(const irr::core::vector3df& point, const irr::core::vector3df& normal)
{
	if (m_desc.impactParticle)
	{
		uint32_t handle = ParticleManager::Get()->spawn(m_desc.impactParticle, SPK::IRR::irr2spk(point));
		if (handle)
			ParticleManager::Get()->setEmitterDirection(handle, normal);
	}

	if (m_desc.impactDecal)
	{
		RenderManager::Get()->decals()->spawn(point, normal, m_desc.impactDecalSize, m_desc.impactDecal);
	}
}

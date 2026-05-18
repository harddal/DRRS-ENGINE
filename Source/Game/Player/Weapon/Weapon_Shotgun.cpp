#include "Weapon_Shotgun.h"

#include "Engine/Engine.h"
#include "../CameraFX.h"

#undef MB_RIGHT

using namespace irr;
using namespace SPK;
using namespace SPK::IRR;

void Weapon_Shotgun::precache()
{

}

void Weapon_Shotgun::init()
{
	m_descriptor.name = "Player_Weapon_Shotgun";
	m_descriptor.id = _entity_null_value;

	m_viewPositionOffset = irr::core::vector3df(0.3f, -0.1f, 0.4f);
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

	initImpactParticleSystem();

	// Pre-build shell casing pool (no per-shot alloc)
	auto* shellMesh = RenderManager::Get()->sceneManager()->getMesh("content/mesh/prop/shells/slug.obj");
	auto* shellTex  = RenderManager::Get()->driver()->getTexture("content/mesh/prop/shells/shellsColor.png");
	for (int i = 0; i < SHELL_POOL_SIZE; i++)
	{
		m_shellPool[i].node = RenderManager::Get()->sceneManager()->addMeshSceneNode(shellMesh);
		if (m_shellPool[i].node)
		{
			m_shellPool[i].node->setMaterialTexture(0, shellTex);
			m_shellPool[i].node->setMaterialFlag(irr::video::EMF_BILINEAR_FILTER, true);
			m_shellPool[i].node->setMaterialFlag(irr::video::EMF_TRILINEAR_FILTER, true);
			m_shellPool[i].node->setRotation(irr::core::vector3df(0.0f, 180.0f, 0.0f));
			m_shellPool[i].node->setScale(irr::core::vector3df(1.0f, 1.0f, 1.0f));
			m_shellPool[i].node->setMaterialType(perpixelMat);
			m_shellPool[i].node->setVisible(false);
		}
		m_shellPool[i].active = false;
	}
}

void Weapon_Shotgun::destroy()
{
	for (auto* sys : m_impactParticleSystems)
		destroyImpactParticleSystem(sys);
	m_impactParticleSystems.clear();

	// Clean up shell casing pool
	for (int i = 0; i < SHELL_POOL_SIZE; i++)
	{
		if (m_shellPool[i].node)
		{
			m_shellPool[i].node->remove();
			m_shellPool[i].node = nullptr;
		}
	}

	RenderManager::Get()->unregisterViewmodelNode(m_mesh.node);
	m_mesh.node->remove();
	WorldManager::Get()->freeEntityID(m_descriptor.id);
}

void Weapon_Shotgun::update()
{
	bool animEnded = m_mesh.animation_call_back->hasAnimationEnded();

	// Unequip: wait for anim to finish, then hide
	if (m_isUnequipping)
	{
		if (animEnded)
		{
			m_isUnequipping = false;
			m_mesh.node->setVisible(false);
		}

		// Recoil recovery still ticks during unequip
		float dt = Engine::Get()->getDeltaTime() / 1000.0f;
		float recovery = m_recoilRecoverySpeed * dt;
		if (m_currentRecoilRotation > 0.0f)
			m_currentRecoilRotation -= std::min(recovery, m_currentRecoilRotation);
		else
			m_currentRecoilRotation += std::min(recovery, -m_currentRecoilRotation);
		if (m_currentRecoilHorizontal > 0.0f)
			m_currentRecoilHorizontal -= std::min(recovery, m_currentRecoilHorizontal);
		else
			m_currentRecoilHorizontal += std::min(recovery, -m_currentRecoilHorizontal);
		m_currentRecoilPosition -= std::min(recovery * 0.005f, m_currentRecoilPosition);

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

	// Recoil recovery
	{
		float dt = Engine::Get()->getDeltaTime() / 1000.0f;
		float recovery = m_recoilRecoverySpeed * dt;

		if (m_currentRecoilRotation > 0.0f)
			m_currentRecoilRotation -= std::min(recovery, m_currentRecoilRotation);
		else
			m_currentRecoilRotation += std::min(recovery, -m_currentRecoilRotation);

		if (m_currentRecoilHorizontal > 0.0f)
			m_currentRecoilHorizontal -= std::min(recovery, m_currentRecoilHorizontal);
		else
			m_currentRecoilHorizontal += std::min(recovery, -m_currentRecoilHorizontal);

		m_currentRecoilPosition -= std::min(recovery * 0.005f, m_currentRecoilPosition);

		irr::core::vector3df pos = m_viewPositionOffset;
		irr::core::vector3df rot = m_viewRotationOffset;
		pos.Y += m_currentRecoilPosition;
		rot.X -= m_currentRecoilRotation;
		rot.Y += m_currentRecoilHorizontal;
		m_mesh.node->setPosition(pos);
		m_mesh.node->setRotation(rot);
	}

	RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
}

void Weapon_Shotgun::persist()
{
	float dt = Engine::Get()->getDeltaTime();

	updateShells(dt);

	// Tick impact particle systems
	auto it = m_impactParticleSystems.begin();
	while (it != m_impactParticleSystems.end())
	{
		if (!(*it)->update(dt / m_impactUpdateRate))
		{
			destroyImpactParticleSystem(*it);
			it = m_impactParticleSystems.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void Weapon_Shotgun::equip()
{
	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag
	m_isEquipping   = true;
	m_isUnequipping = false;
	m_isAnimating   = false;
	m_firedThisPress = false;
	m_currentRecoilRotation   = 0.0f;
	m_currentRecoilHorizontal = 0.0f;
	m_currentRecoilPosition   = 0.0f;

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
	m_mesh.node->setVisible(false);
}

void Weapon_Shotgun::startUnequip()
{
	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag
	m_isUnequipping  = true;
	m_isAnimating    = false;
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

			WorldManager::Get()->gameplaySystem()->damageEntity(hitID, static_cast<unsigned int>(m_damagePerPellet));

			// Spark impact effect at hit point
			createImpactParticleSystem(IRR::irr2spk(raycastResult.point));
		}
	}

	// Eject shell casing
	ejectShell();

	// Heavy recoil kick
	float verticalRecoil   = m_recoilAmount + ((rand() / (float)RAND_MAX) * 2.0f - 1.0f) * m_recoilRandomnessVertical;
	float horizontalRecoil = ((rand() / (float)RAND_MAX) * 2.0f - 1.0f) * m_recoilRandomnessHorizontal;

	m_currentRecoilRotation   += verticalRecoil;
	m_currentRecoilHorizontal += horizontalRecoil;
	m_currentRecoilPosition   += m_recoilPositionKick;

	// Camera shake for the big kick
	g_CameraFX.addRecoil(verticalRecoil * 0.4f, horizontalRecoil * 0.2f);
	g_CameraFX.addShake(1.5f, 120.0f);

	SoundManager::Get()->sound()->play2D("content/sound/weapon/shotgun/fire.wav");
}

void Weapon_Shotgun::reload()
{
	if (m_isAnimating)
		return;

	m_currentRecoilRotation   = 0.0f;
	m_currentRecoilHorizontal = 0.0f;
	m_currentRecoilPosition   = 0.0f;

	m_mesh.node->setLoopMode(false);
	m_mesh.node->setFrameLoop(96, 179);
	m_isAnimating = true;
}

void Weapon_Shotgun::initImpactParticleSystem()
{
	// --- Renderers ---

	// Sparks: direction-aligned thin quads, additive
	IRRQuadRenderer* sparkRenderer = IRRQuadRenderer::create(RenderManager::Get()->device());
	sparkRenderer->setTexturingMode(TEXTURE_2D);
	sparkRenderer->setTexture(RenderManager::Get()->driver()->getTexture("content/texture/particle/spark1.bmp"));
	sparkRenderer->setBlending(BLENDING_ADD);
	sparkRenderer->enableRenderingHint(DEPTH_WRITE, false);
	sparkRenderer->setOrientation(DIRECTION_ALIGNED);
	sparkRenderer->setScale(0.04f, 1.0f); // thin streaks
	sparkRenderer->setShared(true);

	// Smoke puff: alpha-blended small billboards
	IRRQuadRenderer* smokeRenderer = IRRQuadRenderer::create(RenderManager::Get()->device());
	smokeRenderer->setTexturingMode(TEXTURE_2D);
	smokeRenderer->setTexture(RenderManager::Get()->driver()->getTexture("content/texture/particle/smoke_04.png"));
	smokeRenderer->setBlending(BLENDING_ALPHA);
	smokeRenderer->enableRenderingHint(DEPTH_WRITE, false);
	smokeRenderer->setShared(true);

	// --- Models ---

	// Spark model: bright white-orange, fades fast, small
	Model* sparkModel = Model::create(FLAG_RED | FLAG_GREEN | FLAG_BLUE | FLAG_ALPHA | FLAG_SIZE,
		FLAG_ALPHA | FLAG_RED | FLAG_GREEN | FLAG_BLUE,
		FLAG_SIZE);
	sparkModel->setParam(PARAM_RED,   1.0f, 1.0f);
	sparkModel->setParam(PARAM_GREEN, 0.9f, 0.3f);
	sparkModel->setParam(PARAM_BLUE,  0.4f, 0.0f);
	sparkModel->setParam(PARAM_ALPHA, 1.0f, 0.0f);
	sparkModel->setParam(PARAM_SIZE,  0.1f, 0.25f);
	sparkModel->setLifeTime(0.3f, 0.9f);
	sparkModel->setShared(true);

	// Smoke model: gray, puffs out then fades
	Model* smokeModel = Model::create(FLAG_RED | FLAG_GREEN | FLAG_BLUE | FLAG_ALPHA | FLAG_SIZE,
		FLAG_SIZE,
		FLAG_NONE,
		FLAG_ALPHA);
	smokeModel->setParam(PARAM_RED,   0.5f);
	smokeModel->setParam(PARAM_GREEN, 0.45f);
	smokeModel->setParam(PARAM_BLUE,  0.4f);
	smokeModel->setParam(PARAM_SIZE,  0.08f, 0.15f, 0.22f, 0.4f);
	smokeModel->setLifeTime(0.4f, 1.0f);
	smokeModel->setShared(true);

	Interpolator* smokeAlpha = smokeModel->getInterpolator(PARAM_ALPHA);
	smokeAlpha->addEntry(0.0f,  0.0f);
	smokeAlpha->addEntry(0.1f,  0.3f, 0.45f);
	smokeAlpha->addEntry(0.55f, 0.2f, 0.35f);
	smokeAlpha->addEntry(1.0f,  0.0f);

	// --- Zones ---
	Sphere* impactSphere = Sphere::create(Vector3D(0.0f, 0.0f, 0.0f), 0.08f);

	// --- Emitters ---

	// Sparks shoot outward with some force
	NormalEmitter* sparkEmitter = NormalEmitter::create();
	sparkEmitter->setZone(impactSphere);
	sparkEmitter->setFlow(-1);
	sparkEmitter->setTank(14);
	sparkEmitter->setForce(2.5f, 5.5f);

	// Smoke drifts softly upward
	RandomEmitter* smokeEmitter = RandomEmitter::create();
	smokeEmitter->setZone(Sphere::create(Vector3D(0.0f, 0.0f, 0.0f), 0.04f), false);
	smokeEmitter->setFlow(-1);
	smokeEmitter->setTank(5);
	smokeEmitter->setForce(0.05f, 0.15f);

	// --- Groups ---

	Group* sparkGroup = Group::create(sparkModel, 14);
	sparkGroup->addEmitter(sparkEmitter);
	sparkGroup->setRenderer(sparkRenderer);
	sparkGroup->setGravity(Vector3D(0.0f, -3.5f, 0.0f));

	Group* smokeGroup = Group::create(smokeModel, 5);
	smokeGroup->addEmitter(smokeEmitter);
	smokeGroup->setRenderer(smokeRenderer);
	smokeGroup->setGravity(Vector3D(0.0f, 0.1f, 0.0f));
	smokeGroup->setFriction(0.4f);

	// --- Build template system ---

	auto* system = IRRSystem::create(
		RenderManager::Get()->sceneManager()->getRootSceneNode(),
		RenderManager::Get()->sceneManager());
	system->addGroup(sparkGroup);
	system->addGroup(smokeGroup);
	system->setAutoUpdateEnabled(false, false);
	static_cast<irr::scene::ISceneNode*>(system)->setVisible(false);

	m_impactParticleBaseID = system->getSPKID();
}

void Weapon_Shotgun::createImpactParticleSystem(const SPK::Vector3D& pos)
{
	if (m_impactParticleBaseID == SPK::NO_ID)
		return;

	SPK::System* system = SPK_Copy(SPK::System, m_impactParticleBaseID);

	IRRSystem* irrSystem = static_cast<IRRSystem*>(system);
	if (irrSystem)
	{
		irrSystem->setVisible(true);
		irrSystem->setPosition(SPK::IRR::spk2irr(pos));
		irrSystem->updateAbsolutePosition();
	}
	else
	{
		system->setTransformPosition(pos);
		system->updateTransform();
	}

	m_impactParticleSystems.push_back(system);
}

void Weapon_Shotgun::destroyImpactParticleSystem(SPK::System*& system)
{
	SPK_Destroy(system);
	system = nullptr;
}

void Weapon_Shotgun::ejectShell()
{
	if (!m_mesh.node)
		return;

	// Find a free slot in the pool
	ShellCasing* shell = nullptr;
	for (int i = 0; i < SHELL_POOL_SIZE; i++)
	{
		if (!m_shellPool[i].active)
		{
			shell = &m_shellPool[i];
			break;
		}
	}
	if (!shell || !shell->node)
		return; // Pool exhausted, skip

	// Derive ejection vectors from current camera orientation
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>())
		return;

	auto& camera = player.getComponent<CameraComponent>();
	irr::core::vector3df target  = camera.camera->getTarget();
	irr::core::vector3df camPos  = camera.camera->getAbsolutePosition();
	irr::core::vector3df forward = (target - camPos).normalize();

	irr::core::vector3df worldUp(0, 1, 0);
	irr::core::vector3df right   = forward.crossProduct(worldUp).normalize();
	irr::core::vector3df localUp = right.crossProduct(forward).normalize();

	// Eject port is to the right of the weapon, slightly behind and above.
	// In Irrlicht's left-handed system, forward.crossProduct(worldUp) points left,
	// so negate right to get the actual rightward direction.
	m_mesh.node->updateAbsolutePosition();
	irr::core::vector3df ejectPosition = m_mesh.node->getAbsolutePosition()
		+ (-right) * 0.15f
		+ localUp  * 0.05f
		+ forward  * (-0.1f);

	irr::core::vector3df randomOffset(
		Engine::Get()->rng()->getFloat(-0.2f, 0.2f),
		Engine::Get()->rng()->getFloat(-0.2f, 0.2f),
		Engine::Get()->rng()->getFloat(-0.2f, 0.2f)
	);
	irr::core::vector3df ejectionDir = (-right + localUp * 0.5f + randomOffset).normalize();
	float randomSpeed = m_shellEjectionSpeed * Engine::Get()->rng()->getFloat(0.75f, 1.25f);

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
		Engine::Get()->rng()->getFloat(-300.0f, 300.0f)
	);
	shell->spawnTime     = static_cast<float>(Engine::Get()->getCurrentTime());
	shell->active        = true;
	shell->physicsActive = true;
	shell->bounceCount   = 0;
	shell->node->setVisible(true);
}

void Weapon_Shotgun::updateShells(float dt)
{
	const float dt_s = dt * 0.001f; // ms -> seconds
	const float currentTime = static_cast<float>(Engine::Get()->getCurrentTime());
	const float shellLifetime = 10000.0f; // ms

	for (int i = 0; i < SHELL_POOL_SIZE; i++)
	{
		ShellCasing& shell = m_shellPool[i];
		if (!shell.active)
			continue;

		// Lifetime expiry — return slot to pool
		if (currentTime - shell.spawnTime >= shellLifetime)
		{
			shell.active = false;
			shell.node->setVisible(false);
			continue;
		}

		if (!shell.physicsActive)
			continue;

		// Gravity
		shell.velocity.Y -= m_shellGravity * dt_s;

		// Candidate new position
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

				newPos = hit.point + n * 0.05f;

				// Bounce sound — throttled so rapid cascades don't stack
				if (shell.bounceCount < 2 && (currentTime - m_lastShellBounceSound) >= m_shellBounceSoundInterval)
				{
					SoundManager::Get()->sound()->play3D("content/sound/prop/shotgunshell.wav", shell.node->getPosition(), false, false, true, 0, 0.5f);
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

#include "Weapon_Crossbow.h"

#include <cmath>

#include "Engine/Engine.h"
#include "Engine/Renderer/DecalManager.h"
#include "Utility/Utility.h"

#include "../CameraFX.h"

#undef MB_RIGHT

using namespace SPK;
using namespace SPK::IRR;

void Weapon_Crossbow::precache()
{
	ParticleManager::Get()->precache("spark", _asset_psys("spark"));

	// Equip/unequip are preloaded centrally by PlayerWeapon::precacheSharedSounds().
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/crossbow_stretch.wav", true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/crossbow_fire_1.wav",  true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/crossbow_fire_2.wav",  true);

	// Borrowed from the shotgun as a stand-in for seating the bolt. Already
	// preloaded by Weapon_Shotgun::precache(), but loadOrGetSource() dedupes by
	// path, so asking again costs nothing and keeps this weapon self-contained.
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/insert_shell_shotgun.wav", true);
}

void Weapon_Crossbow::init()
{
	m_descriptor.name = "Player_Weapon_Crossbow";
	m_descriptor.id = _entity_null_value;

	m_weapon_type = WEAP_CROSSBOW;

	// Same arms rig as the other glTF weapons, so the same starting transform.
	// Tune with the viewmodel debug UI (F2), not by guessing here.
	m_viewPositionOffset = irr::core::vector3df(0.1000f, -0.21f, 0.1800f);
	m_viewRotationOffset = irr::core::vector3df(0.0f, 180.0f, 0.0f);
	m_viewScaleOffset    = irr::core::vector3df(0.01f, 0.01f, 0.01f);

	m_mesh.mesh = _asset_glb("player/weapon/crossbow_animated");

	m_mesh.trimesh = RenderManager::Get()->loadMesh(m_mesh.mesh);

	// Swap in the stand-in BEFORE the node is created — creating a node in the
	// failure branch and again below orphans the first one.
	const bool usingStandIn = (m_mesh.trimesh == nullptr);
	if (usingStandIn)
	{
		spdlog::warn("PlayerWeapon::init(): failed to load mesh \"{}\", stand-in mesh loaded", m_mesh.mesh);
		m_mesh.trimesh = RenderManager::Get()->sceneManager()->getMesh("content/mesh/primitive/double_tetrahedron.obj");
	}

	m_mesh.node = RenderManager::Get()->sceneManager()->addAnimatedMeshSceneNode(m_mesh.trimesh, nullptr, m_descriptor.id);

	if (usingStandIn)
		m_mesh.node->setMaterialTexture(0, RenderManager::Get()->driver()->getTexture("content/texture/color/magenta.png"));

	m_mesh.node->setMaterialFlag(irr::video::EMF_BILINEAR_FILTER, true);
	m_mesh.node->setMaterialFlag(irr::video::EMF_TRILINEAR_FILTER, true);
	m_mesh.node->setMaterialFlag(irr::video::EMF_ANISOTROPIC_FILTER, true);
	m_mesh.node->setMaterialFlag(irr::video::EMF_ANTI_ALIASING, true);
	m_mesh.node->setMaterialFlag(irr::video::EMF_USE_MIP_MAPS, true);

	// One "allanims" take, 0-170 at 30 fps. This asset has far fewer rest holds
	// than the others — only 0, 99/100, 124/125, 149/150 and 170 — because a
	// fired crossbow CANNOT return to the rest pose: rest is cocked-and-loaded,
	// and after the shot the string is forward and the rail empty. Frames 0-99
	// are therefore one continuous shoot-and-recock take. It is split below so
	// the two halves can carry their own cues and speeds, but they must run back
	// to back — hence Firing always handing off to Reloading.
	//
	//   0-29    trigger f1-9, string looses and the bolt launches f1-3, then the
	//           arms move into the cocking position f10-29        -> fire
	//   30-99   puller draws the string f30-33, loader swings 75 deg f30-43, the
	//           next bolt is placed f64-76, settles by f99         -> reload
	//   101-111 root Y drops to -9.42                              -> unequip
	//   111-124 back up, overshoots to +2.24, settles              -> equip
	//   126-149 crossbow shifts 0.96 units, fingers under 0.36     -> a subtle idle,
	//           unused: idle is pinned to the rest hold at 100 and the
	//           hold-steady motion comes from enableIdleBreathing()
	//   151-169 root Y rises to 4.5 and returns                    -> melee bash
	//
	// Holster and draw share frame 111, the bottom of the drop, the same way the
	// revolver's and shotgun's do.
	//
	// melee ends at 169, not 170: Irrlicht clamps EndFrame to getFrameCount()-1
	// and CSkinnedMesh::getFrameCount() returns the last frame INDEX.
	m_mesh.animationList.emplace_back(sAnimationData("fire",    0,   29,  false));
	m_mesh.animationList.emplace_back(sAnimationData("reload",  30,  99,  false));
	m_mesh.animationList.emplace_back(sAnimationData("idle",    100, 100, true));
	m_mesh.animationList.emplace_back(sAnimationData("unequip", 101, 111, false));
	m_mesh.animationList.emplace_back(sAnimationData("equip",   111, 124, false));
	// Authored but not bound to input — a bash, if a melee slot is wanted
	m_mesh.animationList.emplace_back(sAnimationData("melee",   151, 169, false));

	// Both glTF backends normalise keyframe times to 30 fps Irrlicht frames, so
	// the viewmodel must play at 30 to run at its authored speed.
	m_mesh.fps = 30;
	m_mesh.node->setAnimationSpeed(static_cast<irr::f32>(m_mesh.fps));

	playAnimation("idle"); // safe default until equip() runs

	// The idle clip is a single rest frame, so the hold-steady motion comes from
	// updateWeaponSway() instead. A crossbow is heavy and held well out front.
	enableIdleBreathing(1.3f);

	m_mesh.node->setJointMode(irr::scene::EJUOR_READ);

	m_mesh.animation_call_back = std::make_shared<AnimationCallback>();
	m_mesh.node->setAnimationEndCallback(m_mesh.animation_call_back.get());

	m_mesh.node->setScale(m_viewScaleOffset);

	auto perpixelMat = ShaderMaterialManager::get("phong_perpixel");
	if (perpixelMat != irr::video::EMT_SOLID)
		m_mesh.node->setMaterialType(perpixelMat);

	for (auto i = 0; i < m_mesh.node->getMaterialCount(); i++)
	{
		m_mesh.node->getMaterial(i).Shininess = 0.f;
		m_mesh.node->getMaterial(i).SpecularColor.setAlpha(0);
	}

	// Must come AFTER the material assignment above — it caches the bolt's real
	// material type so setMeshPartVisible() has something to restore.
	resolveMeshPart("stick", m_bolt);

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

	m_crosshair = RenderManager::Get()->driver()->getTexture("content/texture/ui/crosshair/crosshair038.png");

	// No muzzle flash and no brass: a crossbow has neither. The effects module is
	// composed purely for its impact handling, so muzzleJointName is nulled (that
	// is what disables the flash and its light) and both pools are left at zero.
	WeaponEffectsDesc fx;
	fx.muzzleJointName = nullptr;
	fx.tracerPoolSize  = 0;
	fx.shellPoolSize   = 0;
	fx.shellMesh       = nullptr;
	fx.impactParticle  = "spark";
	fx.impactDecalSize = 0.12f;
	m_effects.init(m_mesh.node, fx);
}

void Weapon_Crossbow::destroy()
{
	// Any bolt still in flight owns an ECS entity; dropping the weapon without
	// killing them would strand them in the world.
	for (auto& proj : m_projectiles)
	{
		if (proj.entity.isValid() && proj.entity.hasComponent<DescriptorComponent>())
			WorldManager::Get()->killEntityByID(proj.entity.getComponent<DescriptorComponent>().id);
	}
	m_projectiles.clear();

	m_effects.destroy();

	RenderManager::Get()->unregisterViewmodelNode(m_mesh.node);
	m_mesh.node->remove();

	WorldManager::Get()->freeEntityID(m_descriptor.id);
}

// Single place that starts a clip and moves the machine, so the "what plays
// next" rules stay in one readable block.
void Weapon_Crossbow::enterState(State next)
{
	m_state = next;
	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag

	setClipSpeed(next == State::Reloading ? m_reloadSpeed : 1.0f);

	switch (next)
	{
	case State::Firing:      playAnimation("fire");    break;

	case State::Reloading:
		playAnimation("reload");
		m_insertBoltPlayed = false;

		// The string is drawn on the FIRST frames of this clip, so the cue belongs
		// on the state transition rather than on a frame poll. Measured off the
		// GLB: the wire tips sit 43.59 from the stock while loosed and travel to
		// the cocked 17.49 over f30-f33 at a flat 6.52/frame, then hold there for
		// the entire remaining 66 frames — f30-33 is the only draw in the clip.
		//
		// Triggering on entry also means a future m_reloadSpeed change cannot
		// desync it, which a frame-number cue would.
		//
		// The .wav is 711 ms with its audible ratchet running ~20-580 ms, against
		// a 133 ms draw at 1x, so the creak outlives the visible pull and carries
		// into the bolt placement. That reads as the mechanism still under load
		// rather than as a mistake, but it is the thing to shorten if it grates.
		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/crossbow_stretch", 0.03f, 1, -1.0f, "crossbow_stretch");
		break;

	case State::Equipping:   playAnimation("equip");   break;
	case State::Unequipping: playAnimation("unequip"); break;

	case State::Idle:
	default:
		playAnimation("idle");
		setMeshPartVisible(m_bolt, m_boltLoaded);
		break;
	}
}

void Weapon_Crossbow::update()
{
	if (!m_mesh.node)
		return;

	const bool animEnded = m_mesh.animation_call_back->hasAnimationEnded();
	const irr::f32 frame = m_mesh.node->getFrameNr();

	updateBolt(frame);

	switch (m_state)
	{
	// Holstering: stay visible until the clip finishes so the crossbow is seen
	// being put away. isUnequipping() going false releases WeaponController's
	// pending switch, so the next weapon is only drawn once this one is down.
	case State::Unequipping:
		if (animEnded)
			unequip();
		RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
		return;

	case State::Equipping:
		// Drawn empty — because the last reload was interrupted by the switch —
		// so finish the job instead of dropping into Idle. Idle is frame 100, a
		// cocked-and-loaded pose; there is no uncocked rest pose in the asset, so
		// standing in Idle without a bolt would be a lie the animation cannot
		// tell. Going straight to the recock is the only honest option.
		if (animEnded)
			enterState(m_boltLoaded ? State::Idle : State::Reloading);
		RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
		return;

	// The shot always hands off to the recock. There is no uncocked idle pose in
	// the asset, so this is not a design choice so much as the only thing the
	// animation supports.
	case State::Firing:
		if (animEnded)
			enterState(State::Reloading);
		RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
		return;

	// Deliberately not interruptible: one bolt is one shot, and drawing the
	// string is a single mechanical action with no half-way state to stop at.
	case State::Reloading:
		// Credit the bolt the instant it seats rather than when the clip ends, so
		// a switch after f75 keeps it — the same "credit as it is seated" rule the
		// revolver uses per round. Before f75 the crossbow is genuinely empty and
		// holsters that way.
		// m_boltLoaded doubles as the credit guard: both this and the animEnded
		// path below run the same draw, and testing it is what stops a recock
		// that reaches both from taking two bolts out of the quiver for one on
		// the rail. A dry quiver leaves it false, and the gun holsters empty.
		if (frame >= static_cast<irr::f32>(m_boltSeatFrame) && !m_boltLoaded)
			m_boltLoaded = drawFromReserve(1) > 0;

		if (animEnded)
		{
			if (!m_boltLoaded) // clip ran out; covers a frame skipped past f75
				m_boltLoaded = drawFromReserve(1) > 0;

			enterState(State::Idle);
		}

		// Bolt seating. Measured off the GLB: the stick cuts into the hand at f64
		// (hence m_boltReturnFrame), rides up f67-74, and is dead still from f75
		// to the end of the clip — so f75 is the seat. The shotgun's shell insert
		// stands in for the click, and its placement here is deliberate: the
		// stretch cue at the top of the clip runs dry around f47, leaving the back
		// half silent. This lands in that gap.
		else if (!m_insertBoltPlayed &&
			frame >= static_cast<irr::f32>(m_boltSeatFrame - soundLeadFrames(m_insertBoltLeadSec)))
		{
			m_insertBoltPlayed = true;
			SoundManager::Get()->sound()->playRandomized2D(
				"content/sound/weapon/insert_shell_shotgun", 0.06f, 1, -1.0f, "crossbow_insert");
		}

		RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
		return;

	case State::Idle:
	default:
		break;
	}

	// --- Idle: input is live -------------------------------------------------

	const float currentTime = Engine::Get()->getCurrentTime();

	const bool lmb = InputManager::Get()->isMouseButtonPressed(MB_LEFT);
	if (!lmb)
		m_firedThisPress = false;

	// m_boltLoaded should always be true by the time Idle is reachable — Equipping
	// diverts to the recock when it is not — but the guard is kept so no future
	// path can loose a bolt the crossbow does not have.
	if (lmb && !m_firedThisPress && m_boltLoaded && (currentTime - m_lastFireTime) >= m_fireRate)
	{
		m_lastFireTime   = currentTime;
		m_firedThisPress = true;
		fire();
	}

	// No manual reload: the crossbow recocks itself as part of every shot, so
	// reload() has nothing to do from Idle.

	RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
}

// The 'stick' mesh is the bolt, and it plays two parts in one take.
void Weapon_Crossbow::updateBolt(float frame)
{
	if (m_bolt.buffers.empty())
		return;

	const int f = static_cast<int>(frame);

	switch (m_state)
	{
	case State::Firing:
		// Loosed. The animation would fly it to Z 63.37 over three frames and
		// then park it there, but a real projectile is already doing that job —
		// two bolts would be visible at once. Hidden from the moment of release.
		setMeshPartVisible(m_bolt, false);
		break;

	case State::Reloading:
		// It reappears down in the hand at f64 as the NEXT bolt and rides up to
		// the rail by f76. Keeping it hidden until then is what stops it snapping
		// back from where it was launched.
		setMeshPartVisible(m_bolt, f >= m_boltReturnFrame);
		break;

	// Idle, Equipping, Unequipping — the rail shows a bolt only if one is on it.
	// Without the flag here, holstering mid-reload and drawing again would put a
	// bolt on screen during the equip clip and then have the recock place a
	// second one on top of it.
	default:
		setMeshPartVisible(m_bolt, m_boltLoaded);
		break;
	}
}

void Weapon_Crossbow::persist()
{
	const float dt = Engine::Get()->getDeltaTime();

	m_effects.update(dt);

	// Bolts keep flying while the weapon is holstered, so this runs from persist()
	// rather than update().
	updateProjectiles(dt);
}

void Weapon_Crossbow::fire()
{
	launchBolt();

	// The rail is empty from here until the recock seats the next one at f75.
	m_boltLoaded = false;

	// Kick is modest: a crossbow releases stored energy, it does not detonate.
	addViewKick(
		irr::core::vector3df(0.0f, 0.01f, -0.04f),
		irr::core::vector3df(3.0f, 0.0f, Engine::Get()->rng()->getFloat(-0.6f, 0.6f)));

	g_CameraFX.addRecoil(-0.9f, Engine::Get()->rng()->getFloat(-0.1f, 0.1f));

	// Two takes, picked at random. Note the TRAILING UNDERSCORE in the base path:
	// getVariantSet() scans base + "1" + ".wav", so the assets being named
	// crossbow_fire_1.wav / crossbow_fire_2.wav means the base has to end in '_'
	// for the scan to resolve them. Dropping a _3 in extends the set for free.
	//
	// The string releases over f0-f2 of the fire clip, which is where this sits,
	// so no lead-in is needed.
	SoundManager::Get()->sound()->playRandomized2D(
		"content/sound/weapon/crossbow_fire_", 0.05f, 2, -1.0f, "crossbow_fire");

	enterState(State::Firing);
}

// Spawns the bolt on a ballistic arc solved to land where the crosshair points.
//
// Same solve as the grenade launcher, with the numbers pushed to the flat end —
// a fast bolt against weak gravity. The arc is shallow enough to aim straight
// with at normal range, but it is a real parabola, so distant shots drop.
void Weapon_Crossbow::launchBolt()
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>() || !m_mesh.node)
		return;

	auto& camera = player.getComponent<CameraComponent>();

	// Force the hierarchy current before reading the rail — the viewmodel is
	// hidden during drawAll(), so OnAnimate() skipped its joints.
	camera.camera->updateAbsolutePosition();
	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();

	// Launch from the bolt's own tip. Better than a muzzle offset would be: the
	// asset already tells us exactly where the projectile sits.
	irr::core::matrix4 boltWorld;
	irr::core::vector3df spawnPos;
	if (meshPartWorldTransform(m_bolt, boltWorld))
	{
		spawnPos = m_boltTipOffset;
		boltWorld.transformVect(spawnPos);
	}
	else
	{
		spawnPos = m_mesh.node->getAbsolutePosition();
	}

	// Where the crosshair actually lands, so the arc is solved to hit it.
	//
	// Deliberately NOT the grenade launcher's inline version of this: that one
	// ends its ray at camera.targetNode, which CameraComponent parks at a fixed
	// (0, 0, 100) local, so it silently caps the aim at 100 units no matter what
	// is under the crosshair. The shared helper takes the range explicitly.
	const irr::core::vector3df aimTarget = getCrosshairAimPoint(m_maxAimRange);

	// Low-arc launch velocity that passes through aimTarget
	irr::core::vector3df launchVelocity;
	{
		irr::core::vector3df toTarget(aimTarget.X - spawnPos.X, 0.0f, aimTarget.Z - spawnPos.Z);
		const float d = toTarget.getLength();
		const float h = aimTarget.Y - spawnPos.Y;

		bool solved = false;
		if (d > 0.01f)
		{
			const float v2   = m_projectileSpeed * m_projectileSpeed;
			const float disc = v2 * v2 - m_gravity * (m_gravity * d * d + 2.0f * h * v2);
			if (disc >= 0.0f)
			{
				// Minus root: the flat solution. The plus root is the mortar-like
				// high arc, which is not what a crossbow does.
				const float tanTheta = (v2 - std::sqrt(disc)) / (m_gravity * d);
				const float theta    = std::atan(tanTheta);

				irr::core::vector3df horizDir = toTarget;
				horizDir.normalize();
				launchVelocity    = horizDir * (m_projectileSpeed * std::cos(theta));
				launchVelocity.Y += m_projectileSpeed * std::sin(theta);
				solved = true;
			}
		}

		if (!solved)
		{
			// Target out of range, or so close the horizontal distance is noise
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
	auto& descriptor          = projectileEntity.getComponent<DescriptorComponent>();
	descriptor.id             = WorldManager::Get()->getNewID();
	descriptor.name           = "crossbow_bolt_" + std::to_string(descriptor.id);
	descriptor.type           = ET_DYNAMIC;
	descriptor.isSerializable = false;

	projectileEntity.addComponent<TransformComponent>();
	auto& transform           = projectileEntity.getComponent<TransformComponent>();
	transform.position        = spawnPos;
	transform.initialPosition = spawnPos;
	transform.scale           = m_projectileScale;

	const irr::core::vector3df initialRotation = launchDir.getHorizontalAngle();
	transform.rotation        = initialRotation;
	transform.initialRotation = initialRotation;

	projectileEntity.addComponent<RenderComponent>();
	projectileEntity.getComponent<RenderComponent>().isVisible = true;

	projectileEntity.addComponent<MeshComponent>();
	auto& mesh          = projectileEntity.getComponent<MeshComponent>();
	mesh.mesh           = "content/mesh/prop/missile.obj"; // placeholder until a bolt model exists
	mesh.textures.emplace_back<std::string>("content/mesh/prop/missile.png");
	mesh.isPrimitive    = false;
	mesh.isVisible      = true;
	mesh.castShadows    = false;
	mesh.receiveShadows = false;

	projectileEntity.activate();

	WeaponProjectile proj;
	proj.entity           = projectileEntity;
	proj.speed            = m_projectileSpeed;
	proj.velocity         = launchVelocity;
	proj.previousPosition = spawnPos;
	proj.useTracking      = false;
	proj.isTrackingActive = false;
	proj.targetId         = _entity_null_value;
	proj.distanceTraveled = 0.0f;
	proj.trailParticles   = nullptr;
	proj.isBouncing       = false;
	proj.maxLifetime      = 6000.0f;

	m_projectiles.emplace_back(proj);
}

// Surfaces soft or fibrous enough to hold a broadhead. Everything else —
// stone, metal, glass, water, and anything the texture scan could not classify
// — shatters the bolt, which is the safe default: an unrecognised texture gets
// the old behaviour rather than silently littering the level with stuck bolts.
static bool materialHoldsBolt(E_MANAGED_MATERIAL material)
{
	switch (material)
	{
	case MAT_EARTH:
	case MAT_GRAVEL:
	case MAT_CARPET:
	case MAT_WOOD:
		return true;
	default:
		return false;
	}
}

// What the bolt just hit, as a managed material.
//
// Uses the triangle the sweep already returned rather than casting a second ray —
// RaycastResultData carries both the node and the tri, so the material can be
// resolved directly.
static E_MANAGED_MATERIAL surfaceMaterialAt(const RaycastResultData& hit)
{
	if (!hit.node)
		return MAT_INVALID;

	E_MANAGED_MATERIAL material = MAT_INVALID;
	RenderManager::Get()->getNodeTriangleMaterial(hit.node, hit.tri, material);
	return material;
}

// Swept-raycast flight, so a fast bolt cannot tunnel through thin geometry
// between frames. A bolt either buries itself in a soft surface, shatters on a
// hard one, or expires in flight — it never bounces.
void Weapon_Crossbow::updateProjectiles(float dt)
{
	if (m_projectiles.empty())
		return;

	const float dtSeconds = dt / 1000.0f;

	for (auto it = m_projectiles.begin(); it != m_projectiles.end(); )
	{
		if (!it->entity.isValid() || !it->entity.hasComponent<TransformComponent>())
		{
			it = m_projectiles.erase(it);
			continue;
		}

		auto& transformComp = it->entity.getComponent<TransformComponent>();

		// Wait for the render system to build the node before flying this bolt.
		//
		// This guard is load-bearing, not defensive. TransformComponent::getPosition()
		// returns a ZERO VECTOR while node is null, and the node does not exist until
		// the next world refresh — whereas persist() runs in the same
		// WeaponController::update() call that fire() did. Without this, the first
		// sweep runs from the muzzle to the world ORIGIN, hits the level somewhere
		// across the map, and destroys the bolt on the frame it was loosed: no
		// projectile, no impact at the crosshair, no damage.
		//
		// Lifetime still accrues, so a bolt whose node never arrives expires instead
		// of sitting in the list forever.
		if (!transformComp.node)
		{
			it->lifetime += dt;
			++it;
			continue;
		}

		// Already buried: nothing to sweep, integrate or re-orient. Just run the
		// clock down and clean up. This is what keeps a two-minute lifetime cheap.
		if (it->isStuck)
		{
			it->lifetime += dt;

			if (it->lifetime >= it->maxLifetime)
			{
				if (it->entity.hasComponent<DescriptorComponent>())
					WorldManager::Get()->killEntityByID(it->entity.getComponent<DescriptorComponent>().id);

				it = m_projectiles.erase(it);
				continue;
			}

			++it;
			continue;
		}

		const irr::core::vector3df currentPos = transformComp.getPosition();

		// Sweep from where it was to where it is, padded either end so a contact
		// exactly on a frame boundary is not missed
		const float boltRadius = 0.08f;
		irr::core::vector3df rayStart = it->previousPosition;
		irr::core::vector3df rayEnd   = currentPos;

		irr::core::vector3df rayDirection = rayEnd - rayStart;
		const float rayLength = rayDirection.getLength();
		if (rayLength > 0.001f)
		{
			rayDirection.normalize();
			rayStart -= rayDirection * boltRadius;
			rayEnd   += rayDirection * boltRadius;
		}

		RaycastResultData hit = RenderManager::Get()->raycastWorldPosition(rayStart, rayEnd, true);

		bool struck = false;
		irr::core::vector3df hitPoint  = currentPos;
		irr::core::vector3df hitNormal(0.0f, 1.0f, 0.0f);
		entityid hitID = _entity_null_value;
		bool     hitIsFlesh = false;

		if (hit.hit && hit.node)
		{
			auto& hitEntity = WorldManager::Get()->managerSystem()->getEntityByID(hit.node->getID());

			if (hitEntity.isValid() && hitEntity.hasComponent<DescriptorComponent>())
			{
				auto& hitDescriptor = hitEntity.getComponent<DescriptorComponent>();

				const bool isSelf = it->entity.hasComponent<DescriptorComponent>() &&
					hitDescriptor.id == it->entity.getComponent<DescriptorComponent>().id;

				if (!isSelf && (hitDescriptor.type == ET_STATIC || hitDescriptor.type == ET_DYNAMIC))
				{
					struck    = true;
					hitPoint  = hit.point;
					hitNormal = hit.normal;
					hitID     = hitDescriptor.id;

					// Captured here because hitEntity goes out of scope before the
					// impact-effect decision below.
					hitIsFlesh = hitEntity.hasComponent<DamageReceiverComponent>();
				}
			}
			else if (RenderManager::isWorldGeometryNode(hit.node))
			{
				// Brush chunks / props carry no ECS id — solid surface
				struck    = true;
				hitPoint  = hit.point;
				hitNormal = hit.normal;
			}
		}

		if (struck)
		{
			// Damage lands either way — burying the head in something soft is no
			// reason for it not to have hurt.
			if (hitID != _entity_null_value)
			{
				// The bolt's own flight vector is the shot direction here — there is
				// no camera ray, the projectile has been travelling on its own.
				registerHitFeedback(WorldManager::Get()->gameplaySystem()->damageEntity(
					hitID, static_cast<unsigned int>(m_damage), DAMAGE_TYPE::DEFAULT,
					DamageContext::fromImpact(hitPoint, hitNormal, it->velocity)));
			}

			if (materialHoldsBolt(surfaceMaterialAt(hit)))
			{
				// Bury it. Park the bolt just short of the contact point along its
				// own flight path so the head goes in and the shaft stands proud,
				// keep the orientation it was flying with, and stop simulating.
				irr::core::vector3df flightDir = it->velocity;
				flightDir.normalize();

				const irr::core::vector3df restPos = hitPoint - flightDir * m_stickEmbed;

				transformComp.position = restPos;
				transformComp.node->setPosition(restPos);
				transformComp.node->updateAbsolutePosition();

				it->velocity         = irr::core::vector3df(0.0f, 0.0f, 0.0f);
				it->previousPosition = restPos;
				it->isStuck          = true;
				it->maxLifetime      = m_stuckLifetime;

				// No spark and no bullet hole: the bolt standing in the surface is
				// the impact feedback. Sparking off carpet would read as wrong, and
				// a bullet hole under a bolt that did not make one reads as worse.
				// Add m_effects.impact() back here if a softer thud effect appears.

				++it;
				continue;
			}

			// Hard surface — shatter, exactly as before. Flesh gets no shatter
			// sparks: GoreManager has already put blood at the wound.
			if (!hitIsFlesh)
				m_effects.impact(hitPoint, hitNormal);

			if (it->entity.hasComponent<DescriptorComponent>())
				WorldManager::Get()->killEntityByID(it->entity.getComponent<DescriptorComponent>().id);

			it = m_projectiles.erase(it);
			continue;
		}

		// Gravity first, so the orientation below reflects the arc it is now on
		it->velocity.Y -= m_gravity * dtSeconds;

		const irr::core::vector3df nextPos = currentPos + it->velocity * dtSeconds;

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

		it->previousPosition = currentPos;
		it->lifetime += dt;

		if (it->lifetime >= it->maxLifetime)
		{
			if (it->entity.hasComponent<DescriptorComponent>())
				WorldManager::Get()->killEntityByID(it->entity.getComponent<DescriptorComponent>().id);

			it = m_projectiles.erase(it);
			continue;
		}

		++it;
	}
}

void Weapon_Crossbow::equip()
{
	m_firedThisPress = false;
	resetViewKick();

	// Draw it in whatever state it was holstered in. m_boltLoaded deliberately
	// is NOT reset here — that is the bug this tracks: an interrupted recock has
	// to still be missing its bolt when the crossbow comes back up.
	setMeshPartVisible(m_bolt, m_boltLoaded);

	playEquipSound();

	m_mesh.node->setVisible(true);
	m_mesh.node->setPosition(m_viewPositionOffset);
	m_mesh.node->setRotation(m_viewRotationOffset);

	enterState(State::Equipping);
}

void Weapon_Crossbow::unequip()
{
	m_state = State::Idle;
	setClipSpeed(1.0f);
	setMeshPartVisible(m_bolt, m_boltLoaded);
	m_mesh.node->setVisible(false);
}

void Weapon_Crossbow::startUnequip()
{
	// Already hidden or mid-holster: nothing to play, don't restart the clip
	if (!m_mesh.node || !m_mesh.node->isVisible() || m_state == State::Unequipping)
		return;

	m_firedThisPress = true; // block fire during the transition

	// Holster it as it actually is. Interrupting the recock before the bolt seats
	// at f75 means it goes away empty and comes back empty.
	setMeshPartVisible(m_bolt, m_boltLoaded);

	playUnequipSound();

	enterState(State::Unequipping);
}

void Weapon_Crossbow::idle()
{
}

void Weapon_Crossbow::move()
{
}

void Weapon_Crossbow::reload()
{
	// Normally nothing to do: the recock is part of every shot — see the Firing
	// state — and there is no partial magazine to top up.
	//
	// The exception is a crossbow whose recock came up dry. It then sits empty
	// until bolts turn up, and this is the only route back to loaded, because
	// nothing else will run the recock again.
	if (m_boltLoaded || m_state != State::Idle)
		return;

	if (reserveRemaining() <= 0)
		return;

	enterState(State::Reloading);
}

#include "Weapon_HeavyRifle.h"

#include <algorithm>

// Windows.h defines min/max as macros and this project does not use NOMINMAX
#undef min
#undef max

#include "Engine/Engine.h"
#include "Engine/Renderer/DecalManager.h"
#include "Utility/Utility.h"

#include "../CameraFX.h"

#undef MB_RIGHT

using namespace SPK;
using namespace SPK::IRR;

void Weapon_HeavyRifle::precache()
{
	ParticleManager::Get()->precache("spark", _asset_psys("spark"));
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/rifle/fire.wav",  true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/dryfire.wav",     true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/remove_mag.wav",  true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/insert_mag.wav",  true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/cock_rifle.wav",  true);
}

void Weapon_HeavyRifle::init()
{
	m_descriptor.name = "Player_Weapon_HeavyRifle";
	m_descriptor.id = _entity_null_value;

	m_viewPositionOffset = irr::core::vector3df(0.1000f, -0.1550f, 0.1700f);
	m_viewRotationOffset = irr::core::vector3df(0.0f, 180.0f, 0.0f);
	m_viewScaleOffset = irr::core::vector3df(0.01f, 0.01f, 0.01f);

	m_mesh.mesh = _asset_glb("player/weapon/heavyrifle_animated");

	m_mesh.trimesh = RenderManager::Get()->loadMesh(m_mesh.mesh);
	if (!m_mesh.trimesh)
	{
		spdlog::warn("PlayerWeapon::init(): failed to load mesh \"{}\", stand-in mesh loaded", m_mesh.mesh);

		m_mesh.trimesh = RenderManager::Get()->sceneManager()->getMesh("content/mesh/primitive/double_tetrahedron.obj");
		m_mesh.node = RenderManager::Get()->sceneManager()->addAnimatedMeshSceneNode(m_mesh.trimesh, nullptr, m_descriptor.id);

		auto* t = RenderManager::Get()->driver()->getTexture("content/texture/color/magenta.png");
		m_mesh.node->setMaterialTexture(0, t);
	}

	m_mesh.node = RenderManager::Get()->sceneManager()->addAnimatedMeshSceneNode(m_mesh.trimesh, nullptr, m_descriptor.id);

	m_mesh.node->setMaterialFlag(irr::video::EMF_BILINEAR_FILTER, true);
	m_mesh.node->setMaterialFlag(irr::video::EMF_TRILINEAR_FILTER, true);
	m_mesh.node->setMaterialFlag(irr::video::EMF_ANISOTROPIC_FILTER, true);
	m_mesh.node->setMaterialFlag(irr::video::EMF_ANTI_ALIASING, true);
	m_mesh.node->setMaterialFlag(irr::video::EMF_USE_MIP_MAPS, true);

	//RenderManager::Get()->renderer()->getMaterialSwapper()->swapMaterials(m_mesh.node);

	m_mesh.fps = 30;
	m_mesh.node->setAnimationSpeed(30.0f);

	// One "allanims" take, 0-266 at 30 fps, clips separated by 2-frame holds at
	// the shared rest pose (boundaries at 0, 9/10, 93/94, 152/153, 209/210,
	// 239/240, 266). Holster and draw share frame 163, the bottom of the drop.
	//
	// "move" was 50-79, which is a slice out of the MIDDLE of reload_empty — it
	// would have played part of a magazine change. Removed rather than remapped:
	// there is no walk clip in this asset, and move() is never called anyway.
	//
	// Two clips are authored but unused: 211-239 is a subtle idle (the rifle
	// shifts 1.7 units, fingers under 0.3) — idle is pinned to 209 and the
	// hold-steady motion comes from enableIdleBreathing() instead — and 241-265
	// is a melee bash, listed below but not bound to input.
	//
	// melee ends at 265, not 266: Irrlicht clamps EndFrame to getFrameCount()-1
	// and CSkinnedMesh::getFrameCount() returns the last frame INDEX.
	m_mesh.animationList.emplace_back(sAnimationData("fire",         0,   9,   false));
	m_mesh.animationList.emplace_back(sAnimationData("reload_empty", 10,  93,  false));
	m_mesh.animationList.emplace_back(sAnimationData("reload",       94,  152, false));
	m_mesh.animationList.emplace_back(sAnimationData("unequip",      153, 163, false));
	m_mesh.animationList.emplace_back(sAnimationData("equip",        163, 208, false));
	m_mesh.animationList.emplace_back(sAnimationData("idle",         209, 209, true));
	// Authored but not bound to input — a bash, if a melee slot is wanted
	m_mesh.animationList.emplace_back(sAnimationData("melee",        241, 265, false));

	playAnimation("idle"); // safe default until equip() runs

	// The idle clip is pinned to a single frame, so the hold-steady motion comes
	// from updateWeaponSway() instead. Above default: a heavy rifle held out front.
	enableIdleBreathing(1.3f);

	m_mesh.node->setJointMode(irr::scene::EJUOR_READ);

	m_mesh.animation_call_back = std::make_shared<AnimationCallback>();
	m_mesh.node->setAnimationEndCallback(m_mesh.animation_call_back.get());

	m_mesh.node->setScale(m_viewScaleOffset);

	// Apply the standard PBR shader to every buffer as the baseline (body, gear, etc.)
	auto perpixelMat = ShaderMaterialManager::get("phong_perpixel");
	if (perpixelMat != irr::video::EMT_SOLID)
		m_mesh.node->setMaterialType(perpixelMat);

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

	m_crosshair = RenderManager::Get()->driver()->getTexture("content/texture/ui/crosshair/crosshair038.png");

	// The brass is spawned from the bolt by ejectSpentCase(), not from a port, so
	// shellEjectJoint stays unset and ejectShell() is never called.
	m_bolt = m_mesh.node->getJointNode("bolt");
	if (!m_bolt)
		spdlog::warn("Weapon_HeavyRifle: 'bolt' joint not found — casings will not eject");

	// Size reference only; never hidden. Resolved after the materials above so
	// the cached material type is the real one.
	resolveMeshPart("bullet", m_caseSizeRef);

	// Character sheet: medium rifle flash, oversized bolt casings, every-3rd tracer
	//
	// This .glb has no FIRESPOT empty, so the flash hangs off 'base' — the
	// receiver-and-barrel joint — and inherits the recoil and reload for free.
	// The offset is the bore centre at the muzzle face: the 66 verts within 1
	// unit of the barrel's far end run evenly from Y 6.00 to 10.51 with no gap
	// wider than 0.59, i.e. one ring of radius ~2.25, so the bore is the midpoint
	// at Y 8.26. GltfImport's handedness conversion negates Z.
	WeaponEffectsDesc fx;
	fx.muzzleJointName   = "base";
	fx.muzzleJointOffset = irr::core::vector3df(0.0f, 8.26f, -83.23f);
	fx.flashColor      = irr::video::SColor(255, 255, 204, 76);
	fx.flashSize       = 0.8f;
	fx.lightColor      = irr::video::SColorf(1.0f, 0.8f, 0.2f);
	fx.tracerFrequency = 3;
	fx.shellMesh       = "content/mesh/prop/shells/shellmedium.obj";
	fx.shellScale      = 2.0f;
	fx.shellPoolSize   = 24;
	m_effects.init(m_mesh.node, fx);
}

float Weapon_HeavyRifle::secondsUntilFrame(int targetFrame, float currentFrame) const
{
	// The clip's RESUMED rate, not getAnimationSpeed(): that reads zero while the
	// swap pause is holding, which is precisely when this has to stay meaningful.
	const float fps = static_cast<float>(m_mesh.fps) * m_reloadSpeed;
	if (fps <= 0.0f)
		return 0.0f;

	const float framesAway = static_cast<float>(targetFrame) - currentFrame;
	if (framesAway <= 0.0f)
		return 0.0f;

	float seconds = framesAway / fps;

	// Anything on the far side of the swap pause is that much further off —
	// whether the hold is running now or still ahead. Without this, insert_mag
	// goes on frame count alone: its trigger sits at +23, four frames BEFORE the
	// hold at +27, so its clunk would land half a second early, in the silence.
	const int start      = m_reloadWasEmpty ? m_reloadEmptyStart : m_reloadStart;
	const int pauseFrame = start + m_magSwapOffset;

	if (targetFrame >= pauseFrame)
	{
		if (m_reloadPauseRemaining > 0.0f)
			seconds += m_reloadPauseRemaining * 0.001f;
		else if (!m_reloadPaused && currentFrame < static_cast<float>(pauseFrame))
			seconds += m_reloadPauseMs * 0.001f;
	}

	return seconds;
}

// Reload audio. Each cue fires once, when the event it belongs to is its own
// measured lead away — in SECONDS via secondsUntilFrame(), not in frames, so the
// transient lands on the event whatever the clip speed and across the hold.
//
// m_reloadWasEmpty selects the base frame because the two reload clips are the
// same gesture 84 frames apart — and it also gates the bolt cue, since only the
// empty reload cycles the action.
void Weapon_HeavyRifle::updateReloadSounds(float frame)
{
	const int start = m_reloadWasEmpty ? m_reloadEmptyStart : m_reloadStart;

	if (!m_removeMagPlayed &&
		secondsUntilFrame(start + m_magDetachOffset, frame) <= m_removeMagLeadSec)
	{
		m_removeMagPlayed = true;
		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/remove_mag", 0.04f, 1, -1.0f, "heavyrifle_mag");
	}

	if (!m_insertMagPlayed &&
		secondsUntilFrame(start + m_magSeatOffset, frame) <= m_insertMagLeadSec)
	{
		m_insertMagPlayed = true;
		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/insert_mag", 0.04f, 1, -1.0f, "heavyrifle_maginsert");
	}

	if (m_reloadWasEmpty && !m_cockPlayed &&
		secondsUntilFrame(start + m_boltPullOffset, frame) <= m_cockLeadSec)
	{
		m_cockPlayed = true;
		playCockSound();
	}
}

void Weapon_HeavyRifle::playCockSound()
{
	SoundManager::Get()->sound()->playRandomized2D(
		"content/sound/weapon/cock_rifle", 0.03f, 1, -1.0f, "heavyrifle_cock");
}

// Brass out of the ejection port. Nothing in the animation ejects a case — the
// bolt cycles but no shell prop moves with it — so unlike the shotgun there is
// no animated transform to hand off from, and both the spawn point and the
// throw have to be synthesised from the bolt's own frame.
void Weapon_HeavyRifle::ejectSpentCase()
{
	if (!m_bolt || !m_mesh.node)
		return;

	m_bolt->updateAbsolutePosition();
	const irr::core::matrix4 world = m_bolt->getAbsoluteTransformation();

	irr::core::vector3df port = m_ejectPortOffset;
	world.transformVect(port);

	// Away-from-port and up, from the RIFLE's basis rather than the camera's, so
	// brass leaves correctly whichever way the player is facing. -X to match the
	// port side: with the viewmodel's 180 degree yaw that reads as screen-right.
	irr::core::vector3df axisRight(-1.0f, 0.0f, 0.0f);
	irr::core::vector3df axisUp(0.0f, 1.0f, 0.0f);
	world.rotateVect(axisRight);
	world.rotateVect(axisUp);
	axisRight.normalize();
	axisUp.normalize();

	// Speed scaled off the joint's world scale so it survives a viewmodel-scale
	// change, the same reasoning as the revolver's scatter and the shotgun's throw
	const irr::core::vector3df jointScale = world.getScale();
	const float unit  = std::max(jointScale.X, std::max(jointScale.Y, jointScale.Z));
	const float speed = 300.0f * unit;

	const irr::core::vector3df velocity =
		axisRight * speed * Engine::Get()->rng()->getFloat(0.8f, 1.2f) +
		axisUp    * speed * Engine::Get()->rng()->getFloat(0.3f, 0.6f);

	// Size the casing off the model's OWN magazine round rather than a tuned
	// constant, so it matches the calibre the rifle is drawn holding and stays
	// right if the viewmodel scale changes. Rank-matched per axis because
	// shellmedium.obj is 3:1 where the round is 5.7:1 — one uniform ratio off the
	// long axis would leave it visibly too fat.
	// Turn the casing end for end — see the matching note in the shotgun. Taken
	// straight from the bolt it flies mouth-first back at the camera. Composed as
	// a LOCAL flip (right operand applies first under Irrlicht's operator*), not
	// by adding 180 to the Euler Y, which would yaw it in the parent frame and
	// fall apart the moment the bolt is pitched or rolled.
	irr::core::matrix4 flip;
	flip.setRotationDegrees(irr::core::vector3df(0.0f, 180.0f, 0.0f));
	const irr::core::matrix4 oriented = world * flip;

	m_effects.spawnShellAt(
		port,
		oriented.getRotationDegrees(),
		velocity,
		matchPartScale(m_caseSizeRef, m_effects.shellMeshExtent()));
}

void Weapon_HeavyRifle::destroy()
{
	m_effects.destroy();

	RenderManager::Get()->unregisterViewmodelNode(m_mesh.node);
	m_mesh.node->remove();

	WorldManager::Get()->freeEntityID(m_descriptor.id);
}

void Weapon_HeavyRifle::update()
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
		// The draw racks the bolt at f181-191. Equip plays at 1x, but the lead
		// still goes through soundLeadFrames() so this cannot drift if that ever
		// changes.
		else if (!m_equipCockPlayed && m_mesh.node &&
			m_mesh.node->getFrameNr() >=
				static_cast<irr::f32>(m_equipBoltPullFrame - soundLeadFrames(m_cockLeadSec)))
		{
			m_equipCockPlayed = true;
			playCockSound();
		}
		return;
	}

	if (m_isReloadingAnim)
	{
		if (animEnded)
		{
			m_isReloadingAnim = false;
			m_rounds = m_magSize;
			m_reloadWasEmpty = false;
			setClipSpeed(1.0f);
			m_reloadPauseRemaining = 0.0f;
			playAnimation("idle");
		}
		else if (m_mesh.node)
		{
			const irr::f32 frame = m_mesh.node->getFrameNr();

			// Hold at the apex of the mag swap: old magazine clear, new one not
			// up yet. Implemented by parking the clip speed at zero rather than
			// by skipping frames, so the pose freezes rather than jumping.
			if (m_reloadPauseRemaining > 0.0f)
			{
				m_reloadPauseRemaining -= Engine::Get()->getDeltaTime();
				if (m_reloadPauseRemaining <= 0.0f)
				{
					m_reloadPauseRemaining = 0.0f;
					setClipSpeed(m_reloadSpeed);
				}
			}
			else if (!m_reloadPaused)
			{
				const int start      = m_reloadWasEmpty ? m_reloadEmptyStart : m_reloadStart;
				const int pauseFrame = start + m_magSwapOffset;

				if (frame >= static_cast<irr::f32>(pauseFrame))
				{
					m_reloadPaused         = true;
					m_reloadPauseRemaining = m_reloadPauseMs;
					setClipSpeed(0.0f);
				}
			}

			// Runs during the hold too: insert_mag has to start partway through
			// it for its clunk to land on the magazine seating.
			updateReloadSounds(frame);
		}
		RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
		return;
	}

	bool fireButtonPressed = InputManager::Get()->isMouseButtonPressed(MB_LEFT);
	const float fireRate = 250.0f;
	if (fireButtonPressed && (currentTime - m_lastFireTime) >= fireRate)
	{
		if (m_rounds > 0)
		{
			m_lastFireTime = currentTime;
			fire();
			m_isPlayingFireAnim = true;
		}
		else
		{
			// Dry: click once per press rather than every frame the button is held
			m_lastFireTime = currentTime;
			SoundManager::Get()->sound()->playRandomized2D(
				"content/sound/weapon/dryfire", 0.05f, 1, -1.0f, "heavyrifle_dryfire");
		}
	}

	if (!fireButtonPressed && m_isPlayingFireAnim && animEnded)
	{
		m_isPlayingFireAnim = false;
		playAnimation("idle");
	}

	RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
}

void Weapon_HeavyRifle::persist()
{
	m_effects.update(Engine::Get()->getDeltaTime());
}

void Weapon_HeavyRifle::equip()
{
	m_mesh.node->setVisible(true);
	m_mesh.animation_call_back->hasAnimationEnded();
	setClipSpeed(1.0f); // a reload cut short by a switch must not leave this fast
	m_reloadPauseRemaining = 0.0f;
	m_equipCockPlayed = false; // re-armed for this draw's bolt rack
	playAnimation("equip");
	m_isEquipping = true;
	m_isUnequipping = false;
	m_isPlayingFireAnim = false;
	m_isReloadingAnim = false;
}

void Weapon_HeavyRifle::unequip()
{
	m_isEquipping = false;
	m_isUnequipping = false;
	m_isPlayingFireAnim = false;
	m_isReloadingAnim = false;
	setClipSpeed(1.0f);
	m_reloadPauseRemaining = 0.0f;
	m_mesh.node->setVisible(false);
}

void Weapon_HeavyRifle::startUnequip()
{
	m_isUnequipping = true;
	m_isEquipping = false;
	m_isPlayingFireAnim = false;
	m_isReloadingAnim = false;
	setClipSpeed(1.0f);
	m_reloadPauseRemaining = 0.0f;
	m_mesh.animation_call_back->hasAnimationEnded();
	playAnimation("unequip");
}

void Weapon_HeavyRifle::idle()
{

}

void Weapon_HeavyRifle::move()
{

}

void Weapon_HeavyRifle::fire()
{
	if (m_rounds <= 0)
		return;

	m_rounds--;

	playAnimation("fire");

	// Raycast-based instant hit
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>())
		return;

	auto& camera = player.getComponent<CameraComponent>();

	if (!m_mesh.node)
		return;

	// Force full hierarchy update: camera → weapon → bones
	camera.camera->updateAbsolutePosition();
	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();

	// Taken from WeaponEffects rather than a cached FIRESPOT node. This model has
	// no such bone, and the old code bailed out of fire() entirely when the
	// lookup failed — the gun could not shoot at all. Going through the effects
	// module also means the ray, the tracer and the flash share one definition of
	// where the muzzle is, and it degrades to the weapon node instead of to
	// nothing if a future model has neither bone.
	const irr::core::vector3df muzzlePos = m_effects.muzzleWorldPosition();

	// Get camera target for aiming direction
	irr::core::vector3df target = camera.camera->getTarget();
	irr::core::vector3df cameraPos = camera.camera->getAbsolutePosition();
	irr::core::vector3df forward = (target - cameraPos).normalize();

	// Calculate right and down vectors relative to camera orientation for spread
	irr::core::vector3df up(0, 1, 0);
	irr::core::vector3df right = forward.crossProduct(up).normalize();
	irr::core::vector3df down = right.crossProduct(forward).normalize();

	// Converge on the crosshair aim point so muzzle-origin shots land on centre
	irr::core::vector3df direction = getAimDirection(muzzlePos);

	// Apply random offset in right and down directions
	float spreadRight = Engine::Get()->rng()->getFloat(-m_recoil, m_recoil);
	float spreadDown = Engine::Get()->rng()->getFloat(-m_recoil, m_recoil);
	direction = (direction + right * spreadRight + down * spreadDown).normalize();

	// Perform raycast from muzzle position in spread direction
	// Cast ray a long distance (1000 units)
	irr::core::vector3df rayEnd = muzzlePos + direction * 1000.0f;

	RaycastResultData raycastResult = RenderManager::Get()->raycastWorldPosition(
		muzzlePos,
		rayEnd,
		true  // Exclude debug nodes
	);

	// Check if we hit something
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
				registerHitFeedback(
					WorldManager::Get()->gameplaySystem()->damageEntity(hitDescriptor.id, 25));

				// Sparks fanned off the surface + bullet-hole decal
				m_effects.impact(raycastResult.point, raycastResult.normal);
			}
		}
		else if (RenderManager::isWorldGeometryNode(raycastResult.node))
		{
			// Brush chunks / props carry no ECS id — solid surface hit, nothing to damage
			m_effects.impact(raycastResult.point, raycastResult.normal);
		}
	}

	// Tracer segment toward the hit point (module fires every Nth shot)
	irr::core::vector3df tracerEnd = (raycastResult.hit && raycastResult.node) ?
		raycastResult.point : (muzzlePos + direction * 1000.0f);
	m_effects.spawnTracer(muzzlePos, tracerEnd);

	m_effects.muzzleFlash();
	ejectSpentCase();

	// Heavy-rifle kick — noticeably harder than the pistol, slower cadence earns it
	g_CameraFX.addRecoil(-1.8f, Engine::Get()->rng()->getFloat(-0.2f, 0.2f));
	addViewKick(
		irr::core::vector3df(0.0f, 0.02f, -0.07f),
		irr::core::vector3df(3.5f,
			Engine::Get()->rng()->getFloat(-0.4f, 0.4f),
			Engine::Get()->rng()->getFloat(-0.9f, 0.9f)));

	SoundManager::Get()->sound()->playRandomized2D("content/sound/weapon/rifle/fire", 0.05f, 3, 0.6f, "rifle_fire");
}

void Weapon_HeavyRifle::reload()
{
	if (m_isReloadingAnim || m_isEquipping || m_isUnequipping)
		return;

	if (m_rounds >= m_magSize)
		return; // full — don't burn the clip for nothing

	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag

	// An emptied magazine needs the bolt run as well as the mag swapped, which is
	// the longer clip; with a round still chambered the shorter tactical swap is
	// correct. Picking by round count is why the asset ships both.
	m_reloadWasEmpty = (m_rounds == 0);

	m_removeMagPlayed = false;
	m_insertMagPlayed = false;
	m_cockPlayed      = false;

	m_reloadPaused         = false;
	m_reloadPauseRemaining = 0.0f;

	setClipSpeed(m_reloadSpeed);
	playAnimation(m_reloadWasEmpty ? "reload_empty" : "reload");

	m_isReloadingAnim = true;
	m_isPlayingFireAnim = false;
}



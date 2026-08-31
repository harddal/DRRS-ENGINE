#include "Weapon_SMG.h"

#include <algorithm>

// Windows.h defines min/max as macros and this project does not use NOMINMAX,
// so std::max below would not survive an include-order change without these.
#undef min
#undef max

#include "Engine/Engine.h"

#undef MB_RIGHT

#include "../CameraFX.h"

#undef MB_RIGHT

using namespace irr;
using namespace SPK;
using namespace SPK::IRR;

void Weapon_SMG::precache()
{
	ParticleManager::Get()->precache("spark", _asset_psys("spark"));

	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/dryfire.wav",    true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/cock_rifle.wav", true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/remove_mag.wav", true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/insert_mag.wav", true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/pistol/fire1.wav", true);
}

void Weapon_SMG::init()
{
	m_descriptor.name = "Player_Weapon_SMG";
	m_descriptor.id   = _entity_null_value;

	m_weapon_type     = WEAP_SMG;

	// Same arms rig as the rest of the glTF pack — the 'arms' root sits at the
	// shared (0, 2.945, -17.671). A 77.7-unit barrel is short, so it is held
	// closer in than the rifle. Tune with the F2 window, not here.
	m_viewPositionOffset = irr::core::vector3df(0.1000f, -0.1500f, 0.1000f);
	m_viewRotationOffset = irr::core::vector3df(0.0f, 180.0f, 0.0f);
	m_viewScaleOffset    = irr::core::vector3df(0.01f, 0.01f, 0.01f);

	m_mesh.mesh = _asset_glb("player/weapon/smg_animated");

	m_mesh.trimesh = RenderManager::Get()->loadMesh(m_mesh.mesh);

	// Swap in the stand-in BEFORE the node is created — creating a node in the
	// failure branch and again below orphans the first one.
	const bool usingStandIn = (m_mesh.trimesh == nullptr);
	if (usingStandIn)
	{
		spdlog::warn("Weapon_SMG::init(): failed to load mesh \"{}\", stand-in mesh loaded", m_mesh.mesh);
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

	// Clip table recovered from the .glb — ONE "allanims" take, frames 0-254 at
	// 30 fps, clips separated by rest holds. Boundaries read from the WHOLE POSE
	// (every joint against frame 0), not from the weapon root: the root can be
	// back at its seat while the arms are still moving, which is what made the
	// sniper's draw snap when it was cut on the root alone.
	// Rest holds at 0, 7/8, 87/88, 150/151, 206/207, 235/236, 254.
	//   0-7      trigger f1-7, bolt back 8.8 on f1-2 and home by
	//            f5, the case thrown 2.9 on f4                      -> fire
	//   8-87     magazine out f28, home f48, and THEN the bolt is
	//            racked at f68                                      -> reload_empty
	//   88-150   magazine out f108, home f128, no bolt at all       -> reload
	//   151-206  gun swings away to its apex at f162 (yaw +57,
	//            roll -49, |T| 24.5) and comes back, RACKING THE
	//            BOLT at f182 on the way in                         -> unequip
	//            151-162, equip 162-206
	//   207-235  a small drift, no rotation                         -> idle sway,
	//            unused: idle is pinned to 207 and the hold-steady
	//            motion comes from enableIdleBreathing()
	//   236-254  snaps to a -62 degree roll in two frames and takes
	//            sixteen to recover, no part motion whatever        -> melee bash
	//
	// 236-254 is a bash, not a draw. Same tell as everywhere else in this pack: a
	// bash reaches its pose within a few frames and recovers over three times as
	// long, with a large NEGATIVE roll, while a holster/draw is symmetric about
	// its apex and takes the weapon DOWN.
	//
	// Looping clips MUST be flagged loop=true — a non-looping clip re-armed from
	// the end callback holds its last frame for one tick every cycle.
	m_mesh.animationList.emplace_back(sAnimationData("fire",         0,   7,   false));
	m_mesh.animationList.emplace_back(sAnimationData("reload_empty", 8,   87,  false));
	m_mesh.animationList.emplace_back(sAnimationData("reload",       88,  150, false));
	m_mesh.animationList.emplace_back(sAnimationData("unequip",      151, 162, false));
	m_mesh.animationList.emplace_back(sAnimationData("equip",        162, 206, false));
	m_mesh.animationList.emplace_back(sAnimationData("idle",         207, 207, true));
	// Authored but not bound to input — a stock bash, if a melee slot is wanted
	m_mesh.animationList.emplace_back(sAnimationData("melee",        236, 254, false));

	// Both glTF backends normalise keyframe times to 30 fps Irrlicht frames, so
	// the viewmodel must play at 30 to run at its authored speed.
	m_mesh.fps = 30;
	m_mesh.node->setAnimationSpeed(static_cast<irr::f32>(m_mesh.fps));

	m_mesh.node->setJointMode(irr::scene::EJUOR_READ);

	m_mesh.animation_call_back = std::make_shared<AnimationCallback>();
	m_mesh.node->setAnimationEndCallback(m_mesh.animation_call_back.get());

	playAnimation("idle");

	// The idle clip is pinned to a single frame, so all the hold-steady motion
	// comes from here. Light weapon held high — a touch livelier than the rifle.
	enableIdleBreathing(1.25f);

	m_mesh.node->setScale(m_viewScaleOffset);

	auto perpixelMat = ShaderMaterialManager::get("phong_perpixel");
	if (perpixelMat != irr::video::EMT_SOLID)
		m_mesh.node->setMaterialType(perpixelMat);

	for (auto i = 0; i < m_mesh.node->getMaterialCount(); i++)
	{
		m_mesh.node->getMaterial(i).Shininess = 0.f;
		m_mesh.node->getMaterial(i).SpecularColor.setAlpha(0);
	}

	// Must come AFTER the material assignment — it caches each part's real
	// material type so setMeshPartVisible() has something to restore.
	resolveMeshPart("bullet", m_round);
	resolveMeshPart("mag",    m_mag);

	// Reference point for reload stabilisation: the bore line partway down the
	// barrel. Z negated for the handedness conversion.
	enableClipStabilization("base", irr::core::vector3df(0.0f, 7.54f, -25.0f));
	setStabilizationTuneAmount(0.5f);

	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");

	if (!player.isValid())
	{
		spdlog::error("In function Weapon_SMG::init() -> getEntityByName(\"player\") : Entity 'player' does not exist");
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
		spdlog::error("In function Weapon_SMG::init() -> player.getComponent<CameraComponent>() : Entity 'player' does not have specified component");
	}

	RenderManager::Get()->registerViewmodelNode(m_mesh.node);
	m_mesh.node->setVisible(false);

	m_rounds = m_magSize;

	m_crosshair = RenderManager::Get()->driver()->getTexture("content/texture/ui/crosshair/crosshair038.png");

	// No FIRESPOT empty in this .glb, but 'base' is the receiver-and-barrel joint,
	// so anything parented to it inherits the recoil, the reload and the holster
	// for free. The offset is the bore centre at the muzzle face, and GltfImport's
	// handedness conversion negates Z.
	WeaponEffectsDesc fx;
	fx.muzzleJointName   = "base";
	fx.muzzleJointOffset = irr::core::vector3df(0.0f, 7.54f, -63.65f);
	fx.flashColor        = irr::video::SColor(255, 255, 214, 150);
	fx.flashSize         = 0.55f;
	fx.flashDuration     = 40.0f;
	fx.lightColor        = irr::video::SColorf(1.0f, 0.8f, 0.35f);
	fx.lightRadius       = 3.4f;
	fx.tracerFrequency   = 3;
	fx.tracerPoolSize    = 24;
	// Brass comes from the animated round itself, not from a port, so
	// shellEjectJoint stays unused and ejectShell() is never called.
	fx.shellMesh         = "content/mesh/prop/shells/shellsmall.obj";
	fx.shellSpeed        = 5.0f;
	// Sized off the SHELL LIFETIME, not the magazine. Casings live 10 s and this
	// fires ~12/s, so sustained fire keeps well over a hundred alive at once —
	// the dual SMGs ran their pool dry halfway through a magazine for exactly
	// this arithmetic, so do not size it from m_magSize.
	fx.shellPoolSize     = 144;
	fx.impactParticle    = "spark";
	m_effects.init(m_mesh.node, fx);
}

void Weapon_SMG::destroy()
{
	m_effects.destroy();

	RenderManager::Get()->unregisterViewmodelNode(m_mesh.node);
	m_mesh.node->remove();

	WorldManager::Get()->freeEntityID(m_descriptor.id);
}

// --- State -------------------------------------------------------------------

void Weapon_SMG::enterState(State next)
{
	m_state = next;
	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag

	setClipSpeed(next == State::Firing    ? m_fireSpeed
	           : next == State::Equipping ? m_equipSpeed
	           : 1.0f); // reload sets its own below, once it knows which clip

	// Only the reloads are stabilised — the fire clip is seven frames and would
	// only be fought by it.
	setStabilizationAmount(next == State::Reloading ? stabilizationTuneAmount() : 0.0f);

	switch (next)
	{
	case State::Firing:
		playAnimation("fire");
		m_caseHandedOff = false;
		break;

	case State::Reloading:
		// The clip and the speed are chosen by reload(), which knows whether the
		// chamber is empty; this state only resets the bookkeeping.
		m_magRestValid = false;
		m_magWasAway   = false;
		m_ammoCredited = false;
		m_magOutPlayed = false;
		m_magInPlayed  = false;
		m_boltPlayed   = false;
		break;

	case State::Equipping:
		playAnimation("equip");
		// The draw racks the bolt, so the round goes through the same reuse flick
		// it does when firing and the cue has to be re-armed.
		m_roundRestValid = false;
		m_caseHandedOff  = false;
		m_boltPlayed     = false;
		break;

	case State::Unequipping:
		playAnimation("unequip");
		break;

	case State::Idle:
	default:
		playAnimation("idle");
		setMeshPartVisible(m_round, true);
		setMeshPartVisible(m_mag,   true);
		break;
	}
}

// --- Parts -------------------------------------------------------------------

// The single 'bullet' mesh is both the spent case and the next round, so it has
// to be hidden across the throw or it teleports back into the breech. Driven off
// MEASURED displacement — partPosition() reads the joint that actually moves,
// which is the PARENT of the geometry joint; the geometry joint's own local
// transform is a constant.
//
// NOTE the rest pose is captured ONCE and deliberately NOT reset per shot, unlike
// the rifle's: at 85 ms between rounds the fire clip is re-triggered while the
// round is still out of battery, and re-sampling then would take a mid-throw
// position as "home" and the case would never hide again.
void Weapon_SMG::updateRound(bool throwCase)
{
	if (!m_round.bone || !m_mesh.node)
		return;

	// The viewmodel is hidden during drawAll(), so OnAnimate() skipped it and the
	// joint transforms are stale — force them, as fire() does for the muzzle.
	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();

	const irr::core::vector3df pos = partPosition(m_round);

	if (!m_roundRestValid)
	{
		m_roundRest      = pos;
		m_roundRestValid = true;
		return;
	}

	if ((pos - m_roundRest).getLength() > m_roundLooseEpsilon)
	{
		// Hand off to a physics casing on the frame the animated mesh vanishes —
		// same place, same orientation, so there is no seam. The DRAW passes
		// false: it chambers a round rather than extracting a fired one, so there
		// is nothing to throw.
		if (throwCase && !m_caseHandedOff)
		{
			m_caseHandedOff = true;
			ejectSpentCase();
		}

		setMeshPartVisible(m_round, false);
	}
	else
	{
		setMeshPartVisible(m_round, true);
	}
}

// Hides the magazine while it is away from the gun, and CREDITS THE AMMUNITION
// on the frame it comes home.
//
// Displacement rather than a frame number is what lets both reload clips share
// this: the magazine leaves at f28 in one and f108 in the other, and neither
// number appears here.
void Weapon_SMG::updateMagazine()
{
	if (!m_mag.bone || !m_mesh.node)
		return;

	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();

	const irr::core::vector3df pos = partPosition(m_mag);

	if (!m_magRestValid)
	{
		m_magRest      = pos;
		m_magRestValid = true;
		return;
	}

	const bool away = (pos - m_magRest).getLength() > m_magAwayEpsilon;

	setMeshPartVisible(m_mag, !away);

	if (away)
	{
		m_magWasAway = true;
		return;
	}

	// Home again, having been away: this is the fresh magazine seating.
	if (m_magWasAway && !m_ammoCredited)
	{
		m_ammoCredited = true;
		m_rounds += drawFromReserve(m_magSize - m_rounds);
	}
}

void Weapon_SMG::ejectSpentCase()
{
	irr::core::matrix4 world;
	if (!meshPartWorldTransform(m_round, world))
		return;

	// Away from the port and up, from the GUN's basis rather than the camera's,
	// so brass leaves correctly whichever way the player faces. -X to match the
	// port side: with the viewmodel's 180 degree yaw that reads as screen-right.
	irr::core::vector3df axisRight(-1.0f, 0.0f, 0.0f);
	irr::core::vector3df axisUp(0.0f, 1.0f, 0.0f);
	world.rotateVect(axisRight);
	world.rotateVect(axisUp);
	axisRight.normalize();
	axisUp.normalize();

	const irr::core::vector3df jointScale = world.getScale();
	const float unit  = std::max(jointScale.X, std::max(jointScale.Y, jointScale.Z));
	const float speed = 260.0f * unit;

	const irr::core::vector3df velocity =
		axisRight * speed * Engine::Get()->rng()->getFloat(0.8f, 1.2f) +
		axisUp    * speed * Engine::Get()->rng()->getFloat(0.3f, 0.6f);

	// Turn the casing end for end — taken straight from the breech it flies
	// mouth-first at the camera. A LOCAL flip, since the right operand applies
	// first under Irrlicht's operator*; adding 180 to the Euler Y would yaw it in
	// the parent frame and fall apart the moment the gun is pitched.
	irr::core::matrix4 flip;
	flip.setRotationDegrees(irr::core::vector3df(0.0f, 180.0f, 0.0f));
	const irr::core::matrix4 oriented = world * flip;

	// Never m_viewScaleOffset: the shell meshes are authored in world units and
	// scaling one by 0.01 renders it at a fifth of a millimetre.
	m_effects.spawnShellAt(
		world.getTranslation(),
		oriented.getRotationDegrees(),
		velocity,
		matchPartScale(m_round, m_effects.shellMeshExtent()));
}

// --- Frame loop --------------------------------------------------------------

void Weapon_SMG::update()
{
	if (!m_mesh.node || !m_mesh.node->isVisible())
		return;

	const float currentTime = Engine::Get()->getCurrentTime();
	const float dt          = Engine::Get()->getDeltaTime() / 1000.0f;

	const bool animEnded = m_mesh.animation_call_back->hasAnimationEnded();
	const irr::f32 frame = m_mesh.node->getFrameNr();

	const bool lmb = InputManager::Get()->isMouseButtonPressed(MB_LEFT);
	if (!lmb)
	{
		m_dryFiredThisPress = false;
		m_bloom -= m_bloomDecay * dt;
		if (m_bloom < 0.0f)
			m_bloom = 0.0f;
	}

	switch (m_state)
	{
	case State::Unequipping:
		if (animEnded)
			unequip();
		return;

	case State::Equipping:
		// The draw works the bolt, so the round needs the same treatment it gets
		// when firing — minus the brass, because nothing was fired.
		updateRound(false);

		if (!m_boltPlayed && frame >= static_cast<irr::f32>(m_equipBoltFrame - soundLeadFrames(m_boltLeadSec)))
		{
			m_boltPlayed = true;
			SoundManager::Get()->sound()->playRandomized2D(
				"content/sound/weapon/cock_rifle", 0.07f, 2, -1.0f, "smg_bolt");
		}

		if (animEnded)
			enterState(State::Idle);

		RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
		return;

	case State::Reloading:
		updateMagazine();

		// Each reload clip has its own frame offsets for the same three cues.
		if (m_chamberEmpty)
			updateReloadSounds(frame, m_magOutFrameEmpty, m_magInFrameEmpty, m_boltFrameEmpty);
		else
			updateReloadSounds(frame, m_magOutFrame, m_magInFrame, -1);

		if (animEnded)
		{
			// The magazine is provably home by the end, so credit here too in case
			// a frame-rate hitch stepped over the displacement window.
			if (!m_ammoCredited)
			{
				m_ammoCredited = true;
				m_rounds += drawFromReserve(m_magSize - m_rounds);
			}

			// The empty reload racks the bolt as part of the clip, so the chamber
			// is loaded again by the time it ends.
			m_chamberEmpty = false;

			enterState(State::Idle);
		}
		break;

	case State::Firing:
	case State::Idle:
	default:
		// Full auto, so the trigger is a LEVEL and the fire clip does not block
		// the next round — the cadence timer does. Firing therefore lives in the
		// same branch as idle, and the fire clip simply restarts underneath it.
		updateRound(true);

		if (m_state == State::Firing && animEnded)
			m_state = State::Idle;

		if (!stabilizationRestValid())
		{
			m_mesh.node->updateAbsolutePosition();
			m_mesh.node->animateJoints();
			captureStabilizationRest();
		}

		if (lmb && (currentTime - m_lastFireTime) >= m_fireInterval)
		{
			if (m_rounds > 0)
			{
				m_lastFireTime = currentTime;
				fire();
			}
			else if (!m_dryFiredThisPress)
			{
				m_dryFiredThisPress = true;
				SoundManager::Get()->sound()->playRandomized2D(
					"content/sound/weapon/dryfire", 0.05f, 1, -1.0f, "smg_dryfire");
			}
		}
		break;
	}

	RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
}

void Weapon_SMG::persist()
{
	m_effects.update(Engine::Get()->getDeltaTime());
}

void Weapon_SMG::equip()
{
	m_mesh.node->setVisible(true);
	m_mesh.animation_call_back->hasAnimationEnded();

	m_bloom             = 0.0f;
	m_dryFiredThisPress = true; // don't dry-click on a button held through the switch
	resetViewKick();

	setMeshPartVisible(m_round, true);
	setMeshPartVisible(m_mag,   true);

	playEquipSound();

	enterState(State::Equipping);

	if (!m_mesh.findAnimation("equip"))
		enterState(State::Idle);
}

void Weapon_SMG::unequip()
{
	m_state = State::Idle;

	setClipSpeed(1.0f);
	setStabilizationAmount(0.0f);

	setMeshPartVisible(m_round, true);
	setMeshPartVisible(m_mag,   true);

	m_mesh.node->setVisible(false);
}

void Weapon_SMG::startUnequip()
{
	if (!m_mesh.node || !m_mesh.node->isVisible() || m_state == State::Unequipping)
		return;

	m_dryFiredThisPress = true;

	setClipSpeed(1.0f);
	setStabilizationAmount(0.0f);

	setMeshPartVisible(m_round, true);
	setMeshPartVisible(m_mag,   true);

	playUnequipSound();

	if (m_mesh.findAnimation("unequip"))
		enterState(State::Unequipping);
	else
		unequip();
}

void Weapon_SMG::idle() {}
void Weapon_SMG::move() {}

// --- Firing ------------------------------------------------------------------

void Weapon_SMG::fire()
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>() || !m_mesh.node)
		return;

	m_rounds--;

	// Last round out: the bolt locks back on an empty magazine, so the reload owes
	// a rack and reload() must pick the longer clip.
	if (m_rounds <= 0)
		m_chamberEmpty = true;

	enterState(State::Firing);

	addViewKick(
		irr::core::vector3df(0.0f, 0.012f, -0.030f),
		irr::core::vector3df(1.7f, 0.0f, Engine::Get()->rng()->getFloat(-0.9f, 0.9f)));

	auto& camera = player.getComponent<CameraComponent>();

	camera.camera->updateAbsolutePosition();
	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();

	// The same point the flash is drawn at, by construction
	const irr::core::vector3df muzzlePos = m_effects.muzzleWorldPosition();

	irr::core::vector3df target    = camera.camera->getTarget();
	irr::core::vector3df cameraPos = camera.camera->getAbsolutePosition();
	irr::core::vector3df forward   = (target - cameraPos).normalize();

	irr::core::vector3df up(0, 1, 0);
	irr::core::vector3df right = forward.crossProduct(up).normalize();
	irr::core::vector3df down  = right.crossProduct(forward).normalize();

	irr::core::vector3df direction = getAimDirection(muzzlePos);

	const float spread = m_spreadMin + m_bloom;
	const float capped = (spread > m_spreadMax) ? m_spreadMax : spread;

	const float spreadRight = Engine::Get()->rng()->getFloat(-capped, capped);
	const float spreadDown  = Engine::Get()->rng()->getFloat(-capped, capped);
	direction = (direction + right * spreadRight + down * spreadDown).normalize();

	m_bloom += m_bloomPerShot;

	const irr::core::vector3df rayEnd = muzzlePos + direction * 1000.0f;

	RaycastResultData raycastResult = RenderManager::Get()->raycastWorldPosition(muzzlePos, rayEnd, true);

	if (raycastResult.hit && raycastResult.node)
	{
		auto& hitEntity = WorldManager::Get()->managerSystem()->getEntityByID(raycastResult.node->getID());

		if (hitEntity.isValid() && hitEntity.hasComponent<DescriptorComponent>())
		{
			auto& hitDescriptor = hitEntity.getComponent<DescriptorComponent>();

			if (hitDescriptor.type == ET_STATIC || hitDescriptor.type == ET_DYNAMIC)
			{
				registerHitFeedback(
					WorldManager::Get()->gameplaySystem()->damageEntity(hitDescriptor.id, m_damage, DAMAGE_TYPE::DEFAULT,
						DamageContext::fromImpact(raycastResult.point, raycastResult.normal,
							raycastResult.ray.getVector())));

				// Sparks and a bullet hole are for hard surfaces. Anything carrying a
				// damage receiver is flesh as far as feedback goes, and GoreManager has
				// already covered it.
				if (!hitEntity.hasComponent<DamageReceiverComponent>())
					m_effects.impact(raycastResult.point, raycastResult.normal);
			}
		}
		else if (RenderManager::isWorldGeometryNode(raycastResult.node))
		{
			m_effects.impact(raycastResult.point, raycastResult.normal);
		}
	}

	const irr::core::vector3df tracerEnd = (raycastResult.hit && raycastResult.node)
		? raycastResult.point : (muzzlePos + direction * 1000.0f);
	m_effects.spawnTracer(muzzlePos, tracerEnd);

	m_effects.muzzleFlash();

	g_CameraFX.addRecoil(-0.7f, Engine::Get()->rng()->getFloat(-0.25f, 0.25f));

	SoundManager::Get()->sound()->playRandomized2D(
		"content/sound/weapon/pistol/fire", 0.06f, 6, 0.55f, "smg_fire");
}

void Weapon_SMG::reload()
{
	if (m_state != State::Idle && m_state != State::Firing)
		return;

	if (m_rounds >= m_magSize)
		return; // nothing to top up

	if (reserveRemaining() <= 0)
	{
		playEmptyReserveSound();
		return;
	}

	if (!m_mesh.node)
		return;

	enterState(State::Reloading);

	// A gun run dry needs its bolt racking after the magazine goes in, and one
	// with a round still chambered does not. The two clips differ by exactly
	// that, so the choice is just which one to play.
	if (m_chamberEmpty)
	{
		setClipSpeed(m_emptySpeed);
		playAnimation("reload_empty");
	}
	else
	{
		setClipSpeed(m_reloadSpeed);
		playAnimation("reload");
	}
}

// Frame-triggered reload audio. The frames differ between the two clips, so they
// arrive as arguments — boltFrame < 0 means this clip does not rack the bolt.
void Weapon_SMG::updateReloadSounds(float frame, int magOut, int magIn, int boltFrame)
{
	const int f = static_cast<int>(frame);

	if (!m_magOutPlayed && f >= magOut - soundLeadFrames(m_removeMagLeadSec))
	{
		m_magOutPlayed = true;
		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/remove_mag", 0.05f, 1, -1.0f, "smg_mag");
	}

	if (!m_magInPlayed && f >= magIn - soundLeadFrames(m_insertMagLeadSec))
	{
		m_magInPlayed = true;
		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/insert_mag", 0.05f, 1, -1.0f, "smg_mag");
	}

	if (boltFrame >= 0 && !m_boltPlayed && f >= boltFrame - soundLeadFrames(m_boltLeadSec))
	{
		m_boltPlayed = true;
		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/cock_rifle", 0.07f, 2, -1.0f, "smg_bolt");
	}
}

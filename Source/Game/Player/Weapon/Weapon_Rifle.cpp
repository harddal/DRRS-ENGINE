#include "Weapon_Rifle.h"

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

void Weapon_Rifle::precache()
{
	ParticleManager::Get()->precache("spark", _asset_psys("spark"));

	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/dryfire.wav",    true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/cock_rifle.wav", true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/remove_mag.wav", true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/insert_mag.wav", true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/rifle/fire.wav", true);
}

void Weapon_Rifle::init()
{
	m_descriptor.name = "Player_Weapon_Rifle";
	m_descriptor.id   = _entity_null_value;

	m_weapon_type     = WEAP_RIFLE;

	// rifle_animated.glb carries the same arms rig as the rest of the glTF pack —
	// identical joint names, identical 'arms' root at (0, 2.945, -17.671). A
	// 130-unit barrel puts it between the heavy rifle and the sniper, so it
	// starts from their framing. Tune with the F2 window, not here.
	m_viewPositionOffset = irr::core::vector3df(0.1050f, -0.1650f, 0.0800f);
	m_viewRotationOffset = irr::core::vector3df(0.0f, 180.0f, 0.0f);
	m_viewScaleOffset    = irr::core::vector3df(0.01f, 0.01f, 0.01f);

	m_mesh.mesh = _asset_glb("player/weapon/rifle_animated");

	m_mesh.trimesh = RenderManager::Get()->loadMesh(m_mesh.mesh);

	// Swap in the stand-in BEFORE the node is created — creating a node in the
	// failure branch and again below orphans the first one.
	const bool usingStandIn = (m_mesh.trimesh == nullptr);
	if (usingStandIn)
	{
		spdlog::warn("Weapon_Rifle::init(): failed to load mesh \"{}\", stand-in mesh loaded", m_mesh.mesh);
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

	// Clip table recovered from the .glb — ONE "allanims" take, frames 0-242 at
	// 30 fps, clips separated by rest holds. The boundaries were read from the
	// WHOLE POSE (every joint against frame 0), not from the weapon root: the
	// root can be back at its seat while the arms are still moving, which is what
	// made the sniper's draw snap when it was cut on the root alone.
	// Rest holds at 0, 11/12, 91-94, 149-154, 200/201, 221/222, 242.
	//   0-11     trigger f1-11, bolt slams back 10.5 on f1 and is
	//            home by f5, the case thrown 9.4 on the same frame  -> fire
	//            (a self-loader: the bolt cycles inside the shot)
	//   12-91    magazine out f34, home f53, and THEN the bolt is
	//            cycled at f68                                      -> reload_empty
	//   94-149   magazine out f116, home f135, no bolt at all       -> reload
	//   154-200  rifle swings away to its apex at f161 (yaw +65,
	//            roll -47, dropping 7.7) and comes back, WORKING
	//            THE BOLT at f177 on the way in                     -> unequip
	//            154-161, equip 161-200
	//   201-221  a 1.0-unit drift, no rotation                      -> idle sway,
	//            unused: idle is pinned to 201 and the hold-steady
	//            motion comes from enableIdleBreathing()
	//   222-242  snaps to a -94 degree roll in TWO frames and takes
	//            eighteen to recover, no part motion whatever       -> melee bash
	//
	// THE TWO RELOADS ARE THE POINT OF THIS ASSET. Only the 12-91 take re-cycles
	// the bolt, which is exactly what a rifle needs after running dry and exactly
	// what it must NOT do with a round still chambered. reload() picks between
	// them on m_chamberEmpty.
	//
	// 222-242 is a bash, not a draw, and the tell is the same across this whole
	// pack: a bash reaches its pose within a few frames and recovers over three
	// times as long, with a large NEGATIVE roll, while a holster/draw is
	// symmetric about its apex and takes the weapon DOWN.
	//
	// Looping clips MUST be flagged loop=true — a non-looping clip re-armed from
	// the end callback holds its last frame for one tick every cycle.
	m_mesh.animationList.emplace_back(sAnimationData("fire",         0,   11,  false));
	m_mesh.animationList.emplace_back(sAnimationData("reload_empty", 12,  91,  false));
	m_mesh.animationList.emplace_back(sAnimationData("reload",       94,  149, false));
	m_mesh.animationList.emplace_back(sAnimationData("unequip",      154, 161, false));
	m_mesh.animationList.emplace_back(sAnimationData("equip",        161, 200, false));
	m_mesh.animationList.emplace_back(sAnimationData("idle",         201, 201, true));
	// Authored but not bound to input — a rifle-butt bash, if a melee slot is wanted
	m_mesh.animationList.emplace_back(sAnimationData("melee",        222, 242, false));

	// Both glTF backends normalise keyframe times to 30 fps Irrlicht frames, so
	// the viewmodel must play at 30 to run at its authored speed.
	m_mesh.fps = 30;
	m_mesh.node->setAnimationSpeed(static_cast<irr::f32>(m_mesh.fps));

	m_mesh.node->setJointMode(irr::scene::EJUOR_READ);

	m_mesh.animation_call_back = std::make_shared<AnimationCallback>();
	m_mesh.node->setAnimationEndCallback(m_mesh.animation_call_back.get());

	playAnimation("idle");

	// The idle clip is pinned to a single frame, so all the hold-steady motion
	// comes from here. Braced into the shoulder, so tighter than the pistols.
	enableIdleBreathing(1.10f);

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

	// Reference point for reload stabilisation: the bore line at roughly
	// mid-barrel. 'base' spans Z -30 to +100 and 6.1 is the bore height already
	// measured for the muzzle. Z negated for the handedness conversion.
	enableClipStabilization("base", irr::core::vector3df(0.0f, 6.10f, -35.0f));
	setStabilizationTuneAmount(0.5f);

	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");

	if (!player.isValid())
	{
		spdlog::error("In function Weapon_Rifle::init() -> getEntityByName(\"player\") : Entity 'player' does not exist");
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
		spdlog::error("In function Weapon_Rifle::init() -> player.getComponent<CameraComponent>() : Entity 'player' does not have specified component");
	}

	RenderManager::Get()->registerViewmodelNode(m_mesh.node);
	m_mesh.node->setVisible(false);

	m_rounds = m_magSize;

	m_crosshair = RenderManager::Get()->driver()->getTexture("content/texture/ui/crosshair/crosshair038.png");

	// This .glb has no FIRESPOT empty, but 'base' is the receiver-and-barrel
	// joint, so anything parented to it inherits the recoil, the reload and the
	// holster for free. The offset is the bore centre at the muzzle face — the 82
	// verts within 1 unit of the barrel's far end centre on Y 6.1 — and
	// GltfImport's handedness conversion negates Z.
	WeaponEffectsDesc fx;
	fx.muzzleJointName   = "base";
	fx.muzzleJointOffset = irr::core::vector3df(0.0f, 6.10f, -100.03f);
	fx.flashColor        = irr::video::SColor(255, 255, 210, 140);
	fx.flashSize         = 0.75f;
	fx.flashDuration     = 55.0f;
	fx.lightColor        = irr::video::SColorf(1.0f, 0.78f, 0.3f);
	fx.lightRadius       = 4.5f;
	fx.tracerFrequency   = 3;
	fx.tracerPoolSize    = 16;
	// Brass comes from the animated round itself, not from a port, so
	// shellEjectJoint stays unused and ejectShell() is never called.
	fx.shellMesh         = "content/mesh/prop/shells/shellmedium.obj";
	fx.shellSpeed        = 5.0f;
	// Sized off the SHELL LIFETIME, not the magazine: a casing lives 10 s and this
	// fires 6/s, so roughly 60 are alive during sustained fire even though the
	// magazine only holds 20.
	fx.shellPoolSize     = 64;
	fx.impactParticle    = "spark";
	m_effects.init(m_mesh.node, fx);
}

void Weapon_Rifle::destroy()
{
	m_effects.destroy();

	RenderManager::Get()->unregisterViewmodelNode(m_mesh.node);
	m_mesh.node->remove();

	WorldManager::Get()->freeEntityID(m_descriptor.id);
}

// --- State -------------------------------------------------------------------

void Weapon_Rifle::enterState(State next)
{
	m_state = next;
	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag

	setClipSpeed(next == State::Firing    ? m_fireSpeed
	           : next == State::Equipping ? m_equipSpeed
	           : 1.0f); // reload sets its own below, once it knows which clip

	// Only the reloads are stabilised — they swing the rifle far enough that the
	// muzzle wanders off screen on a 130-unit barrel.
	setStabilizationAmount(next == State::Reloading ? stabilizationTuneAmount() : 0.0f);

	switch (next)
	{
	case State::Firing:
		playAnimation("fire");
		// m_roundRestValid is NOT cleared here. The seated position is a constant,
		// and re-sampling at the top of a fire clip risks catching frame 1 — where
		// the bolt is already back — and taking a mid-throw position as "home".
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
		// The draw cycles the bolt, so the round goes through the same reuse flick
		// it does when firing and the cue has to be re-armed. The rest IS re-sampled
		// here: frame 0 of the draw is a genuine rest hold, and a weapon switch is
		// the one place the joint could have been left somewhere else.
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
void Weapon_Rifle::updateRound(bool throwCase)
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
// this: the magazine leaves at f34 in one and f116 in the other, and neither
// number appears here.
void Weapon_Rifle::updateMagazine()
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

void Weapon_Rifle::ejectSpentCase()
{
	irr::core::matrix4 world;
	if (!meshPartWorldTransform(m_round, world))
		return;

	// Away from the port and up, from the RIFLE's basis rather than the camera's,
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
	const float speed = 280.0f * unit;

	const irr::core::vector3df velocity =
		axisRight * speed * Engine::Get()->rng()->getFloat(0.8f, 1.2f) +
		axisUp    * speed * Engine::Get()->rng()->getFloat(0.3f, 0.6f);

	// Turn the casing end for end — taken straight from the breech it flies
	// mouth-first at the camera. A LOCAL flip, since the right operand applies
	// first under Irrlicht's operator*; adding 180 to the Euler Y would yaw it in
	// the parent frame and fall apart the moment the rifle is pitched.
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

void Weapon_Rifle::update()
{
	if (!m_mesh.node || !m_mesh.node->isVisible())
		return;

	const float currentTime = Engine::Get()->getCurrentTime();

	const bool animEnded = m_mesh.animation_call_back->hasAnimationEnded();
	const irr::f32 frame = m_mesh.node->getFrameNr();

	const bool lmb = InputManager::Get()->isMouseButtonPressed(MB_LEFT);
	if (!lmb)
		m_firedThisPress = false;

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
				"content/sound/weapon/cock_rifle", 0.07f, 2, -1.0f, "rifle_bolt");
		}

		if (animEnded)
			enterState(State::Idle);

		RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
		return;

	case State::Firing:
		updateRound(true);

		if (animEnded)
			enterState(State::Idle);
		break;

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

			// The empty reload cycles the bolt as part of the clip, so the chamber
			// is loaded again by the time it ends.
			m_chamberEmpty = false;

			enterState(State::Idle);
		}
		break;

	case State::Idle:
	default:
		if (!stabilizationRestValid())
		{
			m_mesh.node->updateAbsolutePosition();
			m_mesh.node->animateJoints();
			captureStabilizationRest();
		}

		if (lmb && !m_firedThisPress && (currentTime - m_lastFireTime) >= m_fireInterval)
		{
			m_firedThisPress = true;

			if (m_rounds > 0)
			{
				m_lastFireTime = currentTime;
				fire();
			}
			else
			{
				SoundManager::Get()->sound()->playRandomized2D(
					"content/sound/weapon/dryfire", 0.05f, 1, -1.0f, "rifle_dryfire");
			}
		}
		break;
	}

	RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
}

void Weapon_Rifle::persist()
{
	m_effects.update(Engine::Get()->getDeltaTime());
}

void Weapon_Rifle::equip()
{
	m_mesh.node->setVisible(true);
	m_mesh.animation_call_back->hasAnimationEnded();

	m_firedThisPress = true; // don't fire on a button held through the switch
	resetViewKick();

	setMeshPartVisible(m_round, true);
	setMeshPartVisible(m_mag,   true);

	playEquipSound();

	enterState(State::Equipping);

	if (!m_mesh.findAnimation("equip"))
		enterState(State::Idle);
}

void Weapon_Rifle::unequip()
{
	m_state = State::Idle;

	setClipSpeed(1.0f);
	setStabilizationAmount(0.0f);

	setMeshPartVisible(m_round, true);
	setMeshPartVisible(m_mag,   true);

	m_mesh.node->setVisible(false);
}

void Weapon_Rifle::startUnequip()
{
	if (!m_mesh.node || !m_mesh.node->isVisible() || m_state == State::Unequipping)
		return;

	m_firedThisPress = true;

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

void Weapon_Rifle::idle() {}
void Weapon_Rifle::move() {}

// --- Firing ------------------------------------------------------------------

void Weapon_Rifle::fire()
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>() || !m_mesh.node)
		return;

	m_rounds--;

	// Last round out: the bolt locks back on an empty magazine, so the reload owes
	// a bolt cycle and reload() must pick the longer clip.
	if (m_rounds <= 0)
		m_chamberEmpty = true;

	enterState(State::Firing);

	addViewKick(
		irr::core::vector3df(0.0f, 0.022f, -0.065f),
		irr::core::vector3df(3.4f, 0.0f, Engine::Get()->rng()->getFloat(-1.2f, 1.2f)));

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

	const float spreadRight = Engine::Get()->rng()->getFloat(-m_spread, m_spread);
	const float spreadDown  = Engine::Get()->rng()->getFloat(-m_spread, m_spread);
	direction = (direction + right * spreadRight + down * spreadDown).normalize();

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

	g_CameraFX.addRecoil(-1.5f, Engine::Get()->rng()->getFloat(-0.35f, 0.35f));

	SoundManager::Get()->sound()->playRandomized2D(
		"content/sound/weapon/rifle/fire", 0.05f, 4, 0.7f, "rifle_fire");
}

void Weapon_Rifle::reload()
{
	if (m_state != State::Idle)
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

	// THE ASSET'S BEST FEATURE: a rifle run dry needs its bolt cycling after the
	// magazine goes in, and one with a round still chambered does not. The two
	// clips differ by exactly that, so the choice is just which one to play.
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
// arrive as arguments — boltFrame < 0 means this clip does not cycle the bolt.
void Weapon_Rifle::updateReloadSounds(float frame, int magOut, int magIn, int boltFrame)
{
	const int f = static_cast<int>(frame);

	if (!m_magOutPlayed && f >= magOut - soundLeadFrames(m_removeMagLeadSec))
	{
		m_magOutPlayed = true;
		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/remove_mag", 0.05f, 1, -1.0f, "rifle_mag");
	}

	if (!m_magInPlayed && f >= magIn - soundLeadFrames(m_insertMagLeadSec))
	{
		m_magInPlayed = true;
		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/insert_mag", 0.05f, 1, -1.0f, "rifle_mag");
	}

	if (boltFrame >= 0 && !m_boltPlayed && f >= boltFrame - soundLeadFrames(m_boltLeadSec))
	{
		m_boltPlayed = true;
		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/cock_rifle", 0.07f, 2, -1.0f, "rifle_bolt");
	}
}

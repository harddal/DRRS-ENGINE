#include "Weapon_Shotgun.h"

#include "Engine/Engine.h"
#include "../CameraFX.h"

#undef MB_RIGHT

using namespace irr;
using namespace SPK;
using namespace SPK::IRR;

void Weapon_Shotgun::precache()
{
	ParticleManager::Get()->precache("spark_smoke", _asset_psys("spark_smoke"));

	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/dryfire.wav",              true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/insert_shell_shotgun.wav", true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/shotgun/Shotgun_Quick Pump_01.wav", true);
}

void Weapon_Shotgun::init()
{
	m_descriptor.name = "Player_Weapon_Shotgun";
	m_descriptor.id = _entity_null_value;

	m_weapon_type = WEAP_SHOTGUN;

	m_viewPositionOffset = irr::core::vector3df(0.1000f, -0.1350f, 0.0450f);
	m_viewRotationOffset = irr::core::vector3df(0.0f, 180.0f, 0.0f);
	m_viewScaleOffset = irr::core::vector3df(0.01f, 0.01f, 0.01f);

	m_mesh.mesh = _asset_glb("player/weapon/shotgun_animated");

	m_mesh.trimesh = RenderManager::Get()->loadMesh(m_mesh.mesh);
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
	m_mesh.node->setJointMode(irr::scene::EJUOR_READ);

	m_mesh.animation_call_back = std::make_shared<AnimationCallback>();
	m_mesh.node->setAnimationEndCallback(m_mesh.animation_call_back.get());

	// One "allanims" take, 0-179 at 30 fps, clips separated by 2-frame holds at
	// the shared rest pose (boundaries at 0, 15/16, 42/43, 94/95, 135/136,
	// 160/161, 179). Frames inside each clip, measured off the .glb:
	//   0-15    trigger f1-14                                      -> fire
	//   17-42   pump slides back f21-23, case flicks up f30-32,
	//           then SNAPS back into the gun at f33                -> pump
	//   44-86   shell sits in the gun f44-54, jumps to the hand at
	//           f55 and travels up, seating at f69                 -> reload (one shell)
	//   87-94   closing motion                                     -> reload_end
	//   96-135  gun drops out of frame and comes back              -> unequip / equip
	//   137-160 fingers and gun shift <1.3 units, no root travel   -> a subtle idle,
	//           unused: idle is pinned to 136 and the hold-steady
	//           motion comes from enableIdleBreathing() instead
	//   162-178 arm poles swing 22.6, gun travels 13               -> melee bash
	//
	// Both slug behaviours reuse ONE mesh, which is why updateSlug() has to hide
	// and show it per frame — see the notes there.
	//
	// melee ends at 178, not 179: Irrlicht clamps EndFrame to getFrameCount()-1
	// and CSkinnedMesh::getFrameCount() returns the last frame INDEX.
	m_mesh.animationList.emplace_back(sAnimationData("equip",      105, 135, false));
	m_mesh.animationList.emplace_back(sAnimationData("idle",       136, 136, true));
	m_mesh.animationList.emplace_back(sAnimationData("fire",       0,   15,  false));
	m_mesh.animationList.emplace_back(sAnimationData("pump",       17,  42,  false));
	// One shell per pass. Two ranges, because the authored clip begins with the
	// left hand ON THE PUMP: f44-54 is the one-time move off it and down to the
	// belt, f55-69 brings the shell up, f70-76 settles, f77-86 drops back down
	// ready for the next. Replaying from 44 therefore snapped the hand 55.9 units
	// back up to the pump on every shell. From the second shell on, start at 55
	// instead — the wrist sits at exactly (27.0, -36.1, -2.0) at both f55 and
	// f86, so that repeat is seamless, and it is 11 frames shorter into the
	// bargain.
	m_mesh.animationList.emplace_back(sAnimationData("reload",      44,  86,  false)); // first shell
	m_mesh.animationList.emplace_back(sAnimationData("reload_loop", 55,  86,  false)); // hand already down
	m_mesh.animationList.emplace_back(sAnimationData("reload_end",  87,  94,  false));
	m_mesh.animationList.emplace_back(sAnimationData("unequip",    96,  104, false));
	// Authored but not bound to input — a bash, if a melee slot is wanted
	m_mesh.animationList.emplace_back(sAnimationData("melee",      162, 178, false));

	playAnimation("idle"); // safe default until equip() runs

	// The idle clip is pinned to a single frame, so the hold-steady motion comes
	// from updateWeaponSway() instead. Above default: a long gun held out front.
	enableIdleBreathing(1.25f);

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

	// Must come AFTER the material assignment above — it caches the slug's real
	// material type so setMeshPartVisible() has something to restore.
	resolveMeshPart("slug", m_slug);

	// Reference point for pump stabilisation: the bore line at roughly the gun's
	// mid-length. 'base' spans Z -31 to +84.6 and 4.92 is the bore height already
	// measured for the muzzle, so this splits the rotation error between muzzle
	// and stock instead of pinning one end and swinging the other.
	enableClipStabilization("base", irr::core::vector3df(0.0f, 4.92f, -27.0f));
	setStabilizationTuneAmount(m_pumpStabilize);

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

	// Character sheet: big smoky blast (first muzzle flash this weapon has ever had),
	// slug shells out the side port, smoke-heavy pellet impacts, no tracers
	WeaponEffectsDesc fx;
	// No FIRESPOT empty in this .glb, so the flash hangs off 'base' — the receiver
	// and barrel joint — and inherits the recoil and pump for free. The offset is
	// the bore centre at the muzzle face: the 162 verts within 1 unit of the far
	// end run evenly from Y 2.85 to 7.00 with no gap wider than 0.37, so they are
	// ONE ring of radius ~2.08 and the bore is its midpoint at Y 4.92 — not two
	// tubes with the bore on top, which is what splitting them at the median
	// first suggested. GltfImport's handedness conversion negates Z.
	fx.muzzleJointName   = "base";
	fx.muzzleJointOffset = irr::core::vector3df(0.0f, 4.92f, -84.61f);
	fx.flashColor      = irr::video::SColor(255, 255, 214, 110);
	fx.flashSize       = 1.0f;
	fx.flashDuration   = 60.0f;
	fx.lightColor      = irr::video::SColorf(1.0f, 0.8f, 0.3f);
	fx.lightRadius     = 4.0f;
	fx.tracerPoolSize  = 0;
	// The casing is spawned by ejectSpentShell() from the animated slug's own
	// bone, not from a port — so shellEjectJoint/shellEjectOffset stay unset and
	// ejectShell() is never called. Same approach as the revolver's brass.
	fx.shellMesh       = "content/mesh/prop/shells/slug.obj";
	fx.shellSpeed      = 6.0f;
	fx.shellPoolSize   = 16;
	fx.shellBounceSoundBase = "content/sound/prop/shotgunshell";
	fx.impactParticle  = "spark_smoke";
	fx.impactDecalSize = 0.15f; // smaller per-pellet holes, clustered
	m_effects.init(m_mesh.node, fx);
}

void Weapon_Shotgun::destroy()
{
	m_effects.destroy();

	RenderManager::Get()->unregisterViewmodelNode(m_mesh.node);
	m_mesh.node->remove();
	WorldManager::Get()->freeEntityID(m_descriptor.id);
}

// Single place that starts a clip and moves the machine, so the "what plays
// next" rules live in one readable block instead of being scattered across the
// call sites that trigger them.
void Weapon_Shotgun::enterState(State next)
{
	const State prev = m_state;

	m_state = next;
	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag

	// The working-the-action states run quicker than authored; everything else
	// plays at 1x. Set here rather than at the call sites so no path can leave
	// the node stuck at the faster speed.
	const bool quick = (next == State::Firing
	                 || next == State::Pumping
	                 || next == State::Reloading
	                 || next == State::ReloadEnd);
	setClipSpeed(quick ? m_actionSpeed : 1.0f);

	// Only the pump is stabilised. Same choke point as the speed above, so no
	// path can leave the counter-offset applied to a clip that does not want it.
	// Reads the tunable rather than the constant so the F2 slider survives here.
	setStabilizationAmount(next == State::Pumping ? stabilizationTuneAmount() : 0.0f);

	switch (next)
	{
	case State::Firing:      playAnimation("fire");       break;

	case State::Pumping:
		playAnimation("pump");
		m_slugHandedOff   = false;
		m_pumpSoundPlayed = false;
		// Only a rack that follows a shot has brass in it. Coming from ReloadEnd
		// the chamber was already emptied by the post-fire pump, so that rack
		// chambers a fresh shell and throws nothing.
		m_pumpEjects = (prev == State::Firing);
		break;

	// Second and later shells skip the move off the pump — see the clip table
	case State::Reloading:
		playAnimation(prev == State::Reloading ? "reload_loop" : "reload");
		m_insertShellPlayed = false; // one click per shell, so re-arm each pass
		break;

	case State::ReloadEnd:   playAnimation("reload_end"); break;
	case State::Equipping:
		playAnimation("equip");
		m_equipRackPlayed = false; // re-armed for this draw's rack
		break;

	case State::Unequipping: playAnimation("unequip");    break;

	case State::Idle:
	default:
		playAnimation("idle");
		setMeshPartVisible(m_slug, true);
		break;
	}
}

void Weapon_Shotgun::update()
{
	if (!m_mesh.node)
		return;

	const bool animEnded = m_mesh.animation_call_back->hasAnimationEnded();
	const irr::f32 frame = m_mesh.node->getFrameNr();

	updateSlug(frame);

	// Read up here so the busy states below can see it too, but act on it only
	// inside them: changing state before the switch would let this frame's
	// already-latched animEnded fall through and end the clip we just started.
	const bool lmb = InputManager::Get()->isMouseButtonPressed(MB_LEFT);
	if (!lmb)
		m_firedThisPress = false;

	// A press while the gun is busy, with shells to fire and not already spent on
	// this press, queues a shot for the moment the action closes.
	const bool wantsInterrupt = lmb && !m_firedThisPress && m_shells > 0;

	switch (m_state)
	{
	// Holstering: stay visible until the clip finishes so the gun is seen being
	// put away. isUnequipping() going false releases WeaponController's pending
	// switch, so the next weapon is only drawn once this one is down.
	case State::Unequipping:
		if (animEnded)
			unequip();
		RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
		return;

	case State::Equipping:
		if (animEnded)
		{
			enterState(State::Idle);
		}
		// The draw works the action at f111-124. Equip plays at 1x, but the lead
		// still goes through soundLeadFrames() so it cannot drift if that changes.
		else if (!m_equipRackPlayed &&
			frame >= static_cast<irr::f32>(m_equipRackFrame - soundLeadFrames(m_pumpSoundLeadSec)))
		{
			m_equipRackPlayed = true;
			playPumpSound();
		}
		RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
		return;

	// Every shot racks the action, without exception — that is the weapon's
	// whole character, and it is also what ejects the case.
	case State::Firing:
		if (animEnded)
			enterState(State::Pumping);
		RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
		return;

	case State::Pumping:
		// Racked from the clip's own frame rather than on a timer started by the
		// shot. That is what makes it stay in sync — and what makes it play at
		// all for the rack that closes an empty reload, which never went through
		// fire() and so never armed a timer.
		if (!m_pumpSoundPlayed &&
			frame >= static_cast<irr::f32>(m_rackBackFrame - soundLeadFrames(m_pumpSoundLeadSec)))
		{
			m_pumpSoundPlayed = true;
			playPumpSound();
		}

		if (animEnded)
			finishAction();
		// Buffered input: press during the rack and the shot goes off the moment
		// it finishes, rather than being dropped.
		else if (wantsInterrupt)
		{
			m_firedThisPress  = true;
			m_fireAfterReload = true;
		}
		RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
		return;

	// One clip per shell. Loops itself until the tube is full, so the reload
	// length scales with how much was actually spent.
	case State::Reloading:
		if (animEnded)
		{
			// One clip, one shell, one shell out of the pool. Nothing to guard
			// against double-crediting here: each pass through this state thumbs
			// exactly one shell in, so the draw is naturally per-round.
			if (m_shells < m_magSize)
				m_shells += drawFromReserve(1);

			// Stop when the tube is full OR when the pool has run dry — without
			// the second test an empty reserve loops the loading clip forever.
			if (m_shells < m_magSize && reserveRemaining() > 0)
				enterState(State::Reloading);
			else
				enterState(State::ReloadEnd);
		}
		// Fire pressed mid-reload: stop thumbing shells in, close the gun up and
		// shoot as soon as it is ready. Topping off a tube one shell at a time is
		// a long commitment, and this is the way out of it when something walks
		// in. Shells already loaded are kept — each one was banked as its own
		// clip ended.
		else if (wantsInterrupt)
		{
			m_firedThisPress  = true;
			m_fireAfterReload = true;
			enterState(State::ReloadEnd);
		}
		// One click per shell, landing as it seats at f69. Both reload ranges
		// (44-86 and 55-86) contain that frame, so the same absolute trigger
		// serves the first shell and every repeat.
		else if (!m_insertShellPlayed &&
			frame >= static_cast<irr::f32>(m_reloadSeatFrame - soundLeadFrames(m_insertShellLeadSec)))
		{
			m_insertShellPlayed = true;
			SoundManager::Get()->sound()->playRandomized2D(
				"content/sound/weapon/insert_shell_shotgun", 0.06f, 2, -1.0f, "shotgun_insert");
		}
		RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
		return;

	// Closing the reload. If a shot had run the tube dry, the post-fire pump left
	// the chamber empty, so one more rack is owed before the gun can fire.
	case State::ReloadEnd:
		if (animEnded)
		{
			if (m_needsChamberRack)
			{
				m_needsChamberRack = false;
				// A queued shot survives the hop: m_fireAfterReload is untouched,
				// so the pump fires it once the chamber is loaded. The gun is
				// never fired on an empty chamber just because fire was pressed.
				enterState(State::Pumping);
			}
			else
			{
				finishAction();
			}
		}
		else if (wantsInterrupt)
		{
			m_firedThisPress  = true;
			m_fireAfterReload = true;
		}
		RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
		return;

	case State::Idle:
	default:
		break;
	}

	// --- Idle: input is live -------------------------------------------------

	// Record where the stabilisation reference sits at rest. Done here rather
	// than in init() because the joints are stale while the node is hidden, and
	// once because the rest pose never changes.
	if (!stabilizationRestValid())
	{
		m_mesh.node->updateAbsolutePosition();
		m_mesh.node->animateJoints();
		captureStabilizationRest();
	}

	// lmb and the semi-auto release reset are handled at the top of update(), so
	// the busy states can see the press too.
	if (lmb && !m_firedThisPress)
	{
		if (m_shells > 0)
		{
			fire();
		}
		else
		{
			m_firedThisPress = true;
			SoundManager::Get()->sound()->playRandomized2D(
				"content/sound/weapon/dryfire", 0.05f, 1, -1.0f, "shotgun_dryfire");
		}
	}

	// Reload via R key
	static bool r = false;
	if (InputManager::Get()->getKeyPressOnce(KEYBOARD_KEY::KEY_R, &r))
		reload();

	// Recoil recovery + node transform are handled by updateWeaponSway()

	RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
}

// The one 'slug' mesh plays two parts, and in both the artist parks it back at
// its rest position inside the gun rather than animating it away — so left
// alone it teleports. Which half of the fix applies depends on the clip.
void Weapon_Shotgun::updateSlug(float frame)
{
	if (m_slug.buffers.empty())
		return;

	const int f = static_cast<int>(frame);

	switch (m_state)
	{
	case State::Pumping:
		// The case rides back with the pump and is thrown at the end of that
		// rearward stroke. Hiding the slug and handing it to a physics casing at
		// the same transform is what keeps the swap invisible — and it also means
		// the artist's flick-up-then-snap-home at f30-33 is never seen.
		//
		// A rack with no brass in it (closing an empty reload) leaves the slug
		// alone: hiding it without spawning anything would just make the shell
		// vanish, which reads worse than the flick it avoids.
		if (m_pumpEjects && f >= m_pumpEjectFrame && !m_slugHandedOff)
		{
			m_slugHandedOff = true;
			ejectSpentShell();
			setMeshPartVisible(m_slug, false);
		}
		break;

	case State::Reloading:
		// The shell sits in the gun for f44-54 and then jumps 47 units into the
		// hand at f55. Keeping it hidden until the hand arrives with it is what
		// stops that jump being visible.
		setMeshPartVisible(m_slug, f >= m_reloadShowFrame);
		break;

	default:
		setMeshPartVisible(m_slug, true);
		break;
	}
}

// Back to idle, unless a shot was queued while the gun was busy — in which case
// it goes off now that the action is closed. Routed through enterState(Idle)
// first because fire() only runs from Idle, and that is also what puts the clip
// speed and stabilisation back before the fire clip sets its own.
void Weapon_Shotgun::finishAction()
{
	const bool fireNow = m_fireAfterReload && m_shells > 0;
	m_fireAfterReload = false;

	enterState(State::Idle);

	if (fireNow)
		fire();
}

void Weapon_Shotgun::playPumpSound()
{
	SoundManager::Get()->sound()->play2D(
		"content/sound/weapon/shotgun/Shotgun_Quick Pump_01.wav",
		false, 0, -1.0f, nullptr, false,
		1.0f + Engine::Get()->rng()->getFloat(-0.03f, 0.03f));
}

void Weapon_Shotgun::ejectSpentShell()
{
	if (!m_mesh.node)
		return;

	// Joint transforms are stale while the viewmodel is hidden during drawAll(),
	// so force them before reading, the same way the fire path does.
	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();

	irr::core::matrix4 world;
	if (!meshPartWorldTransform(m_slug, world))
		return;

	// The animation flicks the case straight up with no sideways throw at all, so
	// unlike the revolver — where the ejector's own motion could be measured and
	// inherited — the port direction has to be supplied here. Right and up are
	// taken from the gun's own basis rather than the camera's, so the case leaves
	// the port correctly however the weapon is oriented.
	// -X, not +X: the receiver's ejection port faces the other way round on this
	// model, so throwing along +X sent the case back across the viewmodel.
	irr::core::vector3df axisRight(-1.0f, 0.0f, 0.0f);
	irr::core::vector3df axisUp(0.0f, 1.0f, 0.0f);
	world.rotateVect(axisRight);
	world.rotateVect(axisUp);
	axisRight.normalize();
	axisUp.normalize();

	// Speed derived from the joint's world scale so it stays correct at any
	// viewmodel scale, the same reasoning as the revolver's scatter.
	const irr::core::vector3df jointScale = world.getScale();
	const float unit  = std::max(jointScale.X, std::max(jointScale.Y, jointScale.Z));
	const float speed = 260.0f * unit;

	const irr::core::vector3df velocity =
		axisRight * speed * Engine::Get()->rng()->getFloat(0.7f, 1.1f) +
		axisUp    * speed * Engine::Get()->rng()->getFloat(0.5f, 0.9f);

	// Turn the casing end for end. Taken straight from the source bone it flies
	// mouth-first back at the camera; the open end belongs downrange.
	//
	// Composed as a LOCAL flip — the right operand applies first under Irrlicht's
	// operator* — rather than by adding 180 to the Euler Y. Adding to the Euler
	// yaws it in the PARENT frame, which only happens to look right while the
	// source bone is upright and comes apart as soon as it pitches or rolls.
	irr::core::matrix4 flip;
	flip.setRotationDegrees(irr::core::vector3df(0.0f, 180.0f, 0.0f));
	const irr::core::matrix4 oriented = world * flip;

	m_effects.spawnShellAt(
		world.getTranslation(),
		oriented.getRotationDegrees(),
		velocity,
		matchPartScale(m_slug, m_effects.shellMeshExtent()));
}

void Weapon_Shotgun::persist()
{
	m_effects.update(Engine::Get()->getDeltaTime());

	// The pump rack used to be queued here on a fixed delay from the blast. It is
	// now triggered off the pump clip's own frame in update(), which keeps it in
	// sync however the fire clip is paced and covers the rack that closes an
	// empty reload too.
}

void Weapon_Shotgun::equip()
{
	m_firedThisPress   = false;
	m_slugHandedOff    = false;
	m_fireAfterReload  = false; // a shot queued before the switch does not carry over
	resetViewKick();

	// A reload abandoned by a weapon switch keeps the shells that were already
	// thumbed in — each one is banked by the Reloading state as its clip ends —
	// but the slug must be shown again or it stays hidden for good.
	setMeshPartVisible(m_slug, true);

	playEquipSound();

	m_mesh.node->setPosition(m_viewPositionOffset);
	m_mesh.node->setRotation(m_viewRotationOffset);
	m_mesh.node->setVisible(true);

	enterState(State::Equipping);
}

void Weapon_Shotgun::unequip()
{
	m_slugHandedOff   = false;
	m_fireAfterReload = false;
	m_state           = State::Idle;
	setMeshPartVisible(m_slug, true);
	m_mesh.node->setVisible(false);
}

void Weapon_Shotgun::startUnequip()
{
	// Already hidden or mid-holster: nothing to play, don't restart the clip
	if (!m_mesh.node || !m_mesh.node->isVisible() || m_state == State::Unequipping)
		return;

	m_firedThisPress  = true; // block fire during transition
	m_fireAfterReload = false;
	setMeshPartVisible(m_slug, true);

	playUnequipSound();

	enterState(State::Unequipping);
}

void Weapon_Shotgun::idle()
{
}

void Weapon_Shotgun::move()
{
}

void Weapon_Shotgun::fire()
{
	if (m_state != State::Idle || m_shells <= 0)
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

	m_shells--;

	// Running the tube dry means the pump that follows this shot ejects the last
	// case and leaves the chamber empty, so a closing rack is owed once the
	// reload finishes. Recorded here rather than inferred later, because by then
	// the reload has already refilled m_shells.
	if (m_shells == 0)
		m_needsChamberRack = true;

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

	// Aggregate pellet results so a blast produces ONE feedback event, not eight
	HIT_RESULT bestResult = HIT_RESULT::NONE;

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

			HIT_RESULT r = WorldManager::Get()->gameplaySystem()->damageEntity(
				hitID, static_cast<unsigned int>(m_damagePerPellet), DAMAGE_TYPE::DEFAULT,
				DamageContext::fromImpact(raycastResult.point, raycastResult.normal, pelletDir));
			if (static_cast<int>(r) > static_cast<int>(bestResult))
				bestResult = r;

			// Smoke-heavy impact fanned off the surface + per-pellet bullet hole.
			// Skipped when the pellet found something damageable: GoreManager has
			// already put blood there, and sparking off flesh looks wrong.
			// (HIT_RESULT rather than a component test because this path resolves
			// the target by node id and never fetches the entity.)
			if (r == HIT_RESULT::NONE)
				m_effects.impact(raycastResult.point, raycastResult.normal);
		}
	}

	registerHitFeedback(bestResult);

	// No ejectShell() here: the case leaves during the pump that follows, handed
	// off from the animated slug by ejectSpentShell().
	m_effects.muzzleFlash();

	// Heavy recoil kick
	float verticalRecoil   = m_recoilAmount + Engine::Get()->rng()->getFloat(-m_recoilRandomnessVertical, m_recoilRandomnessVertical);
	float horizontalRecoil = Engine::Get()->rng()->getFloat(-m_recoilRandomnessHorizontal, m_recoilRandomnessHorizontal);

	addViewKick(
		irr::core::vector3df(0.0f, m_recoilPositionKick, 0.0f),
		irr::core::vector3df(-verticalRecoil, horizontalRecoil, 0.0f));

	// Camera shake + FOV punch for the big kick
	g_CameraFX.addRecoil(verticalRecoil * 0.4f, horizontalRecoil * 0.2f);
	g_CameraFX.addShake(1.5f, 120.0f);
	g_CameraFX.addFovKick(2.0f);

	SoundManager::Get()->sound()->playRandomized2D("content/sound/weapon/shotgun/fire", 0.04f);

	enterState(State::Firing);
}

void Weapon_Shotgun::reload()
{
	if (m_state != State::Idle)
		return;

	if (m_shells >= m_magSize)
		return; // tube is full — don't burn a clip per shell for nothing

	// Nothing in the pool to load with. Cued rather than failing silently: silence
	// reads as a dropped input, and the player presses reload again instead of
	// going to look for ammunition.
	if (reserveRemaining() <= 0)
	{
		playEmptyReserveSound();
		return;
	}

	// Only the first shell is started here; the Reloading state re-enters itself
	// once per shell until the tube is full, so the reload costs exactly as long
	// as the number of shells actually spent.
	enterState(State::Reloading);
}


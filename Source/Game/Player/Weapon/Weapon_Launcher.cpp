#include "Weapon_Launcher.h"

#include "Engine/Engine.h"

#include "../CameraFX.h"

#include "Engine/Renderer/Particle/ParticleManager.h"
#include "Engine/Resource/FilePaths.h"

#include <algorithm>
#include <cmath>

// Windows.h defines these as macros and this project does not use NOMINMAX, so
// std::max below would not survive an include-order change without them.
#undef MB_RIGHT
#undef max
#undef min

using namespace irr;
using namespace SPK;
using namespace SPK::IRR;

void Weapon_Launcher::precache()
{
	ParticleManager::Get()->precache("explosion", _asset_psys("explosion"));

	// equip/unequip are shared across weapons and preloaded by WeaponController.
	// The launcher's own fire and bounce cues already exist from the old weapon;
	// the break-open borrows the generic latch and shell cues until a dedicated
	// set is authored. All of these resolve through playRandomized2D/3D, so
	// dropping numbered variants next to them upgrades the gun with no code change.
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/grenade_launcher/fire.wav",   true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/grenade_launcher/bounce.wav", true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/cock_rifle.wav",   true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/insert_shell.wav", true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/dryfire.wav",      true);
}

void Weapon_Launcher::init()
{
	m_descriptor.name = "Player_Weapon_Launcher";
	m_descriptor.id   = _entity_null_value;

	m_weapon_type     = WEAP_LAUNCHER;

	// grenadelauncher_animated.glb carries the same arms rig as the rest of the
	// glTF pack — identical joint names, identical 'arms' root at
	// (0, 2.945, -17.671) — so the other two-handed guns' viewmodel transform is
	// the right starting point. Short and stubby compared to the rifles, so it
	// sits further forward. Tune with the viewmodel debug UI (F2), not here.
	m_viewPositionOffset = irr::core::vector3df(0.1150f, -0.1750f, 0.1600f);
	m_viewRotationOffset = irr::core::vector3df(0.0f, 180.0f, 0.0f);
	m_viewScaleOffset    = irr::core::vector3df(0.01f, 0.01f, 0.01f);

	m_mesh.mesh = _asset_glb("player/weapon/grenadelauncher_animated");

	m_mesh.trimesh = RenderManager::Get()->loadMesh(m_mesh.mesh);

	// Swap in the stand-in BEFORE the node is created — creating a node in the
	// failure branch and again below orphans the first one.
	const bool usingStandIn = (m_mesh.trimesh == nullptr);
	if (usingStandIn)
	{
		spdlog::warn("Weapon_Launcher::init(): failed to load mesh \"{}\", stand-in mesh loaded", m_mesh.mesh);
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

	// Clip table recovered from the .glb itself — the file ships ONE "allanims"
	// take (0-6.0s = frames 0-180 at 30 fps) with every clip concatenated and a
	// 2-frame hold at the shared rest pose between them. The rest pose recurs at
	// frames 0, 12/13, 108/109, 135/136, 161/162 and 180; each range was then
	// identified from the gross motion of the root and of the named parts:
	//   0-12     trigger f0-12, root recoils Z -5 and recovers, and
	//            nothing else moves at all                          -> fire
	//   13-108   'lock' turns 60 deg f21-24, 'front' breaks to 45
	//            deg f33-36, the extractor lifts 'shell' proud by
	//            f39, it is flicked clear f52-60, the SAME mesh
	//            comes back down as the fresh round and seats at
	//            f78, barrel shuts f96-98, latch f97-99            -> reload
	//   109-120  gun swings away, Z -17, out of frame              -> unequip
	//   120-135  the same arc returning to rest                    -> equip
	//   136-161  a 2.8-unit dip and return, no rotation at all     -> a gentle
	//            idle sway, unused: idle is pinned to 136 and the
	//            hold-steady motion comes from enableIdleBreathing()
	//   162-180  gun snaps to a rolled pose (-89) in two frames
	//            and takes sixteen to recover                   -> melee bash
	//
	// That the fire clip touches NOTHING but the trigger and the recoil is what
	// makes this a single-shot break action: the spent case is still in the
	// breech when the clip ends, and only the reload gets it out. Firing
	// therefore always chains into "reload" — see enterState().
	//
	// 109-135 is ONE authored take holding both transitions, split at its apex
	// (f120) so unequip plays the first half and equip the second.
	//
	// 162-180 was bound as the equip at first and is NOT a draw: it reaches its
	// pose in two frames and recovers over sixteen, rolled -89 degrees. Every
	// bash in this pack shares that signature.
	//
	// Looping clips MUST be flagged loop=true — a non-looping clip re-armed from
	// the end callback holds its last frame for one tick every cycle, which is a
	// visible hitch.
	m_mesh.animationList.emplace_back(sAnimationData("fire",    0,   12,  false));
	m_mesh.animationList.emplace_back(sAnimationData("reload",  13,  108, false));
	m_mesh.animationList.emplace_back(sAnimationData("unequip", 109, 120, false));
	m_mesh.animationList.emplace_back(sAnimationData("equip",   120, 135, false));
	m_mesh.animationList.emplace_back(sAnimationData("idle",    136, 136, true));
	// Authored but not bound: the mirror draw and the sway loop described above
	m_mesh.animationList.emplace_back(sAnimationData("melee",     162, 180, false));
	m_mesh.animationList.emplace_back(sAnimationData("idle_sway", 136, 161, true));

	// Both glTF backends normalise keyframe times to 30 fps Irrlicht frames, so
	// the viewmodel must play at 30 to run at its authored speed.
	m_mesh.fps = 30;
	m_mesh.node->setAnimationSpeed(static_cast<irr::f32>(m_mesh.fps));

	m_mesh.node->setJointMode(irr::scene::EJUOR_READ);

	m_mesh.animation_call_back = std::make_shared<AnimationCallback>();
	m_mesh.node->setAnimationEndCallback(m_mesh.animation_call_back.get());

	playAnimation("idle"); // safe default until equip() runs

	// The idle clip is pinned to a single frame, so the hold-steady motion comes
	// from updateWeaponSway() instead. Middling: short and heavy, held across the
	// body rather than out at arm's length.
	enableIdleBreathing(1.05f);

	m_mesh.node->setScale(m_viewScaleOffset);

	// Apply the standard PBR shader to every buffer as the baseline
	auto perpixelMat = ShaderMaterialManager::get("phong_perpixel");
	if (perpixelMat != irr::video::EMT_SOLID)
		m_mesh.node->setMaterialType(perpixelMat);

	for (auto i = 0; i < m_mesh.node->getMaterialCount(); i++)
	{
		m_mesh.node->getMaterial(i).Shininess = 0.f;
		m_mesh.node->getMaterial(i).SpecularColor.setAlpha(0);
	}

	// Must come AFTER the material assignment above — it caches each part's real
	// material type so setMeshPartVisible() has something to restore.
	resolveMeshPart("shell", m_shell);
	resolveMeshPart("projectile", m_warhead);

	// Reference point for reload stabilisation: the bore line roughly mid-barrel.
	// 'base' spans Z -11.68 to +22.68 and the bore sits near the barrel's own
	// centre height, so this splits the rotation error rather than pinning one
	// end and swinging the other. Z is negated because GltfImport's handedness
	// conversion negates it.
	enableClipStabilization("base", irr::core::vector3df(0.0f, 3.86f, -8.0f));
	setStabilizationTuneAmount(0.5f);

	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");

	if (!player.isValid())
	{
		spdlog::error("In function Weapon_Launcher::init() -> getEntityByName(\"player\") : Entity 'player' does not exist");

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
		spdlog::error("In function Weapon_Launcher::init() -> player.getComponent<CameraComponent>() : Entity 'player' does not have specified component");
	}

	RenderManager::Get()->registerViewmodelNode(m_mesh.node);
	m_mesh.node->setVisible(false);

	m_loaded = true;

	m_crosshair = RenderManager::Get()->driver()->getTexture("content/texture/ui/crosshair/crosshair044.png");

	// Character sheet: fat slow muzzle bloom, no tracer (the grenade IS the
	// tracer), no per-shot casing — the spent case is thrown by the reload, from
	// the animated shell's own bone, so shellEjectJoint/shellEjectOffset stay
	// unset and ejectShell() is never called.
	//
	// The flash hangs off 'front' rather than 'base', and that choice matters
	// here more than on the other guns: 'front' is the BARREL, and it tips 45
	// degrees away from the receiver during the reload. Anything parented to
	// 'base' would leave the flash hanging in mid-air where the muzzle used to
	// be. The offset is the bore centre at the muzzle face — the 279 verts within
	// 1 unit of the barrel's far end centre on Y 3.86 — and GltfImport's
	// handedness conversion negates Z.
	WeaponEffectsDesc fx;
	fx.muzzleJointName   = "front";
	fx.muzzleJointOffset = irr::core::vector3df(0.0f, 3.86f, -16.04f);
	fx.flashColor        = irr::video::SColor(255, 255, 200, 120);
	fx.flashSize         = 1.0f;
	fx.flashDuration     = 80.0f;
	fx.lightColor        = irr::video::SColorf(1.0f, 0.7f, 0.3f);
	fx.lightRadius       = 5.0f;
	fx.tracerPoolSize    = 0;
	// shellsmall.obj — the plain brass case. Two meshes were wrong before it:
	// shelllarge necks down to 69% of its diameter at the mouth, which reads as a
	// rifle cartridge, and slug.obj is straight but UV-mapped onto the
	// shotgun-shell part of the atlas, so it came out plastic-hulled.
	// shellsmall is straight-walled end to end — full diameter from base to mouth,
	// the only narrowing being the rim at the head — and textured as brass.
	//
	// Its natural size is irrelevant: matchPartScale() derives the scale from
	// whatever mesh is loaded, rank-matched against the launcher's own 'shell'
	// part, so the case renders at identical world dimensions whichever is used.
	// That is what makes swapping these freely safe.
	fx.shellMesh         = "content/mesh/prop/shells/shellsmall.obj";
	fx.shellSpeed        = 4.0f;
	fx.shellPoolSize     = 8;
	fx.shellBounceSoundBase = "content/sound/prop/shell";
	fx.impactParticle    = nullptr; // detonations do their own theatre
	m_effects.init(m_mesh.node, fx);
}

void Weapon_Launcher::destroy()
{
	m_effects.destroy();

	RenderManager::Get()->unregisterViewmodelNode(m_mesh.node);
	m_mesh.node->remove();

	WorldManager::Get()->freeEntityID(m_descriptor.id);
}

// Single place that starts a clip and moves the machine, so the "what plays
// next" rules live in one readable block instead of being scattered across the
// call sites that trigger them.
void Weapon_Launcher::enterState(State next)
{
	m_state = next;
	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag

	// Only the break-open runs quicker than authored, and only it is stabilised —
	// it tips the whole gun 39 degrees and drops it 11 units, which reads as the
	// weapon falling out of frame if nothing catches it. Set here rather than at
	// the call sites so no path can leave either applied to a clip that does not
	// want it.
	setClipSpeed(next == State::Reloading ? m_reloadSpeed : 1.0f);
	setStabilizationAmount(next == State::Reloading ? stabilizationTuneAmount() : 0.0f);

	switch (next)
	{
	case State::Firing:
		playAnimation("fire");
		break;

	case State::Reloading:
		playAnimation("reload");
		m_shellRestValid  = false;
		m_caseThrown      = false;
		m_latchOpenPlayed = false;
		m_breakOpenPlayed = false;
		m_seatPlayed      = false;
		m_latchShutPlayed = false;
		break;

	case State::Equipping:
		playAnimation("equip");
		break;

	case State::Unequipping:
		playAnimation("unequip");
		break;

	case State::Idle:
	default:
		playAnimation("idle");
		setMeshPartVisible(m_shell, true);
		setMeshPartVisible(m_warhead, m_loaded);
		break;
	}
}

// --- The round in the breech -------------------------------------------------

// The single 'shell' mesh is both the spent case and the fresh round, so it has
// to be hidden across the flick that throws it clear or it visibly teleports out
// of mid-air and back into the barrel. 'projectile' rides it as the warhead and
// is shown only on a LIVE round — the case being discarded has already sent its
// grenade downrange, and a spent case with the warhead still in it is the one
// thing that would give the whole trick away.
//
// Driven off the part's own displacement rather than off frame numbers. The LMG
// shipped with frame-derived triggers that did not fire where the .glb analysis
// said they would; the joint always knows where the shell actually is, needs no
// constant kept in step with the asset, and reads the same at any clip speed.
void Weapon_Launcher::updateShell()
{
	if (!m_shell.bone || !m_mesh.node)
		return;

	// The viewmodel is hidden during drawAll(), so OnAnimate() skipped it and the
	// joint transforms are stale — force them, the same way fire() does before
	// reading the muzzle bone.
	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();

	const irr::core::vector3df pos = partPosition(m_shell);

	// Seated position, sampled on the first frame of the reload where the case is
	// provably still in the breech. Nothing before this moves it.
	if (!m_shellRestValid)
	{
		m_shellRest      = pos;
		m_shellRestValid = true;
		return;
	}

	// NOTE the threshold: the extractor lifts the case 5.7 units proud of the
	// open breech and HOLDS it there for a dozen frames. That pose is correct and
	// must stay on screen — only the flick past it, out to 58, means the case has
	// actually been discarded. Testing against zero would delete the case the
	// instant the extractor touched it.
	const bool thrown = (pos - m_shellRest).getLength() > m_shellThrownEpsilon;

	if (thrown)
	{
		// Hand the case off to a physics casing on the exact frame the animated
		// mesh disappears — same place, same orientation — so there is no seam.
		if (!m_caseThrown)
		{
			m_caseThrown = true;
			ejectSpentCase();
		}

		setMeshPartVisible(m_shell, false);
		setMeshPartVisible(m_warhead, false);
	}
	else if (m_caseThrown)
	{
		// Back within reach of the breech having already thrown the case: this is
		// the FRESH round coming down, so the warhead comes back with it and the
		// gun counts as loaded from here.
		//
		// m_loaded is its own credit guard — the animEnded path below runs the
		// same draw, and testing it here is what keeps a reload that reaches both
		// from taking two grenades out of the pool for one round in the breech.
		if (!m_loaded)
			m_loaded = drawFromReserve(1) > 0;

		setMeshPartVisible(m_shell, true);

		// The warhead is only shown if a grenade was actually there to take. A
		// visible warhead reads as 'live round chambered' and the player acts on
		// it, so an empty pool has to come up as a visibly empty breech.
		setMeshPartVisible(m_warhead, m_loaded);
	}
	else
	{
		// Spent case still sitting in the breech, or riding proud on the
		// extractor. Visible, but with no warhead — that one is downrange.
		setMeshPartVisible(m_shell, true);
		setMeshPartVisible(m_warhead, false);
	}
}

void Weapon_Launcher::ejectSpentCase()
{
	if (!m_shell.bone || !m_mesh.node)
		return;

	irr::core::matrix4 world;
	if (!meshPartWorldTransform(m_shell, world))
		return;

	// Up and away from the breech, from the LAUNCHER's basis rather than the
	// camera's, so the case leaves correctly whichever way the player is facing.
	// A break action throws its case up and back over the shoulder rather than
	// out to the side, which is why this is weighted to 'up' where the rifles'
	// are weighted to 'right'.
	irr::core::vector3df axisRight(-1.0f, 0.0f, 0.0f);
	irr::core::vector3df axisUp(0.0f, 1.0f, 0.0f);
	world.rotateVect(axisRight);
	world.rotateVect(axisUp);
	axisRight.normalize();
	axisUp.normalize();

	// Speed scaled off the joint's world scale so it survives a viewmodel-scale
	// change, the same reasoning as the revolver's scatter and the rifle's throw
	const irr::core::vector3df jointScale = world.getScale();
	const float unit  = std::max(jointScale.X, std::max(jointScale.Y, jointScale.Z));
	const float speed = 240.0f * unit;

	const irr::core::vector3df velocity =
		axisUp    * speed * Engine::Get()->rng()->getFloat(0.9f, 1.3f) +
		axisRight * speed * Engine::Get()->rng()->getFloat(0.2f, 0.5f);

	// Turn the casing end for end. Taken straight from the breech it flies
	// mouth-first back at the camera. Composed as a LOCAL flip — the right
	// operand applies first under Irrlicht's operator* — not by adding 180 to the
	// Euler Y, which would yaw it in the parent frame and fall apart the moment
	// the launcher is pitched or rolled.
	irr::core::matrix4 flip;
	flip.setRotationDegrees(irr::core::vector3df(0.0f, 180.0f, 0.0f));
	const irr::core::matrix4 oriented = world * flip;

	// Size the casing off the model's OWN shell rather than a tuned constant, so
	// it matches the calibre the launcher is drawn holding. Rank-matched per axis
	// because the stand-in rarely shares the part's aspect ratio, and matching
	// only the long axis leaves it visibly too fat.
	m_effects.spawnShellAt(
		world.getTranslation(),
		oriented.getRotationDegrees(),
		velocity,
		matchPartScale(m_shell, m_effects.shellMeshExtent()));
}

// --- Frame loop --------------------------------------------------------------

void Weapon_Launcher::update()
{
	if (!m_mesh.node || !m_mesh.node->isVisible())
		return;

	const bool animEnded = m_mesh.animation_call_back->hasAnimationEnded();
	const irr::f32 frame = m_mesh.node->getFrameNr();

	// Read up here so the busy states can see it too, but act on it only inside
	// them: changing state before the switch would let this frame's already
	// latched animEnded fall through and end the clip we just started.
	const bool lmb = InputManager::Get()->isMouseButtonPressed(MB_LEFT);
	const bool rmb = InputManager::Get()->isMouseButtonPressed(MB_RIGHT);

	if (!lmb && !rmb)
		m_firedThisPress = false;

	switch (m_state)
	{
	// Holstering: stay visible until the clip finishes so the launcher is seen
	// being put away. isUnequipping() going false releases WeaponController's
	// pending switch, so the next weapon is only drawn once this one is down.
	case State::Unequipping:
		if (animEnded)
			unequip();
		return;

	case State::Equipping:
		if (animEnded)
			enterState(State::Idle);
		RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
		return;

	// Every shot breaks the gun open, without exception — that is the whole
	// character of a single-shot launcher, and it is also what gets the spent
	// case out and a fresh round in.
	case State::Firing:
		if (animEnded)
			enterState(State::Reloading);
		break;

	case State::Reloading:
		updateShell();
		updateReloadSounds(frame);

		if (animEnded)
		{
			// The clip runs well past the seat, so reaching the end means the
			// round has certainly gone in — but credit it here too in case a
			// frame-rate hitch stepped clean over the displacement window.
			if (!m_loaded)
				m_loaded = drawFromReserve(1) > 0;

			enterState(State::Idle);
		}
		break;

	case State::Idle:
	default:
		// Record where the stabilisation reference sits at rest. Done here rather
		// than in init() because the joints are stale while the node is hidden,
		// and once because the rest pose never changes.
		if (!stabilizationRestValid())
		{
			m_mesh.node->updateAbsolutePosition();
			m_mesh.node->animateJoints();
			captureStabilizationRest();
		}

		if ((lmb || rmb) && !m_firedThisPress)
		{
			m_firedThisPress = true;

			if (m_loaded)
			{
				// Right mouse lobs a bouncing grenade instead of an impact one.
				// Latched at the moment of the press so a player who rolls off
				// one button onto the other mid-shot still gets what they asked for.
				m_bounceThisPress = rmb && !lmb;
				fire();
			}
			else
			{
				SoundManager::Get()->sound()->playRandomized2D(
					"content/sound/weapon/dryfire", 0.05f, 1, -1.0f, "launcher_dryfire");
			}
		}
		break;
	}

	RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
}

void Weapon_Launcher::persist()
{
	const float dt = Engine::Get()->getDeltaTime();

	// Grenades keep flying, bouncing and detonating while the launcher is
	// holstered — persist() is called for every weapon every frame, which is the
	// whole reason in-flight ordnance lives here rather than in update().
	updateProjectiles(dt);

	m_effects.update(dt);
}

void Weapon_Launcher::equip()
{
	m_mesh.node->setVisible(true);

	// Consume any stale animation-end flag from before the weapon was hidden
	m_mesh.animation_call_back->hasAnimationEnded();

	m_firedThisPress = true; // don't fire on a button already held through the switch
	m_shellRestValid = false;
	resetViewKick();

	setMeshPartVisible(m_shell, true);
	setMeshPartVisible(m_warhead, m_loaded);

	playEquipSound();

	// If the clip is missing, playAnimation leaves the current loop alone and
	// returns false — drop straight to idle rather than freezing on a stale pose.
	enterState(State::Equipping);
	if (!m_mesh.findAnimation("equip"))
		enterState(State::Idle);
}

void Weapon_Launcher::unequip()
{
	m_state = State::Idle;

	// Recover from a break-open cut short by a weapon switch — otherwise the clip
	// speed and the stabilisation offset stay applied for the rest of the
	// weapon's life, and the shell can be left hidden forever.
	setClipSpeed(1.0f);
	setStabilizationAmount(0.0f);

	setMeshPartVisible(m_shell, true);
	setMeshPartVisible(m_warhead, m_loaded);

	m_mesh.node->setVisible(false);
}

void Weapon_Launcher::startUnequip()
{
	// Already hidden, or mid-holster: nothing to play, don't restart the clip
	if (!m_mesh.node || !m_mesh.node->isVisible() || m_state == State::Unequipping)
		return;

	m_firedThisPress = true; // block fire input during unequip

	setClipSpeed(1.0f);
	setStabilizationAmount(0.0f);

	setMeshPartVisible(m_shell, true);
	setMeshPartVisible(m_warhead, m_loaded);

	playUnequipSound();

	// Node stays visible until update() sees the clip end. If the clip were ever
	// missing, playAnimation() returns false and we hide instantly instead.
	if (m_mesh.findAnimation("unequip"))
		enterState(State::Unequipping);
	else
		unequip();
}

void Weapon_Launcher::idle()
{

}

void Weapon_Launcher::move()
{

}

void Weapon_Launcher::fire()
{
	if (!m_mesh.node)
		return;

	m_loaded = false;

	// The warhead has left the breech; the case it came in has not. Hiding it
	// here rather than waiting for the reload is what makes the spent case read
	// as spent for the whole second it sits there before the gun is opened.
	setMeshPartVisible(m_warhead, false);

	// The fire clip only pulls the trigger and rocks the gun; the case is still
	// in the breech when it ends, which is why this ALWAYS chains into the reload.
	enterState(State::Firing);

	spawnProjectile(m_bounceThisPress);

	m_effects.muzzleFlash();

	SoundManager::Get()->sound()->playRandomized2D(
		"content/sound/weapon/grenade_launcher/fire", 0.05f, 2, -1.0f, "launcher_fire");

	// Thumpy single-shot kick — lighter than the rocket, heavier than a rifle
	g_CameraFX.addRecoil(-2.0f, Engine::Get()->rng()->getFloat(-0.25f, 0.25f));

	addViewKick(
		irr::core::vector3df(0.0f, 0.02f, -0.09f),
		irr::core::vector3df(4.0f,
			Engine::Get()->rng()->getFloat(-0.6f, 0.6f),
			Engine::Get()->rng()->getFloat(-1.0f, 1.0f)));
}

void Weapon_Launcher::reload()
{
	// Firing chains into the break-open by itself, so the reload key only matters
	// for the case where the player fired, switched away mid-cycle and came back
	// with the spent case still in the gun.
	if (m_state != State::Idle || m_loaded)
		return;

	// Nothing in the pool to load with. Cued rather than failing silently: silence
	// reads as a dropped input, and the player presses reload again instead of
	// going to look for ammunition.
	if (reserveRemaining() <= 0)
	{
		playEmptyReserveSound();
		return;
	}

	enterState(State::Reloading);
}

// --- Frame-triggered audio ---------------------------------------------------

// Each cue fires once, early by its own measured lead, so the transient lands on
// the visual event instead of trailing it.
void Weapon_Launcher::updateReloadSounds(float frame)
{
	const int f = static_cast<int>(frame);
	const int cockLead = soundLeadFrames(m_cockLeadSec);

	// 'lock' turns its full 60 degrees over f21-24: the latch coming free
	if (!m_latchOpenPlayed && f >= m_latchOpenFrame - cockLead)
	{
		m_latchOpenPlayed = true;

		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/cock_rifle", 0.09f, 2, -1.0f, "launcher_action");
	}

	// 'front' reaches 45 degrees at f36 — the barrel is fully broken open
	if (!m_breakOpenPlayed && f >= m_breakOpenFrame - cockLead)
	{
		m_breakOpenPlayed = true;

		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/cock_rifle", 0.09f, 2, -1.0f, "launcher_action");
	}

	// Fresh round home in the breech at f78
	if (!m_seatPlayed && f >= m_seatFrame - soundLeadFrames(m_insertShellLeadSec))
	{
		m_seatPlayed = true;

		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/insert_shell", 0.06f, 2, -1.0f, "launcher_shell");
	}

	// Barrel swings shut f96-98 and the latch turns back home f97-99
	if (!m_latchShutPlayed && f >= m_latchShutFrame - cockLead)
	{
		m_latchShutPlayed = true;

		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/cock_rifle", 0.09f, 2, -1.0f, "launcher_action");
	}
}

// =============================================================================
// Ballistics, detonation and splash damage.
//
// Lifted from Weapon_GrenadeLauncher, which is left in the tree untouched. The
// only changes are the ones the new model forces: the launch point comes from
// m_effects.muzzleWorldPosition() instead of a FIRESPOT bone this .glb does not
// have, the aim point goes through the base class's getCrosshairAimPoint(), and
// the per-shot feedback moved out to fire() so this function only makes a
// grenade. The arc solve, the bounce handling and the splash falloff are as they
// were.
// =============================================================================

void Weapon_Launcher::spawnProjectile(bool bounce)
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>())
		return;

	auto& camera = player.getComponent<CameraComponent>();

	if (!m_mesh.node)
		return;

	camera.camera->updateAbsolutePosition();
	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();

	// Same point the flash is drawn at, by construction — one definition of
	// "where the muzzle is" rather than two guesses that can drift apart.
	irr::core::vector3df spawnPos = m_effects.muzzleWorldPosition();

	// Where the player is actually pointing. The base class already owns the
	// camera-centre raycast, including the fall-back when nothing is hit.
	const irr::core::vector3df aimTarget = getCrosshairAimPoint(m_maxAimRange);

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
	auto& descriptor          = projectileEntity.getComponent<DescriptorComponent>();
	descriptor.id             = WorldManager::Get()->getNewID();
	descriptor.name           = "grenade_projectile_" + std::to_string(descriptor.id);
	descriptor.type           = ET_DYNAMIC;
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
	mesh.mesh             = "content/mesh/prop/missile.obj";  // placeholder until a grenade model exists
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
}

void Weapon_Launcher::updateProjectiles(float dt)
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

void Weapon_Launcher::detonateAt(const irr::core::vector3df& pos, entityid directHitID,
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

void Weapon_Launcher::applySplashDamage(const irr::core::vector3df& epicentre, entityid directHitEntityID)
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

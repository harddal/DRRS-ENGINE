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
	ParticleManager::Get()->precache("spark",     _asset_psys("spark")); // shrapnel muzzle spit + ricochets

	// equip/unequip are shared across weapons and preloaded by WeaponController.
	// The launcher's own fire and bounce cues already exist from the old weapon;
	// the break-open borrows the generic latch and shell cues until a dedicated
	// set is authored. All of these resolve through playRandomized2D/3D, so
	// dropping numbered variants next to them upgrades the gun with no code change.
	//
	// bounce.wav was authored for the old lobbed grenade, which the alt fire has
	// replaced; it is a hard object striking a hard surface, so it carries over
	// unchanged as the shrapnel ricochet. A dedicated metallic ping dropped in
	// beside it as bounce1/bounce2.wav would be picked up with no code change.
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/grenade_launcher/fire.wav",   true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/grenade_launcher/bounce.wav", true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/shotgun/fire.wav",            true);
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

	m_crosshair = RenderManager::Get()->driver()->getTexture("content/texture/ui/crosshair/crosshair200.png");

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
	destroyShrapnelPool();

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
	setClipSpeed(next == State::Reloading
	             ? statf(WSTAT_RELOAD_SPEED, m_reloadSpeed)
	             : 1.0f);
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
				// Right mouse spends the round as a flak burst instead of a
				// grenade. Latched at the moment of the press so a player who
				// rolls off one button onto the other mid-shot still gets what
				// they asked for.
				//
				// Gated on the skill: until UNLOCK_FLAK is bought this stays
				// false and right mouse lobs an ordinary grenade, so the weapon
				// is a plain grenade launcher rather than one with a dead button.
				m_shrapnelThisPress = rmb && !lmb && hasUnlock(UNLOCK_FLAK);
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

	// Grenades and shrapnel keep flying, bouncing and detonating while the
	// launcher is holstered — persist() is called for every weapon every frame,
	// which is the whole reason in-flight ordnance lives here rather than in
	// update(). It matters more for the flak burst than for the grenade: switching
	// weapons the instant you fire is exactly what a player does with a gun whose
	// reload is 2.2 seconds.
	updateProjectiles(dt);
	updateShrapnel(dt);

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

	m_effects.muzzleFlash();

	if (m_shrapnelThisPress)
	{
		fireShrapnel();

		// The flak burst reads as a scattergun, not a mortar. Same shell casing,
		// same break-open, but the report has to say "canister" — so the launcher's
		// own boom is pitched up out of the way and a shotgun crack is layered
		// under it at half volume. Two cheap voices beat waiting for an asset.
		SoundManager::Get()->sound()->play2D(
			"content/sound/weapon/grenade_launcher/fire.wav", false, 2, 0.8f, "launcher_fire", false, 1.35f);
		SoundManager::Get()->sound()->play2D(
			"content/sound/weapon/shotgun/fire.wav", false, 2, 0.5f, "launcher_fire", false, 0.85f);

		// Sharper and wider than the grenade: a spray weapon should feel like it is
		// trying to get away from you sideways rather than punching straight back.
		g_CameraFX.addRecoil(-2.6f, Engine::Get()->rng()->getFloat(-0.7f, 0.7f));

		addViewKick(
			irr::core::vector3df(0.0f, 0.03f, -0.12f),
			irr::core::vector3df(5.5f,
				Engine::Get()->rng()->getFloat(-1.4f, 1.4f),
				Engine::Get()->rng()->getFloat(-2.0f, 2.0f)));
	}
	else
	{
		spawnProjectile();

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
// grenade. The arc solve and the splash falloff are as they were.
//
// The lobbed BOUNCING grenade the old class supported is gone from this path:
// the right button now spends the round on shrapnel instead, so nothing sets
// WeaponProjectile::isBouncing and the skip-then-detonate branches it gated have
// been removed rather than left as unreachable code. They are in the old class,
// and in this file's history, if the bouncer is ever wanted back.
// =============================================================================

void Weapon_Launcher::spawnProjectile()
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

		// Resolved once here rather than at each of the four uses below: the
		// arc solve, its two components and the fallback lob must all agree, and
		// a stat read that drifted between them would produce a projectile whose
		// stored speed did not match the velocity it was launched with.
		const float launchSpeed = statf(WSTAT_PROJECTILE_SPEED, m_projectileSpeed);

		bool solved = false;
		if (d > 0.01f)
		{
			float v2   = launchSpeed * launchSpeed;
			float disc = v2 * v2 - m_gravity * (m_gravity * d * d + 2.0f * h * v2);
			if (disc >= 0.0f)
			{
				float tanTheta = (v2 - std::sqrt(disc)) / (m_gravity * d);
				float theta    = std::atan(tanTheta);
				irr::core::vector3df horizDir = toTarget;
				horizDir.normalize();
				launchVelocity    = horizDir * (launchSpeed * std::cos(theta));
				launchVelocity.Y += launchSpeed * std::sin(theta);
				solved = true;
			}
		}

		if (!solved)
		{
			// Fallback: static lob angle (target out of range or directly above)
			irr::core::vector3df fallDir = (aimTarget - spawnPos).normalize();
			fallDir.Y += m_lobAngle;
			fallDir.normalize();
			launchVelocity = fallDir * launchSpeed;
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
	proj.speed            = statf(WSTAT_PROJECTILE_SPEED, m_projectileSpeed);
	proj.useTracking      = false;
	proj.targetId         = _entity_null_value;
	proj.distanceTraveled = 0.0f;
	proj.isTrackingActive = false;
	proj.entity           = projectileEntity;
	proj.velocity         = launchVelocity;
	proj.previousPosition = spawnPos;
	proj.trailParticles   = nullptr;
	proj.isBouncing       = false;
	proj.maxLifetime      = 5000.0f;

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

		// Where the next frame's swept raycast originates
		irr::core::vector3df sweepOrigin = currentPos;

		if (hitSomething && hitNode)
		{
			entityid hitEntityID = hitNode->getID();

			if (it->entity.isValid() && it->entity.hasComponent<DescriptorComponent>() &&
				hitEntityID == it->entity.getComponent<DescriptorComponent>().id)
			{
				// Hit own mesh — ignore
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

		if (shouldRemove || it->lifetime >= it->maxLifetime)
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

	// The casing comes apart with it — a sphere of the same glowing shrapnel the
	// alt fire throws, off the same pool. This is what gives the grenade a reason
	// to be aimed at a group rather than at one target: the splash falls off with
	// distance and stops at m_splashRadius, but a fragment keeps going until it
	// hits someone, and it ricochets around corners the blast cannot reach.
	spawnShrapnelBurst(pos, surfaceNormal);

	// Light flash + scorch (oriented to the hit surface) + smoke + proximity feedback
	m_effects.explosionAt(pos,
		irr::video::SColorf(1.0f, 0.75f, 0.35f), 9.0f, 3.5f, 5.0f, 300.0f,
		surfaceNormal);

	if (directHitID != _entity_null_value)
	{
		// Explosive context here too — a grenade planted directly on a bomber
		// should set him off, not merely kill him.
		registerHitFeedback(WorldManager::Get()->gameplaySystem()->damageEntity(
			directHitID,
			static_cast<unsigned int>(stati(WSTAT_DAMAGE, static_cast<int>(m_pointDamage))),
			DAMAGE_TYPE::DEFAULT, DamageContext::fromBlast(pos, pos)));
	}
}

void Weapon_Launcher::applySplashDamage(const irr::core::vector3df& epicentre, entityid directHitEntityID)
{
	// Resolved once, at the top: the radius is used three times below — the
	// early-out, the cull and the falloff divisor — and a falloff computed
	// against a different radius than the cull used would let an entity just
	// inside the ring take a negative share.
	const float splashRadius = statf(WSTAT_SPLASH_RADIUS, m_splashRadius);
	const float splashDamage = statf(WSTAT_SPLASH_DAMAGE, m_splashDamage);
	const float splashForce  = statf(WSTAT_SPLASH_FORCE,  m_splashForce);

	if (splashRadius <= 0.0f || splashDamage <= 0.0f)
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

		if (dist >= splashRadius) continue;

		float falloff = 1.0f - (dist / splashRadius);
		float damage  = splashDamage * falloff;

		if (damage >= 1.0f)
		{
			// The blast context matters beyond gore direction: it sets
			// DamageReceiverComponent::receivedExplosive, which is what lets a
			// carried charge cook off. Without it a suicide bomber caught in
			// this blast just dies instead of going up.
			HIT_RESULT r = WorldManager::Get()->gameplaySystem()->damageEntity(
				desc.id, static_cast<unsigned int>(damage), DAMAGE_TYPE::DEFAULT,
				DamageContext::fromBlast(epicentre, entityPos));

			// Splash-damaging yourself is not a hit confirm
			if (desc.name != "player" && static_cast<int>(r) > static_cast<int>(bestResult))
				bestResult = r;
		}

		if (splashForce > 0.0f && entity.hasComponent<PhysicsComponent>())
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

				float impulse = splashForce * falloff;
				phys.actor->addForce(
					physx::PxVec3(dir.X, dir.Y, dir.Z) * impulse,
					physx::PxForceMode::eIMPULSE);
			}
		}
	}

	registerHitFeedback(bestResult);
}

// =============================================================================
// Alt fire — flak shrapnel.
//
// A cone of incandescent steel that ricochets off the world and cools from
// white-hot to dead grey over about a second and a half. Fourteen fragments per
// round, each carrying its own damage that halves on every bounce, so the shot
// is devastating in a corridor and nearly harmless across a hall — which is the
// whole point of putting it on the same gun as a 200-damage grenade.
//
// Deliberately NOT built on WeaponProjectile: these are pooled scene nodes with
// fake physics, the same shape as the shell casings in WeaponEffects, because
// fourteen ECS entities per shot each with a mesh and a light is an order of
// magnitude more machinery than two seconds of sparks is worth.
// =============================================================================

// The hulls the gib generator produced. They are convex, jagged and normalised
// to roughly two units across, which is exactly the silhouette shrapnel wants —
// no new asset needed, and nothing about them is meat except the texture this
// weapon does not bind.
static const char* const _shard_meshes[] =
{
	"content/mesh/gib/gib_shard.obj",
	"content/mesh/gib/gib_sliver1.obj",
	"content/mesh/gib/gib_sliver2.obj",
};

// The light coming OFF the fragment, which is a different thing from the colour
// of the fragment itself: hot yellow at the muzzle, through orange, to red as it
// dies. It never reaches a cold stop because it does not need one — the halo's
// intensity is faded to nothing separately, so the red end is simply where the
// last visible ember sits.
// The scene target is linear (see the material block in ensureShrapnelPool), so a
// colour picked by eye — which is a colour in sRGB — has to be linearised before it
// is handed to a FIXED-FUNCTION node, because nothing between here and the
// tonemapper will do it. Shaders such as phong_perpixel linearise their own inputs;
// an additive billboard has no shader and would otherwise add an sRGB number to a
// linear buffer, over-contributing everywhere and worst at the dim end — which is
// exactly the part of a cooling ramp that carries the information.
static irr::video::SColor _linearise(irr::video::SColor srgb, float scale)
{
	auto ch = [scale](irr::u32 v) -> irr::u32
	{
		const float lin = powf(v / 255.0f, 2.2f) * scale;
		return static_cast<irr::u32>(irr::core::clamp(lin, 0.0f, 1.0f) * 255.0f);
	};

	return irr::video::SColor(255, ch(srgb.getRed()), ch(srgb.getGreen()), ch(srgb.getBlue()));
}

irr::video::SColor Weapon_Launcher::shardGlowColor(float heat)
{
	struct Stop { float t; float r, g, b; };

	static const Stop stops[] =
	{
		{ 0.00f, 150.0f,  20.0f,  10.0f }, // last ember, on its way out
		{ 0.25f, 235.0f,  45.0f,  15.0f }, // red
		{ 0.60f, 255.0f, 150.0f,  35.0f }, // orange
		{ 1.00f, 255.0f, 245.0f, 130.0f }, // bright hot yellow, out of the barrel
	};

	heat = irr::core::clamp(heat, 0.0f, 1.0f);

	const int count = static_cast<int>(sizeof(stops) / sizeof(stops[0]));

	for (int i = 1; i < count; ++i)
	{
		if (heat > stops[i].t && i != count - 1)
			continue;

		const Stop& a = stops[i - 1];
		const Stop& b = stops[i];

		const float span = b.t - a.t;
		const float k    = span > 0.0001f ? irr::core::clamp((heat - a.t) / span, 0.0f, 1.0f) : 0.0f;

		return irr::video::SColor(255,
			static_cast<irr::u32>(a.r + (b.r - a.r) * k),
			static_cast<irr::u32>(a.g + (b.g - a.g) * k),
			static_cast<irr::u32>(a.b + (b.b - a.b) * k));
	}

	return irr::video::SColor(255, 150, 20, 10);
}

bool Weapon_Launcher::ensureShrapnelPool()
{
	if (m_shrapnelPoolReady)
		return !m_shrapnel.empty();

	m_shrapnelPoolReady = true; // one attempt, whatever happens — do not retry every shot

	auto* rm = RenderManager::Get();
	if (!rm || !rm->sceneManager())
		return false;

	auto* smgr = rm->sceneManager();

	const int shapeCount = static_cast<int>(sizeof(_shard_meshes) / sizeof(_shard_meshes[0]));

	irr::scene::IMesh* sources[sizeof(_shard_meshes) / sizeof(_shard_meshes[0])] = { nullptr };
	int loaded = 0;

	for (int i = 0; i < shapeCount; ++i)
	{
		if (auto* m = smgr->getMesh(_shard_meshes[i]))
		{
			sources[loaded++] = m->getMesh(0);
		}
		else
		{
			spdlog::warn("Weapon_Launcher: shrapnel mesh '{}' missing", _shard_meshes[i]);
		}
	}

	if (loaded == 0)
	{
		spdlog::error("Weapon_Launcher: no shrapnel meshes loaded; alt fire disabled");
		return false;
	}

	irr::video::ITexture* glowTex = rm->driver()
		? rm->driver()->getTexture("content/texture/particle/scorch_03.png")
		: nullptr;

	// A tiling generic metal: the gib hulls' UVs were laid out for the gore atlas,
	// so anything with a meaningful layout would sample arbitrary parts of it,
	// whereas a uniform metal reads correctly at any UV — and at 0.15 units across
	// nobody is reading the texel detail anyway, only the fact that it is dark metal
	// and not flat. This one averages RGB (110, 78, 58).
	//
	// It has been measured. If the shards ever look washed out again the cause is
	// downstream of here — check the colour space the material writes in before
	// touching the texture or the material colours; see the material block below.
	irr::video::ITexture* metalTex = rm->driver()
		? rm->driver()->getTexture("content/texture/terrain/Delven Pack/dlv_metalgen2.png")
		: nullptr;

	if (!metalTex)
		spdlog::warn("Weapon_Launcher: shrapnel metal texture missing; shards will draw untextured");

	m_shrapnel.resize(m_shardPoolSize);

	for (int i = 0; i < m_shardPoolSize; ++i)
	{
		Shard& shard = m_shrapnel[i];

		// Round-robin over the shapes so one burst is visibly made of different
		// fragments rather than fourteen copies of the same chip. The meshes are
		// SHARED across the pool — nothing is per-shard about the geometry any
		// more, now that the heat lives on the halo rather than in vertex colours.
		shard.node = smgr->addMeshSceneNode(sources[i % loaded]);

		if (!shard.node)
			continue;

		// phong_perpixel, LIT — not the unlit EMT_SOLID this used to be.
		//
		// This is the washed-out-cream bug, and every previous pass at it was
		// looking in the wrong place: the material colours were never the problem
		// and neither was the texture (measure it — dlv_metalgen1 averages RGB
		// 110/78/58, a dark warm metal, exactly as intended).
		//
		// The scene renders into an ECF_A16B16G16R16F target in LINEAR space and
		// tonemap.frag encodes it to sRGB on the way out with pow(colour, 1/2.2).
		// Everything that draws into that target therefore owes it linear values,
		// and phong_perpixel pays that with `albedo = pow(texColor.rgb, 2.2)` at
		// the top of its shading.
		//
		// A fixed-function EMT_SOLID node has no such step. It wrote the raw sRGB
		// texel into a buffer that is about to be gamma-encoded a SECOND time:
		// 0.43 -> 0.43^(1/2.2) = 0.68. The dark brown metal left the tonemapper as
		// pale cream (173, 149, 130), and any exposure above 1.0 pushed it the rest
		// of the way to white. Nothing about the material could have fixed that —
		// it is a colour-space error, not a shading one.
		//
		// (Which also settles the old note about EmissiveColor appearing to
		// brighten an unlit node. It cannot, and it did not; what changed the
		// picture was always this double encode.)
		//
		// Lighting is now ON. Unlit was only ever chosen to dodge the black-metal
		// trap below, and being lit is the better look anyway: these are steel
		// chips in a lit world, and the halo still carries all of the heat.
		const auto perpixel = ShaderMaterialManager::get("phong_perpixel");

		if (perpixel != irr::video::EMT_SOLID)
			shard.node->setMaterialType(perpixel);

		shard.node->setMaterialTexture(SLOT_DIFFUSE, metalTex);
		shard.node->setMaterialFlag(irr::video::EMF_LIGHTING, true);
		shard.node->setMaterialFlag(irr::video::EMF_BACK_FACE_CULLING, true);
		shard.node->setMaterialFlag(irr::video::EMF_BILINEAR_FILTER, true);
		shard.node->setMaterialFlag(irr::video::EMF_USE_MIP_MAPS, true);

		for (irr::u32 m = 0; m < shard.node->getMaterialCount(); ++m)
		{
			irr::video::SMaterial& mat = shard.node->getMaterial(m);

			// DIELECTRIC, despite being steel. phong_perpixel reads metallic from
			// SpecularColor's ALPHA and .obj primitives arrive with it at 255, i.e.
			// full metal — and its diffuseFactor is (1 - metallic), so a metallic
			// shard multiplies this dark albedo away to near-black and keeps only a
			// specular tinted by F0 = albedo. That is the same trap the gibs fell
			// into (GoreManager::applyGibMaterial has the long version). Brass
			// casings survive it only because brass really is metal AND bright.
			mat.SpecularColor.setAlpha(0);   // uMetallic 0
			mat.DiffuseColor.setAlpha(255);  // uAlpha — fully opaque

			// uRoughness = 1 - sqrt(Shininess / 128). 26 gives ~0.55: dull enough to
			// stay dark metal, glossy enough to catch a highlight as it tumbles,
			// which is most of what sells a fragment this small as solid.
			mat.Shininess = 26.0f;
		}

		shard.node->setVisible(false);

		// The halo is what actually sells "glowing" — the mesh alone is a bright
		// speck at this size. Additive, and per the additive-vertex-colour rule
		// its tint comes from setColor(), never from the material.
		if (glowTex)
		{
			shard.glow = smgr->addBillboardSceneNode(
				shard.node, irr::core::dimension2df(0.5f, 0.5f));

			if (shard.glow)
			{
				shard.glow->setMaterialTexture(0, glowTex);
				shard.glow->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);
				shard.glow->setMaterialFlag(irr::video::EMF_LIGHTING, false);
				shard.glow->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE, false);
			}
		}
	}

	return true;
}

void Weapon_Launcher::destroyShrapnelPool()
{
	for (auto& shard : m_shrapnel)
	{
		// The billboard is a child, so removing the node takes it with it.
		if (shard.node)
			shard.node->remove();

		shard.node = nullptr;
		shard.glow = nullptr;
	}

	// Nothing to drop: the shard meshes come from the scene manager's own cache
	// and are shared, so the nodes' grabs are the only references this weapon ever
	// held. Removing them above released those.
	m_shrapnel.clear();
	m_shrapnelPoolReady = false;
}

void Weapon_Launcher::fireShrapnel()
{
	if (!ensureShrapnelPool() || !m_mesh.node)
		return;

	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>())
		return;

	auto& camera = player.getComponent<CameraComponent>();
	camera.camera->updateAbsolutePosition();
	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();

	// Same point the flash is drawn at, for the same reason the grenade uses it:
	// one definition of "where the muzzle is" instead of two that drift apart.
	const irr::core::vector3df spawnPos = m_effects.muzzleWorldPosition();

	irr::core::vector3df aim = getCrosshairAimPoint(m_maxAimRange) - spawnPos;
	if (aim.getLengthSQ() < 0.0001f)
		return;
	aim.normalize();

	// Orthonormal basis for the cone. crossProduct with world up degenerates when
	// the player is looking straight up or down, so fall back to an axis that
	// cannot be parallel to the aim in that case.
	irr::core::vector3df up(0.0f, 1.0f, 0.0f);
	if (std::fabs(aim.dotProduct(up)) > 0.99f)
		up.set(1.0f, 0.0f, 0.0f);

	irr::core::vector3df right = aim.crossProduct(up);
	right.normalize();
	irr::core::vector3df coneUp = right.crossProduct(aim);
	coneUp.normalize();

	// tan of the cone half-angle: the radius of the disc the shards are aimed
	// through, one unit downrange.
	// Spread is an INVERTED stat — a smaller cone is a better one — so it is
	// read through statInv() against WSTAT_ACCURACY rather than multiplied.
	const float shardSpread = statInv(WSTAT_ACCURACY, m_shardSpread);
	const float spreadRadius = std::tan(shardSpread * irr::core::DEGTORAD);

	const float shardDamage = statf(WSTAT_ALT_DAMAGE, m_shardDamage);

	// The pool is a fixed array (m_shardPoolSize), so an upgraded burst is
	// clamped to it. acquireShard() already returns null and breaks the loop
	// when the pool is dry; this just keeps the intent visible at the count.
	int burst = stati(WSTAT_PELLETS, m_shardsPerBurst);
	if (burst > m_shardPoolSize)
		burst = m_shardPoolSize;

	auto* rng = Engine::Get()->rng();

	for (int spawned = 0; spawned < burst; ++spawned)
	{
		Shard* slot = acquireShard();
		if (!slot)
			break; // no nodes at all — pool build failed

		// Uniform over the disc, not over the radius — sampling the radius flat
		// piles the fragments into the middle and the burst reads as a slug with
		// fringe rather than as a spread.
		const float theta = rng->getFloat(0.0f, 2.0f * irr::core::PI);
		const float r     = spreadRadius * std::sqrt(rng->getFloat(0.0f, 1.0f));

		irr::core::vector3df dir = aim
			+ right  * (r * std::cos(theta))
			+ coneUp * (r * std::sin(theta));
		dir.normalize();

		const float speed = m_shardSpeed * rng->getFloat(1.0f - m_shardSpeedVar, 1.0f + m_shardSpeedVar);

		// Disarmed: see Shard::armed. These start inside their owner.
		launchShard(*slot, spawnPos, dir, speed, shardDamage, false);
	}

	// Muzzle spit — the canister coming apart at the barrel.
	ParticleManager::Get()->spawn("spark", irr2spk(spawnPos));
}

// The PRIMARY fire's payoff: the grenade's casing coming apart. Same pool, same
// physics and the same cooling as the alt fire's cone — only the distribution
// and the tuning differ, which is the entire reason the launch path was pulled
// out into launchShard().
void Weapon_Launcher::spawnShrapnelBurst(const irr::core::vector3df& origin,
	const irr::core::vector3df& surfaceNormal)
{
	if (!ensureShrapnelPool())
		return;

	auto* rng = Engine::Get()->rng();

	// Lifted off the surface it went off against, so the half of the sphere aimed
	// into the wall is not already embedded in it on frame one. With no normal —
	// an airburst or a timer — the blast centre is already in open space.
	irr::core::vector3df centre = origin;

	if (surfaceNormal.getLengthSQ() > 0.0001f)
	{
		irr::core::vector3df n = surfaceNormal;
		n.normalize();
		centre += n * 0.3f;
	}

	for (int i = 0; i < m_fragCount; ++i)
	{
		Shard* slot = acquireShard();
		if (!slot)
			break;

		// Uniform on the SPHERE. Sampling two angles instead would bunch the
		// fragments at the poles and leave a visible seam around the equator;
		// picking the height uniformly and the ring radius from it is what makes
		// the distribution even.
		const float z     = rng->getFloat(-1.0f, 1.0f);
		const float theta = rng->getFloat(0.0f, 2.0f * irr::core::PI);
		const float r     = std::sqrt(std::max(0.0f, 1.0f - z * z));

		const irr::core::vector3df dir(r * std::cos(theta), z, r * std::sin(theta));

		const float speed = m_fragSpeed * rng->getFloat(1.0f - m_shardSpeedVar, 1.0f + m_shardSpeedVar);

		// ARMED, unlike the muzzle cone: the blast is out in the world rather than
		// inside the player, so a fragment that reaches them is a real one.
		launchShard(*slot, centre, dir, speed, m_fragDamage, true);
	}
}

Weapon_Launcher::Shard* Weapon_Launcher::acquireShard()
{
	// A free slot if there is one, otherwise the oldest shard in the pool.
	// Recycling rather than skipping is what keeps every burst the same size once
	// the floor is covered in debris — the alternative is the weapon quietly
	// getting weaker the longer the fight has been going on.
	Shard* slot = nullptr;

	for (auto& candidate : m_shrapnel)
	{
		if (!candidate.node)
			continue;

		if (!candidate.active)
			return &candidate;

		if (!slot || candidate.age > slot->age)
			slot = &candidate;
	}

	return slot;
}

void Weapon_Launcher::launchShard(Shard& shard, const irr::core::vector3df& origin,
	const irr::core::vector3df& dir, float speed, float damage, bool armed)
{
	if (!shard.node)
		return;

	auto* rng = Engine::Get()->rng();

	const float scale = rng->getFloat(m_shardScaleMin, m_shardScaleMax);

	shard.velocity = dir * speed;
	shard.rotation.set(
		rng->getFloat(0.0f, 360.0f),
		rng->getFloat(0.0f, 360.0f),
		rng->getFloat(0.0f, 360.0f));
	shard.angularVelocity.set(
		rng->getFloat(-900.0f, 900.0f),
		rng->getFloat(-900.0f, 900.0f),
		rng->getFloat(-900.0f, 900.0f));

	shard.age       = 0.0f;
	shard.damage    = damage;
	shard.bounces   = 0;
	shard.active    = true;
	shard.settled   = false;
	shard.armed     = armed;
	shard.scale     = scale;
	shard.fadeStart = m_shardLifetime - m_shardFadeTime;

	// Nudged along its own direction so a fragment cannot start inside the surface
	// it was launched from, which would have it ricochet on its first frame.
	shard.node->setPosition(origin + dir * m_spawnOffset);
	shard.node->setRotation(shard.rotation);
	shard.node->setScale(irr::core::vector3df(scale, scale, scale));
	shard.node->setVisible(true);
}

void Weapon_Launcher::updateShrapnel(float dt)
{
	if (m_shrapnel.empty())
		return;

	const float dt_s = dt * 0.001f;
	const float now  = static_cast<float>(Engine::Get()->getCurrentTime());

	// One hit confirm for the whole burst-frame. Fourteen fragments landing on the
	// same target would otherwise fire fourteen hitmarkers in one tick.
	HIT_RESULT bestResult = HIT_RESULT::NONE;

	for (auto& shard : m_shrapnel)
	{
		if (!shard.active || !shard.node)
			continue;

		shard.age += dt;

		// Fragments shrink away rather than blinking out. Fourteen of them popping
		// on the same frame is far more noticeable than fourteen of them leaving,
		// and an untextured solid has no alpha to fade, so size is the only handle.
		// A fragment that has used up its ricochets starts this immediately — see
		// the bounce block below — which is also what takes it out of collision.
		float fade = 1.0f;

		if (shard.age >= shard.fadeStart)
		{
			fade = 1.0f - (shard.age - shard.fadeStart) / m_shardFadeTime;

			if (fade <= 0.0f)
			{
				shard.active = false;
				shard.node->setVisible(false);
				continue;
			}
		}

		// Settled debris that has finished cooling and is not yet on its way out is
		// completely inert — it lies where it landed for the rest of the minute.
		// With a pool of 112 and a lifetime this long, nearly every shard is in
		// that state nearly all of the time, which is what makes a 60-second
		// lifetime affordable. Note it still has to COOL after it lands: a
		// fragment that comes to rest half a second in is still orange, and
		// freezing its colour there would leave hot metal lying on the floor.
		const bool cold = shard.age >= m_shardCoolTime;

		if (shard.settled && cold && fade >= 1.0f)
			continue;

		// Never landed on anything: fired into the sky, or out through a gap. It is
		// the only kind of shard that keeps costing a raycast, so it goes rather
		// than tumbling for the remaining fifty-odd seconds.
		if (!shard.settled && shard.age >= m_shardMaxFlight && shard.fadeStart > shard.age)
			shard.fadeStart = shard.age;

		if (!shard.settled)
		{
			shard.velocity.Y -= m_shardGravity * dt_s;
		}

		const irr::core::vector3df pos = shard.node->getPosition();
		irr::core::vector3df       newPos = pos + (shard.settled
			? irr::core::vector3df(0.0f, 0.0f, 0.0f)
			: shard.velocity * dt_s);

		// Swept cast along this frame's travel, extended by the shard's own radius
		// at each end so a fast fragment cannot tunnel through a thin wall. A
		// fragment already on its way out is skipped: it is spent, and having it
		// still able to damage things while visibly disappearing is a bad trade.
		const float speed = shard.velocity.getLength();

		if (!shard.settled && speed > 0.001f && fade >= 1.0f)
		{
			const float radius = 0.12f;

			// Extended FORWARD only, never backward. Extending the start backward
			// puts the ray origin inside whatever the shard just bounced off: a
			// head-on ricochet leaves the fragment sitting a hair off the surface,
			// so a start pulled 0.12 back along the new travel direction is behind
			// the wall, re-hits every frame, and burns the whole bounce budget in
			// three frames. The symptom was shrapnel vanishing on any shot that was
			// not fired at a glancing angle — a glancing one slides clear before
			// the next sweep, which is exactly why it looked angle-dependent.
			const irr::core::vector3df travelDir = shard.velocity / speed;
			const irr::core::vector3df rayStart  = pos;
			const irr::core::vector3df rayEnd    = newPos + travelDir * radius;

			RaycastResultData hit = RenderManager::Get()->raycastWorldPosition(rayStart, rayEnd, true);

			if (hit.hit && hit.node)
			{
				// A fragment that lands on something damageable is spent in it —
				// it does not come back out. Anything else is a surface to skip off.
				HIT_RESULT result  = HIT_RESULT::NONE;
				bool       hitSelf = false;
				bool       ignore  = false;

				// Brush chunks and world props carry no ECS id, and an unset node id
				// reads as 0 — which is a VALID entity id. Asking the node for an
				// entity without this guard is how a wall ends up damaging whatever
				// entity 0 happens to be.
				if (!RenderManager::isWorldGeometryNode(hit.node))
				{
					anax::Entity& hitEntity =
						WorldManager::Get()->managerSystem()->getEntityByID(hit.node->getID());

					if (hitEntity.isValid() && hitEntity.hasComponent<DescriptorComponent>())
					{
						auto& hitDesc = hitEntity.getComponent<DescriptorComponent>();

						// ET_PLAYER is in this list deliberately: a fragment that
						// comes back off a wall you were too close to should put it
						// in YOU. That is the price of the launcher's damage, and it
						// is the whole reason firing flak into a doorway you are
						// standing in — or lobbing a grenade at your own feet — is a
						// bad idea.
						//
						// Only ARMED fragments can do it; see Shard::armed for which
						// start that way. (raycastWorldPosition already recasts past
						// the player for rays starting inside the hitbox; this makes
						// the intent explicit and cannot misfire if that behaviour
						// ever changes.)
						const bool player = (hitDesc.type == ET_PLAYER);

						if (player && !shard.armed)
						{
							// PASSES THROUGH rather than bouncing. Letting an unarmed
							// fragment ricochet off its owner would arm it and send it
							// straight back into them, so merely pulling the trigger
							// could cost health — self-damage has to come from the
							// geometry you chose to shoot, never from the shot itself.
							ignore = true;
						}
						else if (player || hitDesc.type == ET_STATIC || hitDesc.type == ET_DYNAMIC)
						{
							hitSelf = player;

							result = WorldManager::Get()->gameplaySystem()->damageEntity(
								hitDesc.id,
								static_cast<unsigned int>(shard.damage),
								DAMAGE_TYPE::DEFAULT,
								DamageContext::fromImpact(hit.point, hit.normal, travelDir));
						}
					}
				}

				// 'ignore' means this was never a contact — the fragment carries on
				// through with its velocity untouched, and the move at the bottom of
				// the loop runs exactly as it would have if the sweep found nothing.
				if (!ignore)
				{
					if (result != HIT_RESULT::NONE)
					{
						// Landed in something. Note the test is on the RESULT, not
						// on whether an entity was there: a crate with no damage
						// receiver is a wall as far as a fragment is concerned, and
						// bouncing off it is both better looking and the honest
						// simulation.
						//
						// Shooting yourself is not a hit confirm — same rule the
						// splash damage follows. A hitmarker for your own blood
						// would read as having landed the shot.
						if (!hitSelf && static_cast<int>(result) > static_cast<int>(bestResult))
							bestResult = result;

						ParticleManager::Get()->spawn("spark", irr2spk(hit.point));

						shard.active = false;
						shard.node->setVisible(false);
						continue;
					}

					// --- Ricochet ------------------------------------------------
					const irr::core::vector3df n = hit.normal;

					shard.velocity = (shard.velocity - n * (2.0f * shard.velocity.dotProduct(n)))
						* m_shardBounceLoss;
					shard.angularVelocity *= 0.6f;
					shard.damage *= m_shardDamageLoss;
					shard.bounces++;

					// Off a surface and heading somewhere it was not aimed: from here
					// it can come home. A muzzle burst arms exactly here.
					shard.armed = true;

					// Clear of the surface by more than the sweep's own forward
					// reach, so the next frame's ray cannot start on the wrong side.
					newPos = hit.point + n * (radius + 0.05f);

					// Throttled as a group: a burst into a corner is fourteen
					// impacts inside one frame, and fourteen voices plus fourteen
					// particle systems on the same tick is a hitch and a clipped mess.
					if (now - m_lastShardSound >= 45.0f)
					{
						m_lastShardSound = now;
						SoundManager::Get()->sound()->playRandomized3D(
							"content/sound/weapon/grenade_launcher/bounce", hit.point,
							0.18f, 3, 0.4f, "launcher_shrapnel");
					}

					if (now - m_lastShardSpark >= 60.0f)
					{
						m_lastShardSpark = now;
						ParticleManager::Get()->spawn("spark", irr2spk(hit.point));
					}

					// Out of ricochets, or it has bled off enough speed that another
					// bounce would be a twitch.
					const bool spent = shard.bounces > m_shardMaxBounces ||
						shard.velocity.getLength() < m_shardSettleSpeed;

					// ...but a fragment can only come to REST on something it could
					// actually rest on. Settling wherever it happened to run out left
					// shards stuck to walls, which is the one thing that reads as a
					// bug rather than as debris. A surface counts as ground only if
					// it is facing meaningfully upward; everything else — walls,
					// ceilings, steep ramps — has to hand the fragment back to
					// gravity.
					const bool ground = n.Y > 0.5f;

					if (spent && ground)
					{
						shard.settled = true;
						shard.velocity.set(0.0f, 0.0f, 0.0f);
						shard.angularVelocity.set(0.0f, 0.0f, 0.0f);

						// Sit it right down on the surface rather than leaving it
						// floating at the clearance the sweep needed.
						newPos = hit.point + n * (shard.scale * 0.8f);
					}
					else if (spent)
					{
						// Spent against a wall or a ceiling. Drop it: straight down,
						// off the face it hit, so it falls clear and settles properly
						// on whatever is below. Overwriting the velocity rather than
						// damping it is deliberate — a fragment with a little
						// tangential energy left skips down the wall in a series of
						// grazing hits, which looks worse than either sticking or
						// falling.
						// Comfortably above m_shardSettleSpeed, or the very next test
						// would call it spent again and it would never get moving.
						shard.velocity.set(0.0f, -m_shardSettleSpeed * 2.0f, 0.0f);
						shard.angularVelocity *= 0.3f;

						// It has to stop counting bounces here, or scraping down a
						// wall exhausts the budget it needs to land on the floor.
						shard.bounces = m_shardMaxBounces;
					}
				}
			}
		}

		shard.node->setPosition(newPos);

		if (!shard.settled)
		{
			shard.rotation += shard.angularVelocity * dt_s;
			shard.node->setRotation(shard.rotation);
		}

		const float drawScale = shard.scale * fade;
		shard.node->setScale(irr::core::vector3df(drawScale, drawScale, drawScale));

		// --- Cooling ---------------------------------------------------------
		// The heat is carried ENTIRELY by the halo; the fragment underneath is just
		// lit metal and is supposed to look like it (dark warm steel, RGB 110/78/58
		// on average). Earlier notes here blamed the metal reading as near-white on
		// an emissive term leaking in — it was not: the shard was drawing its sRGB
		// texel straight into the linear HDR target and being gamma-encoded twice.
		// That is fixed in the material now, so this ramp is finally visible against
		// something dark instead of against an already-white chip.
		const float heat = 1.0f - irr::core::clamp(shard.age / m_shardCoolTime, 0.0f, 1.0f);

		if (shard.glow)
		{
			// Squared, not cubed. Cubing was compensation for a halo that had to get
			// out of the way of a blown-out fragment as fast as possible; with the
			// metal reading dark the ember can be allowed to live across the whole
			// cool time, which is the difference between a cooling shard and a shard
			// that is briefly bright and then simply grey. Still taken down with the
			// shrink, so a shard leaving early does not leave its glow behind.
			const float glowFade = heat * heat * fade;

			if (glowFade > 0.02f)
			{
				// Sized against the SHARD, not in absolute units. The hulls are
				// normalised to ~2 units across, so the fragment draws at about
				// 2 * scale: this stays between one and two times the chunk's own
				// size — a rim when it is cooling, a modest bloom when it is fresh.
				const float size = shard.scale * (2.0f + 2.0f * glowFade);
				shard.glow->setSize(irr::core::dimension2df(size, size));

				// Its OWN ramp, not the metal's — hot yellow through orange to red.
				// Additive: the material colour has no effect here, only setColor(),
				// and scaling the components is what dims it, since an additive
				// billboard has no meaningful alpha to fade instead.
				//
				// Linearised on the way out. The ramp stops are authored by eye and
				// so are sRGB, but this is a fixed-function node writing into the
				// linear HDR target with no shader to convert them (see _linearise).
				// Passing them raw is what made the halo read as a flat wash that was
				// either on or off: the dim end over-contributed most, so the middle
				// of the cool-down had no shape to it. The full 1.0 ceiling is safe
				// now — saturating to white IS what a fragment at full heat should
				// do, and it only lasts as long as the top of the curve.
				const irr::video::SColor ember = shardGlowColor(heat);

				shard.glow->setColor(_linearise(ember, glowFade));

				shard.glow->setVisible(true);
			}
			else
			{
				shard.glow->setVisible(false);
			}
		}
	}

	registerHitFeedback(bestResult);
}

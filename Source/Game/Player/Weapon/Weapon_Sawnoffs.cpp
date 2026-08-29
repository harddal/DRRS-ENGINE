#include "Weapon_Sawnoffs.h"

#include <algorithm>
#include <cmath>
#include <string>

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

// Bore centres at the muzzle face — see the note in the header.
const irr::core::vector3df Weapon_Sawnoffs::m_barrelMuzzle[BARREL_COUNT] =
{
	irr::core::vector3df(-1.69f, 3.57f, -25.57f), // barrel 0 — slug1
	irr::core::vector3df( 1.70f, 3.57f, -25.57f), // barrel 1 — slug2
};

void Weapon_Sawnoffs::precache()
{
	ParticleManager::Get()->precache("spark_smoke", _asset_psys("spark_smoke"));

	// equip/unequip are shared across weapons and preloaded by WeaponController.
	// Borrows the pump shotgun's set — same gauge, same shells — until something
	// dedicated is authored. All of these resolve through playRandomized2D, so
	// dropping numbered variants next to them upgrades the guns with no code change.
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/dryfire.wav",              true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/insert_shell_shotgun.wav", true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/cock_rifle.wav",           true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/shotgun/fire.wav",         true);
}

void Weapon_Sawnoffs::init()
{
	m_descriptor.name = "Player_Weapon_Sawnoffs";
	m_descriptor.id = _entity_null_value;

	// sawnoffs_animated.glb carries the same arms rig as the rest of the glTF
	// pack — identical joint names, identical 'arms' root at (0, 2.945, -17.671).
	// Held out in both hands with the pair 24 model units apart, so this starts
	// from the dual SMGs' framing rather than a long gun's. Tune with the
	// viewmodel debug UI (F2), not by guessing here.
	m_viewPositionOffset = irr::core::vector3df(0.0000f, -0.1600f, 0.1700f);
	m_viewRotationOffset = irr::core::vector3df(0.0f, 180.0f, 0.0f);
	m_viewScaleOffset    = irr::core::vector3df(0.01f, 0.01f, 0.01f);

	m_mesh.mesh = _asset_glb("player/weapon/sawnoffs_animated");

	m_mesh.trimesh = RenderManager::Get()->loadMesh(m_mesh.mesh);

	// Swap in the stand-in BEFORE the node is created — creating a node in the
	// failure branch and again below orphans the first one.
	const bool usingStandIn = (m_mesh.trimesh == nullptr);
	if (usingStandIn)
	{
		spdlog::warn("Weapon_Sawnoffs::init(): failed to load mesh \"{}\", stand-in mesh loaded", m_mesh.mesh);
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
	// take (0-5.0s = frames 0-150 at 30 fps) with every clip concatenated and a
	// 2-frame hold at the shared rest pose between them. BOTH gun roots return to
	// rest on the same frames: 0, 11/12, 23/24, 80/81, 100/101, 127/128 and 150.
	//   0-11     the RIGHT gun kicks 14.3 units and its 'trigger1'
	//            pulls 10.4 deg; the left gun moves 0.2         -> fire_right
	//   12-23    the LEFT gun kicks 15.6 units and 'trigger1_2'
	//            pulls 17.6 deg; the right gun moves 0.4        -> fire_left
	//   24-80    'release' turns 40 deg f30-31, 'front' breaks
	//            to 45 deg f35-38, the extractors nudge all four
	//            shells at f38 and fling them clear by f50, both
	//            guns swing away f51-57 for fresh pairs, the SAME
	//            meshes reappear seated at f58, barrels shut
	//            f66-69 and the latch drops f69-71              -> reload
	//   81-90    both guns swing out 35 units, out of frame     -> unequip
	//   90-100   the same pose returning to rest                -> a mirror draw,
	//            unused; see the note on equip below
	//   101-127  a 1.9-unit sway that starts and ends at rest   -> a real looping
	//            idle, and the second in the pack after the staff
	//   128-150  both guns snap out to a rolled pose and ease
	//            back over 20 frames                            -> equip
	//
	// THAT THERE ARE TWO FIRE CLIPS IS THE WHOLE WEAPON. dual_smgs.glb animates
	// both its guns on the same frames, which forced that weapon to alternate in
	// the effects layer alone; this asset kicks one gun at a time, so the
	// alternation here is real and fire() simply plays the firing gun's clip.
	//
	// 'trigger2' and 'trigger2_2' never move in any clip — only one trigger per
	// gun is animated — so a barrel is chosen in code, not read off the mesh.
	//
	// The equip range starts at 130, NOT at the rest hold at 128: f129 is a pure
	// interpolation between rest and the extreme, so starting there would show
	// the guns snapping out of frame before they come back in.
	//
	// Looping clips MUST be flagged loop=true — a non-looping clip re-armed from
	// the end callback holds its last frame for one tick every cycle, which is a
	// visible hitch.
	m_mesh.animationList.emplace_back(sAnimationData("fire_right", 0,   11,  false));
	m_mesh.animationList.emplace_back(sAnimationData("fire_left",  12,  23,  false));
	m_mesh.animationList.emplace_back(sAnimationData("reload",     24,  80,  false));
	m_mesh.animationList.emplace_back(sAnimationData("unequip",    81,  90,  false));
	m_mesh.animationList.emplace_back(sAnimationData("equip",      130, 150, false));
	m_mesh.animationList.emplace_back(sAnimationData("idle",       101, 127, true));
	// Authored but not bound: the mirror draw described above
	m_mesh.animationList.emplace_back(sAnimationData("equip_alt",  90,  100, false));

	// Both glTF backends normalise keyframe times to 30 fps Irrlicht frames, so
	// the viewmodel must play at 30 to run at its authored speed.
	m_mesh.fps = 30;
	m_mesh.node->setAnimationSpeed(static_cast<irr::f32>(m_mesh.fps));

	m_mesh.node->setJointMode(irr::scene::EJUOR_READ);

	m_mesh.animation_call_back = std::make_shared<AnimationCallback>();
	m_mesh.node->setAnimationEndCallback(m_mesh.animation_call_back.get());

	playAnimation("idle");

	// Light, because unlike most of the pack the idle here is a real 27-frame
	// loop rather than one pinned frame. This only exists to stop that loop
	// reading as a loop — the sway itself is already in the animation.
	enableIdleBreathing(0.7f);

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

	// Must come AFTER the material assignment above — it caches each shell's real
	// material type so setMeshPartVisible() has something to restore.
	resolveGuns();

	// Reference point for reload stabilisation: the right-hand gun's bore line at
	// roughly mid-barrel. Only one gun can be the reference — the counter-offset
	// is a single translation of the whole viewmodel — and the right is the one
	// the eye follows. Z negated for the handedness conversion, as everywhere.
	enableClipStabilization("base", irr::core::vector3df(0.0f, 3.57f, -10.0f));
	setStabilizationTuneAmount(0.45f);

	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");

	if (!player.isValid())
	{
		spdlog::error("In function Weapon_Sawnoffs::init() -> getEntityByName(\"player\") : Entity 'player' does not exist");

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
		spdlog::error("In function Weapon_Sawnoffs::init() -> player.getComponent<CameraComponent>() : Entity 'player' does not have specified component");
	}

	RenderManager::Get()->registerViewmodelNode(m_mesh.node);
	m_mesh.node->setVisible(false);

	m_shells[GUN_RIGHT] = BARREL_COUNT;
	m_shells[GUN_LEFT]  = BARREL_COUNT;

	m_crosshair = RenderManager::Get()->driver()->getTexture("content/texture/ui/crosshair/crosshair001.png");

	// Character sheet, applied identically to both guns. The flash hangs off
	// 'front' — the barrel assembly — because it tips 45 degrees away during the
	// reload; parented to 'base' it would hang where the muzzles used to be. The
	// offset is set per shot to whichever barrel is firing, so what is given here
	// is only the state before the first shot.
	//
	// The biggest flash in the pack and no tracer: a sawn-off is a wall of fire
	// at close range, and a tracer on a pellet spread reads as a single round.
	for (int i = 0; i < GUN_COUNT; ++i)
	{
		WeaponEffectsDesc fx;
		fx.muzzleJointName   = (i == GUN_RIGHT) ? "front" : "front_2";
		fx.muzzleJointOffset = m_barrelMuzzle[0];
		fx.flashColor        = irr::video::SColor(255, 255, 216, 130);
		fx.flashSize         = 1.15f;
		fx.flashSizeVariance = 0.3f;
		fx.flashDuration     = 70.0f;
		fx.lightColor        = irr::video::SColorf(1.0f, 0.8f, 0.35f);
		fx.lightRadius       = 5.5f;
		fx.tracerPoolSize    = 0;
		// Cases are spawned by ejectSpentCase() from each gun's own extractor, not
		// from a port, so shellEjectJoint/shellEjectOffset stay unset and
		// ejectShell() is never called. Same approach as the rest of the pack.
		fx.shellMesh         = "content/mesh/prop/shells/slug.obj";
		fx.shellSpeed        = 6.0f;
		fx.shellPoolSize     = 12; // four cases a reload, with room to overlap
		fx.shellBounceSoundBase = "content/sound/prop/shotgunshell";
		fx.impactParticle    = "spark_smoke";
		fx.impactDecalSize   = 0.13f; // small per-pellet holes, clustered

		m_gun[i].effects.init(m_mesh.node, fx);
	}
}

void Weapon_Sawnoffs::destroy()
{
	for (int i = 0; i < GUN_COUNT; ++i)
		m_gun[i].effects.destroy();

	RenderManager::Get()->unregisterViewmodelNode(m_mesh.node);
	m_mesh.node->remove();

	WorldManager::Get()->freeEntityID(m_descriptor.id);
}

// Resolve the per-gun joints and shells.
//
// THE PREFIXES ARE LOAD-BEARING, for the same reason they were on the dual SMGs:
// resolveMeshPart() matches the first joint whose name STARTS WITH what it is
// given, and the left gun's parts are the right gun's names with "_2" spliced in
// — so a bare "slug1" matches BOTH slug1_sawnoff_0 and slug1_2_sawnoff_0 and
// resolves to whichever comes first in joint order. Reaching down to the
// "_sawnoff" the mesh leaf carries is what makes each one unique.
//
// The extractors are safe as plain names because getJointNode() is an exact
// match, not a prefix one, and "unloader" and "unloader_2" are distinct.
void Weapon_Sawnoffs::resolveGuns()
{
	if (!m_mesh.node)
		return;

	for (int i = 0; i < GUN_COUNT; ++i)
	{
		const std::string suffix = (i == GUN_RIGHT) ? "" : "_2";

		const std::string unloaderName = "unloader" + suffix;
		m_gun[i].unloader = m_mesh.node->getJointNode(unloaderName.c_str());

		if (!m_gun[i].unloader)
			spdlog::warn("Weapon_Sawnoffs: '{}' joint not found — that gun will not throw cases", unloaderName);

		for (int barrel = 0; barrel < BARREL_COUNT; ++barrel)
		{
			// slug1 is barrel 0, slug2 is barrel 1 — matching m_barrelMuzzle,
			// which was derived from the same X split.
			const std::string prefix = "slug" + std::to_string(barrel + 1) + suffix + "_sawnoff";

			if (!resolveMeshPart(prefix.c_str(), m_gun[i].slug[barrel]))
				spdlog::warn("Weapon_Sawnoffs: '{}' not found — that shell will teleport during the reload", prefix);
		}

		m_gun[i].slugRestValid = false;
	}
}

// --- Shells ------------------------------------------------------------------

// All four shells are flung clear during the reload and the SAME meshes come
// back as the fresh pairs, so each has to be hidden across its throw or it
// visibly teleports out of mid-air and back into the breech. The reuse trick the
// whole weapon pack is built on, four at a time.
//
// Driven off measured displacement rather than frame numbers, for the reason
// spelled out in the header: the joint always knows where a shell actually is.
void Weapon_Sawnoffs::updateSlugs()
{
	if (!m_mesh.node)
		return;

	// The viewmodel is hidden during drawAll(), so OnAnimate() skipped it and the
	// joint transforms are stale — force them once for all four shells, the same
	// way fire() does before reading the muzzle bones.
	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();

	for (int i = 0; i < GUN_COUNT; ++i)
	{
		SideFX& gun = m_gun[i];

		for (int barrel = 0; barrel < BARREL_COUNT; ++barrel)
		{
			if (!gun.slug[barrel].bone)
				continue;

			const irr::core::vector3df pos = gun.slug[barrel].bone->getPosition();

			// Seated position, sampled on the first frame of the reload where the
			// shells are provably still in the breech. Nothing before this moves them.
			if (!gun.slugRestValid)
			{
				gun.slugRest[barrel] = pos;
				continue;
			}

			// NOTE the threshold: the extractor NUDGES the shells about 3 units
			// proud of the breech before they are properly flung, and that nudge
			// is correct and must stay on screen. Only the throw past it, out to
			// 60, means a case has actually been discarded.
			const bool thrown = (pos - gun.slugRest[barrel]).getLength() > m_slugThrownEpsilon;

			if (thrown)
			{
				// Hand the case off to a physics casing on the exact frame the
				// animated mesh disappears — same place, same orientation — so
				// there is no seam.
				if (!gun.slugThrown[barrel])
				{
					gun.slugThrown[barrel] = true;
					ejectSpentCase(i, barrel);
				}

				setMeshPartVisible(gun.slug[barrel], false);
			}
			else
			{
				// Seated, or riding the extractor: visible either way. Coming back
				// through the threshold is the FRESH pair arriving.
				setMeshPartVisible(gun.slug[barrel], true);
			}
		}

		// Set after the first pass over both barrels, so both rest positions are
		// captured before either is tested against one.
		gun.slugRestValid = true;
	}
}

void Weapon_Sawnoffs::ejectSpentCase(int gun, int barrel)
{
	if (gun < 0 || gun >= GUN_COUNT || barrel < 0 || barrel >= BARREL_COUNT || !m_mesh.node)
		return;

	SideFX& side = m_gun[gun];

	irr::core::matrix4 world;
	if (!meshPartWorldTransform(side.slug[barrel], world))
		return;

	// Up and back out of the breech, from THIS GUN's basis rather than the
	// camera's, so cases leave correctly whichever way the player is facing. A
	// break action throws up and over rather than out to the side, which is why
	// this is weighted to 'up' where the rifles' are weighted to 'right'.
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
	const float speed = 230.0f * unit;

	// Scattered per case, or four cases leave as one rigid block
	const irr::core::vector3df velocity =
		axisUp    * speed * Engine::Get()->rng()->getFloat(0.85f, 1.25f) +
		axisRight * speed * Engine::Get()->rng()->getFloat(-0.35f, 0.35f);

	// Turn the casing end for end. Taken straight from the breech it flies
	// mouth-first back at the camera. Composed as a LOCAL flip — the right
	// operand applies first under Irrlicht's operator* — not by adding 180 to the
	// Euler Y, which would yaw it in the parent frame and fall apart the moment
	// the gun is pitched or rolled.
	irr::core::matrix4 flip;
	flip.setRotationDegrees(irr::core::vector3df(0.0f, 180.0f, 0.0f));
	const irr::core::matrix4 oriented = world * flip;

	side.effects.spawnShellAt(
		world.getTranslation(),
		oriented.getRotationDegrees(),
		velocity,
		matchPartScale(side.slug[barrel], side.effects.shellMeshExtent()));
}

// --- State -------------------------------------------------------------------

void Weapon_Sawnoffs::enterState(State next)
{
	m_state = next;
	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag

	// Only the break-open runs slower than its clip speed of 1 and is stabilised;
	// firing runs quick. Set here rather than at the call sites so no path can
	// leave either applied to a clip that does not want it.
	setClipSpeed(next == State::Reloading ? m_reloadSpeed
	           : next == State::Firing    ? m_fireSpeed
	           : 1.0f);

	setStabilizationAmount(next == State::Reloading ? stabilizationTuneAmount() : 0.0f);

	switch (next)
	{
	case State::Firing:
		// The clip is chosen by fire(), which knows which gun went off — this
		// state only carries the speed and the stabilisation.
		break;

	case State::Reloading:
		playAnimation("reload");
		m_ammoCredited    = false;
		m_latchPlayed     = false;
		m_breakOpenPlayed = false;
		m_seatPlayed      = false;
		m_closePlayed     = false;

		for (int i = 0; i < GUN_COUNT; ++i)
		{
			m_gun[i].slugRestValid = false;
			m_gun[i].slugThrown[0] = false;
			m_gun[i].slugThrown[1] = false;
		}
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
		for (int i = 0; i < GUN_COUNT; ++i)
			for (int barrel = 0; barrel < BARREL_COUNT; ++barrel)
				setMeshPartVisible(m_gun[i].slug[barrel], true);
		break;
	}
}

// --- Frame loop --------------------------------------------------------------

void Weapon_Sawnoffs::update()
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
	// Holstering: stay visible until the clip finishes so the guns are seen going
	// away. isUnequipping() going false releases WeaponController's pending
	// switch, so the next weapon is only drawn once these are down.
	case State::Unequipping:
		if (animEnded)
			unequip();
		return;

	case State::Equipping:
		if (animEnded)
			enterState(State::Idle);
		RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
		return;

	case State::Firing:
		if (animEnded)
			enterState(State::Idle);
		break;

	case State::Reloading:
		updateSlugs();
		updateReloadSounds(frame);

		if (!m_ammoCredited && frame >= static_cast<irr::f32>(m_seatFrame))
		{
			m_ammoCredited = true;
			m_shells[GUN_RIGHT] = BARREL_COUNT;
			m_shells[GUN_LEFT]  = BARREL_COUNT;
		}

		if (animEnded)
		{
			// The clip runs well past the seat, so reaching the end means the
			// fresh pairs are certainly in — but credit them here too in case a
			// frame-rate hitch stepped clean over the displacement window.
			m_shells[GUN_RIGHT] = BARREL_COUNT;
			m_shells[GUN_LEFT]  = BARREL_COUNT;

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

		if (lmb && !m_firedThisPress && (currentTime - m_lastFireTime) >= m_fireInterval)
		{
			m_firedThisPress = true;

			if (totalShells() > 0)
			{
				m_lastFireTime = currentTime;
				fire();
			}
			else
			{
				SoundManager::Get()->sound()->playRandomized2D(
					"content/sound/weapon/dryfire", 0.05f, 1, -1.0f, "sawnoff_dryfire");
			}
		}
		break;
	}

	RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
}

void Weapon_Sawnoffs::persist()
{
	const float dt = Engine::Get()->getDeltaTime();

	for (int i = 0; i < GUN_COUNT; ++i)
		m_gun[i].effects.update(dt);
}

void Weapon_Sawnoffs::equip()
{
	m_mesh.node->setVisible(true);

	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag

	m_firedThisPress = true; // don't fire on a button already held through the switch
	resetViewKick();

	for (int i = 0; i < GUN_COUNT; ++i)
	{
		m_gun[i].slugRestValid = false;

		for (int barrel = 0; barrel < BARREL_COUNT; ++barrel)
			setMeshPartVisible(m_gun[i].slug[barrel], true);
	}

	playEquipSound();

	enterState(State::Equipping);

	if (!m_mesh.findAnimation("equip"))
		enterState(State::Idle);
}

void Weapon_Sawnoffs::unequip()
{
	m_state = State::Idle;

	// Recover from a break-open cut short by a weapon switch — otherwise the clip
	// speed and the stabilisation offset stay applied for the rest of the
	// weapon's life, and a shell can be left hidden forever.
	setClipSpeed(1.0f);
	setStabilizationAmount(0.0f);

	for (int i = 0; i < GUN_COUNT; ++i)
		for (int barrel = 0; barrel < BARREL_COUNT; ++barrel)
			setMeshPartVisible(m_gun[i].slug[barrel], true);

	m_mesh.node->setVisible(false);
}

void Weapon_Sawnoffs::startUnequip()
{
	// Already hidden, or mid-holster: nothing to play, don't restart the clip
	if (!m_mesh.node || !m_mesh.node->isVisible() || m_state == State::Unequipping)
		return;

	m_firedThisPress = true; // block fire input during unequip

	setClipSpeed(1.0f);
	setStabilizationAmount(0.0f);

	for (int i = 0; i < GUN_COUNT; ++i)
		for (int barrel = 0; barrel < BARREL_COUNT; ++barrel)
			setMeshPartVisible(m_gun[i].slug[barrel], true);

	playUnequipSound();

	if (m_mesh.findAnimation("unequip"))
		enterState(State::Unequipping);
	else
		unequip();
}

void Weapon_Sawnoffs::idle()
{

}

void Weapon_Sawnoffs::move()
{

}

// --- Firing ------------------------------------------------------------------

void Weapon_Sawnoffs::fire()
{
	if (!m_mesh.node)
		return;

	// Whose turn it is, falling through to the other gun when the preferred one
	// is empty — so the last two shells always fire rather than the alternation
	// stalling on a spent barrel.
	int gun = m_nextGun;
	if (m_shells[gun] <= 0)
		gun = (gun == GUN_RIGHT) ? GUN_LEFT : GUN_RIGHT;

	if (m_shells[gun] <= 0)
		return;

	// Barrels empty from the outside in, so the LAST shell in each gun is always
	// barrel 0 — which keeps the pair symmetric as they run down.
	const int barrel = m_shells[gun] - 1;

	m_shells[gun]--;

	// This gun's own clip. The asset kicks one gun at a time, which is what makes
	// the alternation real rather than a trick of the effects.
	enterState(State::Firing);
	playAnimation(gun == GUN_RIGHT ? "fire_right" : "fire_left");

	fireBarrel(gun, barrel);

	// Hand the turn to the other gun. Done LAST so everything above saw one
	// consistent value, and stored rather than derived from a shot counter so the
	// pattern survives a reload or a weapon switch.
	m_nextGun = (gun == GUN_RIGHT) ? GUN_LEFT : GUN_RIGHT;
}

// One barrel's worth: the flash moved to that bore, a cone of pellets from it,
// and a kick that rolls toward the gun that fired.
void Weapon_Sawnoffs::fireBarrel(int gun, int barrel)
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>())
		return;

	SideFX& side = m_gun[gun];

	auto& camera = player.getComponent<CameraComponent>();

	// Force full hierarchy update so bone world positions are current
	camera.camera->updateAbsolutePosition();
	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();

	// Move this gun's single muzzle attachment onto the barrel that is firing.
	// Cheaper than a WeaponEffects per barrel, and exact: the two bores are 3.4
	// model units apart, which is plainly visible at viewmodel range.
	side.effects.debugMuzzleOffset() = m_barrelMuzzle[barrel];
	side.effects.applyMuzzleOffset();

	const irr::core::vector3df muzzlePos = side.effects.muzzleWorldPosition();

	// Camera basis for the spread cone
	irr::core::vector3df target    = camera.camera->getTarget();
	irr::core::vector3df cameraPos = camera.camera->getAbsolutePosition();
	irr::core::vector3df forward   = (target - cameraPos).normalize();

	irr::core::vector3df up(0, 1, 0);
	irr::core::vector3df right = forward.crossProduct(up).normalize();
	irr::core::vector3df down  = right.crossProduct(forward).normalize();

	// Converge on the crosshair aim point. It matters here as much as on the dual
	// SMGs: the bores sit 12 model units either side of centre, so parallel
	// camera-forward rays would put the two guns' patterns in different places.
	const irr::core::vector3df aimDir = getAimDirection(muzzlePos);

	const float spreadRad = m_spreadAngle * 3.14159265f / 180.0f;

	for (int p = 0; p < m_pelletCount; ++p)
	{
		// Rolled inside a cone rather than in a square, so the pattern is round
		const float angle  = Engine::Get()->rng()->getFloat(0.0f, 6.2831853f);
		const float radius = std::sqrt(Engine::Get()->rng()->getFloat(0.0f, 1.0f)) * spreadRad;

		irr::core::vector3df direction =
			(aimDir
			 + right * (std::cos(angle) * radius)
			 + down  * (std::sin(angle) * radius)).normalize();

		const irr::core::vector3df rayEnd = muzzlePos + direction * 1000.0f;

		RaycastResultData raycastResult = RenderManager::Get()->raycastWorldPosition(
			muzzlePos, rayEnd, true);

		if (!raycastResult.hit || !raycastResult.node)
			continue;

		auto& hitEntity = WorldManager::Get()->managerSystem()->getEntityByID(raycastResult.node->getID());

		if (hitEntity.isValid() && hitEntity.hasComponent<DescriptorComponent>())
		{
			auto& hitDescriptor = hitEntity.getComponent<DescriptorComponent>();

			if (hitDescriptor.type == ET_STATIC || hitDescriptor.type == ET_DYNAMIC)
			{
				registerHitFeedback(
					WorldManager::Get()->gameplaySystem()->damageEntity(
						hitDescriptor.id, static_cast<unsigned int>(m_damagePerPellet)));

				side.effects.impact(raycastResult.point, raycastResult.normal);
			}
		}
		else if (RenderManager::isWorldGeometryNode(raycastResult.node))
		{
			side.effects.impact(raycastResult.point, raycastResult.normal);
		}
	}

	side.effects.muzzleFlash();

	// Kick rolls TOWARD the gun that fired — and unlike the dual SMGs, where that
	// would have been a lie, the clip really does kick that gun alone.
	const float sideSign = (gun == GUN_RIGHT) ? 1.0f : -1.0f;

	addViewKick(
		irr::core::vector3df(sideSign * 0.03f, 0.05f, -0.16f),
		irr::core::vector3df(8.0f, sideSign * 2.0f, sideSign * 5.0f));

	g_CameraFX.addRecoil(-3.0f, sideSign * Engine::Get()->rng()->getFloat(0.4f, 1.0f));
	g_CameraFX.addFovKick(1.8f);

	SoundManager::Get()->sound()->playRandomized2D(
		"content/sound/weapon/shotgun/fire", 0.06f, 3, 0.8f, "sawnoff_fire");
}

void Weapon_Sawnoffs::reload()
{
	if (m_state != State::Idle)
		return;

	if (totalShells() >= GUN_COUNT * BARREL_COUNT)
		return; // nothing to top up

	if (!m_mesh.node)
		return;

	enterState(State::Reloading);
}

// Frame-triggered reload audio. Both guns work together, so each cue is one
// sound rather than two — two copies a frame apart read as a flam, not as a pair.
void Weapon_Sawnoffs::updateReloadSounds(float frame)
{
	const int f = static_cast<int>(frame);
	const int cockLead = soundLeadFrames(m_cockLeadSec);

	// 'release' turns its full 40 degrees over f30-31: the latches coming free
	if (!m_latchPlayed && f >= m_latchFrame - cockLead)
	{
		m_latchPlayed = true;

		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/cock_rifle", 0.09f, 2, -1.0f, "sawnoff_action");
	}

	// 'front' reaches 45 degrees at f38 — barrels fully broken open
	if (!m_breakOpenPlayed && f >= m_breakOpenFrame - cockLead)
	{
		m_breakOpenPlayed = true;

		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/cock_rifle", 0.09f, 2, -1.0f, "sawnoff_action");
	}

	// Fresh pairs home at f58 — the same frame the ammo is credited
	if (!m_seatPlayed && f >= m_seatFrame - soundLeadFrames(m_insertShellLeadSec))
	{
		m_seatPlayed = true;

		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/insert_shell_shotgun", 0.06f, 2, -1.0f, "sawnoff_shell");
	}

	// Barrels swing shut f66-69 and the latches drop f69-71
	if (!m_closePlayed && f >= m_closeFrame - cockLead)
	{
		m_closePlayed = true;

		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/cock_rifle", 0.09f, 2, -1.0f, "sawnoff_action");
	}
}

#include "Weapon_DualSMG.h"

#include <algorithm>
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

void Weapon_DualSMG::precache()
{
	ParticleManager::Get()->precache("spark", _asset_psys("spark"));

	// equip/unequip are shared across weapons and preloaded by WeaponController.
	// Borrows the existing library until a dedicated SMG set is authored; all of
	// these resolve through playRandomized2D, so dropping numbered variants next
	// to them upgrades the guns with no code change.
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/dryfire.wav",       true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/remove_mag.wav",    true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/insert_mag.wav",    true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/pistol/fire1.wav",  true);
}

void Weapon_DualSMG::init()
{
	m_descriptor.name = "Player_Weapon_DualSMG";
	m_descriptor.id = _entity_null_value;

	m_weapon_type = WEAP_DUALSMG;

	// dual_smgs.glb carries the same arms rig as the rest of the glTF pack —
	// identical joint names, identical 'arms' root at (0, 2.945, -17.671). Held
	// out at arm's length rather than braced, and the pair spans 26 model units
	// across, so this sits lower and closer than the two-handed guns to keep both
	// weapons on screen. Tune with the viewmodel debug UI (F2), not here.
	m_viewPositionOffset = irr::core::vector3df(0.0000f, -0.1550f, 0.3600f);
	m_viewRotationOffset = irr::core::vector3df(0.0f, 180.0f, 0.0f);
	m_viewScaleOffset    = irr::core::vector3df(0.01f, 0.01f, 0.01f);

	m_mesh.mesh = _asset_glb("player/weapon/dual_smgs");

	m_mesh.trimesh = RenderManager::Get()->loadMesh(m_mesh.mesh);

	// Swap in the stand-in BEFORE the node is created — creating a node in the
	// failure branch and again below orphans the first one.
	const bool usingStandIn = (m_mesh.trimesh == nullptr);
	if (usingStandIn)
	{
		spdlog::warn("Weapon_DualSMG::init(): failed to load mesh \"{}\", stand-in mesh loaded", m_mesh.mesh);
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
	// take (0-4.1s = frames 0-123 at 30 fps) with every clip concatenated and a
	// 2-frame hold at the shared rest pose between them. BOTH gun roots return to
	// rest on the same frames — 0, 7/8, 54/55, 76/77, 102/103 and 123 — which is
	// the first sign that this asset treats the pair as one weapon throughout:
	//   0-7      BOTH triggers pull and BOTH bolts run back Z -9.8 on f1,
	//            both guns recoil Z -2.6 and recover               -> fire
	//   8-54     both guns swing out, both magazines break free at
	//            f18-29, hang clear to f38, and the SAME meshes
	//            come back seated by f41                           -> reload
	//   55-76    both guns swing DOWN 31 units and back, no
	//            magazine motion at all                            -> unequip 55-64,
	//            equip 64-76 — one take holding both transitions,
	//            split at its apex
	//   77-102   under 1.4 units of drift, no rotation to speak of -> a gentle
	//            idle sway, unused: idle is pinned to 77 and the
	//            hold-steady motion comes from enableIdleBreathing()
	//   103-123  both guns snap to a rolled pose (-75) in six
	//            frames and take fourteen to recover               -> melee bash
	//
	// BOTH TRANSITION RANGES WERE ORIGINALLY READ THE WRONG WAY ROUND. 55-76 was
	// taken for a bash and 103-123 was split into a holster and a draw; it is the
	// other way about. The tell, consistent across this whole weapon pack: a bash
	// snaps to its pose within a few frames, recovers over three times as long,
	// and carries a large NEGATIVE roll (-75 here). A holster/draw is symmetric
	// about its apex and takes the guns DOWN — Y -31.3 at f64 against the bash's
	// +4.4.
	//
	// THE FIRE CLIP IS THE CONSTRAINT ON THIS WEAPON. It pulls both triggers and
	// cycles both bolts on the same frames, so there is no per-side clip to play
	// and the alternation has to live in the effects layer — see fire(). If this
	// .glb is ever re-exported with the right and left halves split into their
	// own takes, add them here as "fire_right" and "fire_left" and fire() will
	// start using them on its own; nothing else has to change.
	//
	// Looping clips MUST be flagged loop=true — a non-looping clip re-armed from
	// the end callback holds its last frame for one tick every cycle, which is a
	// visible hitch.
	m_mesh.animationList.emplace_back(sAnimationData("fire",    0,   7,   false));
	m_mesh.animationList.emplace_back(sAnimationData("reload",  8,   54,  false));
	m_mesh.animationList.emplace_back(sAnimationData("unequip", 55,  64,  false));
	m_mesh.animationList.emplace_back(sAnimationData("equip",   64,  76,  false));
	m_mesh.animationList.emplace_back(sAnimationData("idle",    77,  77,  true));
	// Authored but not bound: the bash and the sway loop above
	m_mesh.animationList.emplace_back(sAnimationData("melee",     103, 123, false));
	m_mesh.animationList.emplace_back(sAnimationData("idle_sway", 77, 102, true));

	// Both glTF backends normalise keyframe times to 30 fps Irrlicht frames, so
	// the viewmodel must play at 30 to run at its authored speed.
	m_mesh.fps = 30;
	m_mesh.node->setAnimationSpeed(static_cast<irr::f32>(m_mesh.fps));

	m_mesh.node->setJointMode(irr::scene::EJUOR_READ);

	m_mesh.animation_call_back = std::make_shared<AnimationCallback>();
	m_mesh.node->setAnimationEndCallback(m_mesh.animation_call_back.get());

	playAnimation("idle"); // safe default until equip() runs

	// The idle clip is pinned to a single frame, so the hold-steady motion comes
	// from updateWeaponSway() instead. The widest in the pack: nothing here is
	// braced against anything, and two guns held out at arm's length should
	// wander more than a rifle pulled into the shoulder.
	enableIdleBreathing(1.45f);

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

	// Must come AFTER the material assignment above — it caches each magazine's
	// real material type so setMeshPartVisible() has something to restore.
	resolveGuns();

	// Reference point for reload stabilisation: the right-hand gun's bore line at
	// roughly mid-barrel. Only one gun can be the reference — the counter-offset
	// is a single translation of the whole viewmodel — and the right is the one
	// the player's eye follows. 'base' spans Z -12.4 to +23.9 and 1.08 is the
	// bore height already measured for the muzzle. Z is negated because
	// GltfImport's handedness conversion negates it.
	enableClipStabilization("base", irr::core::vector3df(0.0f, 1.08f, -6.0f));
	setStabilizationTuneAmount(0.4f);

	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");

	if (!player.isValid())
	{
		spdlog::error("In function Weapon_DualSMG::init() -> getEntityByName(\"player\") : Entity 'player' does not exist");

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
		spdlog::error("In function Weapon_DualSMG::init() -> player.getComponent<CameraComponent>() : Entity 'player' does not have specified component");
	}

	RenderManager::Get()->registerViewmodelNode(m_mesh.node);
	m_mesh.node->setVisible(false);

	m_rounds = magSize();

	// Put the left gun in whatever state the flag asks for before it is ever drawn
	applyDualWield();

	m_crosshair = RenderManager::Get()->driver()->getTexture("content/texture/ui/crosshair/crosshair012.png");

	// Character sheet, applied identically to both guns. The muzzle offset is the
	// bore centre at the barrel face — the 82 verts within 1 unit of the far end
	// centre on (-0.45, 1.08) — and GltfImport's handedness conversion negates Z.
	// The two gun meshes are IDENTICAL geometry mirrored by root position, so the
	// same joint-local offset is correct for both; only the joint name differs.
	//
	// Tracers every other round rather than every third: with the flashes
	// alternating, an every-third tracer would land on the same gun twice in a
	// row and read as a stutter. Every second means each gun traces every time
	// its turn comes round.
	for (int i = 0; i < GUN_COUNT; ++i)
	{
		WeaponEffectsDesc fx;
		fx.muzzleJointName   = (i == GUN_RIGHT) ? "base" : "base_2";
		fx.muzzleJointOffset = irr::core::vector3df(-0.45f, 1.08f, -23.94f);
		fx.flashColor        = irr::video::SColor(255, 255, 210, 140);
		fx.flashSize         = 0.45f;   // small and fast — there are a lot of them
		fx.flashSizeVariance = 0.4f;
		fx.flashDuration     = 35.0f;
		fx.lightColor        = irr::video::SColorf(1.0f, 0.75f, 0.3f);
		fx.lightRadius       = 3.0f;
		fx.tracerFrequency   = 2;
		fx.tracerWidth       = 0.08f;
		fx.tracerPoolSize    = 16;
		// Brass is spawned by ejectSpentCase() from the gun's own animated
		// ejector, not from a port, so shellEjectJoint/shellEjectOffset stay unset
		// and ejectShell() is never called. Same approach as the rest of the pack.
		fx.shellMesh         = "content/mesh/prop/shells/shellsmall.obj";
		// Sized off the SHELL LIFETIME, not off the magazine — that is the trap.
		// A 30-round magazine empties in 3 s but a casing lives for 10 s, so brass
		// from the previous magazine is still lying about when the next one
		// starts. Across a dump-reload-dump cycle roughly 80 casings per gun are
		// alive at once; a pool of 32 covered exactly one magazine and then ran
		// dry halfway through the second.
		fx.shellPoolSize     = 96;
		fx.impactParticle    = "spark";
		fx.impactDecalSize   = 0.12f;

		m_gun[i].effects.init(m_mesh.node, fx);
	}
}

void Weapon_DualSMG::destroy()
{
	for (int i = 0; i < GUN_COUNT; ++i)
		m_gun[i].effects.destroy();

	RenderManager::Get()->unregisterViewmodelNode(m_mesh.node);
	m_mesh.node->remove();

	WorldManager::Get()->freeEntityID(m_descriptor.id);
}

// Resolve the per-gun joints.
//
// THE PREFIXES ARE LOAD-BEARING. resolveMeshPart() matches the first joint whose
// name STARTS WITH what it is given, and the left gun's parts are the right
// gun's names with "_2" spliced in — so a bare "mag" matches BOTH mag_smg2_0 and
// mag_2_smg2_0 and resolves to whichever happens to come first in joint order.
// Reaching down to the "_smg2" the mesh leaf carries is what makes each one
// unique: "mag_smg2" cannot match "mag_2_smg2_0", and vice versa.
//
// The ejectors are safe as plain names because getJointNode() is an exact match,
// not a prefix one, and "ejector" and "ejector_2" are distinct.
void Weapon_DualSMG::resolveGuns()
{
	if (!m_mesh.node)
		return;

	for (int i = 0; i < GUN_COUNT; ++i)
	{
		const std::string suffix = (i == GUN_RIGHT) ? "" : "_2";

		const std::string ejectorName = "ejector" + suffix;
		m_gun[i].ejector = m_mesh.node->getJointNode(ejectorName.c_str());

		if (!m_gun[i].ejector)
			spdlog::warn("Weapon_DualSMG: '{}' joint not found — that gun will not eject casings", ejectorName);

		const std::string magPrefix   = "mag" + suffix + "_smg2";
		const std::string roundPrefix = "bullet" + suffix + "_smg2";

		if (!resolveMeshPart(magPrefix.c_str(), m_gun[i].mag))
			spdlog::warn("Weapon_DualSMG: '{}' not found — that magazine will teleport during the reload", magPrefix);

		if (!resolveMeshPart(roundPrefix.c_str(), m_gun[i].round))
			spdlog::warn("Weapon_DualSMG: '{}' not found — that round will float free during the reload", roundPrefix);

		m_gun[i].magRestValid = false;
	}

	// Every drawable piece of the LEFT gun, so setDualWield(false) can take the
	// whole weapon off screen rather than just its body. Only the left is
	// collected — the right one is never hidden.
	static const char* kParts[] = { "base", "bolt", "ejector", "trigger", "mag", "bullet" };

	m_gun[GUN_LEFT].allParts.clear();

	for (const char* part : kParts)
	{
		// Same prefix trap as above: the left gun's names are the right gun's
		// with "_2" spliced in, so the leaf's own "_smg2" is what disambiguates.
		const std::string prefix = std::string(part) + "_2_smg2";

		MeshPart resolved;
		if (resolveMeshPart(prefix.c_str(), resolved))
			m_gun[GUN_LEFT].allParts.push_back(resolved);
		else
			spdlog::warn("Weapon_DualSMG: '{}' not found — it will stay visible in single-gun mode", prefix);
	}
}

// Show or hide the left-hand gun and rescale everything it contributed.
//
// CAVEAT WORTH KNOWING: this hides the left WEAPON, not the left ARM. The arms
// are one skinned mesh with a single primitive, so there is no buffer to switch
// off for half of it — the left hand goes on gripping thin air, still animated
// as though it were holding something. For a proper one-gun weapon the asset to
// use is smg_animated.glb, which is authored with both hands on a single SMG.
// This flag is for making the pair optional, not for passing as that.
void Weapon_DualSMG::applyDualWield()
{
	for (auto& part : m_gun[GUN_LEFT].allParts)
		setMeshPartVisible(part, m_dualWield);

	// Keep the mag and round handles in step — they are separate copies of the
	// same parts, and updateMagazines() would otherwise show a hidden gun's
	// magazine again on the next reload.
	if (!m_dualWield)
	{
		setMeshPartVisible(m_gun[GUN_LEFT].mag,   false);
		setMeshPartVisible(m_gun[GUN_LEFT].round, false);
	}

	// Never leave more loaded than the guns in hand can hold
	if (m_rounds > magSize())
		m_rounds = magSize();
}

void Weapon_DualSMG::setDualWield(bool enabled)
{
	if (m_dualWield == enabled)
		return;

	m_dualWield = enabled;
	applyDualWield();
}

// --- Magazines ---------------------------------------------------------------

// The clip throws both magazines clear and then brings the SAME meshes back as
// the fresh ones, so each has to be hidden while it is away or it visibly
// teleports across the screen. The reuse trick the whole pack is built on.
//
// Driven off measured displacement rather than frame numbers, for the reason
// spelled out in the header: the joint always knows where the magazine actually
// is, and that cannot disagree with the frame the clip claims to be on.
void Weapon_DualSMG::updateMagazines()
{
	if (!m_mesh.node)
		return;

	// The viewmodel is hidden during drawAll(), so OnAnimate() skipped it and the
	// joint transforms are stale — force them once for both guns, the same way
	// fire() does before reading the muzzle bones.
	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();

	for (int i = 0; i < activeGuns(); ++i)
	{
		SideFX& gun = m_gun[i];
		if (!gun.mag.bone)
			continue;

		const irr::core::vector3df pos = partPosition(gun.mag);

		// Seated position, sampled on the first frame of the reload where the
		// magazine is provably still in the well. Nothing before this moves it.
		if (!gun.magRestValid)
		{
			gun.magRest      = pos;
			gun.magRestValid = true;
			continue;
		}

		const bool dropped = (pos - gun.magRest).getLength() > m_magDroppedEpsilon;

		// The round goes with its magazine, always. It is a separate joint, so
		// hiding only the magazine leaves it hanging in the air on its own.
		setMeshPartVisible(gun.mag,   !dropped);
		setMeshPartVisible(gun.round, !dropped);
	}
}

// --- Brass -------------------------------------------------------------------

void Weapon_DualSMG::ejectSpentCase(int gun)
{
	if (gun < 0 || gun >= GUN_COUNT || !m_mesh.node)
		return;

	SideFX& side = m_gun[gun];
	if (!side.ejector)
		return;

	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();

	side.ejector->updateAbsolutePosition();
	const irr::core::matrix4 world = side.ejector->getAbsoluteTransformation();

	irr::core::vector3df port = m_ejectPortOffset;
	world.transformVect(port);

	// Away from the port and up, from THIS GUN's basis rather than the camera's,
	// so brass leaves correctly whichever way the player is facing. -X to match
	// the port side: with the viewmodel's 180 degree yaw that reads as
	// screen-right. Both guns throw the same way, as real ones would — they are
	// the same weapon, not a mirrored pair.
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
	const float speed = 280.0f * unit;

	const irr::core::vector3df velocity =
		axisRight * speed * Engine::Get()->rng()->getFloat(0.8f, 1.2f) +
		axisUp    * speed * Engine::Get()->rng()->getFloat(0.3f, 0.6f);

	// Turn the casing end for end. Taken straight from the ejector it flies
	// mouth-first back at the camera. Composed as a LOCAL flip — the right
	// operand applies first under Irrlicht's operator* — not by adding 180 to the
	// Euler Y, which would yaw it in the parent frame and fall apart the moment
	// the gun is pitched or rolled.
	irr::core::matrix4 flip;
	flip.setRotationDegrees(irr::core::vector3df(0.0f, 180.0f, 0.0f));
	const irr::core::matrix4 oriented = world * flip;

	// Sized off the gun's OWN round, not off m_viewScaleOffset. That was the bug:
	// shellsmall.obj is already authored in world units at 4 cm long, so scaling
	// it by the viewmodel's 0.01 produced a 0.4 mm casing — ejecting perfectly and
	// far too small to see. Rank-matched per axis, as everywhere else in the pack.
	side.effects.spawnShellAt(
		port,
		oriented.getRotationDegrees(),
		velocity,
		matchPartScale(side.round, side.effects.shellMeshExtent()));
}

// --- Frame loop --------------------------------------------------------------

void Weapon_DualSMG::update()
{
	if (!m_mesh.node || !m_mesh.node->isVisible())
		return;

	const float currentTime = Engine::Get()->getCurrentTime();
	const float dt_s        = Engine::Get()->getDeltaTime() * 0.001f;

	// Consume the animation-end signal once per frame to avoid double-reads
	const bool animEnded = m_mesh.animation_call_back->hasAnimationEnded();
	const irr::f32 frame = m_mesh.node->getFrameNr();

	// State: holstering. The guns stay visible until the clip finishes so they
	// are seen going away; isUnequipping() going false is what releases
	// WeaponController's pending switch.
	if (m_isUnequipping)
	{
		if (animEnded)
			unequip();

		RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);

		return;
	}

	if (m_isEquipping)
	{
		if (animEnded)
		{
			m_isEquipping = false;
			playAnimation("idle");
		}
	}
	// State: double magazine change. Not interruptible by a fire press — both
	// guns are out of the fight until it finishes, which is the cost of carrying
	// two of them.
	else if (m_isReloading)
	{
		updateMagazines();
		updateReloadSounds(frame);

		if (!m_ammoCredited && frame >= static_cast<irr::f32>(m_magInFrame))
		{
			m_ammoCredited = true;
			m_rounds += drawFromReserve(magSize() - m_rounds);
		}

		if (animEnded)
		{
			if (!m_ammoCredited)
			{
				m_ammoCredited = true;
				m_rounds += drawFromReserve(magSize() - m_rounds);
			}

			endReload();
			playAnimation("idle");
		}
	}
	// State: fire clip -> idle. Firing is NOT blocked while this plays — fire()
	// restarts the frame loop, so the cadence limiter alone paces the rounds.
	else if (m_isFireAnim)
	{
		// Brass owed by the round just fired, thrown once the clip has moved the
		// ejector off its rest. Checked BEFORE the end handling so a frame-rate
		// hitch big enough to step over the whole 7-frame clip in one tick still
		// throws the case: Irrlicht clamps the frame to EndFrame when it fires
		// the end callback, which is past m_caseEjectFrame.
		if (frame >= static_cast<irr::f32>(m_caseEjectFrame))
		{
			for (int i = 0; i < GUN_COUNT; ++i)
			{
				if (!m_gun[i].caseOwed)
					continue;

				m_gun[i].caseOwed = false;
				ejectSpentCase(i);
			}
		}

		if (animEnded)
		{
			m_isFireAnim = false;
			setClipSpeed(1.0f); // the 3.8x is the fire clip's alone
			playAnimation("idle");
		}
	}

	// Record where the stabilisation reference sits at rest. Done here rather
	// than in init() because the joints are stale while the node is hidden, and
	// gated on the guns being genuinely idle: every other state has a clip
	// driving them, and a reference captured mid-draw would make the rest pose
	// itself look like a drift to be countered.
	if (!stabilizationRestValid()
		&& !m_isFireAnim && !m_isReloading && !m_isEquipping && !m_isUnequipping)
	{
		m_mesh.node->updateAbsolutePosition();
		m_mesh.node->animateJoints();
		captureStabilizationRest();
	}

	const bool lmbPressed = InputManager::Get()->isMouseButtonPressed(MB_LEFT);

	if (!lmbPressed)
		m_dryFiredThisPress = false;

	// A live trigger: held, with the guns able to answer it. Named separately
	// because the bloom below has to tell "not shooting" from "between rounds of
	// a burst" — at this cadence only about one frame in four actually fires.
	const bool triggerLive = lmbPressed
		&& m_rounds > 0
		&& !m_isReloading
		&& !m_isEquipping;

	if (triggerLive && (currentTime - m_lastFireTime) >= m_fireInterval)
	{
		m_lastFireTime = currentTime;
		fire();
	}
	else if (lmbPressed && !m_dryFiredThisPress && !m_isReloading && !m_isEquipping && m_rounds <= 0)
	{
		// Both guns empty: one click per press, not one per cadence tick.
		m_dryFiredThisPress = true;

		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/dryfire", 0.05f, 1, -1.0f, "dualsmg_dryfire");
	}

	if (!triggerLive)
	{
		// Bloom recovers only while the trigger is off. Gated on the TRIGGER, not
		// on whether a round went out this frame: the latter would decay bloom on
		// the three frames in four that fall between rounds, which is more than it
		// gains per round — held fire would get TIGHTER the longer it was held.
		m_bloom -= m_bloomDecay * dt_s;
		if (m_bloom < 0.0f)
			m_bloom = 0.0f;
	}

	// Reload input is NOT handled here: WeaponController::update() already drives
	// current_weapon->reload() from the remappable "reload" action, so a local
	// hardcoded KEY_R would just ignore a rebind in input.xml. reload() is
	// idempotent, which is what makes that level-triggered action safe.

	RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
}

void Weapon_DualSMG::persist()
{
	const float dt = Engine::Get()->getDeltaTime();

	for (int i = 0; i < GUN_COUNT; ++i)
		m_gun[i].effects.update(dt);
}

void Weapon_DualSMG::equip()
{
	m_mesh.node->setVisible(true);

	// Consume any stale animation-end flag from before the weapon was hidden
	m_mesh.animation_call_back->hasAnimationEnded();

	m_isFireAnim        = false;
	for (int i = 0; i < GUN_COUNT; ++i) m_gun[i].caseOwed = false;
	m_dryFiredThisPress = false;
	m_bloom             = 0.0f;
	resetViewKick();

	playEquipSound();

	// Recover from a magazine change cut short by a weapon switch — otherwise the
	// clip speed and the stabilisation offset stay applied for the rest of the
	// weapon's life, and a magazine can be left hidden forever. endReload() ends
	// by re-applying the dual-wield state, so a toggle flipped while this weapon
	// was holstered takes effect on the draw.
	endReload();

	// If the clip is missing, playAnimation leaves the current loop alone and
	// returns false — drop straight to idle rather than freezing on a stale pose.
	m_isEquipping = playAnimation("equip");
	if (!m_isEquipping)
		playAnimation("idle");
}

void Weapon_DualSMG::unequip()
{
	m_isUnequipping = false;
	m_isFireAnim    = false;
	m_isEquipping   = false;
	for (int i = 0; i < GUN_COUNT; ++i) m_gun[i].caseOwed = false;

	endReload();

	m_mesh.node->setVisible(false);
}

void Weapon_DualSMG::startUnequip()
{
	// Already hidden, or mid-holster: nothing to play, don't restart the clip
	if (!m_mesh.node || !m_mesh.node->isVisible() || m_isUnequipping)
		return;

	m_isFireAnim  = false;
	m_isEquipping = false;
	for (int i = 0; i < GUN_COUNT; ++i) m_gun[i].caseOwed = false;

	playUnequipSound();

	// Holstering mid-swap keeps whatever ammo had already been credited
	endReload();

	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag

	// Node stays visible until update() sees the clip end. If the clip were ever
	// missing, playAnimation() returns false and we hide instantly instead.
	m_isUnequipping = playAnimation("unequip");
	if (!m_isUnequipping)
		unequip();
}

void Weapon_DualSMG::idle()
{

}

void Weapon_DualSMG::move()
{

}

// --- Firing ------------------------------------------------------------------

void Weapon_DualSMG::fire()
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>() || !m_mesh.node)
		return;

	// How many guns actually go off this pulse. Normally every gun in hand, but
	// clamped by what is left: with one round in a pair, the right gun fires it
	// alone rather than the pulse being dropped or a round conjured for the left.
	const int firing = std::min(activeGuns(), m_rounds);
	if (firing <= 0)
		return;

	m_rounds -= firing;

	setClipSpeed(m_fireSpeed);
	playAnimation("fire");
	m_isFireAnim = true;

	// Kick straight back and up. No roll bias: both guns go off together, so the
	// recoil is symmetric — biasing it to one side would read as a limp. Small,
	// because at this cadence the spring never fully recovers between pulses and
	// a large kick would stack into an unusable climb within half a second.
	// Scaled by how many guns fired, so single-gun mode is genuinely lighter.
	const float weight = static_cast<float>(firing);

	addViewKick(
		irr::core::vector3df(0.0f, 0.009f * weight, -0.024f * weight),
		irr::core::vector3df(1.5f * weight, 0.0f,
			Engine::Get()->rng()->getFloat(-0.8f, 0.8f)));

	auto& camera = player.getComponent<CameraComponent>();

	// Force full hierarchy update ONCE for the pulse so both guns read current
	// bone positions — doing it per gun would animate the joints twice a frame
	// for no gain.
	camera.camera->updateAbsolutePosition();
	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();

	// Camera basis for the spread offsets, shared by both guns
	irr::core::vector3df target    = camera.camera->getTarget();
	irr::core::vector3df cameraPos = camera.camera->getAbsolutePosition();
	irr::core::vector3df forward   = (target - cameraPos).normalize();

	irr::core::vector3df up(0, 1, 0);
	irr::core::vector3df right = forward.crossProduct(up).normalize();
	irr::core::vector3df down  = right.crossProduct(forward).normalize();

	for (int gun = 0; gun < firing; ++gun)
		fireOneGun(gun, forward, right, down);

	// Bloom climbs once per PULSE, not once per round — charging it twice for
	// two guns would make the pair bloom out in half the time and undo the whole
	// reason to carry them.
	m_bloom += m_bloomPerShot;
	if (m_bloom > 1.0f)
		m_bloom = 1.0f;

	// One camera recoil for the pulse, weighted by how many guns fired
	g_CameraFX.addRecoil(
		-0.45f * weight,
		Engine::Get()->rng()->getFloat(-0.3f, 0.3f));

	// One report for the pulse. Two samples a frame apart read as a flam rather
	// than as two guns, and the concurrency cap would eat one of them anyway.
	SoundManager::Get()->sound()->playRandomized2D(
		"content/sound/weapon/pistol/fire", 0.08f, 4, 0.40f, "dualsmg_fire");
}

// One gun's worth of a pulse: its own muzzle, its own ray, its own flash, tracer
// and brass. Split out so the two guns cannot drift apart — every per-gun effect
// is in this one function, reading the one index it was handed.
void Weapon_DualSMG::fireOneGun(int gun, const irr::core::vector3df& forward,
                                const irr::core::vector3df& right,
                                const irr::core::vector3df& down)
{
	SideFX& side = m_gun[gun];

	// This gun's OWN muzzle — the same point its flash is drawn at, by
	// construction, so the shot and the flash cannot disagree.
	const irr::core::vector3df muzzlePos = side.effects.muzzleWorldPosition();

	// Converge on the crosshair aim point. This matters more here than on any
	// other weapon in the pack: the muzzles sit 26 model units either side of
	// centre, so parallel camera-forward rays would put the two guns' shots in
	// visibly different places.
	irr::core::vector3df direction = getAimDirection(muzzlePos);

	// Rolled independently per gun, so a pulse puts two rounds in two places
	// rather than one doubled hole.
	const float spread = m_spreadMin + (m_spreadMax - m_spreadMin) * m_bloom;

	const float spreadRight = Engine::Get()->rng()->getFloat(-spread, spread);
	const float spreadDown  = Engine::Get()->rng()->getFloat(-spread, spread);
	direction = (direction + right * spreadRight + down * spreadDown).normalize();

	const irr::core::vector3df rayEnd = muzzlePos + direction * 1000.0f;

	RaycastResultData raycastResult = RenderManager::Get()->raycastWorldPosition(
		muzzlePos,
		rayEnd,
		true  // Exclude debug nodes
	);

	if (raycastResult.hit && raycastResult.node)
	{
		auto& hitEntity = WorldManager::Get()->managerSystem()->getEntityByID(raycastResult.node->getID());

		if (hitEntity.isValid() && hitEntity.hasComponent<DescriptorComponent>())
		{
			auto& hitDescriptor = hitEntity.getComponent<DescriptorComponent>();

			// Only register collision with static or dynamic entities
			if (hitDescriptor.type == ET_STATIC || hitDescriptor.type == ET_DYNAMIC)
			{
				// Damage through the gameplay chokepoint; drives hitmarker/kill feedback
				registerHitFeedback(
					WorldManager::Get()->gameplaySystem()->damageEntity(hitDescriptor.id, m_damage, DAMAGE_TYPE::DEFAULT,
						DamageContext::fromImpact(raycastResult.point, raycastResult.normal,
							raycastResult.ray.getVector())));

				// Sparks and a bullet hole are for hard surfaces. Anything carrying a
				// damage receiver is flesh as far as feedback goes, and GoreManager has
				// already covered it.
				if (!hitEntity.hasComponent<DamageReceiverComponent>())
					side.effects.impact(raycastResult.point, raycastResult.normal);
			}
		}
		else if (RenderManager::isWorldGeometryNode(raycastResult.node))
		{
			// Brush chunks / props carry no ECS id — solid surface hit, nothing to damage
			side.effects.impact(raycastResult.point, raycastResult.normal);
		}
	}

	// Tracer from this gun (its module fires every other round)
	const irr::core::vector3df tracerEnd = (raycastResult.hit && raycastResult.node) ?
		raycastResult.point : (muzzlePos + direction * 1000.0f);
	side.effects.spawnTracer(muzzlePos, tracerEnd);

	side.effects.muzzleFlash();

	side.caseOwed = true;
}

void Weapon_DualSMG::reload()
{
	if (m_isReloading || m_isEquipping || m_isUnequipping)
		return;

	if (m_rounds >= magSize())
		return; // nothing to top up

	// Nothing in the pool to load with. Cued rather than failing silently: silence
	// reads as a dropped input, and the player presses reload again instead of
	// going to look for ammunition.
	if (reserveRemaining() <= 0)
	{
		playEmptyReserveSound();
		return;
	}

	if (!m_mesh.node)
		return;

	m_ammoCredited = false;
	m_magOutPlayed = false;
	m_magInPlayed  = false;

	// Sample where each magazine sits when it is seated. Taken here because both
	// are provably home on the clip's first frame, which makes this the one
	// moment the rest pose can be read without assuming anything about what the
	// guns were doing beforehand.
	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();

	for (int i = 0; i < GUN_COUNT; ++i)
	{
		m_gun[i].magRestValid = false;

		if (m_gun[i].mag.bone)
		{
			m_gun[i].magRest      = partPosition(m_gun[i].mag);
			m_gun[i].magRestValid = true;
		}
	}

	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag

	setClipSpeed(m_reloadSpeed);
	setStabilizationAmount(stabilizationTuneAmount());

	m_isReloading = playAnimation("reload");
	m_isFireAnim  = false;
	for (int i = 0; i < GUN_COUNT; ++i) m_gun[i].caseOwed = false;

	if (!m_isReloading)
		endReload(); // clip missing: undo the speed and stabilisation we just set
}

// Frame-triggered reload audio. Both magazines move together, so each cue is one
// sound rather than two — two copies a frame apart read as a flam, not as a pair.
void Weapon_DualSMG::updateReloadSounds(float frame)
{
	const int f = static_cast<int>(frame);

	if (!m_magOutPlayed && f >= m_magOutFrame - soundLeadFrames(m_removeMagLeadSec))
	{
		m_magOutPlayed = true;

		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/remove_mag", 0.05f, 1, -1.0f, "dualsmg_mag");
	}

	if (!m_magInPlayed && f >= m_magInFrame - soundLeadFrames(m_insertMagLeadSec))
	{
		m_magInPlayed = true;

		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/insert_mag", 0.05f, 1, -1.0f, "dualsmg_mag");
	}
}

// Leaves m_rounds alone: ammo is credited by update() on the frame the fresh
// magazines seat, so this only tears down the reload's own bookkeeping. Every
// exit from a reload runs through here, which is what makes it the one place the
// sped-up playback and the stabilisation offset have to be put back.
void Weapon_DualSMG::endReload()
{
	m_isReloading  = false;
	m_ammoCredited = false;
	m_magOutPlayed = false;
	m_magInPlayed  = false;

	setClipSpeed(1.0f);
	setStabilizationAmount(0.0f);

	for (int i = 0; i < GUN_COUNT; ++i)
	{
		m_gun[i].magRestValid = false;
		setMeshPartVisible(m_gun[i].mag,   true);
		setMeshPartVisible(m_gun[i].round, true);
	}

	// ...and then put the left gun back the way the toggle wants it. The loop
	// above deliberately restores BOTH so nothing is left hidden by a reload that
	// was cut short; this is what stops it resurrecting a gun that is switched off.
	applyDualWield();
}

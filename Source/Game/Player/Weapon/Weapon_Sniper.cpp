#include "Weapon_Sniper.h"

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

void Weapon_Sniper::precache()
{
	ParticleManager::Get()->precache("spark", _asset_psys("spark"));

	// equip/unequip are shared across weapons and preloaded by WeaponController.
	// Borrows the heavy rifle's set until a dedicated sniper report is authored;
	// everything here resolves through playRandomized2D, so dropping numbered
	// variants next to these upgrades them with no code change.
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/dryfire.wav",     true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/cock_rifle.wav",  true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/remove_mag.wav",  true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/insert_mag.wav",  true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/rifle/fire.wav",  true);
}

void Weapon_Sniper::init()
{
	m_descriptor.name = "Player_Weapon_Sniper";
	m_descriptor.id = _entity_null_value;

	m_weapon_type = WEAP_SNIPER;

	// sniper_animated.glb carries the same arms rig as the rest of the pack —
	// identical joint names, identical 'arms' root at (0, 2.945, -17.671) — so
	// the other long guns' viewmodel transform is the right starting point. This
	// barrel is the longest in the pack at 146 model units, so it sits further
	// back again. Tune with the viewmodel debug UI (F2), not by guessing here.
	m_viewPositionOffset = irr::core::vector3df(0.1100f, -0.1450f, 0.0700f);
	m_viewRotationOffset = irr::core::vector3df(0.0f, 180.0f, 0.0f);
	m_viewScaleOffset    = irr::core::vector3df(0.01f, 0.01f, 0.01f);

	m_mesh.mesh = _asset_glb("player/weapon/sniper_animated");

	m_mesh.trimesh = RenderManager::Get()->loadMesh(m_mesh.mesh);

	// Swap in the stand-in BEFORE the node is created — creating a node in the
	// failure branch and again below orphans the first one.
	const bool usingStandIn = (m_mesh.trimesh == nullptr);
	if (usingStandIn)
	{
		spdlog::warn("Weapon_Sniper::init(): failed to load mesh \"{}\", stand-in mesh loaded", m_mesh.mesh);
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
	// take (0-6.83s = frames 0-205 at 30 fps) with every clip concatenated and a
	// 2-frame hold at the shared rest pose between them. The rest pose recurs at
	// frames 0, 10/11, 60/61, 104/105, 164/165, 185/186 and 205; each range was
	// then identified from the gross motion of the 'sniper' root and the parts:
	//   0-10     trigger f0-10, root recoils Z -6.8 and recovers, and
	//            NOTHING else moves — no bolt at all                -> fire
	//   11-60    gun rolls -15 deg into the shoulder, bolt handle
	//            turns -90 over f24-27, draws back Z -10 over
	//            f30-33, holds open, round flicks clear at f41-43,
	//            bolt closes f41-44, handle down f47-49             -> cycle
	//   61-104   magazine drops away f70-79 and is replaced by f89  -> reload
	//   105-164  ONE take holding both transitions. The rifle swings
	//            away to its apex at f114 (yaw +68, Z -21.4, out of
	//            frame), comes back up by f127, and then WORKS THE
	//            BOLT on the way in — handle up f128-132, drawn back
	//            f134-138, closed f146-148, handle down f152-154 —
	//            before settling at f164                            -> unequip
	//            105-114, equip 114-164
	//   165-185  a 1.4-unit dip and return, no rotation at all      -> a gentle
	//            idle sway, unused: idle is pinned to 165 and the
	//            hold-steady motion comes from enableIdleBreathing()
	//   186-205  gun snaps to a rolled pose (-89) in two frames and
	//            takes seventeen to recover                     -> melee bash
	//
	// That the fire clip touches nothing but the trigger and the recoil is what
	// makes this a bolt gun rather than a semi-auto: the case is still in the
	// chamber when the clip ends, and only the cycle takes it out. Firing
	// therefore ALWAYS chains into "cycle" — see enterState().
	//
	// The equip ENDS AT 164, and getting that wrong is what made the right hand
	// snap. 105-164 is one clip and the rifle's ROOT is back near its seat by
	// f122 — but the ARMS are still moving, and f124-154 is a full bolt cycle
	// that chambers a round as part of the draw. Cutting at 122 dropped the hand
	// mid-motion into the idle pose.
	//
	// The lesson generalises: a clip boundary has to be read from the WHOLE POSE,
	// not from the weapon root. The root can be home while the hands are not.
	//
	// 186-205 was bound as the equip at first and is NOT a draw. The tell is the
	// timing: it reaches its pose in two frames and recovers over seventeen, with
	// a -89 degree roll. A draw is symmetric and does not roll the rifle over.
	//
	// Looping clips MUST be flagged loop=true — a non-looping clip re-armed from
	// the end callback holds its last frame for one tick every cycle, which is a
	// visible hitch.
	m_mesh.animationList.emplace_back(sAnimationData("fire",    0,   10,  false));
	m_mesh.animationList.emplace_back(sAnimationData("cycle",   11,  60,  false));
	m_mesh.animationList.emplace_back(sAnimationData("reload",  61,  104, false));
	m_mesh.animationList.emplace_back(sAnimationData("unequip", 105, 114, false));
	m_mesh.animationList.emplace_back(sAnimationData("equip",   114, 164, false));
	m_mesh.animationList.emplace_back(sAnimationData("idle",    165, 165, true));
	// Authored but not bound: the mirror draw and the sway loop described above
	m_mesh.animationList.emplace_back(sAnimationData("melee",     186, 205, false));
	m_mesh.animationList.emplace_back(sAnimationData("idle_sway", 165, 185, true));

	// Both glTF backends normalise keyframe times to 30 fps Irrlicht frames, so
	// the viewmodel must play at 30 to run at its authored speed.
	m_mesh.fps = 30;
	m_mesh.node->setAnimationSpeed(static_cast<irr::f32>(m_mesh.fps));

	m_mesh.node->setJointMode(irr::scene::EJUOR_READ);

	m_mesh.animation_call_back = std::make_shared<AnimationCallback>();
	m_mesh.node->setAnimationEndCallback(m_mesh.animation_call_back.get());

	playAnimation("idle"); // safe default until equip() runs

	// The idle clip is pinned to a single frame, so the hold-steady motion comes
	// from updateWeaponSway() instead. Low: a rifle this long is braced hard into
	// the shoulder, and a wide idle would make it read like a toy. It is damped
	// further still while scoped — see updateScope().
	enableIdleBreathing(0.8f);

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

	// Must come AFTER the material assignment above — it caches the round's real
	// material type so setMeshPartVisible() has something to restore.
	resolveMeshPart("bullet", m_round);

	// Reference point for cycle stabilisation: the bore line at roughly the gun's
	// mid-length. 'base' spans Z -32.6 to +113.4 and 5.7 is the bore height
	// already measured for the muzzle, so this splits the rotation error between
	// muzzle and stock instead of pinning one end and swinging the other. Z is
	// negated because GltfImport's handedness conversion negates it.
	enableClipStabilization("base", irr::core::vector3df(0.0f, 5.70f, -40.0f));
	setStabilizationTuneAmount(0.55f);

	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");

	if (!player.isValid())
	{
		spdlog::error("In function Weapon_Sniper::init() -> getEntityByName(\"player\") : Entity 'player' does not exist");

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
		spdlog::error("In function Weapon_Sniper::init() -> player.getComponent<CameraComponent>() : Entity 'player' does not have specified component");
	}

	RenderManager::Get()->registerViewmodelNode(m_mesh.node);
	m_mesh.node->setVisible(false);

	m_rounds = m_magSize;

	m_crosshair = RenderManager::Get()->driver()->getTexture("content/texture/ui/crosshair/crosshair038.png");

	// Character sheet: one big flash, always a tracer, brass thrown by the bolt.
	//
	// This .glb has no FIRESPOT empty, but it does not need one: 'base' is the
	// receiver-and-barrel joint, so anything parented to it inherits the recoil,
	// the bolt cycle and the holster for free. The offset is the bore centre at
	// the muzzle face — the 82 verts within 1 unit of the barrel's far end run
	// from Y 3.51 to 7.98 and X -2.24 to 2.24 with no gap, i.e. one ring of
	// radius ~2.24 centred at Y 5.70 — and GltfImport's handedness conversion
	// negates Z. Being joint-local, it is in model units and needs no change if
	// the viewmodel scale is retuned.
	WeaponEffectsDesc fx;
	fx.muzzleJointName   = "base";
	fx.muzzleJointOffset = irr::core::vector3df(0.0f, 5.70f, -113.39f);
	fx.flashColor        = irr::video::SColor(255, 255, 226, 170);
	fx.flashSize         = 1.1f;   // biggest flash in the pack — long barrel, big charge
	fx.flashDuration     = 75.0f;
	fx.lightColor        = irr::video::SColorf(1.0f, 0.85f, 0.45f);
	fx.lightRadius       = 6.5f;
	fx.tracerFrequency   = 1;      // every shot: at this range the trace IS the feedback
	fx.tracerSpeed       = 700.0f;
	fx.tracerWidth       = 0.13f;
	fx.tracerSegmentLength = 7.0f;
	fx.tracerPoolSize    = 8;
	// The brass is spawned by ejectSpentCase() from the animated round's own
	// bone, not from a port, so shellEjectJoint/shellEjectOffset stay unset and
	// ejectShell() is never called. Same approach as the revolver and shotgun.
	fx.shellMesh         = "content/mesh/prop/shells/shellmedium.obj";
	fx.shellSpeed        = 5.0f;
	fx.shellPoolSize     = 12;
	fx.impactParticle    = "spark";
	m_effects.init(m_mesh.node, fx);
}

void Weapon_Sniper::destroy()
{
	m_effects.destroy();

	RenderManager::Get()->unregisterViewmodelNode(m_mesh.node);
	m_mesh.node->remove();

	WorldManager::Get()->freeEntityID(m_descriptor.id);
}

// Single place that starts a clip and moves the machine, so the "what plays
// next" rules live in one readable block instead of being scattered across the
// call sites that trigger them.
void Weapon_Sniper::enterState(State next)
{
	m_state = next;
	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag

	// Working-the-action states run quicker than authored; everything else plays
	// at 1x. Set here rather than at the call sites so no path can leave the node
	// stuck at the faster speed.
	setClipSpeed(next == State::Cycling   ? m_cycleSpeed
	           : next == State::Reloading ? m_reloadSpeed
	           : next == State::Equipping ? m_equipSpeed
	           : 1.0f);

	// Only the bolt cycle is stabilised — it rolls the gun 22 degrees into the
	// shoulder, which swung out along a 146-unit barrel throws the muzzle a long
	// way across the screen. Same choke point as the speed above, so no path can
	// leave the counter-offset applied to a clip that does not want it.
	setStabilizationAmount(next == State::Cycling ? stabilizationTuneAmount() : 0.0f);

	switch (next)
	{
	case State::Firing:
		playAnimation("fire");
		break;

	case State::Cycling:
		playAnimation("cycle");
		m_caseHandedOff  = false;
		m_boltLiftPlayed = false;
		m_boltHomePlayed = false;
		break;

	case State::Reloading:
		playAnimation("reload");
		m_magOutPlayed = false;
		m_magInPlayed  = false;
		m_ammoCredited = false;
		break;

	case State::Equipping:
		playAnimation("equip");
		// The draw works the bolt (see the clip table), so the round goes through
		// the same reuse flick it does during a cycle and needs re-sampling — and
		// the bolt cues have to be re-armed exactly as they are for a cycle.
		m_roundRestValid = false;
		m_boltLiftPlayed = false;
		m_boltHomePlayed = false;
		break;

	case State::Unequipping:
		playAnimation("unequip");
		break;

	case State::Idle:
	default:
		playAnimation("idle");
		setMeshPartVisible(m_round, true); // whatever the cycle left hidden
		break;
	}
}

// --- The chambered round -----------------------------------------------------

// The single 'bullet' mesh is both the extracted case and the fresh round, so it
// has to be hidden across the flick that throws it clear or it visibly teleports
// from mid-air back into the breech.
//
// Driven off the part's own displacement rather than off frame numbers. The LMG
// shipped with frame-derived triggers that did not fire where the .glb analysis
// said they would; the joint always knows where the round actually is, needs no
// constant kept in step with the asset, and reads the same at any clip speed.
void Weapon_Sniper::updateRound(bool throwCase)
{
	if (!m_round.bone || !m_mesh.node)
		return;

	// The viewmodel is hidden during drawAll(), so OnAnimate() skipped it and the
	// joint transforms are stale — force them, the same way fire() does before
	// reading the muzzle bone.
	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();

	const irr::core::vector3df pos = partPosition(m_round);

	// Seated position, sampled on the first frame of the cycle where the round is
	// provably still in the breech. Nothing else in the clip table moves it.
	if (!m_roundRestValid)
	{
		m_roundRest      = pos;
		m_roundRestValid = true;
		return;
	}

	const bool loose = (pos - m_roundRest).getLength() > m_roundLooseEpsilon;

	if (loose)
	{
		// Hand the case off to a physics casing on the exact frame the animated
		// mesh disappears — same place, same orientation — so there is no seam.
		// The draw chambers a round rather than extracting a fired one, so it has
		// nothing to throw — hiding the mesh is all it needs.
		if (throwCase && !m_caseHandedOff)
		{
			m_caseHandedOff = true;
			ejectSpentCase();
		}

		setMeshPartVisible(m_round, false);
	}
	else
	{
		// Back at the seat: this is the FRESH round, not the case that just left.
		setMeshPartVisible(m_round, true);
	}
}

void Weapon_Sniper::ejectSpentCase()
{
	if (!m_round.bone || !m_mesh.node)
		return;

	irr::core::matrix4 world;
	if (!meshPartWorldTransform(m_round, world))
		return;

	// Away from the port and up, from the RIFLE's basis rather than the camera's,
	// so brass leaves correctly whichever way the player is facing. -X to match
	// the port side: with the viewmodel's 180 degree yaw that reads as
	// screen-right, which is where a right-handed bolt gun throws its cases.
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
	const float speed = 260.0f * unit;

	const irr::core::vector3df velocity =
		axisRight * speed * Engine::Get()->rng()->getFloat(0.8f, 1.2f) +
		axisUp    * speed * Engine::Get()->rng()->getFloat(0.4f, 0.8f);

	// Turn the casing end for end. Taken straight from the breech it flies
	// mouth-first back at the camera. Composed as a LOCAL flip — the right
	// operand applies first under Irrlicht's operator* — not by adding 180 to the
	// Euler Y, which would yaw it in the parent frame and fall apart the moment
	// the rifle is pitched or rolled.
	irr::core::matrix4 flip;
	flip.setRotationDegrees(irr::core::vector3df(0.0f, 180.0f, 0.0f));
	const irr::core::matrix4 oriented = world * flip;

	// Size the casing off the model's OWN chambered round rather than a tuned
	// constant, so it matches the calibre the rifle is drawn holding. Rank-matched
	// per axis because shellmedium.obj is 3:1 where this round is 4.6:1 — one
	// uniform ratio off the long axis would leave the case visibly too fat, and
	// bulk goes as the square of the diameter.
	m_effects.spawnShellAt(
		world.getTranslation(),
		oriented.getRotationDegrees(),
		velocity,
		matchPartScale(m_round, m_effects.shellMeshExtent()));
}

// --- Scope -------------------------------------------------------------------

// Everything the sight does is a function of one blend value, so the transition
// can never half-apply. The FOV, the spread, the sway damping and the overlay
// all read m_scopeBlend rather than a "scoped" boolean.
void Weapon_Sniper::updateScope(float dt)
{
	const float dt_s = dt * 0.001f;

	// The sight drops for anything that moves the gun. Held rather than toggled,
	// so releasing the button always brings it down and no state can strand it up.
	const bool wants = m_scopeWanted && scopeAllowed();

	float t = m_scopeBlendSpeed * dt_s;
	if (t > 1.0f) t = 1.0f;

	m_scopeBlend += ((wants ? 1.0f : 0.0f) - m_scopeBlend) * t;

	if (m_scopeBlend < 0.001f) m_scopeBlend = 0.0f;
	if (m_scopeBlend > 0.999f) m_scopeBlend = 1.0f;

	// Sustained FOV offset, eased inside CameraFX. Set every frame on purpose —
	// it is a target, not an event, and writing it unconditionally means no path
	// out of this weapon can leave the camera zoomed.
	g_CameraFX.setFovZoom(m_scopeFovZoom * m_scopeBlend);

	// A scope magnifies hand movement exactly as much as it magnifies the target,
	// so the procedural idle has to come down with it or the reticle is unusable.
	m_swayAmount = 0.02f * (1.0f - m_scopeBlend * (1.0f - m_scopedSwayScale));
	m_idleBreath.scale = 0.8f * (1.0f - m_scopeBlend * (1.0f - m_scopedSwayScale));

	// Past the threshold the model gives way to the overlay. Swapped while the
	// view is still closing so the overlay is already there by the time the
	// player is looking through it.
	//
	// Tracked with its own flag rather than by reading node visibility, because
	// equip/unequip own that flag too and the two must not fight: this only ever
	// hides a node it knows is otherwise visible, and only ever shows one it
	// hid itself.
	// Drive the scope post-process. The aperture opens from wide to its final
	// size across the blend, so raising the sight reads as the view closing in
	// rather than as a mask snapping on; the pass is switched off entirely below
	// the threshold so its taps cost nothing when nobody is scoped.
	auto* rm = RenderManager::Get();

	if (auto* scope = rm->scopeCallback())
	{
		const bool on = (m_scopeBlend >= m_scopeOverlayAt);

		if (on)
		{
			// Remap the blend's last quarter onto 0..1 so the aperture is still
			// closing after the pass switches on.
			const float t = (m_scopeBlend - m_scopeOverlayAt) / (1.0f - m_scopeOverlayAt);

			m_scopeAperture   = m_scopeApertureOpen
			                  + (m_scopeApertureFull - m_scopeApertureOpen) * t;
			scope->aperture   = m_scopeAperture;
			scope->softness   = m_scopeSoftness;
			scope->blurRadius = m_scopeBlurRadius;
			scope->vignette   = m_scopeVignette;
		}

		if (on != m_scopePassOn)
		{
			m_scopePassOn = on;
			rm->setScopeEnabled(on);
		}
	}

	const bool wantHidden = (m_scopeBlend >= m_scopeOverlayAt);

	if (wantHidden && !m_modelHiddenByScope)
	{
		m_modelHiddenByScope = true;
		m_mesh.node->setVisible(false);
	}
	else if (!wantHidden && m_modelHiddenByScope)
	{
		m_modelHiddenByScope = false;
		m_mesh.node->setVisible(true);
	}
}

// Switch the post-process pass off and remember that it is off.
//
// This matters more than it looks: the pass is GLOBAL renderer state, not
// something owned by this weapon's node. Leaving it on would blur the screen for
// whatever the player switches to next — the same trap as the sustained FOV zoom,
// which is why every teardown path here clears both together.
void Weapon_Sniper::disableScopePass()
{
	if (!m_scopePassOn)
		return;

	m_scopePassOn = false;

	if (auto* rm = RenderManager::Get())
		rm->setScopeEnabled(false);
}

// The sight itself is a POST-PROCESS pass (see scope.frag): a sharp circular
// aperture over a heavily blurred, darkened surround. This function only draws
// the reticle inside it.
//
// The reticle is built from rectangles rather than from a texture on purpose. It
// stays a crisp one-pixel line at any resolution, where a scaled-up crosshair
// .png would be a blurry smear at the size a scope needs; and its arms can be
// sized off the aperture, which is a uniform that moves with the zoom.
void Weapon_Sniper::drawScopeOverlay()
{
	if (m_scopeBlend < m_scopeOverlayAt)
		return;

	auto* rm = RenderManager::Get();
	const auto& cfg = rm->getConfiguration();

	const irr::s32 cx = cfg.width  / 2;
	const irr::s32 cy = cfg.height / 2;

	// Matches scope.frag's mapping exactly: aperture 1.0 is half the SHORT axis.
	const float shortAxis = static_cast<float>(cfg.height < cfg.width ? cfg.height : cfg.width);
	const irr::s32 radius = static_cast<irr::s32>(m_scopeAperture * shortAxis * 0.5f);

	const irr::video::SColor ink(215, 8, 8, 10);

	const irr::s32 t   = 1;                 // arm thickness, half-width
	const irr::s32 gap = radius / 12;       // clear space around the aim point
	const irr::s32 arm = radius - radius / 20; // stop just inside the aperture edge

	// Four arms with a gap at the middle, so the reticle never covers the thing
	// being aimed at.
	rm->renderRectangle2D(irr::core::rect<irr::s32>(cx - arm, cy - t, cx - gap, cy + t), ink);
	rm->renderRectangle2D(irr::core::rect<irr::s32>(cx + gap, cy - t, cx + arm, cy + t), ink);
	rm->renderRectangle2D(irr::core::rect<irr::s32>(cx - t, cy - arm, cx + t, cy - gap), ink);
	rm->renderRectangle2D(irr::core::rect<irr::s32>(cx - t, cy + gap, cx + t, cy + arm), ink);

	// Centre dot — the actual aim point, and the only mark inside the gap
	rm->renderRectangle2D(irr::core::rect<irr::s32>(cx - 1, cy - 1, cx + 1, cy + 1), ink);

	// Mil-dot ticks along the horizontal, a shorthand for "this is a ranging
	// optic" that costs four more rectangles.
	const irr::s32 tick = radius / 3;
	for (irr::s32 i = 1; i <= 2; ++i)
	{
		const irr::s32 x = tick * i;
		if (x >= arm) break;

		rm->renderRectangle2D(irr::core::rect<irr::s32>(cx - x - t, cy - 4, cx - x + t, cy + 4), ink);
		rm->renderRectangle2D(irr::core::rect<irr::s32>(cx + x - t, cy - 4, cx + x + t, cy + 4), ink);
	}
}

// --- Frame loop --------------------------------------------------------------

void Weapon_Sniper::update()
{
	// The scope deliberately hides the viewmodel node, and that must not read as
	// "holstered" — hence the second test. Everything below still runs while
	// scoped, which is what keeps firing, cycling and reloading working there.
	if (!m_mesh.node)
		return;

	if (!m_mesh.node->isVisible() && !m_modelHiddenByScope)
		return;

	const float dt = Engine::Get()->getDeltaTime();

	const bool animEnded = m_mesh.animation_call_back->hasAnimationEnded();
	const irr::f32 frame = m_mesh.node->getFrameNr();

	// Read up here so the busy states can see it too, but act on it only inside
	// them: changing state before the switch would let this frame's already
	// latched animEnded fall through and end the clip we just started.
	const bool lmb = InputManager::Get()->isMouseButtonPressed(MB_LEFT);
	if (!lmb)
		m_firedThisPress = false;

	m_scopeWanted = InputManager::Get()->isMouseButtonPressed(MB_RIGHT);
	updateScope(dt);

	switch (m_state)
	{
	// Holstering: stay visible until the clip finishes so the rifle is seen being
	// put away. isUnequipping() going false releases WeaponController's pending
	// switch, so the next weapon is only drawn once this one is down.
	case State::Unequipping:
		if (animEnded)
			unequip();
		return;

	case State::Equipping:
		// Same reuse flick as the cycle, minus the brass — and the same bolt
		// working, so it gets the same two cues at its own frame offsets.
		updateRound(false);
		updateBoltSounds(frame, m_equipBoltLiftFrame, m_equipBoltHomeFrame);

		if (animEnded)
		{
			m_roundRestValid = false;
			enterState(State::Idle);
		}
		drawScopeOverlay();
		RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
		return;

	// Every shot works the action, without exception — that is the weapon's whole
	// character, and it is also what ejects the case and chambers the next round.
	case State::Firing:
		if (animEnded)
			enterState(State::Cycling);
		break;

	case State::Cycling:
		updateRound(m_cycleThrowsCase);
		updateBoltSounds(frame, m_boltLiftFrame, m_boltHomeFrame);

		if (animEnded)
		{
			m_roundRestValid  = false; // re-sampled on the next cycle
			m_cycleThrowsCase = true;  // back to the normal post-shot rack

			enterState(State::Idle);

			// A shot asked for while the bolt was still open goes off the instant
			// it closes. Buffered rather than immediate, because the gun was not
			// loaded when the button went down.
			if (m_fireAfterCycle && m_rounds > 0)
			{
				m_fireAfterCycle = false;
				fire();
			}
			else
			{
				m_fireAfterCycle = false;
			}
		}
		break;

	case State::Reloading:
		updateReloadSounds(frame);

		if (!m_ammoCredited && frame >= static_cast<irr::f32>(m_magInFrame))
		{
			m_ammoCredited = true;
			m_rounds += drawFromReserve(m_magSize - m_rounds);
		}

		if (animEnded)
		{
			// BOTH paths draw. Crediting only at the frame trigger would hand out
			// a free magazine whenever a hitch stepped over it; crediting only at
			// the end would pay out late. m_ammoCredited is what keeps a reload
			// that hits both from being paid twice.
			if (!m_ammoCredited)
			{
				m_ammoCredited = true;
				m_rounds += drawFromReserve(m_magSize - m_rounds);
			}

			// A magazine loaded into a rifle that was run dry does not chamber
			// itself. Work the bolt — and throw nothing, because the case from
			// the last shot went out with the cycle that followed it.
			if (m_chamberEmpty && m_rounds > 0)
			{
				m_chamberEmpty    = false;
				m_cycleThrowsCase = false;
				m_roundRestValid  = false;

				enterState(State::Cycling);
				break;
			}

			m_chamberEmpty = false;

			enterState(State::Idle);
		}
		break;

	case State::Idle:
	default:
		break;
	}

	// --- Idle: input is live -------------------------------------------------

	if (m_state == State::Idle)
	{
		// Record where the stabilisation reference sits at rest. Done here rather
		// than in init() because the joints are stale while the node is hidden,
		// and once because the rest pose never changes.
		if (!stabilizationRestValid())
		{
			m_mesh.node->updateAbsolutePosition();
			m_mesh.node->animateJoints();
			captureStabilizationRest();
		}

		if (lmb && !m_firedThisPress)
		{
			m_firedThisPress = true;

			if (m_rounds > 0)
			{
				fire();
			}
			else
			{
				SoundManager::Get()->sound()->playRandomized2D(
					"content/sound/weapon/dryfire", 0.05f, 1, -1.0f, "sniper_dryfire");
			}
		}
	}
	else if (lmb && !m_firedThisPress && m_state == State::Cycling && m_rounds > 0)
	{
		// Pressed while the bolt is still running: buffer it rather than drop it.
		m_firedThisPress = true;
		m_fireAfterCycle = true;
	}

	drawScopeOverlay();

	// The crosshair is meaningless once the scope is up — the reticle in the
	// overlay is the aiming mark, and drawing both leaves two marks on screen.
	if (m_scopeBlend < m_scopeOverlayAt)
		RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
}

void Weapon_Sniper::persist()
{
	m_effects.update(Engine::Get()->getDeltaTime());
}

void Weapon_Sniper::equip()
{
	m_mesh.node->setVisible(true);
	m_modelHiddenByScope = false;

	// Consume any stale animation-end flag from before the weapon was hidden
	m_mesh.animation_call_back->hasAnimationEnded();

	m_firedThisPress = true;  // don't fire on a button already held through the switch
	m_fireAfterCycle = false;
	m_roundRestValid = false;
	// A cycle abandoned by a weapon switch must not leave the next one silent;
	// m_chamberEmpty is NOT cleared here on purpose — a rifle put away empty is
	// still empty when it comes back out.
	m_cycleThrowsCase = true;
	m_scopeBlend     = 0.0f;
	m_scopeWanted    = false;
	g_CameraFX.clearFovZoom();
	disableScopePass();
	resetViewKick();

	setMeshPartVisible(m_round, true);

	playEquipSound();

	// If the clip is missing, playAnimation leaves the current loop alone and
	// returns false — drop straight to idle rather than freezing on a stale pose.
	enterState(State::Equipping);
	if (!m_mesh.findAnimation("equip"))
		enterState(State::Idle);
}

void Weapon_Sniper::unequip()
{
	m_state = State::Idle;

	// The scope must come down with the weapon, and instantly: easing it out over
	// the next quarter second would leave the NEXT weapon zoomed while it draws.
	m_scopeBlend         = 0.0f;
	m_scopeWanted        = false;
	m_modelHiddenByScope = false;
	g_CameraFX.clearFovZoom();
	disableScopePass();

	// Put the shared sway/breathing scales back — they are members of the base
	// class, so leaving them damped would follow the player to the next weapon.
	m_swayAmount       = 0.02f;
	m_idleBreath.scale = 0.8f;

	setMeshPartVisible(m_round, true);

	m_mesh.node->setVisible(false);
}

void Weapon_Sniper::startUnequip()
{
	// Already hidden, or mid-holster: nothing to play, don't restart the clip
	if (!m_mesh.node || m_state == State::Unequipping)
		return;

	if (!m_mesh.node->isVisible() && !m_modelHiddenByScope)
		return;

	// The rifle has to be back on screen to be seen going away, and the scope has
	// to be down before the holster clip plays or the overlay hangs over it.
	m_mesh.node->setVisible(true);
	m_modelHiddenByScope = false;
	m_scopeBlend         = 0.0f;
	m_scopeWanted        = false;
	g_CameraFX.clearFovZoom();
	disableScopePass();

	m_swayAmount       = 0.02f;
	m_idleBreath.scale = 0.8f;

	m_firedThisPress = true; // block fire input during unequip
	m_fireAfterCycle = false;

	setMeshPartVisible(m_round, true);

	playUnequipSound();

	// Node stays visible until update() sees the clip end. If the clip were ever
	// missing, playAnimation() returns false and we hide instantly instead.
	if (m_mesh.findAnimation("unequip"))
		enterState(State::Unequipping);
	else
		unequip();
}

void Weapon_Sniper::idle()
{

}

void Weapon_Sniper::move()
{

}

// --- Firing ------------------------------------------------------------------

void Weapon_Sniper::fire()
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>() || !m_mesh.node)
		return;

	m_rounds--;

	// That was the last one: the cycle this chains into will throw the case and
	// find an empty magazine behind it, leaving nothing chambered. The reload
	// then owes a rack — see the Reloading state.
	if (m_rounds <= 0)
		m_chamberEmpty = true;

	// The fire clip only pulls the trigger and rocks the gun; the case is still in
	// the chamber when it ends, which is why this ALWAYS chains into the cycle.
	enterState(State::Firing);

	// Viewmodel kick. The heaviest in the pack by some way — but scaled down as
	// the scope comes up, because at 3.5x magnification a full-size kick throws
	// the target clean off screen and the player loses their own shot.
	const float kickScale = 1.0f - m_scopeBlend * 0.55f;

	addViewKick(
		irr::core::vector3df(0.0f, 0.06f, -0.20f) * kickScale,
		irr::core::vector3df(9.0f * kickScale, 0.0f,
			Engine::Get()->rng()->getFloat(-2.5f, 2.5f) * kickScale));

	auto& camera = player.getComponent<CameraComponent>();

	// Force full hierarchy update so bone world positions are current
	camera.camera->updateAbsolutePosition();
	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();

	// Same point the flash is drawn at, by construction — one definition of
	// "where the muzzle is" rather than two guesses that can drift apart.
	const irr::core::vector3df muzzlePos = m_effects.muzzleWorldPosition();

	// Camera basis for the spread offsets
	irr::core::vector3df target    = camera.camera->getTarget();
	irr::core::vector3df cameraPos = camera.camera->getAbsolutePosition();
	irr::core::vector3df forward   = (target - cameraPos).normalize();

	irr::core::vector3df up(0, 1, 0);
	irr::core::vector3df right = forward.crossProduct(up).normalize();
	irr::core::vector3df down  = right.crossProduct(forward).normalize();

	// Converge on the crosshair aim point — the muzzle sits below-right of screen
	// centre, so a plain camera-forward ray would land offset from the crosshair
	irr::core::vector3df direction = getAimDirection(muzzlePos);

	// Scoped shots are near-exact; hip shots are not. Interpolated on the same
	// blend as everything else, so a shot taken half way into the sight picture
	// gets half the benefit — no threshold to game.
	const float spread =
		m_spread * (1.0f - m_scopeBlend * (1.0f - m_scopedSpreadScale));

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
					m_effects.impact(raycastResult.point, raycastResult.normal);
			}
		}
		else if (RenderManager::isWorldGeometryNode(raycastResult.node))
		{
			// Brush chunks / props carry no ECS id — solid surface hit, nothing to damage
			m_effects.impact(raycastResult.point, raycastResult.normal);
		}
	}

	// Tracer on every shot — at this range the trace is what tells the player
	// where the round actually went.
	const irr::core::vector3df tracerEnd = (raycastResult.hit && raycastResult.node) ?
		raycastResult.point : (muzzlePos + direction * 1000.0f);
	m_effects.spawnTracer(muzzlePos, tracerEnd);

	m_effects.muzzleFlash();

	// Camera recoil, damped the same way the viewmodel kick is: the whole point
	// of the scope is that the player can still see what they hit.
	const float recoilYaw = Engine::Get()->rng()->getFloat(-0.5f, 0.5f);
	g_CameraFX.addRecoil(-4.2f * kickScale, recoilYaw * kickScale);
	g_CameraFX.addFovKick(1.5f * kickScale);

	SoundManager::Get()->sound()->playRandomized2D(
		"content/sound/weapon/rifle/fire", 0.04f, 2, 0.85f, "sniper_fire");
}

void Weapon_Sniper::reload()
{
	if (m_state != State::Idle)
		return;

	if (m_rounds >= m_magSize)
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

	enterState(State::Reloading);
}

// --- Frame-triggered audio ---------------------------------------------------

// Each cue fires once, early by its own measured lead, so the transient lands on
// the visual event instead of trailing it.
void Weapon_Sniper::updateBoltSounds(float frame, int liftFrame, int homeFrame)
{
	const int f = static_cast<int>(frame);
	const int lead = soundLeadFrames(m_boltLeadSec);

	// Handle turns up and the bolt is drawn back
	if (!m_boltLiftPlayed && f >= liftFrame - lead)
	{
		m_boltLiftPlayed = true;

		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/cock_rifle", 0.07f, 2, -1.0f, "sniper_bolt");
	}

	// Bolt runs home and the handle turns back down
	if (!m_boltHomePlayed && f >= homeFrame - lead)
	{
		m_boltHomePlayed = true;

		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/cock_rifle", 0.07f, 2, -1.0f, "sniper_bolt");
	}
}

void Weapon_Sniper::updateReloadSounds(float frame)
{
	const int f = static_cast<int>(frame);

	// Magazine breaks free at f70-72 and drops away
	if (!m_magOutPlayed && f >= m_magOutFrame - soundLeadFrames(m_removeMagLeadSec))
	{
		m_magOutPlayed = true;

		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/remove_mag", 0.05f, 1, -1.0f, "sniper_mag");
	}

	// Fresh magazine home at f89 — the same frame the ammo is credited
	if (!m_magInPlayed && f >= m_magInFrame - soundLeadFrames(m_insertMagLeadSec))
	{
		m_magInPlayed = true;

		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/insert_mag", 0.05f, 1, -1.0f, "sniper_mag");
	}
}

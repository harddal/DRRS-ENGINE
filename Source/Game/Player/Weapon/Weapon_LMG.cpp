#include "Weapon_LMG.h"

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

void Weapon_LMG::precache()
{
	ParticleManager::Get()->precache("spark", _asset_psys("spark"));

	// equip/unequip are shared across weapons and preloaded by WeaponController.
	//
	// There is no lmg_*.wav set yet, so this borrows the heavy rifle's entire
	// set — same report, same magazine and charging-handle cues — which is the
	// closest match in the library and keeps the two guns sounding like they
	// come from the same armoury.
	//
	// The minigun's fire1-3.wav were the obvious alternative and are the wrong
	// choice: they peak at 18% of full scale against rifle/fire.wav's 90%, so
	// once attenuated enough to stack at this cadence they are barely audible.
	//
	// Every path below resolves through playRandomized2D, which scans for
	// numbered variants and falls back to the bare name — so dropping
	// lmg_fire1.wav/lmg_fire2.wav next to an lmg_fire.wav upgrades the gun to a
	// proper variant set with no code change beyond the base path.
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/dryfire.wav",     true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/cock_rifle.wav",  true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/remove_mag.wav",  true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/insert_mag.wav",  true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/rifle/fire.wav",  true);
}

void Weapon_LMG::init()
{
	m_descriptor.name = "Player_Weapon_LMG";
	m_descriptor.id = _entity_null_value;

	m_weapon_type = WEAP_LMG;

	// lmg_animated.glb carries the same arms rig as every other weapon in this
	// pack — identical joint names, identical 'arms' root at (0, 2.945, -17.671)
	// — so the shotgun's viewmodel transform is the right starting point for
	// another long two-handed gun. Pulled slightly further back than the
	// shotgun's because this barrel is 137 model units against its 116. Tune
	// with the viewmodel debug UI (F2) in WeaponController, not by guessing here.
	m_viewPositionOffset = irr::core::vector3df(0.1100f, -0.1800f, 0.1350f);
	m_viewRotationOffset = irr::core::vector3df(0.0f, 180.0f, 0.0f);
	m_viewScaleOffset    = irr::core::vector3df(0.01f, 0.01f, 0.01f);

	m_mesh.mesh = _asset_glb("player/weapon/lmg_animated");

	m_mesh.trimesh = RenderManager::Get()->loadMesh(m_mesh.mesh);

	// Swap in the stand-in BEFORE the node is created — creating a node in the
	// failure branch and again below orphans the first one.
	const bool usingStandIn = (m_mesh.trimesh == nullptr);
	if (usingStandIn)
	{
		spdlog::warn("Weapon_LMG::init(): failed to load mesh \"{}\", stand-in mesh loaded", m_mesh.mesh);
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
	// take (0-14.1s = frames 0-423 at 30 fps) with every clip concatenated and a
	// 2-frame hold at the shared rest pose between them. The rest pose is the
	// 'LMG' root at (0, -0.318, -0.166) with zero rotation, and it recurs at
	// frames 0, 9/10, 180/181, 204/205, 233/234, 252/253 and 423; each range was
	// then identified from the gross motion of that root and of the named parts:
	//   0-9      trigger f1-9, 'bolt' snaps back f1, 'ejector' f2-6,
	//            gun recoils Z -2.57 and recovers                  -> fire
	//   10-180   'loader' back f21-24, 'lid_2' opens 100 deg f49-51,
	//            'mag' off f65-78 and home f92, cover shuts f126-8,
	//            latch closes f162-5                               -> reload
	//   181-191  gun swings down and right, yaw +58, DROPPING to
	//            Y -13.1, out of frame                             -> unequip
	//   192-204  the same arc returning to rest                    -> equip
	//   205-233  a 0.9-unit dip and return, no rotation at all     -> a gentle
	//            idle sway, unused: idle is pinned to 205 and the
	//            hold-steady motion comes from enableIdleBreathing()
	//   234-252  gun snaps to a rolled pose (-74.8) in TWO frames
	//            and takes sixteen to recover                    -> melee bash
	//   253-423  the belt change again, with the gun dipped much
	//            lower through the swap                            -> unused
	//
	// 181-204 is ONE authored take holding both transitions: out to the extreme at
	// f191/192, then back. It is split at that apex, so unequip plays the first
	// half and equip the second.
	//
	// 234-252 was bound as the equip at first and is NOT a draw. Every bash in
	// this pack has the same signature and it is easy to mistake: the pose is
	// reached within two or three frames and then recovered over fifteen or more,
	// with a large NEGATIVE roll. A holster or draw is symmetric and takes the
	// weapon DOWN — Y -13.1 here against the bash's +3.5.
	//
	// 253-423 is the same belt change re-timed, and it drops the whole gun to
	// Y -12.4 during the swap — far enough that the receiver leaves the bottom of
	// the screen. 10-180 is the tamer take and is the one used.
	//
	// Looping clips MUST be flagged loop=true — a non-looping clip re-armed from
	// the end callback holds its last frame for one tick every cycle, which is a
	// visible hitch.
	m_mesh.animationList.emplace_back(sAnimationData("fire",    0,   9,   false));
	m_mesh.animationList.emplace_back(sAnimationData("reload",  10,  180, false));
	m_mesh.animationList.emplace_back(sAnimationData("unequip", 181, 191, false));
	m_mesh.animationList.emplace_back(sAnimationData("equip",   192, 204, false));
	m_mesh.animationList.emplace_back(sAnimationData("idle",    205, 205, true));
	// Authored but not bound: the mirror draw and the sway loop described above
	m_mesh.animationList.emplace_back(sAnimationData("melee",     234, 252, false));
	m_mesh.animationList.emplace_back(sAnimationData("idle_sway", 205, 233, true));

	// Both glTF backends normalise keyframe times to 30 fps Irrlicht frames, so
	// the viewmodel must play at 30 to run at its authored speed.
	m_mesh.fps = 30;
	m_mesh.node->setAnimationSpeed(static_cast<irr::f32>(m_mesh.fps));

	playAnimation("idle"); // safe default until equip() runs

	// The idle clip is pinned to a single frame, so the hold-steady motion comes
	// from updateWeaponSway() instead. Below the shotgun's: this thing is braced
	// against the body with both hands, and a wide idle would make it read light.
	enableIdleBreathing(0.9f);

	m_mesh.node->setJointMode(irr::scene::EJUOR_READ);

	m_mesh.animation_call_back = std::make_shared<AnimationCallback>();
	m_mesh.node->setAnimationEndCallback(m_mesh.animation_call_back.get());

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

	// Must come AFTER the material assignment above — it caches each link's real
	// material type so setMeshPartVisible() has something to restore.
	resolveBelt();

	// Reference point for reload stabilisation: the bore line at roughly the
	// gun's mid-length. 'base' spans Z -48.97 to +88.02 and 4.77 is the bore
	// height already measured for the muzzle, so this splits the rotation error
	// between muzzle and stock instead of pinning one end and swinging the other.
	// Z is negated because GltfImport's right-to-left-handed conversion negates it.
	enableClipStabilization("base", irr::core::vector3df(0.0f, 4.77f, -19.5f));
	setStabilizationTuneAmount(m_reloadStabilize);

	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");

	if (!player.isValid())
	{
		spdlog::error("In function Weapon_LMG::init() -> getEntityByName(\"player\") : Entity 'player' does not exist");

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
		spdlog::error("In function Weapon_LMG::init() -> player.getComponent<CameraComponent>() : Entity 'player' does not have specified component");
	}

	RenderManager::Get()->registerViewmodelNode(m_mesh.node);
	m_mesh.node->setVisible(false);

	m_ammo = m_ammoCapacity;

	// The brass is spawned by ejectSpentCase() from the animated 'ejector' — the
	// part the fire clip drives through the extraction stroke — rather than from
	// a port derived from the camera basis, so shellEjectJoint/shellEjectOffset
	// stay unset and ejectShell() is never called. Same approach as the revolver
	// and the heavy rifle.
	m_ejector = m_mesh.node->getJointNode("ejector");
	if (!m_ejector)
		spdlog::warn("Weapon_LMG: 'ejector' joint not found — casings will not eject");

	// The ammo box, watched during the belt change so the belt can be taken off
	// screen for exactly as long as the box is away from the gun. Without it a
	// partial belt flies off as a detached fragment — see updateBoxOffSeat().
	m_magBone = m_mesh.node->getJointNode("mag");
	if (!m_magBone)
		spdlog::warn("Weapon_LMG: 'mag' joint not found — a partial belt will break during the reload");

	m_crosshair = RenderManager::Get()->driver()->getTexture("content/texture/ui/crosshair/crosshair038.png");

	// Character sheet: hot, wide flash and a heavy stream of brass. The pools are
	// sized for the cadence, not for the magazine — at 600 rpm a 16-slot casing
	// pool recycles every 1.6 s, which is shorter than a casing's own settle
	// time, so it has to be much bigger here than on a single-shot weapon.
	//
	// This .glb has no FIRESPOT empty, but it does not need one: 'base' is the
	// receiver-and-barrel joint, so anything parented to it inherits the recoil,
	// the belt change and the holster for free. The offset is the bore centre at
	// the muzzle face — the 82 verts within 1 unit of the barrel's far end run
	// from Y 2.14 to 7.51 and X -2.68 to 2.70 with no gap, i.e. one ring of
	// radius ~2.7 centred at Y 4.77 — and GltfImport's handedness conversion
	// negates Z. Being joint-local, it is in model units and needs no change if
	// the viewmodel scale is retuned.
	WeaponEffectsDesc fx;
	fx.muzzleJointName   = "base";
	fx.muzzleJointOffset = irr::core::vector3df(0.0f, 4.77f, -88.02f);
	fx.flashColor        = irr::video::SColor(255, 255, 200, 130);
	fx.flashSize         = 0.7f;
	fx.flashSizeVariance = 0.35f;
	fx.flashDuration     = 45.0f; // shorter than the cadence, or the flash never blinks
	fx.lightColor        = irr::video::SColorf(1.0f, 0.72f, 0.28f);
	fx.lightRadius       = 5.0f;
	fx.tracerFrequency   = 3;     // the classic every-fifth-round belt, near enough
	fx.tracerWidth       = 0.12f;
	fx.tracerPoolSize    = 24;
	fx.shellMesh         = "content/mesh/prop/shells/shellmedium.obj";
	// A 100-round belt at 600 rpm takes ten seconds to empty, which is EXACTLY
	// the casing lifetime — so a full belt has every one of its cases still on
	// the ground when the last round goes. 48 covered under half a belt; this
	// covers a whole one with a little headroom. Same lifetime-not-magazine
	// reasoning as the dual SMGs.
	fx.shellPoolSize     = 112;
	fx.impactParticle    = "spark";
	m_effects.init(m_mesh.node, fx);
}

void Weapon_LMG::destroy()
{
	m_effects.destroy();

	RenderManager::Get()->unregisterViewmodelNode(m_mesh.node);
	m_mesh.node->remove();

	WorldManager::Get()->freeEntityID(m_descriptor.id);
}

// --- Belt -------------------------------------------------------------------

// Map each of the ten belt rounds to the mesh buffers that draw it.
//
// The rounds are not skin bones, but they ARE addressable: GltfImport creates an
// Irrlicht SJoint for EVERY glTF node, and for a non-skinned node it hands the
// node's geometry to that joint as buffer indices in SJoint::AttachedMeshes,
// which CSkinnedMesh then drives by writing the joint's animated matrix into
// SSkinMeshBuffer::Transformation. So the lookup is joint-name -> AttachedMeshes
// -> buffer index, NOT getJointNode()/setVisible(): the rounds are buffers of
// the viewmodel mesh, not child scene nodes. resolveMeshPart() does exactly that.
//
// The prefix has to carry the digit ("bullet3", not "bullet") or every link
// would resolve to bullet0, which is the first match in joint order.
void Weapon_LMG::resolveBelt()
{
	for (int i = 0; i < m_beltLinks; ++i)
	{
		const std::string prefix = "bullet" + std::to_string(i);

		if (!resolveMeshPart(prefix.c_str(), m_belt[i]))
			spdlog::warn("Weapon_LMG: belt link '{}' not found — it will stay visible whatever the ammo count", prefix);
	}

	m_beltShown = -1; // nothing applied yet; force the first updateBelt() through
}

// How many of the ten links a given round count should leave on the arc.
//
// One link is one ROUND, not a fraction of the magazine. The arc physically
// holds ten rounds, and while the box still has more than that the belt feeding
// out of it is continuous — so it stays full, and the rounds the player can see
// are literally the last ten in the gun. Below ten the belt is the whole
// remaining supply, and it drains one link per shot until the feed tray is bare.
//
// A proportional mapping (a link per tenth of the magazine) was the first cut
// and it is wrong twice over: the belt would thin out while the box was still
// nearly full, and the last visible link would stand for ten rounds rather than
// for the one round that is actually about to fire.
int Weapon_LMG::beltLinksForAmmo(int ammo) const
{
	if (ammo <= 0)
		return 0;

	return ammo < m_beltLinks ? ammo : m_beltLinks;
}

// How many links to show right now: the ammo count, except across the window
// where the box is in the air and the ammo count cannot describe what the
// animation is doing.
//
// Everywhere else the belt is a pure function of m_ammo, and it is worth keeping
// it that way — that is what makes the fresh belt appear at exactly the moment
// the ammo lands, with no second counter to fall out of step. This is the one
// state the clip has that the round count has no way to express.
int Weapon_LMG::beltLinksThisFrame() const
{
	// Box off the gun: show nothing, rather than a fragment of belt with no
	// visible attachment to anything. This covers the return trip as well as the
	// throw — the strand is stretched out along the arc for both.
	if (m_isReloading && m_boxOffSeat)
		return 0;

	return beltLinksForAmmo(m_ammo);
}

// Is the ammo box away from its seat this frame?
//
// Read from the joint rather than inferred from the clip's frame number, which
// is what the first version of this did and got wrong. The rest position is
// sampled at the start of the reload because the box is provably at its seat on
// the clip's first frame — no dependence on the gun having been idle first.
//
// The comparison is in JOINT-LOCAL space, so it measures the box against the
// gun and is unaffected by the gun itself being swung around by the clip, by
// sway, or by the player turning.
void Weapon_LMG::updateBoxOffSeat()
{
	if (!m_magBone || !m_magRestValid || !m_mesh.node)
	{
		m_boxOffSeat = false;
		return;
	}

	// The viewmodel is hidden during drawAll(), so OnAnimate() skipped it and the
	// joint transforms are stale — force them, the same way fire() does before
	// reading the muzzle bone.
	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();

	m_boxOffSeat =
		(m_magBone->getPosition() - m_magRest).getLength() > m_boxOffSeatEpsilon;
}

// Applies whatever beltLinksThisFrame() asks for. Interrupting the reload drops
// the override with it, so a belt change abandoned mid-throw goes straight back
// to showing the rounds the player actually still has.
void Weapon_LMG::updateBelt()
{
	const int visible = beltLinksThisFrame();

	// Only touch materials when the count actually changes — this runs every
	// frame and a held trigger would otherwise rewrite ten materials at 600 rpm.
	if (visible == m_beltShown)
		return;

	m_beltShown = visible;

	// Rounds leave from the FEED end, so the arc empties from the box end up:
	// links [0, hidden) are the tail that the belt has already pulled through.
	const int hidden = m_beltLinks - visible;

	for (int i = 0; i < m_beltLinks; ++i)
		setMeshPartVisible(m_belt[i], i >= hidden);
}

// --- Frame loop -------------------------------------------------------------

void Weapon_LMG::update()
{
	if (!m_mesh.node || !m_mesh.node->isVisible())
		return;

	const float currentTime = Engine::Get()->getCurrentTime();
	const float dt_s        = Engine::Get()->getDeltaTime() * 0.001f;

	// Consume the animation-end signal once per frame to avoid double-reads
	const bool animEnded = m_mesh.animation_call_back->hasAnimationEnded();

	const irr::f32 frame = m_mesh.node->getFrameNr();

	// Where the ammo box is right now. Must precede updateBelt(), which asks.
	if (m_isReloading)
		updateBoxOffSeat();
	else
		m_boxOffSeat = false;

	// State: holstering. The node stays visible until the clip finishes so the
	// gun is seen dropping out of frame; isUnequipping() going false is what
	// releases WeaponController's pending switch, so the next weapon is only
	// drawn once this one is actually put away.
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
	// State: belt change. One long clip with five frame-triggered cues in it,
	// and the ammo landing at m_boxOnFrame. Deliberately NOT interruptible by a
	// fire press the way the revolver's reload is: the feed cover is standing
	// open through most of this, and a gun with its cover open cannot fire. That
	// commitment is the cost the weapon pays for its sustained fire.
	else if (m_isReloading)
	{
		if (animEnded)
		{
			// The clip runs to the rest pose well past m_boxOnFrame, so reaching
			// the end means the ammo has certainly landed — but credit it here
			// too in case a frame-rate hitch stepped clean over that frame.
			if (!m_ammoCredited)
			{
				m_ammoCredited = true;
				m_ammo += drawFromReserve(m_ammoCapacity - m_ammo);
			}

			endReload();
			playAnimation("idle");
		}
		else
		{
			if (!m_ammoCredited && frame >= static_cast<irr::f32>(m_boxOnFrame))
			{
				m_ammoCredited = true;

				// A short draw is fine here and needs no extra handling: the belt
				// arc is rendered from m_ammo by beltLinksForAmmo(), so a partial
				// box comes up as a visibly short strand on its own.
				m_ammo += drawFromReserve(m_ammoCapacity - m_ammo);
			}

			updateReloadSounds(frame);
		}
	}
	// State: fire clip -> idle. Firing is NOT blocked while this plays — fire()
	// restarts the frame loop, so the cadence limiter alone paces the rounds and
	// a held trigger reads as one continuous cycle.
	else if (m_isFireAnim)
	{
		// Brass owed by the round just fired, thrown once the extraction stroke
		// tops out. Deferred out of fire() because fire() restarts the clip at
		// frame 0, where the ejector is still home; by here it has travelled.
		//
		// Checked BEFORE the end handling so a frame-rate hitch big enough to
		// step over the whole 9-frame clip in one tick still throws the case:
		// Irrlicht clamps the frame to EndFrame when it fires the end callback,
		// which is past m_caseEjectFrame, so this catches that pass too.
		if (m_caseOwed && frame >= static_cast<irr::f32>(m_caseEjectFrame))
		{
			m_caseOwed = false;
			ejectSpentCase();
		}

		if (animEnded)
		{
			m_isFireAnim = false;
			setClipSpeed(1.0f); // the 3x is the fire clip's alone
			playAnimation("idle");
		}
	}

	// The belt is the ammo readout, and it has to be right while the gun is being
	// holstered or reloaded just as much as while it is being fired. Applied
	// AFTER the state machine so that on the frame the box seats, the ammo it
	// credits is already banked — the fresh belt then appears in the same tick,
	// inside the box, rather than one tick later once it has ridden clear of it.
	updateBelt();

	// Record where the stabilisation reference sits at rest. Done here rather
	// than in init() because the joints are stale while the node is hidden, and
	// once because the rest pose never changes.
	//
	// Gated on the gun being genuinely idle: every other state has a clip
	// driving the joints, and a reference captured mid-draw would make the rest
	// pose itself look like a drift to be countered. The procedural breathing
	// running here is harmless — it moves the NODE, and the node's own transform
	// cancels out of the node-local reference point.
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

	// A live trigger: held, with the gun able to answer it. Kept as its own name
	// because the bloom below has to distinguish "not shooting" from "between
	// rounds of a burst" — at 600 rpm only about one frame in six actually fires.
	const bool triggerLive = lmbPressed
		&& m_ammo > 0
		&& !m_isReloading
		&& !m_isEquipping;

	if (triggerLive && (currentTime - m_lastFireTime) >= m_fireInterval)
	{
		m_lastFireTime = currentTime;
		fire();
	}
	else if (lmbPressed && !m_dryFiredThisPress && !m_isReloading && !m_isEquipping && m_ammo <= 0)
	{
		// Empty belt: one click per press, not one per cadence tick. An open-bolt
		// gun run dry does nothing at all when you squeeze it again, so a
		// repeating click on a held trigger would be both wrong and maddening.
		// Deliberately does NOT auto-reload — the player has to press reload, so
		// running the belt dry is a real commitment rather than a free pause.
		m_dryFiredThisPress = true;

		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/dryfire", 0.05f, 1, -1.0f, "lmg_dryfire");
	}

	if (!triggerLive)
	{
		// Bloom recovers only while the trigger is off, so a player who taps
		// keeps a tight cone and one who leans on it does not. Recovery is in
		// real time rather than per round for the same reason.
		//
		// This is gated on the TRIGGER, not on whether a round went out this
		// frame: gating it on the latter would decay bloom on the five frames in
		// six that fall between rounds, which is more than it gains per round —
		// held fire would then get TIGHTER the longer it was held.
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

void Weapon_LMG::persist()
{
	m_effects.update(Engine::Get()->getDeltaTime());
}

void Weapon_LMG::equip()
{
	m_mesh.node->setVisible(true);

	// Consume any stale animation-end flag from before the weapon was hidden
	m_mesh.animation_call_back->hasAnimationEnded();

	m_isFireAnim        = false;
	m_caseOwed          = false;
	m_dryFiredThisPress = false;
	m_bloom             = 0.0f;
	resetViewKick();

	playEquipSound();

	// Recover from a belt change cut short by a weapon switch — otherwise the
	// clip speed and the stabilisation offset stay applied for the rest of the
	// weapon's life. Whatever the ammo count is at that point is what the belt
	// shows, because updateBelt() derives it fresh every frame.
	endReload();

	// If the clip is missing, playAnimation leaves the current loop alone and
	// returns false — drop straight to idle rather than freezing on a stale pose.
	m_isEquipping = playAnimation("equip");
	if (!m_isEquipping)
		playAnimation("idle");
}

void Weapon_LMG::unequip()
{
	m_isUnequipping = false;
	m_isFireAnim    = false;
	m_isEquipping   = false;
	m_caseOwed      = false;

	endReload();

	m_mesh.node->setVisible(false);
}

void Weapon_LMG::startUnequip()
{
	// Already hidden, or mid-holster: nothing to play, don't restart the clip
	if (!m_mesh.node || !m_mesh.node->isVisible() || m_isUnequipping)
		return;

	m_isFireAnim  = false;
	m_isEquipping = false;
	m_caseOwed    = false;

	playUnequipSound();

	// Holstering mid-swap keeps whatever ammo had already been credited and
	// drops the rest of the belt change
	endReload();

	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag

	// Node stays visible until update() sees the clip end. If the clip were ever
	// missing, playAnimation() returns false and we hide instantly instead.
	m_isUnequipping = playAnimation("unequip");
	if (!m_isUnequipping)
		unequip();
}

void Weapon_LMG::idle()
{

}

void Weapon_LMG::move()
{

}

// --- Firing -----------------------------------------------------------------

void Weapon_LMG::fire()
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>() || !m_mesh.node)
		return;

	m_ammo--;

	// Restarts if already playing, which at this cadence is every time — the
	// clip is timed so that restart lands exactly where the cycle began.
	setClipSpeed(m_fireSpeed);
	playAnimation("fire");
	m_isFireAnim = true;
	m_caseOwed   = true;

	// Small per-round kick, layered on top of the clip's own recoil and recovered
	// by the spring in updateWeaponSway(). An order of magnitude below the
	// revolver's: at 600 rpm the springs never fully recover between rounds, so
	// a revolver-sized kick here stacks into an unusable climb within half a second.
	addViewKick(
		irr::core::vector3df(0.0f, 0.010f, -0.030f),
		irr::core::vector3df(1.6f, 0.0f,
			Engine::Get()->rng()->getFloat(-0.6f, 0.6f)));

	auto& camera = player.getComponent<CameraComponent>();

	// Force full hierarchy update so bone world positions are current
	camera.camera->updateAbsolutePosition();
	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();

	// Same point the flash is drawn at, by construction — one definition of
	// "where the muzzle is" rather than two guesses that can drift apart.
	const irr::core::vector3df muzzlePos = m_effects.muzzleWorldPosition();

	// Camera basis for the spread offsets
	irr::core::vector3df target   = camera.camera->getTarget();
	irr::core::vector3df cameraPos = camera.camera->getAbsolutePosition();
	irr::core::vector3df forward  = (target - cameraPos).normalize();

	irr::core::vector3df up(0, 1, 0);
	irr::core::vector3df right = forward.crossProduct(up).normalize();
	irr::core::vector3df down  = right.crossProduct(forward).normalize();

	// Converge on the crosshair aim point — the muzzle sits below-right of screen
	// centre, so a plain camera-forward ray would land offset from the crosshair
	irr::core::vector3df direction = getAimDirection(muzzlePos);

	// Cone widened by however hard the trigger has been leaned on. Applied
	// BEFORE the bloom is advanced, so the first round of a burst is the tight one.
	const float spread = m_spreadMin + (m_spreadMax - m_spreadMin) * m_bloom;

	const float spreadRight = Engine::Get()->rng()->getFloat(-spread, spread);
	const float spreadDown  = Engine::Get()->rng()->getFloat(-spread, spread);
	direction = (direction + right * spreadRight + down * spreadDown).normalize();

	m_bloom += m_bloomPerShot;
	if (m_bloom > 1.0f)
		m_bloom = 1.0f;

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
				// Damage through the gameplay chokepoint; drives hitmarker/kill feedback.
				// The context is what lets GoreManager put blood at the wound and throw
				// splatter downrange, instead of spraying from the entity's centre.
				registerHitFeedback(
					WorldManager::Get()->gameplaySystem()->damageEntity(
						hitDescriptor.id, m_damage, DAMAGE_TYPE::DEFAULT,
						DamageContext::fromImpact(raycastResult.point, raycastResult.normal, direction)));

				// Sparks and a bullet hole are for hard surfaces. Anything carrying a
				// damage receiver is flesh as far as feedback goes, and GoreManager has
				// already covered it — this call used to spark off zombies.
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

	// Tracer segment toward the hit point (module fires every Nth round)
	const irr::core::vector3df tracerEnd = (raycastResult.hit && raycastResult.node) ?
		raycastResult.point : (muzzlePos + direction * 1000.0f);
	m_effects.spawnTracer(muzzlePos, tracerEnd);

	m_effects.muzzleFlash();

	// Light camera recoil per round. Same reasoning as the view kick above: it is
	// the ACCUMULATION over a burst that has to feel heavy, not any one round.
	const float recoilYaw = Engine::Get()->rng()->getFloat(-0.25f, 0.25f);
	g_CameraFX.addRecoil(-0.55f, recoilYaw);

	// The heavy rifle's report, pooled for a cadence it was not recorded at.
	// rifle/fire.wav stays audible for about 0.4 s, so at 100 ms between rounds
	// four voices are always live and a fifth is arriving; maxConcurrent 5 lets
	// each tail run out instead of being cut off mid-decay, and 0.32 keeps the
	// nominal sum near the 1.8 the heavy rifle already runs at (3 x 0.6) rather
	// than at the 3.0 five voices at full volume would reach.
	//
	// The jitter matters more here than on a semi-auto: without it, sixty
	// identical samples a tenth of a second apart phase into a single tone.
	SoundManager::Get()->sound()->playRandomized2D(
		"content/sound/weapon/rifle/fire", 0.06f, 5, 0.32f, "lmg_fire");
}

// Hand the spent case off to a pooled physics casing at the ejector's real
// position, orientation and a velocity taken from the gun's own basis — so brass
// leaves correctly whichever way the player happens to be facing.
void Weapon_LMG::ejectSpentCase()
{
	if (!m_ejector || !m_mesh.node)
		return;

	// The viewmodel is hidden during drawAll(), so OnAnimate() skipped it and the
	// joint transforms are stale — force them, the same way fire() does before
	// reading the muzzle bone.
	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();

	m_ejector->updateAbsolutePosition();
	const irr::core::matrix4 world = m_ejector->getAbsoluteTransformation();

	irr::core::vector3df port = m_ejectPortOffset;
	world.transformVect(port);

	// Away-from-port and up, from the GUN's basis rather than the camera's. -X to
	// match the port side: with the viewmodel's 180 degree yaw that reads as
	// screen-right.
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
	const float speed = 320.0f * unit;

	const irr::core::vector3df velocity =
		axisRight * speed * Engine::Get()->rng()->getFloat(0.8f, 1.2f) +
		axisUp    * speed * Engine::Get()->rng()->getFloat(0.25f, 0.55f);

	// Turn the casing end for end. Taken straight from the ejector it flies
	// mouth-first back at the camera. Composed as a LOCAL flip — the right
	// operand applies first under Irrlicht's operator* — not by adding 180 to the
	// Euler Y, which would yaw it in the parent frame and fall apart the moment
	// the gun is pitched or rolled.
	irr::core::matrix4 flip;
	flip.setRotationDegrees(irr::core::vector3df(0.0f, 180.0f, 0.0f));
	const irr::core::matrix4 oriented = world * flip;

	// Size the casing off the model's OWN belt round rather than a tuned
	// constant, so it matches the calibre the gun is drawn feeding and stays
	// right if the viewmodel scale changes. Rank-matched per axis because
	// shellmedium.obj is 3:1 where the round is 5.5:1 — one uniform ratio off the
	// long axis would leave the case visibly too fat, and bulk goes as the square
	// of the diameter. m_belt[9] is used purely as a size reference; hiding it
	// does not affect the geometry this reads.
	m_effects.spawnShellAt(
		port,
		oriented.getRotationDegrees(),
		velocity,
		matchPartScale(m_belt[m_beltLinks - 1], m_effects.shellMeshExtent()));
}

// --- Belt change ------------------------------------------------------------

void Weapon_LMG::reload()
{
	if (m_isReloading || m_isEquipping || m_isUnequipping)
		return;

	if (m_ammo >= m_ammoCapacity)
		return; // nothing to top up — don't burn three and a half seconds on a full belt

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

	m_ammoCredited     = false;
	m_latchOpenPlayed  = false;
	m_boxOffPlayed     = false;
	m_boxOnPlayed      = false;
	m_coverClosePlayed = false;
	m_latchClosePlayed = false;

	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag

	setClipSpeed(m_reloadSpeed);
	setStabilizationAmount(stabilizationTuneAmount());

	// Sample where the ammo box sits when it is seated. Taken here because the
	// box is provably home on the clip's first frame, which makes this the one
	// moment the rest pose can be read without assuming anything about what the
	// gun was doing beforehand.
	m_magRestValid = false;
	m_boxOffSeat   = false;

	if (m_magBone)
	{
		m_mesh.node->updateAbsolutePosition();
		m_mesh.node->animateJoints();

		m_magRest      = m_magBone->getPosition();
		m_magRestValid = true;
	}

	m_isReloading = playAnimation("reload");
	m_isFireAnim  = false;
	m_caseOwed    = false;

	if (!m_isReloading)
		endReload(); // clip missing: undo the speed and stabilisation we just set
}

// Frame-triggered belt-change audio. Each cue fires once, early by its own
// measured lead, so the transient lands on the visual event instead of trailing
// it. The pitch jitter matters more than usual here because cock_rifle.wav plays
// three times in one reload — without it the latch, the cover and the charge
// would be audibly the same click.
void Weapon_LMG::updateReloadSounds(float frame)
{
	const int f = static_cast<int>(frame);

	const int cockLead   = soundLeadFrames(m_cockLeadSec);
	const int removeLead = soundLeadFrames(m_removeMagLeadSec);
	const int insertLead = soundLeadFrames(m_insertMagLeadSec);

	// 'loader' slides back over f21-24: the feed-cover latch coming free
	if (!m_latchOpenPlayed && f >= m_latchOpenFrame - cockLead)
	{
		m_latchOpenPlayed = true;

		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/cock_rifle", 0.08f, 2, -1.0f, "lmg_action");
	}

	// 'mag' breaks free of the receiver at f65-68 and is thrown clear
	if (!m_boxOffPlayed && f >= m_boxOffFrame - removeLead)
	{
		m_boxOffPlayed = true;

		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/remove_mag", 0.05f, 1, -1.0f, "lmg_box");
	}

	// Fresh box home at f92 — the same frame the ammo is credited in update()
	if (!m_boxOnPlayed && f >= m_boxOnFrame - insertLead)
	{
		m_boxOnPlayed = true;

		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/insert_mag", 0.05f, 1, -1.0f, "lmg_box");
	}

	// 'lid_2' swings its 100 degrees shut over f126-128
	if (!m_coverClosePlayed && f >= m_coverCloseFrame - cockLead)
	{
		m_coverClosePlayed = true;

		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/cock_rifle", 0.08f, 2, -1.0f, "lmg_action");
	}

	// 'loader' returns over f162-165: latch home, gun charged
	if (!m_latchClosePlayed && f >= m_latchCloseFrame - cockLead)
	{
		m_latchClosePlayed = true;

		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/cock_rifle", 0.08f, 2, -1.0f, "lmg_action");
	}
}

// Leaves m_ammo alone: it is credited by update() on the frame the box seats, so
// this only tears down the belt change's own bookkeeping. Safe to call on a
// completed reload or on one abandoned part-way through, and every exit from a
// reload runs through here — which is what makes it the one place the sped-up
// playback and the stabilisation offset have to be put back.
void Weapon_LMG::endReload()
{
	m_isReloading      = false;
	m_boxOffSeat       = false;
	m_magRestValid     = false;
	m_ammoCredited     = false;
	m_latchOpenPlayed  = false;
	m_boxOffPlayed     = false;
	m_boxOnPlayed      = false;
	m_coverClosePlayed = false;
	m_latchClosePlayed = false;

	setClipSpeed(1.0f);
	setStabilizationAmount(0.0f);
}

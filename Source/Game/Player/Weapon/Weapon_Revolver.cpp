#include "Weapon_Revolver.h"

#include <ISkinnedMesh.h>

#include <algorithm>
#include <string>
#include <utility>

// Windows.h defines min/max as macros and this project does not use NOMINMAX,
// so std::max below would not survive an include-order change without these.
#undef min
#undef max

#include "Engine/Engine.h"
#include "Engine/Renderer/DecalManager.h"

#undef MB_RIGHT

#include "Engine/Engine.h"
#include "Utility/Utility.h"

#include "../CameraFX.h"

#undef MB_RIGHT

using namespace SPK;
using namespace SPK::IRR;

void Weapon_Revolver::precache()
{
	ParticleManager::Get()->precache("spark", _asset_psys("spark"));

	// equip/unequip are shared across weapons and preloaded by WeaponController
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/dryfire.wav",                true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/insert_shell.wav",           true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/revolver_fire1.wav",         true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/revolver_fire2.wav",         true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/revolver_cylinder_spin.wav", true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/revolver_close_cylinder.wav", true);
}

void Weapon_Revolver::init()
{
	m_descriptor.name = "Player_Weapon_Revolver";
	m_descriptor.id = _entity_null_value;

	// revolver_animated.glb carries the same arms rig as knife_animated.glb
	// (identical joint names, identical 'arms' root at 0,2.945,-17.671), so the
	// knife's viewmodel transform is the correct starting point. Tune with the
	// viewmodel debug UI in WeaponController, not by guessing here.
	m_viewPositionOffset = irr::core::vector3df(0.1250f, -0.1250f, 0.3600f);
	m_viewRotationOffset = irr::core::vector3df(0.0f, 180.0f, 0.0f);
	m_viewScaleOffset    = irr::core::vector3df(0.01f, 0.01f, 0.01f);

	m_mesh.mesh = _asset_glb("player/weapon/revolver_animated");

	m_mesh.trimesh = RenderManager::Get()->loadMesh(m_mesh.mesh);

	// Swap in the stand-in BEFORE the node is created — creating a node in the
	// failure branch and again below orphans the first one (the bug Weapon_Melee
	// already fixed; Weapon_Pistol still has it).
	const bool usingStandIn = (m_mesh.trimesh == nullptr);
	if (usingStandIn)
	{
		spdlog::warn("PlayerWeapon::init(): failed to load mesh \"{}\", stand-in mesh loaded", m_mesh.mesh);
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
	// take (0-11.3s = frames 0-339 at 30 fps) with every clip concatenated and a
	// 2-frame hold at the shared rest pose between them. Rest holds bound the
	// clips at 0, 12, 227/228, 282/283, 318/319/320 and 339; each range was then
	// identified from the gross motion of the 'revolver' root and the 'chamber'
	// (cylinder) rotation:
	//   0-12    hammer f1-2, trigger f1-10, cylinder turns 60 deg  -> fire
	//   13-227  unloader swings f21-216, bullet1..6 f29-192        -> reload
	//   229-240 gun drops Y -0.98 to -30.06, Z to -21.72, off view -> unequip
	//   240-282 back up to Y +1.23 by f248, then the cylinder spins
	//           0-60-120-180-120-60-0 twice over f252-282, settling  -> equip
	//   284-319 peak deviation 0.43 against 2.4 elsewhere, breathing -> idle
	//   321-338 gun whips left to X -9.57, holds, returns to rest    -> melee
	//
	// Holster and draw meet at frame 240, the bottom of the drop, which is why
	// they share it: the gun is fully out of frame there.
	//
	// Looping clips MUST be flagged loop=true — a non-looping clip re-armed from
	// the end callback holds its last frame for one tick every cycle, which is a
	// visible hitch (see the long note in Weapon_Melee::init()).
	//
	// Irrlicht clamps EndFrame to getFrameCount()-1, and CSkinnedMesh::
	// getFrameCount() returns the last frame INDEX, so frame 339 is unreachable —
	// hence melee ending at 338 rather than 339.
	//
	// "reload" covers the whole 13-227 take but is only played verbatim for a
	// full six-round load. A partial reload plays a computed sub-range instead
	// (open + eject + N loading swoops) and then cuts to "reload_close", because
	// setFrameLoop can only play one contiguous range and the six loading swoops
	// sit back to back in the middle of the clip. The cut is cheap: the loading
	// motion is cyclic, so the arm pose at any round's seat frame is within 0.17
	// to 0.33 of the pose at frame 193 — less than the idle's own 0.43 breathing
	// amplitude, against a 2.4 full-clip range.
	//
	// There is no walk/run clip in this asset, so move() stays empty.
	m_mesh.animationList.emplace_back(sAnimationData("fire",    0,   12,  false));
	m_mesh.animationList.emplace_back(sAnimationData("reload",  13,  227, false));
	m_mesh.animationList.emplace_back(sAnimationData("reload_close", 193, 227, false));
	m_mesh.animationList.emplace_back(sAnimationData("unequip", 229, 240, false));
	m_mesh.animationList.emplace_back(sAnimationData("equip",   240, 282, false));
	m_mesh.animationList.emplace_back(sAnimationData("idle",    284, 284, true));
	// Authored but not bound to input — a pistol-whip, if a melee slot is wanted
	m_mesh.animationList.emplace_back(sAnimationData("melee",   321, 338, false));

	// Both glTF backends normalise keyframe times to 30 fps Irrlicht frames, so
	// the viewmodel must play at 30 to run at its authored speed.
	m_mesh.fps = 30;
	m_mesh.node->setAnimationSpeed(static_cast<irr::f32>(m_mesh.fps));

	playAnimation("idle"); // safe default until equip() runs

	// The idle clip is pinned to a single frame, so the hold-steady motion comes
	// from updateWeaponSway() instead. Slightly above default: a revolver this
	// size is held out at arm's length rather than braced in against the body.
	enableIdleBreathing(1.15f);

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

	// Must come AFTER the material assignment above — it caches each round's real
	// material type so setRoundVisible() has something to restore.
	resolveCylinderRounds();

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

	m_cylinder = m_cylinderSize;

	m_crosshair = RenderManager::Get()->driver()->getTexture("content/texture/ui/crosshair/crosshair038.png");

	// Character sheet: big slow flash, every other shot tracers. The casing pool
	// exists for the reload dump, NOT for per-shot ejection — a revolver holds its
	// brass until the ejector rod pushes it out. ejectShell() is therefore never
	// called; ejectSpentCase() drives the pool through spawnShellAt() instead, so
	// shellEjectJoint/shellEjectOffset are deliberately left unset.
	// This .glb has no FIRESPOT empty, but it does not need one: 'base' is the
	// frame-and-barrel joint, so anything parented to it inherits the recoil,
	// the reload swing and the holster for free. The offset below is the bore
	// centre of the muzzle face, measured off the barrel mesh itself — the 128
	// vertices within 1.5 units of its far end average to (0, 7.99, 38.33) in
	// glTF space, and GltfImport's right-to-left-handed conversion negates Z.
	// Being joint-local, it is in model units and needs no change if the
	// viewmodel scale is retuned.
	WeaponEffectsDesc fx;
	fx.muzzleJointName   = "base";
	fx.muzzleJointOffset = irr::core::vector3df(0.0f, 7.99f, -38.33f);
	fx.flashColor          = irr::video::SColor(255, 255, 190, 120);
	fx.flashSize           = 0.55f;
	fx.flashDuration       = 70.0f;
	fx.lightColor          = irr::video::SColorf(1.0f, 0.7f, 0.25f);
	fx.lightRadius         = 4.5f;
	fx.tracerFrequency     = 2;
	fx.shellMesh           = "content/mesh/prop/shells/shellsmall.obj";
	fx.shellPoolSize       = 12; // two full cylinders in flight at once
	m_effects.init(m_mesh.node, fx);
}

void Weapon_Revolver::destroy()
{
	m_effects.destroy();

	RenderManager::Get()->unregisterViewmodelNode(m_mesh.node);
	m_mesh.node->remove();

	WorldManager::Get()->freeEntityID(m_descriptor.id);
}

void Weapon_Revolver::update()
{
	if (!m_mesh.node || !m_mesh.node->isVisible())
		return;

	float currentTime = Engine::Get()->getCurrentTime();

	// Consume animation-end signal once per frame to avoid double-reads
	bool animEnded = m_mesh.animation_call_back->hasAnimationEnded();

	// State: holstering. The node stays visible until the clip finishes so the gun
	// is seen dropping out of frame; isUnequipping() going false is what releases
	// WeaponController's pending switch, so the next weapon is only drawn after
	// this one is actually put away.
	if (m_isUnequipping)
	{
		if (animEnded)
			unequip();

		return;
	}

	// State: equip animation → idle. The draw raises the gun over f240-248 and
	// then spins the cylinder from f252, so the spin cue is frame-triggered
	// rather than played at equip() — at the start it would be ~0.4s early.
	if (m_isEquipping)
	{
		if (animEnded)
		{
			m_isEquipping = false;
			playAnimation("idle");
		}
		else if (!m_equipSpinPlayed &&
			m_mesh.node->getFrameNr() >=
				static_cast<irr::f32>(m_equipSpinFrame - soundLeadFrames(m_spinSoundLeadSec)))
		{
			m_equipSpinPlayed = true;

			SoundManager::Get()->sound()->playRandomized2D(
				"content/sound/weapon/revolver_cylinder_spin", 0.03f, 1, -1.0f, "revolver_spin");
		}
	}
	// State: reload. Phase 1 opens the cylinder, ejects and loads m_reloadRounds
	// rounds; phase 2 swings it shut. Rounds are credited one at a time as each
	// reaches its chamber, so interrupting the reload keeps whatever was already
	// loaded — see creditSeatedRounds().
	else if (m_isReloading)
	{
		if (animEnded)
		{
			if (m_reloadPhase == 1)
			{
				// Phase 1 ends on the last round's seat frame, so every round it
				// set out to load is home — credit any the frame sampling missed.
				creditSeatedRounds(static_cast<float>(m_reloadCloseStart));

				m_reloadPhase = 2;
				m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag
				setClipSpeed(m_reloadSpeed); // the close is part of the reload
				playAnimation("reload_close");
			}
			else
			{
				// Capture before endReload() clears it
				const bool fireOnClose = m_fireAfterReload;

				endReload();

				// The cylinder is shut, so an interrupted reload's shot can now go
				// off. Reaching this from the normal end of a reload with the
				// button held does the same thing, which makes a fire press during
				// the close read as buffered input rather than as a dropped one.
				if (fireOnClose && m_cylinder > 0)
				{
					m_lastFireTime = currentTime;
					fire(); // plays the fire clip itself
				}
				else
				{
					playAnimation("idle");
				}
			}
		}
		else
		{
			const irr::f32 frame = m_mesh.node->getFrameNr();
			trackSpentCases(frame);   // must precede updateReloadRounds — it supplies the hand-off velocity
			updateReloadRounds(frame);
			creditSeatedRounds(frame);
			updateReloadSounds(frame);
		}
	}
	// State: fire animation → idle. Firing is NOT blocked while this plays —
	// fire() restarts the frame loop so the cadence limiter alone paces shots.
	else if (m_isFireAnim)
	{
		if (animEnded)
		{
			m_isFireAnim = false;
			playAnimation("idle");
		}
	}

	// Single action: clear the fire lock when the mouse is released
	bool lmbPressed = InputManager::Get()->isMouseButtonPressed(MB_LEFT);
	if (!lmbPressed)
		m_firedThisPress = false;

	// Fire pressed mid-reload: bail out of loading, shut the cylinder and shoot.
	// Gated on m_cylinder > 0 because with nothing chambered the interrupt would
	// only trade a long wait for a useless one — the player would close an empty
	// gun and dry-click. That the count is meaningful mid-clip is exactly what
	// creditSeatedRounds() buys by banking each round as it seats.
	if (m_isReloading && lmbPressed && !m_firedThisPress && m_cylinder > 0)
	{
		m_firedThisPress = true; // single action: one shot per press
		interruptReloadToFire();
	}

	bool wantsFire = lmbPressed
		&& !m_firedThisPress
		&& !m_isReloading
		&& !m_isEquipping
		&& (currentTime - m_lastFireTime) >= m_minFireInterval;

	if (wantsFire)
	{
		if (m_cylinder > 0)
		{
			m_lastFireTime = currentTime;
			m_firedThisPress = true;
			fire();
		}
		else
		{
			// Empty cylinder: dry click and nothing else. Deliberately does NOT
			// auto-reload — the player has to press reload, so running the
			// cylinder dry is a real commitment rather than a free pause.
			m_firedThisPress = true;
			SoundManager::Get()->sound()->playRandomized2D(
				"content/sound/weapon/dryfire", 0.05f, 1, -1.0f, "revolver_dryfire");
		}
	}

	// Reload input is NOT handled here: WeaponController::update() already drives
	// current_weapon->reload() from the remappable "reload" action, so a local
	// hardcoded KEY_R would just ignore a rebind in input.xml. reload() is
	// idempotent, which is what makes that level-triggered action safe.

	RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
}

void Weapon_Revolver::persist()
{
	m_effects.update(Engine::Get()->getDeltaTime());
}

void Weapon_Revolver::equip()
{
	m_mesh.node->setVisible(true);

	// Consume any stale animation-end flag from before the weapon was hidden
	m_mesh.animation_call_back->hasAnimationEnded();

	m_isFireAnim = false;
	m_firedThisPress = false;
	resetViewKick();

	playEquipSound();
	m_equipSpinPlayed = false; // re-armed for this draw's cylinder-spin flourish

	// Recover from a reload cut short by a weapon switch — otherwise the rounds
	// hidden mid-clip stay hidden for the rest of the weapon's life. Rounds that
	// had already seated stay in the cylinder; only the unloaded remainder is lost.
	endReload();

	// If the clip is missing, playAnimation leaves the current loop alone and
	// returns false — drop straight to idle rather than freezing on a stale pose.
	m_isEquipping = playAnimation("equip");
	if (!m_isEquipping)
		playAnimation("idle");
}

void Weapon_Revolver::unequip()
{
	m_isUnequipping = false;
	m_isFireAnim = false;
	m_isEquipping = false;
	endReload();
	m_mesh.node->setVisible(false);
}

void Weapon_Revolver::startUnequip()
{
	// Already hidden, or mid-holster: nothing to play, don't restart the clip
	if (!m_mesh.node || !m_mesh.node->isVisible() || m_isUnequipping)
		return;

	m_isFireAnim = false;
	m_isEquipping = false;
	m_firedThisPress = true; // block fire input during unequip

	playUnequipSound();

	// Holstering mid-reload banks whatever already seated and drops the rest
	endReload();

	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag

	// Node stays visible until update() sees the clip end. If the clip were ever
	// missing, playAnimation() returns false and we hide instantly instead.
	m_isUnequipping = playAnimation("unequip");
	if (!m_isUnequipping)
		unequip();
}

// --- Cylinder rounds --------------------------------------------------------

// Map each of the six rounds to the mesh buffers that draw it.
//
// The rounds are not skin bones, but they ARE addressable: GltfImport creates an
// Irrlicht SJoint for EVERY glTF node (createNodeJoint), and for a non-skinned
// node it hands the node's geometry to that joint as buffer indices in
// SJoint::AttachedMeshes, which CSkinnedMesh then drives by writing the joint's
// animated matrix into SSkinMeshBuffer::Transformation. So the lookup is
// joint-name -> AttachedMeshes -> buffer index, NOT getJointNode()/setVisible():
// the rounds are buffers of the viewmodel mesh, not child scene nodes.
void Weapon_Revolver::resolveCylinderRounds()
{
	for (int i = 0; i < m_roundCount; ++i)
	{
		m_rounds[i].buffers.clear();
		m_rounds[i].visible = true;
		m_rounds[i].bone = nullptr;
		m_rounds[i].hasLastWorldPos = false;
	}

	if (!m_mesh.node)
		return;

	irr::scene::IAnimatedMesh* animated = m_mesh.node->getMesh();
	if (!animated || animated->getMeshType() != irr::scene::EAMT_SKINNED)
	{
		spdlog::warn("Weapon_Revolver: viewmodel mesh is not skinned — cylinder rounds cannot be hidden during the reload");
		return;
	}

	auto* skinned = static_cast<irr::scene::ISkinnedMesh*>(animated);
	const irr::u32 bufferCount = static_cast<irr::u32>(m_mesh.node->getMaterialCount());

	// The mesh nodes are named "bullet<N>_revolver_0"; match on the "bullet<N>"
	// prefix so a re-export that renames the leaf still resolves.
	const irr::core::array<irr::scene::ISkinnedMesh::SJoint*>& joints = skinned->getAllJoints();
	for (irr::u32 j = 0; j < joints.size(); ++j)
	{
		const irr::scene::ISkinnedMesh::SJoint* joint = joints[j];
		if (!joint || joint->AttachedMeshes.empty())
			continue;

		const std::string name = joint->Name.c_str();
		for (int i = 0; i < m_roundCount; ++i)
		{
			const std::string prefix = "bullet" + std::to_string(i + 1);
			if (name.compare(0, prefix.size(), prefix) != 0)
				continue;

			for (irr::u32 a = 0; a < joint->AttachedMeshes.size(); ++a)
			{
				const irr::u32 buffer = joint->AttachedMeshes[a];
				if (buffer < bufferCount)
					m_rounds[i].buffers.push_back(buffer);
			}

			// Same joint, addressed as a scene node. It draws nothing — the
			// geometry is in the buffers above — but its absolute transform
			// tracks the animated round, which is what a handed-off casing
			// needs in order to appear exactly where the mesh vanished.
			m_rounds[i].bone = m_mesh.node->getJointNode(joint->Name.c_str());
			break;
		}
	}

	for (int i = 0; i < m_roundCount; ++i)
	{
		if (m_rounds[i].buffers.empty())
		{
			spdlog::warn("Weapon_Revolver: no mesh buffer found for cylinder round {} — it will stay visible through the reload", i + 1);
			continue;
		}

		m_rounds[i].material = m_mesh.node->getMaterial(m_rounds[i].buffers.front()).MaterialType;
	}
}

void Weapon_Revolver::setRoundVisible(int index, bool visible)
{
	if (index < 0 || index >= m_roundCount || !m_mesh.node)
		return;

	CylinderRound& round = m_rounds[index];
	if (round.buffers.empty() || round.visible == visible)
		return;

	// Irrlicht has no per-mesh-buffer visibility flag, so this flips the material
	// type instead. CAnimatedMeshSceneNode::render() draws a buffer only when its
	// material's transparency matches the current pass, and viewmodels are drawn
	// by a bare render() call after drawAll() has finished — where the render pass
	// is ESNRP_NONE, so transparent buffers are skipped outright. That makes the
	// swap an exact hide, and it reverses for free.
	for (size_t b = 0; b < round.buffers.size(); ++b)
	{
		m_mesh.node->getMaterial(round.buffers[b]).MaterialType =
			visible ? round.material : irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL;
	}

	round.visible = visible;
}

void Weapon_Revolver::setAllRoundsVisible(bool visible)
{
	for (int i = 0; i < m_roundCount; ++i)
		setRoundVisible(i, visible);
}

// Spent cases ride the ejector out to full extension at frame 34 and then slide
// straight back into the chambers, because the clip reuses the same six meshes as
// the fresh rounds — which are therefore all seated before the first loading
// swoop even starts. Hiding each spent case at the top of the ejector push and
// bringing it back when its own swoop begins is what makes the cases read as
// dropping away and the chamber read as empty until a round is actually seated.
//
// Only rounds [0, m_reloadRounds) are spent. The rest are still live and stay
// visible the whole way through: the ejector pushing them out and letting them
// drop back is correct behaviour for an unfired round, and it is the one part of
// this clip the artist animated accurately.
void Weapon_Revolver::updateReloadRounds(float frame)
{
	const int f = static_cast<int>(frame);

	for (int i = 0; i < m_reloadRounds && i < m_roundCount; ++i)
	{
		const int insertFrame = m_firstInsertFrame + m_insertStride * i;
		const bool visible = (f <= m_ejectFrame) || (f >= insertFrame);

		// Hand the case off to a physics casing on the exact frame the animated
		// mesh disappears — same position, same orientation, same velocity — so
		// there is no seam to see. The animated round travels barely a couple of
		// centimetres before it is hidden, which is precisely why the hand-off
		// has to be this tight.
		if (!visible && m_rounds[i].visible)
			ejectSpentCase(i);

		setRoundVisible(i, visible);
	}
}

// Sample spent-round world positions across the ejector push so ejectSpentCase()
// can inherit the animation's own velocity rather than invent a direction. Only
// runs for the handful of frames between m_ejectTrackFrame and m_ejectFrame.
void Weapon_Revolver::trackSpentCases(float frame)
{
	const int f = static_cast<int>(frame);
	if (!m_mesh.node || f < m_ejectTrackFrame || f > m_ejectFrame)
		return;

	// The viewmodel is hidden during drawAll(), so OnAnimate() skipped it and the
	// joint transforms are stale — force them, the same way fire() does before
	// reading the muzzle bone.
	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();

	const float dt = Engine::Get()->getDeltaTime() * 0.001f;

	for (int i = 0; i < m_reloadRounds && i < m_roundCount; ++i)
	{
		CylinderRound& round = m_rounds[i];
		if (!round.bone)
			continue;

		round.bone->updateAbsolutePosition();
		const irr::core::vector3df pos = round.bone->getAbsolutePosition();

		round.velocity = (round.hasLastWorldPos && dt > 0.0f)
			? (pos - round.lastWorldPos) / dt
			: irr::core::vector3df(0.0f, 0.0f, 0.0f);

		// Peak speed over the push, not the speed at the hide frame. The rounds
		// are near the top of their travel when they vanish, so the instantaneous
		// value there is almost zero — the peak is what says how hard the ejector
		// was actually throwing them, and scatter derived from it stays correct at
		// any world scale.
		round.peakSpeed = std::max(round.peakSpeed, round.velocity.getLength());

		round.lastWorldPos    = pos;
		round.hasLastWorldPos = true;
	}
}

void Weapon_Revolver::ejectSpentCase(int index)
{
	if (index < 0 || index >= m_roundCount || !m_mesh.node)
		return;

	CylinderRound& round = m_rounds[index];
	if (!round.bone)
		return;

	round.bone->updateAbsolutePosition();
	const irr::core::matrix4 world = round.bone->getAbsoluteTransformation();

	// Scatter so six cases don't drop as one rigid clump, sized as a fraction of
	// how hard the ejector was throwing them rather than as an absolute — the
	// latter would be meaningless without knowing this world's unit scale.
	const float spread = round.peakSpeed * 0.25f;
	const irr::core::vector3df scatter(
		Engine::Get()->rng()->getFloat(-spread, spread),
		Engine::Get()->rng()->getFloat(-spread * 0.5f, spread * 0.5f),
		Engine::Get()->rng()->getFloat(-spread, spread));

	m_effects.spawnShellAt(
		world.getTranslation(),
		world.getRotationDegrees(),
		round.velocity + scatter,
		spentCaseScale(index));

	round.hasLastWorldPos = false;
	round.peakSpeed = 0.0f;
}

// Size the casing to the round it replaces, PER AXIS.
//
// A single uniform ratio off the longest axis does not work: the round is a
// slender 1.46 x 1.46 x 5.79 (4:1) and shellsmall.obj is a stubby 0.02 x 0.02 x
// 0.041 (2:1), so matching the lengths leaves the casing twice too fat — and
// since bulk goes as the square of the diameter, that alone reads as "much too
// large". Each axis therefore gets its own ratio.
//
// The axes are matched by RANK (smallest to smallest, largest to largest) rather
// than by index, because the glTF -> Irrlicht conversion is free to permute axes;
// rank-matching survives that, an X-to-X mapping would not.
irr::core::vector3df Weapon_Revolver::spentCaseScale(int index) const
{
	const irr::core::vector3df fallback(m_viewScaleOffset);

	if (index < 0 || index >= m_roundCount || !m_mesh.node)
		return fallback;

	const CylinderRound& round = m_rounds[index];
	if (!round.bone || round.buffers.empty())
		return fallback;

	irr::scene::IMesh* mesh = m_mesh.node->getMesh();
	if (!mesh || round.buffers.front() >= mesh->getMeshBufferCount())
		return fallback;

	const irr::core::vector3df roundExtent =
		mesh->getMeshBuffer(round.buffers.front())->getBoundingBox().getExtent();
	const irr::core::vector3df worldScale =
		round.bone->getAbsoluteTransformation().getScale();
	const irr::core::vector3df shellExtent = m_effects.shellMeshExtent();

	// Round size in world units, and the casing mesh's own size, as sortable axes
	float roundAxes[3] = {
		roundExtent.X * worldScale.X,
		roundExtent.Y * worldScale.Y,
		roundExtent.Z * worldScale.Z };
	float shellAxes[3] = { shellExtent.X, shellExtent.Y, shellExtent.Z };

	// Rank of each of the casing's axes (0 = shortest), so the ratio can be
	// written back to the right component after sorting.
	int shellRank[3] = { 0, 1, 2 };
	for (int a = 0; a < 3; ++a)
		for (int b = a + 1; b < 3; ++b)
			if (shellAxes[shellRank[b]] < shellAxes[shellRank[a]])
				std::swap(shellRank[a], shellRank[b]);

	std::sort(roundAxes, roundAxes + 3);

	irr::core::vector3df scale = fallback;
	float* out[3] = { &scale.X, &scale.Y, &scale.Z };

	for (int r = 0; r < 3; ++r)
	{
		const float shellAxis = shellAxes[shellRank[r]];
		if (shellAxis <= 0.0f || roundAxes[r] <= 0.0f)
			return fallback;

		*out[shellRank[r]] = roundAxes[r] / shellAxis;
	}

	return scale;
}

// Credit each round the moment it reaches its chamber rather than at the end of
// the reload, so a switch-away mid-clip keeps the rounds that visibly went in.
// The loop walks forward from m_reloadCredited instead of testing one round, so
// a frame-rate hitch that skips past several seat frames still credits them all.
void Weapon_Revolver::creditSeatedRounds(float frame)
{
	const int f = static_cast<int>(frame);

	while (m_reloadCredited < m_reloadRounds)
	{
		const int seatFrame = m_firstSeatFrame + m_insertStride * m_reloadCredited;
		if (f < seatFrame)
			break;

		if (m_cylinder < m_cylinderSize)
			m_cylinder++;

		m_reloadCredited++;
	}
}

// Abandon the loading pass and cut to the cylinder close, with the shot queued
// for the moment it latches. The gun is never fired with the crane open — that
// is the whole reason this is a queued shot rather than an immediate one.
void Weapon_Revolver::interruptReloadToFire()
{
	if (!m_isReloading || !m_mesh.node)
		return;

	// Already closing (either interrupted a moment ago, or a full reload reached
	// its close on its own): just queue the shot, don't restart the clip.
	if (m_reloadPhase != 2)
	{
		// Stop loading. Rounds already seated keep their credit; trimming the
		// target to what actually went in also stops updateReloadRounds() from
		// re-showing rounds abandoned in the hand and stops updateReloadSounds()
		// clicking for rounds that will never arrive. Those chambers correctly
		// read as empty for the rest of the close.
		m_reloadRounds = m_reloadCredited;

		m_reloadPhase = 2;
		m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag
		playAnimation("reload_close");
	}

	m_fireAfterReload = true;
}

// Frame-triggered reload audio. Each cue fires once, early by its own measured
// lead, so the transient lands on the visual event instead of trailing it.
void Weapon_Revolver::updateReloadSounds(float frame)
{
	const int f = static_cast<int>(frame);

	// One click per round. Walks forward like creditSeatedRounds() so a frame
	// hitch that skips several trigger points still plays them all, and the
	// concurrency cap keeps that from becoming one blast.
	const int insertLead = soundLeadFrames(m_insertSoundLeadSec);

	while (m_insertSoundsPlayed < m_reloadRounds)
	{
		const int seatFrame = m_firstSeatFrame + m_insertStride * m_insertSoundsPlayed;
		if (f < seatFrame - insertLead)
			break;

		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/insert_shell", 0.07f, 2, -1.0f, "revolver_insert");

		m_insertSoundsPlayed++;
	}

	// The crane swings shut over f212-217 and latches at 217
	if (!m_closeSoundPlayed && f >= m_cylinderLatchFrame - soundLeadFrames(m_closeSoundLeadSec))
	{
		m_closeSoundPlayed = true;

		SoundManager::Get()->sound()->playRandomized2D(
			"content/sound/weapon/revolver_close_cylinder", 0.04f, 1, -1.0f, "revolver_close");
	}
}

// Leaves m_cylinder alone: rounds are banked by creditSeatedRounds() as they
// seat, so this only tears down the reload's own bookkeeping. Safe to call on a
// completed reload or on one abandoned part-way through.
void Weapon_Revolver::endReload()
{
	m_isReloading        = false;
	m_reloadPhase        = 0;
	m_reloadRounds       = 0;
	m_reloadCredited     = 0;
	m_insertSoundsPlayed = 0;
	m_closeSoundPlayed   = false;
	m_fireAfterReload    = false;

	// Every exit from a reload runs through here, so this is the one place the
	// sped-up playback has to be put back.
	setClipSpeed(1.0f);

	for (int i = 0; i < m_roundCount; ++i)
	{
		m_rounds[i].hasLastWorldPos = false;
		m_rounds[i].peakSpeed = 0.0f;
	}

	setAllRoundsVisible(true);
}

void Weapon_Revolver::idle()
{

}

void Weapon_Revolver::move()
{

}

void Weapon_Revolver::fire()
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>() || !m_mesh.node)
		return;

	m_cylinder--;

	// Trigger the hammer/trigger clip (restarts if already playing)
	playAnimation("fire");
	m_isFireAnim = true;

	// Programmatic kick layered on top — heavier than the pistol, spring recovers
	// in updateWeaponSway()
	addViewKick(
		irr::core::vector3df(0.0f, 0.05f, -0.14f),                    // strong rise, shoved back into screen
		irr::core::vector3df(11.0f, 0.0f,                             // muzzle climbs hard
			Engine::Get()->rng()->getFloat(-2.0f, 2.0f)));            // roll variation

	auto& camera = player.getComponent<CameraComponent>();

	// Force full hierarchy update so bone world positions are current
	camera.camera->updateAbsolutePosition();
	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();

	// Same point the flash is drawn at, by construction — one definition of
	// "where the muzzle is" rather than two guesses that can drift apart.
	const irr::core::vector3df muzzlePos = m_effects.muzzleWorldPosition();

	// Camera basis for the spread offsets
	irr::core::vector3df target = camera.camera->getTarget();
	irr::core::vector3df cameraPos = camera.camera->getAbsolutePosition();
	irr::core::vector3df forward = (target - cameraPos).normalize();

	irr::core::vector3df up(0, 1, 0);
	irr::core::vector3df right = forward.crossProduct(up).normalize();
	irr::core::vector3df down = right.crossProduct(forward).normalize();

	// Converge on the crosshair aim point — the muzzle sits below-right of screen
	// centre, so a plain camera-forward ray would land offset from the crosshair
	irr::core::vector3df direction = getAimDirection(muzzlePos);

	float spreadRight = Engine::Get()->rng()->getFloat(-m_spread, m_spread);
	float spreadDown = Engine::Get()->rng()->getFloat(-m_spread, m_spread);
	direction = (direction + right * spreadRight + down * spreadDown).normalize();

	irr::core::vector3df rayEnd = muzzlePos + direction * 1000.0f;

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
					WorldManager::Get()->gameplaySystem()->damageEntity(hitDescriptor.id, m_damage));

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

	// Hard camera kick -- a magnum should hurt to shoot
	float recoilYaw = Engine::Get()->rng()->getFloat(-0.3f, 0.3f);
	g_CameraFX.addRecoil(-3.4f, recoilYaw);

	// revolver_fire1/2.wav — getVariantSet() picks between them and won't repeat
	// the same one twice in a row
	SoundManager::Get()->sound()->playRandomized2D(
		"content/sound/weapon/revolver_fire", 0.05f, 3, -1.0f, "revolver_fire");
}

void Weapon_Revolver::reload()
{
	if (m_isReloading || m_isEquipping || m_isUnequipping)
		return;

	const int missing = m_cylinderSize - m_cylinder;
	if (missing <= 0)
		return; // nothing to top up — don't burn seven seconds on a full cylinder

	if (!m_mesh.node)
		return;

	m_reloadRounds       = missing;
	m_reloadCredited     = 0;
	m_insertSoundsPlayed = 0;
	m_closeSoundPlayed   = false;

	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag

	// Phase 1 runs from the cylinder swinging open through the seating of round
	// m_reloadRounds. That end frame is computed, so it cannot be a named clip —
	// hence the direct setFrameLoop here while every other clip goes through
	// playAnimation(). A six-round reload lands exactly on m_reloadCloseStart,
	// making the phase-2 handoff seamless; shorter ones cut, cheaply (see init()).
	const int lastSeatFrame = m_firstSeatFrame + m_insertStride * (m_reloadRounds - 1);

	setClipSpeed(m_reloadSpeed);

	m_mesh.node->setLoopMode(false);
	m_mesh.node->setFrameLoop(m_reloadStartFrame, lastSeatFrame);

	m_isReloading = true;
	m_reloadPhase = 1;
	m_isFireAnim = false;
}

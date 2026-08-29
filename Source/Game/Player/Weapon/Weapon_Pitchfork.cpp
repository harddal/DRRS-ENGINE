#include "Weapon_Pitchfork.h"

#include <algorithm>
#include <cmath>

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

namespace
{
	// Contact frames measured off the .glb by tracking the TINE TIPS, not the
	// root: the tips travel 30 to 107 units where the root moves 6 to 36, and it
	// is the tips arriving that reads as the hit.
	//
	// The three thrusts plateau at full extension for two or three frames and
	// contact is taken mid-plateau; the sweep is a sharp single-frame peak and
	// contact is that peak.

	// clip 0-20, straight thrust, no rotation at all — tips out 29.6/30.2/30.8
	// across f2-4. The fastest attack in the set: full extension by frame 4.
	constexpr Weapon_Pitchfork::MeleeAttack kThrustA { "stab1",  3, 30, 3.2f, 1, 0.0f };

	// clip 21-46, thrust with a 16 degree roll — tips out 31.0/31.4/31.8 f23-25
	constexpr Weapon_Pitchfork::MeleeAttack kThrustB { "stab2", 24, 32, 3.2f, 1, 0.0f };

	// clip 47-67, a longer lunge with a 35 degree roll — tips reach 43.3 by f51,
	// half again as far as the other two, so it hits harder and reaches further
	constexpr Weapon_Pitchfork::MeleeAttack kThrustC { "stab3", 50, 38, 3.6f, 1, 0.0f };

	// clip 68-88, the wide sweep — tips travel 107 units and the weapon rotates
	// through (-40, 21, -63). Swung ACROSS rather than at a point, so it casts a
	// fan and can catch several targets; shorter reach than a thrust, because an
	// arc trades depth for width.
	constexpr Weapon_Pitchfork::MeleeAttack kSweep   { "sweep", 77, 55, 2.8f, 5, 55.0f };
}

void Weapon_Pitchfork::precache()
{
	ParticleManager::Get()->precache("spark", _asset_psys("spark"));

	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/sword/fire.wav", true);
}

void Weapon_Pitchfork::init()
{
	m_descriptor.name = "Player_Weapon_Pitchfork";
	m_descriptor.id = _entity_null_value;

	// pitchfork_animated.glb carries the same arms rig as the rest of the glTF
	// pack — identical joint names, identical 'arms' root at (0, 2.945, -17.671).
	// The haft is 118 model units long and the grip sits near its middle, so this
	// pulls back and down harder than any other weapon here to keep the tines in
	// frame rather than through the camera. Tune with the F2 window, not here.
	m_viewPositionOffset = irr::core::vector3df(0.0900f, -0.1050f, 0.3700f);
	m_viewRotationOffset = irr::core::vector3df(0.0f, 180.0f, 0.0f);
	m_viewScaleOffset    = irr::core::vector3df(0.01f, 0.01f, 0.01f);

	m_mesh.mesh = _asset_glb("player/weapon/pitchfork_animated");

	m_mesh.trimesh = RenderManager::Get()->loadMesh(m_mesh.mesh);

	// Swap in the stand-in BEFORE the node is created — creating a node in the
	// failure branch and again below orphans the first one.
	const bool usingStandIn = (m_mesh.trimesh == nullptr);
	if (usingStandIn)
	{
		spdlog::warn("Weapon_Pitchfork::init(): failed to load mesh \"{}\", stand-in mesh loaded", m_mesh.mesh);
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
	// take (0-3.83s = frames 0-115 at 30 fps) with a 2-frame hold at the rest
	// pose between clips. Only the 'pitchfork' node animates besides the arms.
	// Boundaries are its rest returns: 0, 20/21, 46/47, 67/68, 88/89 and 115.
	//   0-20     tips punch straight out 30.8 units with NO
	//            rotation whatever, then recover                 -> stab1
	//   21-46    the same thrust with a 16 deg roll, tips 31.8   -> stab2
	//   47-67    a longer lunge, 35 deg roll, tips out 43.3      -> stab3
	//   68-88    tips travel 107 units through a (-40, 21, -63)
	//            rotation — swung across, not thrust             -> sweep
	//   89-115   under 5.6 units of drift, starts and ends at
	//            rest                                            -> a real looping
	//            idle, the third in the pack
	//
	// THERE IS NO DRAW AND NO HOLSTER IN THIS ASSET. Every other rest-to-rest
	// range is an attack. That is why this class does not override
	// startUnequip()/isUnequipping() the way Weapon_Melee does — with no clip to
	// play, holding the switch open would only stall the next weapon. See equip()
	// for what softens the pop.
	//
	// The idle ENDS AT 114, not 115: Irrlicht clamps EndFrame to
	// getFrameCount()-1, and CSkinnedMesh::getFrameCount() returns the last frame
	// INDEX, so 115 is unreachable and asking for it silently gets 114 anyway.
	// Stating it costs nothing here — pose(114) is 0.2 units from pose(89), so
	// the loop closes cleanly regardless.
	m_mesh.animationList.emplace_back(sAnimationData("stab1", 0,  20,  false));
	m_mesh.animationList.emplace_back(sAnimationData("stab2", 21, 46,  false));
	m_mesh.animationList.emplace_back(sAnimationData("stab3", 47, 67,  false));
	m_mesh.animationList.emplace_back(sAnimationData("sweep", 68, 88,  false));
	m_mesh.animationList.emplace_back(sAnimationData("idle",  89, 114, true));

	// Both glTF backends normalise keyframe times to 30 fps Irrlicht frames, so
	// the viewmodel must play at 30 to run at its authored speed.
	m_mesh.fps = 30;
	m_mesh.node->setAnimationSpeed(static_cast<irr::f32>(m_mesh.fps));

	m_mesh.node->setJointMode(irr::scene::EJUOR_READ);

	m_mesh.animation_call_back = std::make_shared<AnimationCallback>();
	m_mesh.node->setAnimationEndCallback(m_mesh.animation_call_back.get());

	playAnimation("idle");

	// Light: the idle here is a real 26-frame loop rather than one pinned frame,
	// so this only exists to stop that loop reading as a loop. Wider than the
	// staff's all the same — this thing is heavy and held out at the end of a
	// long haft, and it should wander.
	enableIdleBreathing(0.95f);

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

	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");

	if (!player.isValid())
	{
		spdlog::error("In function Weapon_Pitchfork::init() -> getEntityByName(\"player\") : Entity 'player' does not exist");

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
		spdlog::error("In function Weapon_Pitchfork::init() -> player.getComponent<CameraComponent>() : Entity 'player' does not have specified component");
	}

	RenderManager::Get()->registerViewmodelNode(m_mesh.node);
	m_mesh.node->setVisible(false);

	// Five rays is the widest fan any attack asks for
	m_hitThisSwing.reserve(8);
}

void Weapon_Pitchfork::destroy()
{
	RenderManager::Get()->unregisterViewmodelNode(m_mesh.node);
	m_mesh.node->remove();

	WorldManager::Get()->freeEntityID(m_descriptor.id);
}

void Weapon_Pitchfork::update()
{
	if (!m_mesh.node || !m_mesh.node->isVisible())
		return;

	const bool animEnded = m_mesh.animation_call_back->hasAnimationEnded();

	if (m_isSwinging)
	{
		// The strike lands when the tines visually reach the target, not when the
		// button went down — anticipation, then payoff.
		if (!m_damageDone && m_mesh.node->getFrameNr() >= static_cast<irr::f32>(m_attack.contactFrame))
		{
			m_damageDone = true;
			performStrike();
		}

		if (animEnded)
		{
			m_isSwinging = false;
			idle();
		}

		// Committed to the swing — no new attacks until it finishes
		return;
	}

	// Re-loop the idle when it runs out. The clip is flagged looping, so this is
	// only a safety net for a clip that ended some other way.
	if (animEnded)
		idle();

	// Attack input
	static bool ml = false, mr = false;

	if (InputManager::Get()->getMousePressOnce(MOUSE_BUTTON::MB_LEFT, &ml))
	{
		// Thrust — three variants, so repeated jabs do not read as one clip on
		// a loop. They differ in reach and damage as well as in look, which is
		// the point of keeping those numbers in the row rather than on the class.
		const int roll = Engine::Get()->rng()->getInt(0, 2);
		startSwing(roll == 0 ? kThrustA : (roll == 1 ? kThrustB : kThrustC));
	}
	else if (InputManager::Get()->getMousePressOnce(MOUSE_BUTTON::MB_RIGHT, &mr))
	{
		// Sweep — long wind-up, hits across a front rather than at a point
		startSwing(kSweep);
	}
}

void Weapon_Pitchfork::equip()
{
	m_mesh.node->setVisible(true);

	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag

	m_isSwinging = false;
	m_damageDone = false;
	resetViewKick();

	playEquipSound();

	idle();

	// There is no draw clip in this asset, so the weapon would otherwise simply
	// appear. A kick on the shared spring gives it a moment of settle on the way
	// in — the whole haft swings up and steadies — which costs nothing and is a
	// great deal better than a pop.
	addViewKick(
		irr::core::vector3df(0.0f, -0.09f, 0.10f),
		irr::core::vector3df(-16.0f, 0.0f, 7.0f));
}

void Weapon_Pitchfork::unequip()
{
	m_isSwinging = false;
	m_damageDone = false;

	m_mesh.node->setVisible(false);
}

void Weapon_Pitchfork::idle()
{
	playAnimation("idle");
}

void Weapon_Pitchfork::move()
{

}

// PlayerWeapon's primary-action hook. Attacks are driven from update() because
// they are edge-triggered and pick a variant, so this only exists to satisfy the
// base interface.
void Weapon_Pitchfork::fire()
{

}

void Weapon_Pitchfork::reload()
{

}

void Weapon_Pitchfork::startSwing(const MeleeAttack& attack)
{
	// contactFrame is absolute, so it must fall inside the clip's own range. Out
	// of range fails SILENTLY — below it the strike lands on frame one, above it
	// performStrike() is never reached at all. Both look like "melee does no
	// damage" with nothing in the log, so complain here instead.
	if (const sAnimationData* clip = m_mesh.findAnimation(attack.anim))
	{
		if (attack.contactFrame < clip->frames.X || attack.contactFrame > clip->frames.Y)
			spdlog::error("Weapon_Pitchfork::startSwing(): contact frame {} is outside clip '{}' ({}-{}) — strike will not land correctly",
				attack.contactFrame, attack.anim, clip->frames.X, clip->frames.Y);
	}

	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag

	playAnimation(attack.anim);

	m_isSwinging = true;
	m_damageDone = false;
	m_attack     = attack;

	m_hitThisSwing.clear();

	// Anticipation: whoosh and a lunge at the START of the swing — the payoff
	// (damage, impact, hit stop) comes later, on the contact frame. Pitched down
	// for the sweep, which is a heavier movement than a jab.
	SoundManager::Get()->sound()->play2D(
		"content/sound/weapon/sword/fire.wav",
		false, 0, -1.0f, nullptr, false,
		attack.rays > 1 ? 0.75f : 0.95f);

	g_CameraFX.addRecoil(0.8f, Engine::Get()->rng()->getFloat(-0.2f, 0.2f));

	addViewKick(
		irr::core::vector3df(0.0f, -0.01f, 0.05f),
		irr::core::vector3df(-2.0f, 0.0f, Engine::Get()->rng()->getFloat(-1.0f, 1.0f)));
}

void Weapon_Pitchfork::performStrike()
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>())
		return;

	auto& camera = player.getComponent<CameraComponent>();

	const irr::core::vector3df cameraPos = camera.camera->getAbsolutePosition();
	const irr::core::vector3df forward   = (camera.camera->getTarget() - cameraPos).normalize();

	irr::core::vector3df up(0, 1, 0);
	irr::core::vector3df right = forward.crossProduct(up).normalize();

	bool connected = false;
	HIT_RESULT best = HIT_RESULT::NONE;

	// A thrust is one ray; the sweep fans across its arc. Rays are spread evenly
	// from one edge of the arc to the other, so the middle of a five-ray fan is
	// dead ahead and the outer pair mark the extremes of the swing.
	const int rays = m_attack.rays < 1 ? 1 : m_attack.rays;

	for (int i = 0; i < rays; ++i)
	{
		float offsetDeg = 0.0f;

		if (rays > 1)
		{
			const float t = static_cast<float>(i) / static_cast<float>(rays - 1); // 0..1
			offsetDeg = (t - 0.5f) * m_attack.arcDegrees;
		}

		const float rad = offsetDeg * 3.14159265f / 180.0f;

		const irr::core::vector3df direction =
			(forward * std::cos(rad) + right * std::sin(rad)).normalize();

		const irr::core::vector3df rayEnd = cameraPos + direction * m_attack.reach;

		RaycastResultData raycastResult =
			RenderManager::Get()->raycastWorldPosition(cameraPos, rayEnd, true);

		if (!raycastResult.hit || !raycastResult.node)
			continue;

		auto& hitEntity = WorldManager::Get()->managerSystem()->getEntityByID(raycastResult.node->getID());

		if (hitEntity.isValid() && hitEntity.hasComponent<DescriptorComponent>())
		{
			auto& hitDescriptor = hitEntity.getComponent<DescriptorComponent>();

			if (hitDescriptor.type != ET_STATIC && hitDescriptor.type != ET_DYNAMIC)
				continue;

			// One hit per entity per swing. Without this a target standing in the
			// middle of the sweep's fan takes the damage once per ray, which is
			// five times over and turns the heavy attack into an instant kill.
			if (std::find(m_hitThisSwing.begin(), m_hitThisSwing.end(), hitDescriptor.id)
				!= m_hitThisSwing.end())
				continue;

			m_hitThisSwing.push_back(hitDescriptor.id);

			// Damage through the gameplay chokepoint; drives hitmarker/kill feedback
			const HIT_RESULT result = WorldManager::Get()->gameplaySystem()->damageEntity(
				hitDescriptor.id, m_attack.damage);

			ParticleManager::Get()->spawn("spark", SPK::IRR::irr2spk(raycastResult.point));

			if (result != HIT_RESULT::NONE)
			{
				connected = true;

				if (static_cast<int>(result) > static_cast<int>(best))
					best = result;
			}
		}
		else if (RenderManager::isWorldGeometryNode(raycastResult.node))
		{
			// Brush chunks / props carry no ECS id — clang off the surface
			ParticleManager::Get()->spawn("spark", SPK::IRR::irr2spk(raycastResult.point));
		}
	}

	// One feedback event for the whole swing, however many rays found something —
	// a sweep that catches three targets should not tick the hitmarker three
	// times or stack three hit stops.
	registerHitFeedback(best);

	if (connected)
	{
		// Connect crunch: impact thunk (placeholder — a pitched-down whoosh until
		// something better exists), camera bite, and the hit-stop payoff.
		SoundManager::Get()->sound()->play2D(
			"content/sound/weapon/sword/fire.wav",
			false, 0, 0.5f, nullptr, false, 0.6f);

		g_CameraFX.addShake(1.0f, 120.0f);
		g_CameraFX.addRecoil(0.7f, 0.0f);

		Engine::Get()->requestHitStop(best == HIT_RESULT::KILL ? 80.0f : 45.0f);
	}
}

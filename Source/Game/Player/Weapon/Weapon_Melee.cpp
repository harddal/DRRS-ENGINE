#include "Weapon_Melee.h"

#include "Engine/Engine.h"

#include "../CameraFX.h"

#undef MB_RIGHT

using namespace SPK;
using namespace SPK::IRR;

namespace
{
	// One row per attack, so the clip name, its contact frame and its damage stay
	// together instead of the frame being repeated as a literal at the call site
	// (that duplication is why the contact frames silently kept the old .b3d
	// numbers — 270/279/303 — after the mesh moved to knife_animated.glb, which
	// left performStrike() unreachable because getFrameNr() never got that high).
	//
	// contactFrame is an ABSOLUTE frame in the shared 0-122 timeline, matching
	// m_mesh.animationList. Measured from the blade's world-space motion in the
	// .glb: each swing drives the knife out, HOLDS it at full extension for a few
	// frames, then recovers — contact is the frame it arrives at that extension.
	struct MeleeAttack
	{
		const char*  anim;
		int          contactFrame;
		unsigned int damage;
	};

	// clip 34-52, extension plateaus 37-42
	constexpr MeleeAttack kLightA { "fire1", 37, 10 };
	// clip 53-69, sharp extension peak at 54
	constexpr MeleeAttack kLightB { "fire2", 55, 10 };
	// clip 71-89, extension plateaus 75-80
	constexpr MeleeAttack kHeavy  { "fire3", 75, 25 };
	// clip 90-110, sharp extension peak at 93 — authored but not yet bound to input
	constexpr MeleeAttack kStab   { "stab",  93, 20 };
}

void Weapon_Melee::precache()
{
	ParticleManager::Get()->precache("spark", _asset_psys("spark"));
}

void Weapon_Melee::init()
{
	m_descriptor.name = "Player_Weapon_Melee";
	m_descriptor.id = _entity_null_value;

	m_viewPositionOffset = irr::core::vector3df(0.1200f, -0.0650f, 0.3100f);
	m_viewRotationOffset = irr::core::vector3df(0.0f, 180.0f, 0.0f);
	m_viewScaleOffset    = irr::core::vector3df(0.01f, 0.01f, 0.01f);

	m_mesh.mesh = _asset_glb("player/weapon/knife_animated");

	m_mesh.trimesh = RenderManager::Get()->loadMesh(m_mesh.mesh);

	// Swap in the stand-in mesh BEFORE the node is created — creating a node
	// inside the failure branch and then again below orphaned the first one.
	const bool usingStandIn = (m_mesh.trimesh == nullptr);
	if (usingStandIn)
	{
		spdlog::warn("PlayerWeapon::init(): failed to load mesh \"{}\", stand-in mesh loaded", m_mesh.mesh);
		m_mesh.trimesh = RenderManager::Get()->sceneManager()->getMesh("content/mesh/primitive/double_tetrahedron.obj");
	}

	m_mesh.node = RenderManager::Get()->sceneManager()->addAnimatedMeshSceneNode(m_mesh.trimesh, nullptr, m_descriptor.id);

	if (usingStandIn)
		m_mesh.node->setMaterialTexture(0, RenderManager::Get()->driver()->getTexture("content/texture/color/magenta.png"));

	// Looping clips MUST be flagged loop=true. A non-looping clip re-armed from
	// the end callback (the old "idle" setup) clamps on its last frame until the
	// next update() tick notices and calls setFrameLoop, which resets the
	// playhead — that one-frame hold every cycle is the visible hitch.
	//
	// Irrlicht does NOT interpolate across the loop seam (CAnimatedMeshSceneNode
	// ::buildFrameNr wraps with fmod over EndFrame-StartFrame), so the pose at
	// the END frame must be identical to the pose at the START frame — the
	// duplicated boundary frame Blender cyclic actions normally carry.
	// knife_animated.glb bakes every clip into one 0-153 timeline separated by
	// 2-frame rest holds. The idle runs 0-41: rest at 0, peak at 20, back to
	// rest at 41 (pose(41) == pose(0) to 0.03 deg), with 42 a static duplicate.
	// Ending it at 33 cut the clip mid-descent, ~11.5 deg off the rest pose —
	// that snap back to the start was the hitch. Do NOT extend it to 42; the
	// duplicate frame would hold the pose for one frame every cycle.
	// NOTE: the timeline's last frame is 122, and Irrlicht clamps EndFrame to
	// getFrameCount()-1 = 121 (CSkinnedMesh::getFrameCount returns the last frame
	// INDEX, and setFrameLoop then subtracts one), so frame 122 is unreachable.
	// That costs a little: pose(122) is 3.8 deg from the idle's frame 0, pose(121)
	// is 13.4 deg — so the equip->idle handoff pops slightly more than it should.
	// Asset fix if it reads badly: add one trailing frame in Blender so the
	// timeline ends at 123, which makes 122 reachable. 123 here was out of range
	// and clamped to the same 121 anyway.
	m_mesh.animationList.emplace_back(sAnimationData("equip",   116, 122, false));
	m_mesh.animationList.emplace_back(sAnimationData("idle",    0,   0,  true));
	m_mesh.animationList.emplace_back(sAnimationData("fire1",   34, 52, false));
	m_mesh.animationList.emplace_back(sAnimationData("fire2",   53, 69, false));
	m_mesh.animationList.emplace_back(sAnimationData("fire3",   71, 89, false));
	m_mesh.animationList.emplace_back(sAnimationData("stab", 90, 110, false));
	m_mesh.animationList.emplace_back(sAnimationData("unequip", 111, 115, false));

	// BOTH glTF backends normalise keyframe times to 30 fps Irrlicht frames —
	// GltfImport uses seconds * 30, IrrAssimpImport uses mTime * (30/ticksPerSec)
	// — so the viewmodel must play at 30 to run at its authored speed, and the
	// clip ranges above are backend-independent. The old 20 here was tuned for
	// the previous .b3d knife and made the glTF clips run at 2/3 speed.
	m_mesh.fps = 30;
	m_mesh.node->setAnimationSpeed(static_cast<irr::f32>(m_mesh.fps));
	m_mesh.node->setLoopMode(false);
	m_mesh.node->setFrameLoop(0, 0);

	// The idle clip is pinned to a single frame, so the hold-steady motion comes
	// from updateWeaponSway() instead. Below default: a knife is held close in,
	// so there is less lever arm for a breath to move it.
	enableIdleBreathing(0.8f);

	m_mesh.animation_call_back = std::make_shared<AnimationCallback>();
	m_mesh.node->setAnimationEndCallback(m_mesh.animation_call_back.get());

	m_mesh.node->setScale(m_viewScaleOffset);

	auto perpixelMat = ShaderMaterialManager::get("phong_perpixel");
	if (perpixelMat != irr::video::EMT_SOLID) // only assign if shader compiled successfully
	{
		m_mesh.node->setMaterialType(perpixelMat);
	}

	for (auto i = 0; i < m_mesh.node->getMaterialCount(); i++)
	{
		m_mesh.node->getMaterial(i).Shininess = 0.f;
		m_mesh.node->getMaterial(i).SpecularColor.setAlpha(0);
	}

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
}

void Weapon_Melee::destroy()
{
	RenderManager::Get()->unregisterViewmodelNode(m_mesh.node);
	m_mesh.node->remove();

	WorldManager::Get()->freeEntityID(m_descriptor.id);
}

void Weapon_Melee::update()
{
	if (!m_mesh.node || !m_mesh.node->isVisible())
		return;

	bool animEnded = m_mesh.animation_call_back->hasAnimationEnded();

	// Holstering: hold the node visible until the clip finishes, then hide it.
	// isUnequipping() going false is what releases WeaponController's pending
	// switch, so the next weapon is only drawn after this one is put away.
	if (m_isUnequipping)
	{
		if (animEnded)
			unequip();

		return; // no attacks or idle re-loop while holstering
	}

	// Drawing: hand off to the looping idle once the equip clip finishes
	if (m_isEquipping)
	{
		if (animEnded)
		{
			m_isEquipping = false;
			idle();
		}

		return; // no attacks until the knife is up
	}

	if (m_isSwinging)
	{
		// The strike lands when the blade visually reaches the target
		if (!m_damageDone && m_mesh.node->getFrameNr() >= static_cast<irr::f32>(m_contactFrame))
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

	// Re-loop the idle animation when it runs out
	if (animEnded)
		idle();

	// Attack input
	static bool ml = false, mr = false;
	if (InputManager::Get()->getMousePressOnce(MOUSE_BUTTON::MB_LEFT, &ml))
	{
		// Light attack — two swing variants
		const MeleeAttack& atk = (Engine::Get()->rng()->getInt(0, 1) == 0) ? kLightA : kLightB;
		startSwing(atk.anim, atk.contactFrame, atk.damage);
	}
	else if (InputManager::Get()->getMousePressOnce(MOUSE_BUTTON::MB_RIGHT, &mr))
	{
		// Heavy attack — longer wind-up, heavier hit
		startSwing(kHeavy.anim, kHeavy.contactFrame, kHeavy.damage);
	}
}

void Weapon_Melee::equip()
{
	m_mesh.node->setVisible(true);

	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag
	m_isSwinging     = false;
	m_damageDone     = false;
	m_isUnequipping  = false;
	m_isEquipping    = true;

	playEquipSound();

	// Draw animation; update() drops into idle when it finishes. If the clip is
	// missing, playAnimation leaves the current loop alone and returns false —
	// fall straight through to idle rather than freezing on the wrong pose.
	if (!playAnimation("equip"))
	{
		m_isEquipping = false;
		idle();
	}
}

void Weapon_Melee::startUnequip()
{
	// Already hidden (or mid-holster): nothing to play, don't restart the clip
	if (!m_mesh.node || !m_mesh.node->isVisible() || m_isUnequipping)
		return;

	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag
	m_isSwinging  = false;
	m_isEquipping = false;

	playUnequipSound();

	if (playAnimation("unequip"))
	{
		// Node stays visible until the clip ends — update() calls unequip() then.
		m_isUnequipping = true;
	}
	else
	{
		unequip(); // no clip: instant hide, same as the base class
	}
}

void Weapon_Melee::unequip()
{
	m_isUnequipping = false;
	m_isEquipping   = false;
	m_mesh.node->setVisible(false);
}

void Weapon_Melee::idle()
{
	playAnimation("idle");
}

void Weapon_Melee::move()
{
	
}

void Weapon_Melee::fire()
{

}

void Weapon_Melee::startSwing(const std::string& animation, int contactFrame, unsigned int damage)
{
	// contactFrame is absolute, so it must fall inside the clip's own range. Out
	// of range fails SILENTLY — below the range the strike lands on frame one,
	// above it performStrike() is never reached at all. Both look like "melee
	// does no damage" with nothing in the log, so complain here instead.
	if (const sAnimationData* clip = m_mesh.findAnimation(animation))
	{
		if (contactFrame < clip->frames.X || contactFrame > clip->frames.Y)
			spdlog::error("Weapon_Melee::startSwing(): contact frame {} is outside clip '{}' ({}-{}) — strike will not land correctly",
				contactFrame, animation, clip->frames.X, clip->frames.Y);
	}

	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag

	playAnimation(animation);

	m_isSwinging   = true;
	m_damageDone   = false;
	m_contactFrame = contactFrame;
	m_attackDamage = damage;

	// Anticipation: whoosh + forward lunge at swing START — the payoff
	// (damage/impact) comes later, on the contact frame
	SoundManager::Get()->sound()->playRandomized2D("content/sound/weapon/sword/fire", 0.08f);
	g_CameraFX.addRecoil(0.8f, Engine::Get()->rng()->getFloat(-0.2f, 0.2f));
	addViewKick(
		irr::core::vector3df(0.0f, -0.01f, 0.05f),
		irr::core::vector3df(-2.0f, 0.0f, Engine::Get()->rng()->getFloat(-1.0f, 1.0f)));
}

void Weapon_Melee::performStrike()
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>())
		return;

	auto& camera = player.getComponent<CameraComponent>();
	irr::core::vector3df cameraPos = camera.camera->getAbsolutePosition();
	irr::core::vector3df forward = (camera.camera->getTarget() - cameraPos).normalize();

	irr::core::vector3df rayEnd = cameraPos + forward * 2.0f;

	RaycastResultData raycastResult = RenderManager::Get()->raycastWorldPosition(cameraPos, rayEnd, true);

	if (raycastResult.hit && raycastResult.node)
	{
		auto& hitEntity = WorldManager::Get()->managerSystem()->getEntityByID(raycastResult.node->getID());

		// Check if hit entity is valid and has correct type
		if (hitEntity.isValid() && hitEntity.hasComponent<DescriptorComponent>())
		{
			auto& hitDescriptor = hitEntity.getComponent<DescriptorComponent>();

			// Only register collision with static or dynamic entities
			if (hitDescriptor.type == ET_STATIC || hitDescriptor.type == ET_DYNAMIC)
			{
				// Damage through the gameplay chokepoint; drives hitmarker/kill feedback
				HIT_RESULT result = WorldManager::Get()->gameplaySystem()->damageEntity(
					hitDescriptor.id, m_attackDamage);
				registerHitFeedback(result);

				// Create impact spark particles at hit position
				ParticleManager::Get()->spawn("spark", SPK::IRR::irr2spk(raycastResult.point));

				if (result != HIT_RESULT::NONE)
				{
					// Connect crunch: impact thunk (placeholder — pitched-down
					// whoosh until sword/impact1..N.wav exists), camera bite,
					// and the hit-stop payoff
					SoundManager::Get()->sound()->play2D(
						"content/sound/weapon/sword/fire.wav",
						false, 0, 0.5f, nullptr, false, 0.7f);
					g_CameraFX.addShake(0.8f, 100.0f);
					g_CameraFX.addRecoil(0.6f, 0.0f);

					Engine::Get()->requestHitStop(result == HIT_RESULT::KILL ? 70.0f : 35.0f);
				}
			}
		}
		else if (RenderManager::isWorldGeometryNode(raycastResult.node))
		{
			// Brush chunks / props carry no ECS id — clang off the surface
			ParticleManager::Get()->spawn("spark", SPK::IRR::irr2spk(raycastResult.point));
		}
	}
}

void Weapon_Melee::reload()
{

}
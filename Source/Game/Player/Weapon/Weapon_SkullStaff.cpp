#include "Weapon_SkullStaff.h"

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

// =============================================================================
// THE SPELLBOOK
//
// This table is the framework. To add a spell that throws something, add a row —
// no other file changes, and nothing in the class needs to know how many rows
// there are. To add a spell that behaves in a genuinely new way, add a row with
// a new SpellBehaviour and one case in castSpell(); both places are marked.
//
// Every row picks its own cast animation, so a heavy spell can look heavy:
// "cast" is the 0.67 s jab and "cast_channel" is the 2.5 s raised hold, both
// authored in the .glb.
//
// SOUL FIRE is the finished one. GRAVE BLOOM exists to prove the table does what
// it claims — it introduces no new code at all, only different numbers and a
// different clip — so treat its balance as a placeholder and retune or delete it.
// =============================================================================
const SpellDesc Weapon_SkullStaff::s_spells[] =
{
	{
		/* name           */ "Soul Fire",
		/* behaviour      */ SPELL_PROJECTILE,
		/* manaCost       */ 18,
		/* cooldownMs     */ 420.0f,
		/* castClip       */ "cast",
		/* castSpeed      */ 1.15f,
		/* releaseDelayMs */ 150.0f,   // just after the jab reaches full extension

		/* speed          */ 55.0f,
		/* gravity        */ 0.0f,     // dead straight — it is fire, not a rock
		/* lifetimeMs     */ 4000.0f,

		/* directDamage   */ 45.0f,
		/* splashDamage   */ 22.0f,
		/* splashRadius   */ 2.6f,

		/* trailTexture   */ "content/texture/particle/flame_02.png",
		/* trailColorFrom */ irr::video::SColor(255, 255, 190, 90),
		/* trailColorTo   */ irr::video::SColor(255, 200,  60, 20),
		/* impactParticle */ "explosion",
		/* lightColor     */ irr::video::SColorf(1.0f, 0.55f, 0.2f),
		/* lightRadius    */ 5.0f,
		/* castSound      */ "content/sound/weapon/plasma_rifle/fire",
		/* onCast         */ nullptr,
		/* canCast        */ nullptr,
	},
	{
		/* name           */ "Grave Bloom",
		/* behaviour      */ SPELL_PROJECTILE,
		/* manaCost       */ 55,
		/* cooldownMs     */ 1400.0f,
		/* castClip       */ "cast_channel",
		/* castSpeed      */ 1.7f,     // 2.5 s authored -> ~1.5 s
		/* releaseDelayMs */ 1150.0f,  // released near the end of the channel

		/* speed          */ 26.0f,    // slow and heavy; you lead your target
		/* gravity        */ 7.0f,     // and it arcs
		/* lifetimeMs     */ 6000.0f,

		/* directDamage   */ 70.0f,
		/* splashDamage   */ 65.0f,
		/* splashRadius   */ 5.5f,

		/* trailTexture   */ "content/texture/particle/magic_01.png",
		/* trailColorFrom */ irr::video::SColor(255, 150, 255, 170),
		/* trailColorTo   */ irr::video::SColor(255,  30, 140,  70),
		/* impactParticle */ "bio_splash",
		/* lightColor     */ irr::video::SColorf(0.35f, 1.0f, 0.5f),
		/* lightRadius    */ 7.5f,
		/* castSound      */ "content/sound/weapon/pulse_rifle/fire",
		/* onCast         */ nullptr,
		/* canCast        */ nullptr,
	},
	{
		// A UTILITY SPELL, and the reason the hooks exist. Nothing leaves the
		// staff — SPELL_SELF is a delivery that does nothing — so the whole spell
		// IS the hook. Note how little of the row it needs: the projectile and
		// trail fields are all dead here, which is the honest cost of one flat
		// descriptor, and the alternative (a variant, or a descriptor per
		// behaviour) buys nothing at this size.
		/* name           */ "Mend",
		/* behaviour      */ SPELL_SELF,
		/* manaCost       */ 40,
		/* cooldownMs     */ 2600.0f,
		/* castClip       */ "cast_channel",
		/* castSpeed      */ 1.5f,
		/* releaseDelayMs */ 900.0f,

		/* speed          */ 0.0f,
		/* gravity        */ 0.0f,
		/* lifetimeMs     */ 0.0f,

		/* directDamage   */ 0.0f,
		/* splashDamage   */ 0.0f,
		/* splashRadius   */ 0.0f,

		/* trailTexture   */ nullptr,
		/* trailColorFrom */ irr::video::SColor(255, 255, 255, 255),
		/* trailColorTo   */ irr::video::SColor(255, 255, 255, 255),
		/* impactParticle */ nullptr,
		/* lightColor     */ irr::video::SColorf(0.6f, 1.0f, 0.75f),
		/* lightRadius    */ 6.0f,
		/* castSound      */ "content/sound/effect/powerhum",
		/* onCast         */ &Weapon_SkullStaff::spellMend,
		/* canCast        */ &Weapon_SkullStaff::mendWouldHelp,
	},
};

const int Weapon_SkullStaff::s_spellCount = static_cast<int>(sizeof(s_spells) / sizeof(s_spells[0]));

const SpellDesc& Weapon_SkullStaff::spell() const
{
	const int i = (m_spell < 0 || m_spell >= s_spellCount) ? 0 : m_spell;
	return s_spells[i];
}

// One definition of "could this be cast right now", read by both the input path
// and the HUD — so what the player is shown and what a click actually does can
// never disagree.
bool Weapon_SkullStaff::canCastCurrent() const
{
	const SpellDesc& s = spell();

	if (Engine::Get()->getCurrentTime() < m_nextReadyTime[m_spell])
		return false;

	if (m_mana < static_cast<float>(s.manaCost))
		return false;

	// The spell's own gate, if it has one
	if (s.canCast && !(this->*s.canCast)(s))
		return false;

	return true;
}

// --- Utility spell: Mend -----------------------------------------------------
//
// DamageReceiverComponent::health is the single source of truth for player
// health — PlayerController copies it out into g_PlayerData.currentHealth every
// frame — so healing means writing that one field and letting the mirror catch
// up. Writing g_PlayerData directly would be overwritten within the frame.
void Weapon_SkullStaff::spellMend(const SpellDesc& desc)
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");

	if (!player.isValid() || !player.hasComponent<DamageReceiverComponent>())
		return;

	auto& receiver = player.getComponent<DamageReceiverComponent>();

	// Healing scales off what the spell cost, so retuning manaCost retunes the
	// spell rather than leaving two numbers to keep in step by hand.
	const int amount = desc.manaCost;

	receiver.health += amount;

	if (receiver.health > receiver.threshold)
		receiver.health = receiver.threshold;
}

// The gate that stops Mend being cast at full health. Without it the mana is
// spent to discover the spell was pointless, which is the exact trap the
// canCast hook exists to close.
bool Weapon_SkullStaff::mendWouldHelp(const SpellDesc& /*desc*/) const
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");

	if (!player.isValid() || !player.hasComponent<DamageReceiverComponent>())
		return false;

	const auto& receiver = player.getComponent<DamageReceiverComponent>();

	return receiver.health < receiver.threshold;
}

void Weapon_SkullStaff::precache()
{
	// Every impact effect any spell in the table names. Walked rather than listed
	// so a new row cannot forget to precache itself and hitch on its first cast.
	for (int i = 0; i < s_spellCount; ++i)
	{
		if (s_spells[i].impactParticle)
			ParticleManager::Get()->precache(s_spells[i].impactParticle, _asset_psys(s_spells[i].impactParticle));
	}

	// equip/unequip are shared across weapons and preloaded by WeaponController.
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/plasma_rifle/fire.wav", true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/pulse_rifle/fire.wav",  true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/effect/explosion1.wav",        true);
	SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/dryfire.wav",           true);
}

void Weapon_SkullStaff::init()
{
	m_descriptor.name = "Player_Weapon_SkullStaff";
	m_descriptor.id = _entity_null_value;

	// skullstaff_animated.glb carries the same arms rig as the rest of the glTF
	// pack — identical joint names, identical 'arms' root at (0, 2.945, -17.671).
	// Held out in one hand, and the staff is 91 model units tall with the skull
	// at the TOP, so this sits low and to the right to keep the head in frame.
	// Tune with the viewmodel debug UI (F2), not by guessing here.
	m_viewPositionOffset = irr::core::vector3df(0.0350f, -0.2150f, 0.3300f);
	m_viewRotationOffset = irr::core::vector3df(0.00f, 180.00f, 12.00f);
	m_viewScaleOffset    = irr::core::vector3df(0.01f, 0.01f, 0.01f);

	m_mesh.mesh = _asset_glb("player/weapon/skullstaff_animated");

	m_mesh.trimesh = RenderManager::Get()->loadMesh(m_mesh.mesh);

	// Swap in the stand-in BEFORE the node is created — creating a node in the
	// failure branch and again below orphans the first one.
	const bool usingStandIn = (m_mesh.trimesh == nullptr);
	if (usingStandIn)
	{
		spdlog::warn("Weapon_SkullStaff::init(): failed to load mesh \"{}\", stand-in mesh loaded", m_mesh.mesh);
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
	// take (0-6.0s = frames 0-179 at 30 fps). NOTE THE ROOT: the staff hangs off
	// 'R_wrist_Goal' and is static relative to it, so the clip boundaries are the
	// wrist goal's rest returns — at 0, 60/61, 135/136, 155/156 and 179 — not the
	// 'skullstaff' node's, which never moves at all.
	//   0-60     drift under 2.1 units, jaw working 0-6.7 deg, eye
	//            looking about; starts and ends at rest            -> idle, and a
	//            REAL looping one — the only authored idle in the
	//            whole pack, so this weapon does not need its idle
	//            faked from a single pinned frame
	//   62-135   staff snaps to a raised pose on ONE frame, holds
	//            it while the jaw chatters 0.7-21.9 deg, eases back
	//            to rest over f127-135                             -> cast_channel
	//   136-155  a sharp jab out to (15, 10.5, -17.9), then settles
	//            back through the raised pose to rest              -> cast
	//   156-165  staff lowers away, out to (-6.1, -18.5, -16.9)    -> unequip
	//   165-179  the same pose easing back to rest                 -> equip
	//
	// THE CHANNEL SNAPS. There is no wind-up: f61 is the rest pose and f62 is
	// already fully raised. Nothing can be done about that in code, so the cast
	// flash and sound are triggered at the same instant to cover it — a spell is
	// allowed to snap in a way a rifle bolt is not. The jab at 136-155 has no
	// such problem and is the better-looking cast of the two.
	//
	// Looping clips MUST be flagged loop=true — a non-looping clip re-armed from
	// the end callback holds its last frame for one tick every cycle, which is a
	// visible hitch.
	m_mesh.animationList.emplace_back(sAnimationData("idle",         0,   60,  true));
	m_mesh.animationList.emplace_back(sAnimationData("cast_channel", 62,  135, false));
	m_mesh.animationList.emplace_back(sAnimationData("cast",         136, 155, false));
	m_mesh.animationList.emplace_back(sAnimationData("unequip",      156, 165, false));
	m_mesh.animationList.emplace_back(sAnimationData("equip",        165, 179, false));

	// Both glTF backends normalise keyframe times to 30 fps Irrlicht frames, so
	// the viewmodel must play at 30 to run at its authored speed.
	m_mesh.fps = 30;
	m_mesh.node->setAnimationSpeed(static_cast<irr::f32>(m_mesh.fps));

	m_mesh.node->setJointMode(irr::scene::EJUOR_READ);

	m_mesh.animation_call_back = std::make_shared<AnimationCallback>();
	m_mesh.node->setAnimationEndCallback(m_mesh.animation_call_back.get());

	playAnimation("idle");

	// Light, because unlike every other weapon here the idle clip is a real
	// two-second loop rather than one pinned frame. This only exists to stop that
	// loop reading as a loop — the sway itself is already in the animation.
	enableIdleBreathing(0.6f);

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
		spdlog::error("In function Weapon_SkullStaff::init() -> getEntityByName(\"player\") : Entity 'player' does not exist");

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
		spdlog::error("In function Weapon_SkullStaff::init() -> player.getComponent<CameraComponent>() : Entity 'player' does not have specified component");
	}

	RenderManager::Get()->registerViewmodelNode(m_mesh.node);
	m_mesh.node->setVisible(false);

	m_mana = m_manaMax;
	m_nextReadyTime.assign(s_spellCount, 0.0f);

	m_crosshair = RenderManager::Get()->driver()->getTexture("content/texture/ui/crosshair/crosshair033.png");

	// Spells leave from the skull's MOUTH, so the effects hang off 'jaw' — which
	// is animated, and chatters through both casts. That is deliberate: the glow
	// rides the jaw and the spell looks like the skull is spitting it, rather
	// than like a light bolted to a stick. The offset is the middle of the jaw's
	// front opening: the mesh spans Y -9.6 to 0.52 and Z -0.56 to 7.82, so the
	// mouth is around (0, -4.5, 7.0) and GltfImport's handedness conversion
	// negates Z.
	//
	// The flash colour is overwritten per cast from the spell's own lightColor,
	// so what is set here is only the state before the first spell goes off.
	WeaponEffectsDesc fx;
	fx.muzzleJointName   = "jaw";
	fx.muzzleJointOffset = irr::core::vector3df(0.0f, -4.5f, -7.0f);
	fx.flashColor        = irr::video::SColor(255, 255, 190, 90);
	fx.flashSize         = 0.8f;
	fx.flashDuration     = 110.0f; // longer than a gun's — this is a glow, not a bang
	fx.lightColor        = irr::video::SColorf(1.0f, 0.55f, 0.2f);
	fx.lightRadius       = 5.0f;
	fx.tracerPoolSize    = 0;
	fx.shellPoolSize     = 0;      // no brass; it is a staff
	fx.impactParticle    = nullptr; // detonations do their own theatre
	m_effects.init(m_mesh.node, fx);
}

void Weapon_SkullStaff::destroy()
{
	m_effects.destroy();

	RenderManager::Get()->unregisterViewmodelNode(m_mesh.node);
	m_mesh.node->remove();

	WorldManager::Get()->freeEntityID(m_descriptor.id);
}

// --- Spell selection ---------------------------------------------------------

void Weapon_SkullStaff::selectSpell(int index)
{
	if (index < 0 || index >= s_spellCount || index == m_spell)
		return;

	// Deliberately allowed mid-cast: the bolt already in the air remembers which
	// spell threw it (see m_projectileSpell), so switching cannot retroactively
	// change what is about to land.
	m_spell = index;
}

void Weapon_SkullStaff::cycleSpell()
{
	selectSpell((m_spell + 1) % s_spellCount);
}

// --- State -------------------------------------------------------------------

void Weapon_SkullStaff::enterState(State next)
{
	m_state = next;
	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag

	switch (next)
	{
	case State::Casting:
		setClipSpeed(spell().castSpeed);
		playAnimation(spell().castClip);
		m_spellReleased = false;
		m_castStartTime = Engine::Get()->getCurrentTime();
		break;

	case State::Equipping:
		setClipSpeed(1.0f);
		playAnimation("equip");
		break;

	case State::Unequipping:
		setClipSpeed(1.0f);
		playAnimation("unequip");
		break;

	case State::Idle:
	default:
		setClipSpeed(1.0f);
		playAnimation("idle");
		break;
	}
}

// --- Frame loop --------------------------------------------------------------

void Weapon_SkullStaff::update()
{
	if (!m_mesh.node || !m_mesh.node->isVisible())
		return;

	const float now  = Engine::Get()->getCurrentTime();
	const float dt   = Engine::Get()->getDeltaTime();
	const float dt_s = dt * 0.001f;

	const bool animEnded = m_mesh.animation_call_back->hasAnimationEnded();

	// Mana regenerates in every state, including mid-cast and while the staff is
	// being drawn — it is a property of the caster, not of the weapon's idleness.
	m_mana += m_manaRegen * dt_s;
	if (m_mana > m_manaMax)
		m_mana = m_manaMax;

	switch (m_state)
	{
	// Holstering: stay visible until the clip finishes so the staff is seen going
	// down. isUnequipping() going false releases WeaponController's pending
	// switch, so the next weapon is only drawn once this one is away.
	case State::Unequipping:
		if (animEnded)
			unequip();
		return;

	case State::Equipping:
		if (animEnded)
			enterState(State::Idle);
		drawSpellHud();
		RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
		return;

	case State::Casting:
		// The spell leaves the skull part-way through the clip, on a WALL-CLOCK
		// delay rather than an animation frame. Frame-derived triggers are what
		// the LMG shipped with and they did not fire where the .glb analysis said
		// they would; a delay in milliseconds cannot drift from the clip speed.
		if (!m_spellReleased && (now - m_castStartTime) >= spell().releaseDelayMs)
		{
			m_spellReleased = true;
			castSpell();
		}

		if (animEnded)
		{
			// A cast that somehow ended before its own release still has to let
			// the spell go, or the mana was spent for nothing.
			if (!m_spellReleased)
			{
				m_spellReleased = true;
				castSpell();
			}

			enterState(State::Idle);
		}
		break;

	case State::Idle:
	default:
		break;
	}

	// --- Idle: input is live -------------------------------------------------

	const bool rmb = InputManager::Get()->isMouseButtonPressed(MB_RIGHT);

	// Edge-triggered, so holding right mouse cycles once rather than running
	// through the spellbook at the frame rate.
	if (rmb && !m_cyclePressed)
		cycleSpell();

	m_cyclePressed = rmb;

	if (m_state == State::Idle && InputManager::Get()->isMouseButtonPressed(MB_LEFT))
	{
		const SpellDesc& s = spell();

		if (canCastCurrent())
		{
			m_mana -= static_cast<float>(s.manaCost);
			m_nextReadyTime[m_spell] = now + s.cooldownMs;

			enterState(State::Casting);
		}
		else if (now >= m_nextReadyTime[m_spell])
		{
			// Refused for a reason that is not the cooldown — no mana, or the
			// spell's own gate said no. Worth a sound either way, because the
			// staff otherwise fails silently and reads as broken input. Rate
			// limited by borrowing the cooldown slot so it cannot machine-gun.
			m_nextReadyTime[m_spell] = now + 350.0f;

			SoundManager::Get()->sound()->playRandomized2D(
				"content/sound/weapon/dryfire", 0.05f, 1, -1.0f, "staff_nomana");
		}
	}

	drawSpellHud();

	RenderManager::Get()->renderImage2D(m_crosshair, _weapon_crosshair_center_position);
}

void Weapon_SkullStaff::persist()
{
	const float dt = Engine::Get()->getDeltaTime();

	// Bolts keep flying and detonating while the staff is holstered — persist()
	// runs for every weapon every frame, which is the whole reason in-flight
	// ordnance lives here rather than in update().
	updateProjectiles(dt);

	m_effects.update(dt);
}

// The current spell and the mana pool, drawn under the crosshair. There is no
// HUD to put them in — HUDController's ammo block is commented out — and a
// spellbook the player cannot read is not a spellbook.
void Weapon_SkullStaff::drawSpellHud()
{
	const auto& cfg = RenderManager::Get()->getConfiguration();

	const int cx = cfg.width / 2;
	const int cy = cfg.height / 2;

	const SpellDesc& s = spell();

	// Dimmed while the spell cannot actually be cast — including when its own
	// canCast gate refuses it — so the reason a click did nothing is on screen
	// rather than something to work out by ear.
	const irr::video::SColor nameColor = canCastCurrent()
		? irr::video::SColor(230, 225, 225, 255)
		: irr::video::SColor(150, 140, 140, 160);

	irr::core::stringw name(s.name);
	RenderManager::Get()->renderText2D(
		name, TEXT_DEFAULT_FONT::SMALL,
		irr::core::vector2di(cx, cy + 46), nameColor, true, false);

	irr::core::stringw mana(L"Mana ");
	mana += static_cast<irr::s32>(m_mana);
	mana += L" / ";
	mana += static_cast<irr::s32>(m_manaMax);

	RenderManager::Get()->renderText2D(
		mana, TEXT_DEFAULT_FONT::SMALL,
		irr::core::vector2di(cx, cy + 62),
		irr::video::SColor(190, 150, 200, 255), true, false);
}

void Weapon_SkullStaff::equip()
{
	m_mesh.node->setVisible(true);

	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag

	m_spellReleased = false;
	m_cyclePressed  = true; // don't cycle on a right button already held through the switch
	resetViewKick();

	playEquipSound();

	enterState(State::Equipping);

	if (!m_mesh.findAnimation("equip"))
		enterState(State::Idle);
}

void Weapon_SkullStaff::unequip()
{
	m_state = State::Idle;
	setClipSpeed(1.0f);
	m_mesh.node->setVisible(false);
}

void Weapon_SkullStaff::startUnequip()
{
	// Already hidden, or mid-holster: nothing to play, don't restart the clip
	if (!m_mesh.node || !m_mesh.node->isVisible() || m_state == State::Unequipping)
		return;

	setClipSpeed(1.0f);

	playUnequipSound();

	m_mesh.animation_call_back->hasAnimationEnded(); // consume stale flag

	if (m_mesh.findAnimation("unequip"))
		enterState(State::Unequipping);
	else
		unequip();
}

void Weapon_SkullStaff::idle()
{

}

void Weapon_SkullStaff::move()
{

}

// PlayerWeapon's fire() is the "primary action" hook. Casting is driven from
// update() because it has to be gated on mana and cooldown together, so this
// exists only so the base interface is satisfied and any future caller that
// pokes fire() directly does the same thing a click would.
void Weapon_SkullStaff::fire()
{
	if (m_state != State::Idle)
		return;

	const float now = Engine::Get()->getCurrentTime();
	const SpellDesc& s = spell();

	if (now < m_nextReadyTime[m_spell] || m_mana < static_cast<float>(s.manaCost))
		return;

	m_mana -= static_cast<float>(s.manaCost);
	m_nextReadyTime[m_spell] = now + s.cooldownMs;

	enterState(State::Casting);
}

// Nothing to reload — mana regenerates. Left empty rather than absent because
// WeaponController drives reload() on every weapon from the remappable action.
void Weapon_SkullStaff::reload()
{

}

// =============================================================================
// Casting
//
// THIS IS THE EXTENSION POINT. A spell with a new SpellBehaviour gets a case
// here; a spell that only differs in numbers, colour, clip or cost needs nothing
// but its row in s_spells[].
// =============================================================================
void Weapon_SkullStaff::castSpell()
{
	const SpellDesc& s = spell();

	// Force the hierarchy up to date so the jaw bone the effects hang off is
	// where the animation says, not where it was last drawn.
	m_mesh.node->updateAbsolutePosition();
	m_mesh.node->animateJoints();

	switch (s.behaviour)
	{
	case SPELL_PROJECTILE:
		spawnSpellBolt(s, m_spell);
		break;

	// Nothing leaves the staff. Not an error and not unimplemented: this is the
	// delivery a utility spell wants, and its onCast hook below is the spell.
	case SPELL_SELF:
		break;

	// case SPELL_HITSCAN:
	//     A beam: raycast from m_effects.muzzleWorldPosition() along
	//     getAimDirection(), damage what it hits, draw the beam with
	//     m_effects.spawnTracer(). Nothing else in this class needs to change.
	//
	// case SPELL_SELF:
	//     A buff or heal: no projectile at all — act on the player entity
	//     directly. The cast animation, mana cost and cooldown already work.

	default:
		spdlog::warn("Weapon_SkullStaff: spell '{}' has behaviour {} which castSpell() does not implement",
			s.name, static_cast<int>(s.behaviour));
		break;
	}

	// The spell's own hook, AFTER the delivery, so the two compose: a utility
	// spell is SPELL_SELF plus this, and a bolt with a rider effect is
	// SPELL_PROJECTILE plus this.
	if (s.onCast)
		(this->*s.onCast)(s);

	// Shared presentation, whatever the behaviour: the skull lights up in the
	// spell's own colour and something is heard. Taken from the table rather than
	// hardcoded so a new spell gets its own look for free.
	m_effects.muzzleFlash();

	if (s.castSound)
		SoundManager::Get()->sound()->playRandomized2D(s.castSound, 0.06f, 3, 0.8f, "staff_cast");

	// Recoil scaled by what the spell cost — a cantrip should not shove the
	// camera the way a heavy cast does, and manaCost is already a decent proxy
	// for how big the spell is meant to feel.
	const float weight = static_cast<float>(s.manaCost) / 30.0f;

	g_CameraFX.addRecoil(-1.4f * weight, Engine::Get()->rng()->getFloat(-0.3f, 0.3f));
	g_CameraFX.addFovKick(1.2f * weight);

	addViewKick(
		irr::core::vector3df(0.0f, 0.02f * weight, -0.07f * weight),
		irr::core::vector3df(3.0f * weight, 0.0f,
			Engine::Get()->rng()->getFloat(-1.5f, 1.5f)));
}

void Weapon_SkullStaff::spawnSpellBolt(const SpellDesc& desc, int spellIndex)
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<CameraComponent>())
		return;

	// Out of the skull's mouth — the same point the flash is drawn at, so the
	// bolt and the glow cannot disagree.
	const irr::core::vector3df spawnPos = m_effects.muzzleWorldPosition();

	// Converge on the crosshair. The mouth sits well off screen centre on a staff
	// held out to one side, so a parallel camera-forward ray would land visibly
	// wide of where the player is pointing.
	const irr::core::vector3df direction = getAimDirection(spawnPos);

	anax::Entity boltEntity = WorldManager::Get()->managerSystem()->getWorld().createEntity();

	boltEntity.addComponent<DescriptorComponent>();
	auto& descriptor          = boltEntity.getComponent<DescriptorComponent>();
	descriptor.id             = WorldManager::Get()->getNewID();
	descriptor.name           = "spell_bolt_" + std::to_string(descriptor.id);
	descriptor.type           = ET_DYNAMIC;
	descriptor.isSerializable = false;

	boltEntity.addComponent<TransformComponent>();
	auto& transform           = boltEntity.getComponent<TransformComponent>();
	transform.position        = spawnPos;
	transform.initialPosition = spawnPos;

	const irr::core::vector3df initialRotation = direction.getHorizontalAngle();
	transform.rotation        = initialRotation;
	transform.initialRotation = initialRotation;

	boltEntity.addComponent<RenderComponent>();
	boltEntity.getComponent<RenderComponent>().isVisible = true;

	// No mesh of its own: the bolt IS the particle trail and the light. A solid
	// core would only fight the additive glow, and there is no spell-ball model
	// in the project to use.
	boltEntity.addComponent<LightComponent>();
	auto& light         = boltEntity.getComponent<LightComponent>();
	light.type          = LT_POINT;
	light.visible       = true;
	light.radius        = desc.lightRadius;
	light.color_diffuse = desc.lightColor;
	light.offset        = irr::core::vector3df(0.0f, 0.0f, 0.0f);

	boltEntity.activate();

	WeaponProjectile proj;
	proj.speed            = desc.speed;
	proj.useTracking      = false;
	proj.targetId         = _entity_null_value;
	proj.distanceTraveled = 0.0f;
	proj.isTrackingActive = false;
	proj.entity           = boltEntity;
	proj.velocity         = direction * desc.speed;
	proj.previousPosition = spawnPos;
	proj.trailParticles   = nullptr;
	proj.isBouncing       = false;
	proj.maxLifetime      = desc.lifetimeMs;

	m_projectiles.emplace_back(proj);

	// Parallel to m_projectiles: a bolt in the air has to remember which spell
	// threw it, because the player can cycle spells while it is still flying and
	// its damage and detonation must not change under it.
	m_projectileSpell.emplace_back(spellIndex);
}

void Weapon_SkullStaff::updateProjectiles(float dt)
{
	for (size_t i = 0; i < m_projectiles.size();)
	{
		WeaponProjectile& proj = m_projectiles[i];

		// The spell this bolt was cast with, not the one currently equipped
		const SpellDesc& desc = s_spells[
			(m_projectileSpell[i] >= 0 && m_projectileSpell[i] < s_spellCount) ? m_projectileSpell[i] : 0];

		bool remove = false;

		if (!proj.entity.isValid() || !proj.entity.hasComponent<TransformComponent>())
		{
			++i;
			continue;
		}

		auto& transformComp = proj.entity.getComponent<TransformComponent>();

		if (!transformComp.node)
		{
			++i;
			continue;
		}

		// Trail, created once the transform node exists. Additive and unlit, so
		// it reads as light rather than as smoke.
		if (!proj.trailParticles)
		{
			auto* ps = RenderManager::Get()->sceneManager()->addParticleSystemSceneNode(false, transformComp.node);

			auto* emitter = ps->createPointEmitter(
				irr::core::vector3df(0, 0, 0),
				40, 70,
				desc.trailColorFrom,
				desc.trailColorTo,
				200, 420,
				4,
				irr::core::dimension2df(0.18f, 0.18f),
				irr::core::dimension2df(0.34f, 0.34f));

			ps->setEmitter(emitter);
			emitter->drop();

			auto* fade = ps->createFadeOutParticleAffector();
			ps->addAffector(fade);
			fade->drop();

			ps->setMaterialFlag(irr::video::EMF_LIGHTING,      false);
			ps->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE, false);
			ps->setMaterialType(m_trailMaterialType);

			auto* tex = RenderManager::Get()->driver()->getTexture(desc.trailTexture);
			if (!tex)
				tex = RenderManager::Get()->driver()->getTexture("content/texture/color/magenta.png");
			if (tex)
				ps->setMaterialTexture(0, tex);

			ps->setPosition(irr::core::vector3df(0, 0, 0));
			proj.trailParticles = ps;
		}

		const irr::core::vector3df currentPos = transformComp.getPosition();

		// Swept raycast, so a fast bolt cannot tunnel through a wall between frames
		const float sphereRadius = 0.2f;
		irr::core::vector3df rayStart = proj.previousPosition;
		irr::core::vector3df rayEnd   = currentPos;

		irr::core::vector3df rayDir = (rayEnd - rayStart);
		const float rayLength = rayDir.getLength();
		if (rayLength > 0.001f)
		{
			rayDir.normalize();
			rayStart -= rayDir * sphereRadius;
			rayEnd   += rayDir * sphereRadius;
		}

		RaycastResultData hit = RenderManager::Get()->raycastWorldPosition(rayStart, rayEnd, true);

		if (hit.hit && hit.node)
		{
			const entityid hitEntityID = hit.node->getID();
			bool solid = false;

			auto& hitEntity = WorldManager::Get()->managerSystem()->getEntityByID(hitEntityID);

			if (hitEntity.isValid() && hitEntity.hasComponent<DescriptorComponent>())
			{
				auto& hitDescriptor = hitEntity.getComponent<DescriptorComponent>();

				// Never detonate on the bolt's own node
				const entityid selfID = proj.entity.hasComponent<DescriptorComponent>()
					? proj.entity.getComponent<DescriptorComponent>().id
					: _entity_null_value;

				if (hitDescriptor.id != selfID)
					solid = (hitDescriptor.type == ET_STATIC || hitDescriptor.type == ET_DYNAMIC);
			}
			else if (RenderManager::isWorldGeometryNode(hit.node))
			{
				solid = true;
			}

			if (solid)
			{
				detonate(desc, hit.point, hitEntityID, hit.normal);
				remove = true;
			}
		}

		if (!remove)
		{
			const float dtSeconds = dt / 1000.0f;

			proj.velocity.Y -= desc.gravity * dtSeconds;

			const irr::core::vector3df nextPos = currentPos + proj.velocity * dtSeconds;

			transformComp.position = nextPos;

			irr::core::vector3df dir = proj.velocity;
			dir.normalize();
			transformComp.rotation = dir.getHorizontalAngle();

			transformComp.node->setPosition(nextPos);
			transformComp.node->setRotation(transformComp.rotation);
			transformComp.node->updateAbsolutePosition();

			proj.previousPosition = currentPos;
			proj.lifetime += dt;

			// Spells fizzle rather than detonate when they simply run out — an
			// airburst at maximum range would be free damage on nothing.
			if (proj.lifetime >= proj.maxLifetime)
				remove = true;
		}

		if (remove)
		{
			if (proj.trailParticles)
			{
				proj.trailParticles->remove();
				proj.trailParticles = nullptr;
			}

			if (proj.entity.isValid() && proj.entity.hasComponent<DescriptorComponent>())
				WorldManager::Get()->killEntityByID(proj.entity.getComponent<DescriptorComponent>().id);

			m_projectiles.erase(m_projectiles.begin() + i);
			m_projectileSpell.erase(m_projectileSpell.begin() + i);
		}
		else
		{
			++i;
		}
	}
}

void Weapon_SkullStaff::detonate(const SpellDesc& desc, const irr::core::vector3df& pos,
                                 entityid directHitID, const irr::core::vector3df& surfaceNormal)
{
	SoundManager::Get()->sound()->playRandomized3D("content/sound/effect/explosion", pos, 0.06f);

	if (desc.impactParticle)
		ParticleManager::Get()->spawn(desc.impactParticle, irr2spk(pos));

	if (desc.splashDamage > 0.0f && desc.splashRadius > 0.0f)
		applySplashDamage(desc, pos, directHitID);

	// Light flash + scorch oriented to the hit surface + smoke + proximity shake,
	// in the spell's own colour. Radius and shake scale off the splash, so a
	// small spell does not put on a large spell's show.
	const float scale = desc.splashRadius > 0.0f ? desc.splashRadius : 1.5f;

	m_effects.explosionAt(pos, desc.lightColor,
		desc.lightRadius * 1.4f,
		1.2f * scale,          // shake peak
		scale * 1.6f,          // shake falloff distance
		220.0f,
		surfaceNormal);

	if (directHitID != _entity_null_value)
	{
		registerHitFeedback(WorldManager::Get()->gameplaySystem()->damageEntity(
			directHitID, static_cast<unsigned int>(desc.directDamage)));
	}
}

void Weapon_SkullStaff::applySplashDamage(const SpellDesc& desc,
                                          const irr::core::vector3df& epicentre,
                                          entityid directHitEntityID)
{
	// One feedback event per detonation regardless of how many entities it caught
	HIT_RESULT bestResult = HIT_RESULT::NONE;

	auto& entities = WorldManager::Get()->managerSystem()->getEntities();
	for (auto& entity : entities)
	{
		if (!entity.isValid()) continue;
		if (!entity.hasComponent<DescriptorComponent>()) continue;
		if (!entity.hasComponent<TransformComponent>()) continue;

		auto& desc_c = entity.getComponent<DescriptorComponent>();

		if (desc_c.id == directHitEntityID) continue;
		if (!desc_c.isAlive) continue;

		const irr::core::vector3df entityPos = entity.getComponent<TransformComponent>().getPosition();
		const float dist = (entityPos - epicentre).getLength();

		if (dist >= desc.splashRadius) continue;

		const float falloff = 1.0f - (dist / desc.splashRadius);
		const float damage  = desc.splashDamage * falloff;

		if (damage >= 1.0f)
		{
			HIT_RESULT r = WorldManager::Get()->gameplaySystem()->damageEntity(
				desc_c.id, static_cast<unsigned int>(damage));

			// Splash-damaging yourself is not a hit confirm
			if (desc_c.name != "player" && static_cast<int>(r) > static_cast<int>(bestResult))
				bestResult = r;
		}
	}

	registerHitFeedback(bestResult);
}

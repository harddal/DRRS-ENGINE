#pragma once

#include <vector>

#include "../WeaponData.h"
#include "WeaponEffects.h"

// ---------------------------------------------------------------------------
// Weapon_Launcher — break-action single-shot grenade launcher.
//
// NAMED Weapon_Launcher, not Weapon_GrenadeLauncher, because that name belongs
// to the older .b3d weapon which is deliberately left byte-identical (see the
// standing rule about not touching the deprecated set — one regression has
// already been caused by editing shared values in it). The ballistics,
// detonation and splash-damage code below is lifted from that class rather than
// rewritten: it works, and it is the part of a grenade launcher that is
// genuinely hard to get right.
//
// The character of the weapon is that ONE shot is all it has. Firing chains
// straight into the break-open reload — barrel tips down, the spent case is
// thrown clear, a fresh round drops in, the barrel latches — so every shot
// costs the full cycle. That chain is the whole design, so fire() cannot
// return to Idle on its own; see enterState().
// ---------------------------------------------------------------------------
class Weapon_Launcher : public PlayerWeapon
{
public:
	void precache();
	void init();
	void update();
	void persist();
	void destroy();
	void equip();
	void unequip();
	void startUnequip() override;
	bool isUnequipping() const override { return m_state == State::Unequipping; }
	void idle();
	void move();
	void fire();
	void reload();

private:
	// Explicit state machine, like the shotgun's and the sniper's: fire ALWAYS
	// owes the break-open cycle, and a set of independent booleans cannot express
	// "this clip owes another clip".
	enum class State
	{
		Idle,
		Equipping,
		Unequipping,
		Firing,   // "fire"   -> Reloading, always
		Reloading // "reload" -> Idle
	};

	State m_state = State::Idle;

	// Single chamber. Not a count: there is exactly one round, and the only
	// question is whether it is there. An infinite reserve, like every other
	// weapon in this pack — the magazine is the limit, not the supply.
	bool m_loaded = true;

	bool m_firedThisPress = false; // one shot per click on each trigger

	// The authored break-open is a deliberate 3.2 s. Trimmed rather than
	// re-authored, and still by far the longest thing this weapon does — being
	// caught with the barrel open is what it trades its damage for.
	static constexpr float m_reloadSpeed = 1.45f; // -> 2.2 s

	// --- The round in the breech ---------------------------------------------
	// ONE 'shell' mesh plays both parts, like the sniper's 'bullet' and the
	// shotgun's 'slug': the extractor lifts it proud of the open breech, it is
	// flicked clear as the spent case, and the same mesh comes back down as the
	// fresh round. It has to be hidden across that flick or it visibly teleports
	// out of mid-air and back into the barrel.
	//
	// 'projectile' is a CHILD of 'shell' — the warhead sitting in the case mouth
	// — so it gets its own visibility: present on a live round, gone on the spent
	// case that is thrown away, because it is downrange by then. That one detail
	// is most of what sells the reload as a real break action.
	MeshPart m_shell;
	MeshPart m_warhead;

	// Driven off the shell's MEASURED displacement, not off frame numbers. The
	// LMG shipped with frame-derived part triggers and they did not fire at the
	// frames the .glb analysis predicted; asking the joint where the part
	// actually is cannot be wrong about that, needs no constant kept in step
	// with the asset, and reads the same at any clip speed.
	irr::core::vector3df m_shellRest;
	bool                 m_shellRestValid = false;
	bool                 m_caseThrown     = false;

	// Model units. The extractor lifts the case 5.7 and holds it there for a
	// dozen frames — that pose is CORRECT and must stay visible — and only the
	// flick past it, out to 58.4, means the case has actually been discarded.
	// The threshold therefore sits above the proud position, not above zero.
	static constexpr float m_shellThrownEpsilon = 8.0f;

	// --- Ballistics (lifted from Weapon_GrenadeLauncher) ---------------------
	// Exposed to the viewmodel debug window through debugBallistics(), because
	// whether a shot reads as a grenade or as a mortar is judged by eye.
	float m_projectileSpeed = 50.0f; // launch speed; widens and flattens the arc
	float m_lobAngle        = 0.15f; // fallback launch pitch when no arc solves
	float m_gravity         = 12.0f; // exaggerated over real 9.8 for game pacing
	float m_maxAimRange     = 1000.0f;
	const float m_spawnOffset = 0.5f;

	float m_pointDamage  = 70.0f;
	float m_splashDamage = 60.0f;
	float m_splashRadius = 4.0f;
	float m_splashForce  = 0.8f;

	// Right mouse lobs a BOUNCING grenade instead: it skips once off the first
	// surface and detonates on the second contact or on a shorter timer. The old
	// class already supported this and nothing used it — it is the reason to
	// have a grenade launcher rather than a rocket launcher, so it is bound here.
	bool m_bounceThisPress = false;

	std::vector<WeaponProjectile> m_projectiles;

	irr::video::E_MATERIAL_TYPE m_particleTrailMaterialType =
		irr::video::E_MATERIAL_TYPE::EMT_TRANSPARENT_ALPHA_CHANNEL;

	// --- Audio ---------------------------------------------------------------
	// Seconds from the start of each .wav to the transient that should land on
	// the visual event. SECONDS, not frames: the reload runs at m_reloadSpeed, so
	// the frame count a fixed lead-in spans differs from 1x. Convert with
	// soundLeadFrames() at the point of use, never with a constant.
	static constexpr float m_cockLeadSec      = 0.047f; // cock_rifle.wav peak
	static constexpr float m_insertShellLeadSec = 0.306f; // insert_shell.wav peak

	// Frames within the reload clip, measured off the .glb (see init())
	static const int m_latchOpenFrame  = 24; // 'lock' has turned its full 60 deg
	static const int m_breakOpenFrame  = 36; // 'front' reaches 45 deg — barrel open
	static const int m_seatFrame       = 78; // fresh round home in the breech
	static const int m_latchShutFrame  = 98; // barrel closed and locked

	bool m_latchOpenPlayed = false;
	bool m_breakOpenPlayed = false;
	bool m_seatPlayed      = false;
	bool m_latchShutPlayed = false;

	WeaponEffects m_effects;

	irr::video::ITexture* m_crosshair = nullptr;

	void enterState(State next);
	void updateShell();
	void ejectSpentCase();
	void updateReloadSounds(float frame);

	// Lifted wholesale from Weapon_GrenadeLauncher — see the note at the top.
	void spawnProjectile(bool bounce);
	void updateProjectiles(float dt);
	// surfaceNormal: impact normal for contact detonations (scorch orientation);
	// zero vector for timer detonations (floor probe fallback)
	void detonateAt(const irr::core::vector3df& pos, entityid directHitID,
		const irr::core::vector3df& surfaceNormal = irr::core::vector3df(0.0f, 0.0f, 0.0f));
	void applySplashDamage(const irr::core::vector3df& epicentre, entityid directHitEntityID);

public:
	WeaponEffects* debugEffects() override { return &m_effects; }

	bool debugBallistics(BallisticTuning& out) override
	{
		out.speed       = &m_projectileSpeed;
		out.gravity     = &m_gravity;
		out.maxAimRange = &m_maxAimRange;
		return true;
	}
};

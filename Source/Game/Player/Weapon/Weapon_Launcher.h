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
//
// Left mouse launches the grenade, which throws a SPHERE of casing fragments
// when it goes off. Right mouse spends the SAME round on a flak burst instead: a
// cone of that shrapnel straight out of the barrel. One shell, two entirely
// different answers to "what is in front of me" — a grenade for the room and a
// canister for the doorway — and both cost the full 2.2-second break-open, which
// is what keeps the choice interesting.
//
// Both fragment types are the same pool running the same physics (see Shard):
// incandescent, ricocheting, cooling from hot yellow to grey steel, losing half
// their bite on every bounce, and dangerous to the player once armed.
// ---------------------------------------------------------------------------
class Weapon_Launcher : public PlayerWeapon
{
public:
	// --- Capability bits -----------------------------------------------------
	// This weapon's own unlock vocabulary. Public because the skill table names
	// these by symbol (Weapon_Launcher::UNLOCK_FLAK) rather than by a magic
	// integer, which is what makes a mistyped bit a compile error instead of a
	// dead upgrade nobody notices.
	//
	// UNLOCK_FLAK is the alt fire. Until it is bought, right mouse lobs an
	// ordinary grenade and this is a plain grenade launcher.
	//
	// UNLOCK_AIRBURST is declared but NOT IMPLEMENTED — there is no airburst
	// behaviour to gate yet. It is here to show where the next one goes; do not
	// add a skill row for it until the behaviour exists, or skill_validate will
	// (correctly) start complaining.
	enum
	{
		UNLOCK_FLAK     = 0,
		UNLOCK_AIRBURST = 1
	};

	unsigned int supportedStats() const override
	{
		return WSTAT_BIT(WSTAT_DAMAGE)
		     | WSTAT_BIT(WSTAT_SPLASH_DAMAGE)
		     | WSTAT_BIT(WSTAT_SPLASH_RADIUS)
		     | WSTAT_BIT(WSTAT_SPLASH_FORCE)
		     | WSTAT_BIT(WSTAT_PROJECTILE_SPEED)
		     | WSTAT_BIT(WSTAT_RELOAD_SPEED)
		     | WSTAT_BIT(WSTAT_ALT_DAMAGE)
		     | WSTAT_BIT(WSTAT_PELLETS)
		     | WSTAT_BIT(WSTAT_ACCURACY);
	}

	// Starts with nothing: the flak has to be earned.
	unsigned int defaultUnlocks() const override { return 0; }

	const char* unlockName(int bit) const override
	{
		switch (bit)
		{
		case UNLOCK_FLAK:     return "Flak Canister";
		case UNLOCK_AIRBURST: return "Airburst";
		default:              return nullptr;
		}
	}

	// True while the alt fire is available AND the player is asking for it. The
	// HUD reads this too, so a locked alt fire is not advertised by a prompt that
	// does nothing.
	bool flakUnlocked() const { return hasUnlock(UNLOCK_FLAK); }

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

	int displayAmmo() const override { return m_loaded ? 1 : 0; }
	// Magazine contents for the save sidecar. See WeaponMagState — slot 0 is this
	// weapon's only counter.
	void saveMagState(WeaponMagState& out) const override { out.slots[0] = m_loaded ? 1 : 0; }
	void loadMagState(const WeaponMagState& in) override  { if (in.slots[0] >= 0) m_loaded = in.slots[0] > 0; }

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

	float m_pointDamage  = 200.0f;
	float m_splashDamage = 100.0f;
	float m_splashRadius = 4.0f;
	float m_splashForce  = 1.6f; // x2 for real-time physics, see PHYSX_DEFAULT_GRAVITY

	std::vector<WeaponProjectile> m_projectiles;

	// --- Alt fire: flak shrapnel ---------------------------------------------
	// Right mouse spends the same round on a cone of glowing steel instead of a
	// grenade — the UT flak cannon's primary. Nothing about it is a projectile in
	// the WeaponProjectile sense: there is no ECS entity, no mesh component and no
	// light per shard, because fourteen of those per shot times three shots in the
	// air is a hundred and twenty entities for two seconds of theatre. The shards
	// are a fixed pool of scene nodes running the same fake physics the shell
	// casings do, which is the pattern this codebase already trusts for "lots of
	// small things that bounce and then go away".
	//
	// The fragment is a plain lit metal chunk and the HEAT IS THE HALO — the child
	// billboard is the only thing that changes colour. That is why the slots share
	// the loaded meshes rather than each owning a private copy: nothing is per-shard
	// about the geometry, only the transform and the halo's tint.
	struct Shard
	{
		irr::scene::IMeshSceneNode*      node = nullptr;
		irr::scene::IBillboardSceneNode* glow = nullptr;  // additive halo — this is the heat

		irr::core::vector3df velocity;        // units/second
		irr::core::vector3df angularVelocity; // degrees/second
		irr::core::vector3df rotation;

		float age    = 0.0f;  // ms since launch; drives the cooling curve
		float damage = 0.0f;  // decays with every ricochet — a spent fragment should be spent
		int   bounces = 0;
		bool  active  = false;
		bool  settled = false; // came to rest on a surface; no longer simulated at all

		// Whether this fragment is allowed to hit the PLAYER. A muzzle burst starts
		// disarmed and arms on its first ricochet, because a shard leaving the
		// barrel is inside its owner and would otherwise cost health just for
		// pulling the trigger. Casing fragments from a detonation are armed from
		// the start: the blast is not inside the player, and a grenade you dropped
		// at your own feet is supposed to hurt.
		bool  armed = false;

		float scale     = 0.1f;    // per-shard size, randomized at launch
		float fadeStart = 0.0f;    // ms at which it starts shrinking away; see updateShrapnel()
	};

	std::vector<Shard> m_shrapnel;
	bool m_shrapnelPoolReady = false;

	bool m_shrapnelThisPress = false; // this shot was the alt fire, latched at the press

	// Fragments lie on the floor for a full minute, so the pool is no longer
	// sized by "how many can be in the air" — a minute of sustained fire is far
	// more bursts than any pool wants to hold. It holds eight bursts' worth and
	// fireShrapnel() recycles the OLDEST shard when it runs dry, so the shot the
	// player just took always comes out at full count and it is old debris that
	// quietly goes away.
	static const int m_shardsPerBurst = 14;
	static const int m_shardPoolSize  = m_shardsPerBurst * 8;

	// Per-shard, so the burst total is 14x this at point-blank. Landing five or
	// six is the realistic case at usable range, which is where the ~200 comes
	// from; the full 476 is the reward for being close enough to be in trouble.
	float m_shardDamage      = 34.0f;
	float m_shardSpeed       = 110.0f;
	float m_shardSpeedVar    = 0.35f;  // ± fraction, so the cone stretches as it flies
	float m_shardSpread      = 7.5f;   // cone HALF-angle, degrees
	float m_shardGravity     = 9.0f;   // lighter than the grenade's — these are fast and short-lived
	float m_shardBounceLoss  = 0.55f;  // fraction of speed kept per ricochet
	float m_shardDamageLoss  = 0.5f;   // fraction of damage kept per ricochet
	int   m_shardMaxBounces  = 4;
	float m_shardSettleSpeed = 3.0f;    // below this after a bounce, it stops where it landed

	// A full minute, so a burst leaves a scatter of cooled metal on the floor to
	// walk through rather than a two-second effect. Nothing simulates for that
	// long: a shard settles within a second or so and is then inert until it is
	// either recycled by a later burst or shrinks away at the end.
	float m_shardLifetime    = 60000.0f; // ms
	float m_shardFadeTime    = 400.0f;   // ms spent shrinking away at the end of that
	float m_shardCoolTime    = 1500.0f;  // ms from bright orange to cold grey

	// A shard still in the air after this has been fired into open sky or out of
	// the level, and it is the only thing in the system that costs a raycast per
	// frame. Cut it loose rather than let it fall for the remaining 52 seconds:
	// the long lifetime is for debris on the floor, not for tumbling specks.
	float m_shardMaxFlight   = 8000.0f;  // ms
	float m_shardScaleMin    = 0.05f;   // gib hulls are normalised to ~2 units across
	float m_shardScaleMax    = 0.09f;

	// --- Casing fragments from a detonation ----------------------------------
	// The PRIMARY fire's grenade throws these spherically when it goes off, which
	// is the same system the alt fire uses and deliberately so: one shrapnel pool,
	// one set of physics, two ways in. Weaker per fragment than the flak shell —
	// there are more of them, they go in every direction so most of them miss, and
	// the grenade already carries 200 point plus 100 splash without any of this.
	int   m_fragCount  = 18;
	float m_fragSpeed  = 85.0f;
	float m_fragDamage = 15.0f;

	// Ricochet cues are throttled as a group, not per shard: fourteen fragments
	// hitting a corner within one frame would otherwise stack fourteen voices and
	// fourteen particle systems on the same tick.
	float m_lastShardSound = 0.0f;
	float m_lastShardSpark = 0.0f;

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
	void spawnProjectile();
	void updateProjectiles(float dt);

	// Alt fire. ensureShrapnelPool() is lazy rather than part of init() because it
	// builds 42 scene nodes and 42 mesh copies, and a player who never presses the
	// right button should not pay for them.
	bool ensureShrapnelPool();
	void destroyShrapnelPool();

	// A free pool slot, or the OLDEST shard when the pool is full. Never null once
	// the pool has nodes, so a burst is never short-changed by old debris.
	Shard* acquireShard();

	// Puts one fragment in the air. Every launch goes through here so the alt
	// fire's cone and the grenade's casing burst cannot drift apart.
	void launchShard(Shard& shard, const irr::core::vector3df& origin,
		const irr::core::vector3df& dir, float speed, float damage, bool armed);

	void fireShrapnel();                                   // alt fire: cone from the muzzle
	void spawnShrapnelBurst(const irr::core::vector3df& origin,  // detonation: sphere
		const irr::core::vector3df& surfaceNormal);
	void updateShrapnel(float dt);

	// Colour of the additive HALO, which is the only thing that carries the heat:
	// hot yellow -> orange -> red, with the intensity taking it to nothing.
	static irr::video::SColor shardGlowColor(float heat);
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

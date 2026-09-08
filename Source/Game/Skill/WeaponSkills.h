#pragma once

// ---------------------------------------------------------------------------
// WeaponSkills.h — the vocabulary the skill system and the weapons share.
//
// Deliberately free of every other include. WeaponData.h pulls this in so that
// PlayerWeapon can declare its accessors, and SkillSystem.h pulls in BOTH this
// and WeaponData.h; putting PLAYER_WEAPON-dependent types (SkillDef) in here
// instead would make that circular.
//
// Two kinds of skill live in this system:
//
//   SEFF_STAT    scales or offsets a number the weapon already reads.
//   SEFF_UNLOCK  turns on a capability the weapon did not previously have —
//                an alt fire, a spell, a fire mode.
//
// They resolve into two different caches but are bought the same way and are
// rows in the same table.
// ---------------------------------------------------------------------------

// --- Stats -----------------------------------------------------------------
//
// THE SENSE CONVENTION, WHICH IS NOT NEGOTIABLE AND MUST NOT BE REVISITED:
//
//   Every stat here is defined so that a BIGGER multiplier is BETTER FOR THE
//   PLAYER. Always. Even when the underlying member runs the other way.
//
// Some raw members are bigger-is-better (damage); some are smaller-is-better
// (spread, and fire INTERVAL). If a skill row were written against the raw
// member, "+10% fire rate" would silently become "+10% interval" — i.e. SLOWER —
// and the bug would read as a balance opinion rather than as an inversion.
//
// So the inversion lives in exactly one place: PlayerWeapon::statInv(), which is
// what an interval or a spread is read through. See the per-stat notes.
enum WEAPON_STAT
{
	// Damage. Three of them, because a weapon with two fire modes genuinely has
	// three independent numbers — the launcher's direct hit, its splash, and its
	// flak shards — and collapsing them would make them impossible to tune apart.
	WSTAT_DAMAGE,           // direct/primary hit
	WSTAT_SPLASH_DAMAGE,    // area damage at the epicentre, before falloff
	WSTAT_ALT_DAMAGE,       // the alt fire's own damage number

	WSTAT_FIRE_RATE,        // higher = faster.  READ WITH statInv() — interval / mul
	WSTAT_ACCURACY,         // higher = tighter. READ WITH statInv() — spread / mul
	WSTAT_RECOIL_CONTROL,   // higher = less kick. READ WITH statInv()

	WSTAT_MAG_SIZE,         // integer; add-dominant
	WSTAT_RELOAD_SPEED,     // higher = faster. DIRECT multiply — it scales clip speed
	WSTAT_PROJECTILE_SPEED,
	WSTAT_SPLASH_RADIUS,
	WSTAT_SPLASH_FORCE,
	WSTAT_PELLETS,          // integer, add-only: shotgun pellets, flak shards
	WSTAT_RESERVE_MAX,      // ammo pool cap — keyed by AMMO_TYPE, not by weapon

	WSTAT_COUNT
};

// Bit for a stat, for PlayerWeapon::supportedStats(). WSTAT_COUNT must stay
// under 32 for this to keep working; there is room for a good while yet.
#define WSTAT_BIT(s) (1u << (static_cast<unsigned int>(s)))

const char* weaponStatName(WEAPON_STAT stat);

// --- Trees -----------------------------------------------------------------
//
// A tree is the unit skills are AUTHORED and PAID for in. It is deliberately
// NOT WEAPON_CATEGORY: the selection-bar buckets group weapons by how you reach
// one in a fight, which is not how weapons want to be upgraded. WEAPCAT_EXOTIC
// holds the crossbow and the skull staff, two weapons with nothing shareable
// between them.
//
// A tree may cover several weapons (the SMG and the dual SMGs share handling),
// and STREE_NONE is a perfectly good answer for a weapon that is not meant to
// be upgraded at all.
enum SKILL_TREE
{
	STREE_NONE = 0,   // no tree; this weapon cannot be upgraded
	STREE_MELEE,
	STREE_SIDEARM,
	STREE_SHOTGUN,
	STREE_SMG,
	STREE_RIFLE,
	STREE_LMG,
	STREE_SNIPER,
	STREE_LAUNCHER,
	STREE_CROSSBOW,
	STREE_STAFF,
	STREE_COUNT
};

const char* skillTreeName(SKILL_TREE tree);

// --- Effects ---------------------------------------------------------------
enum SKILL_EFFECT
{
	SEFF_STAT,    // scales/offsets a number
	SEFF_UNLOCK   // turns on a capability bit
};

enum STAT_OP
{
	SOP_ADD,      // effective = base + (value * rank)
	SOP_MUL       // effective = base * (1 + value * rank)
};

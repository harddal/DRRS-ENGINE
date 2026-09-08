#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "Game/Player/WeaponData.h"
#include "Game/Skill/WeaponSkills.h"

// ---------------------------------------------------------------------------
// SkillSystem — earns points, holds bought ranks, and resolves them into two
// flat caches the weapons read per shot.
//
// The single most important property: NOTHING IS EVER WRITTEN BACK INTO A
// WEAPON. Weapons keep their authored values as the BASE and read the effective
// value through PlayerWeapon::statf/stati/statInv/hasUnlock. That is what
// removes the whole class of stale-state bug that a write-through design has —
// there is no cached base to restore, no re-apply pass to forget to run after a
// save load, and no fight with the F2 viewmodel tuner, which goes on editing the
// base while the skill multiplies on top of it.
//
// Only RANKS are persisted. Both caches are derived and are rebuilt from the
// rank map on every change, so they can never drift out of step with the table.
// ---------------------------------------------------------------------------

// One row of the skill table. Flat POD with a discriminator and fields that only
// apply to one kind — the same shape as SpellDesc in Weapon_SkullStaff.h, which
// is already the house style for this sort of table and avoids needing a variant.
struct SkillDef
{
	const char*   id;           // STABLE STRING. This is the save-file key.
	const char*   name;
	const char*   description;

	// --- Placement: where the row appears, and which points gate it ----------
	SKILL_TREE    tree;
	int           tier;         // 1..N, gated by points spent in THIS tree
	const char*   prereq;       // id of a required skill, or nullptr

	// Prereqs MAY cross trees. 'prereq' is a plain id and is looked up globally,
	// so a launcher row can require something bought in the shotgun tree. That is
	// deliberate — see the note on findDef().

	int           maxRank;
	int           costPerRank;

	// --- Target: which weapons the row actually touches ----------------------
	// WEAP_COUNT means "every weapon in the tree", which is the common case. A
	// shared tree still needs single-weapon rows: an akimbo skill sitting in
	// STREE_SMG has no meaning for the single SMG.
	PLAYER_WEAPON target;

	SKILL_EFFECT  effect;

	// --- SEFF_STAT ----------------------------------------------------------
	WEAPON_STAT   stat;
	STAT_OP       op;
	float         valuePerRank;

	// --- SEFF_UNLOCK --------------------------------------------------------
	// Bit index in the TARGET weapon's own unlock enum. An unlock always names a
	// specific target: bit indices are declared per weapon and mean different
	// things in each, so there is no such thing as a tree-wide unlock.
	int           unlockBit;
};

class SkillSystem
{
public:
	// File-scope instance, deliberately not owned by a manager. It constructs to
	// identity values with no dependency on any other global, so there is no
	// init-order question to get wrong and nothing to remember to tick.
	static SkillSystem* Get();

	// --- Points -------------------------------------------------------------
	int  unspentPoints() const { return m_points; }
	int  spentPoints() const;
	int  spentInTree(SKILL_TREE tree) const;

	void awardPoints(int n);

	// --- Buying -------------------------------------------------------------
	int  rank(const char* id) const;

	// Why a row cannot be bought right now, for the UI tooltip and the console.
	// Returns true when it can. 'reason' is filled only on false.
	bool canBuy(const char* id, std::string* reason = nullptr) const;

	// Spends the cost and re-resolves. Returns false and changes nothing when
	// canBuy() would have refused.
	bool buy(const char* id);

	// Sets a rank outright, ignoring cost and prerequisites. This is the LOAD
	// path and the debug path — never the player-facing one.
	void setRank(const char* id, int rank);

	// Clears every rank. Points are NOT refunded: respec is deliberately not a
	// feature yet (see the plan). This exists for `skill_reset` and for starting
	// a new game, both of which want the points gone too.
	void resetAll(bool alsoClearPoints = true);

	// --- Resolved queries ---------------------------------------------------
	// Hot path: flat array indexing, safe to call per shot. Out-of-range weapons
	// and stats return identity rather than asserting, so a weapon that was never
	// registered simply behaves as if it had no skills.
	float statMul(PLAYER_WEAPON weapon, WEAPON_STAT stat) const;
	float statAdd(PLAYER_WEAPON weapon, WEAPON_STAT stat) const;
	bool  hasUnlock(PLAYER_WEAPON weapon, int bit) const;

	// The reserve cap is keyed by AMMO_TYPE, not by weapon, because it lives in
	// the free function ammoReserveMax() rather than on any weapon. It is the
	// only stat that needs its own path.
	float reserveMulti(AMMO_TYPE type) const;

	// --- Table access -------------------------------------------------------
	static const SkillDef* defs();
	static int             defCount();

	// First row carrying this id. Several rows MAY share one id — that is how a
	// multi-effect skill is written ("Flak Canister" is an unlock row plus a
	// splash-radius row), and they are bought and ranked together.
	const SkillDef* findDef(const char* id) const;

	// Every distinct id in the table, in table order.
	void allSkillIds(std::vector<std::string>& out) const;

	// --- Tier gating, for the UI --------------------------------------------
	// Points that must already be SPENT IN THE TREE before a row of this tier
	// can be bought. canBuy() enforces this; the panel needs to read it to draw
	// a locked tier's header, and re-deriving it there would give two tables to
	// keep in step. Clamped, so an out-of-range tier answers like the nearest
	// real one rather than reading off the end.
	static int tierRequirement(int tier);
	static int maxTier();

	// --- Save ---------------------------------------------------------------
	// Neutral shapes so PlayerSaveState needs no dependency on this header.
	void captureRanks(std::vector<std::pair<std::string, int>>& out) const;
	void applyRanks(const std::vector<std::pair<std::string, int>>& in, int unspent);

	// --- Diagnostics --------------------------------------------------------
	// Rebuilds both caches from the rank map. Called automatically by buy(),
	// setRank(), applyRanks() and resetAll() — exposed for the console.
	void resolve();

	// Flags rows that can never do anything: a stat the target weapon does not
	// read, an unlock bit the target does not name, a dangling prereq, a bad
	// tier. Fills 'problems' with one line each and returns the count.
	//
	// Needs the live weapons to ask supportedStats()/unlockName(), so it reaches
	// through g_PlayerController and reports a problem of its own if there is no
	// player yet.
	int validate(std::vector<std::string>& problems) const;

private:
	SkillSystem();

	struct StatMod
	{
		float add = 0.0f;
		float mul = 1.0f;
	};

	// Resolved, per weapon. Trees are flattened away by resolve() — nothing
	// downstream of it ever asks what tree a weapon is in.
	StatMod      m_stat[WEAP_COUNT][WSTAT_COUNT];
	unsigned int m_unlocks[WEAP_COUNT];
	float        m_reserveMul[AMMO_COUNT];

	std::map<std::string, int> m_ranks;   // id -> rank; only this is persisted
	int m_points = 0;                     // unspent

	// Puts both caches in their identity state. Separate from resolve() so the
	// constructor can use it without touching any other global — see the note
	// on the constructor.
	void resetCaches();

	// Applies one row at 'rank' into the caches.
	void applyRow(const SkillDef& def, int rank);

	// True when 'def' touches this weapon: same tree, and either untargeted
	// (WEAP_COUNT) or targeted at exactly it.
	static bool rowTouches(const SkillDef& def, PLAYER_WEAPON weapon);
};

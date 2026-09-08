#include "Game/Skill/SkillSystem.h"

#include <algorithm>

#include "spdlog/spdlog.h"

#include "Game/Player/PlayerController.h"
#include "Game/Player/WeaponController.h"

// ---------------------------------------------------------------------------
// Vocabulary names, for the console dumps and the UI.
// ---------------------------------------------------------------------------

const char* weaponStatName(WEAPON_STAT stat)
{
	switch (stat)
	{
	case WSTAT_DAMAGE:           return "damage";
	case WSTAT_SPLASH_DAMAGE:    return "splash_damage";
	case WSTAT_ALT_DAMAGE:       return "alt_damage";
	case WSTAT_FIRE_RATE:        return "fire_rate";
	case WSTAT_ACCURACY:         return "accuracy";
	case WSTAT_RECOIL_CONTROL:   return "recoil_control";
	case WSTAT_MAG_SIZE:         return "mag_size";
	case WSTAT_RELOAD_SPEED:     return "reload_speed";
	case WSTAT_PROJECTILE_SPEED: return "projectile_speed";
	case WSTAT_SPLASH_RADIUS:    return "splash_radius";
	case WSTAT_SPLASH_FORCE:     return "splash_force";
	case WSTAT_PELLETS:          return "pellets";
	case WSTAT_RESERVE_MAX:      return "reserve_max";
	default:                     return "?";
	}
}

const char* skillTreeName(SKILL_TREE tree)
{
	switch (tree)
	{
	case STREE_NONE:     return "none";
	case STREE_MELEE:    return "melee";
	case STREE_SIDEARM:  return "sidearm";
	case STREE_SHOTGUN:  return "shotgun";
	case STREE_SMG:      return "smg";
	case STREE_RIFLE:    return "rifle";
	case STREE_LMG:      return "lmg";
	case STREE_SNIPER:   return "sniper";
	case STREE_LAUNCHER: return "launcher";
	case STREE_CROSSBOW: return "crossbow";
	case STREE_STAFF:    return "staff";
	default:             return "?";
	}
}

namespace
{
	// Points that must already be spent IN THE SAME TREE before a tier opens.
	// Indexed by tier, so tier 1 is free and index 0 is never used. This is the
	// standard rule and needs no extra state — it is derived from the ranks that
	// are already stored.
	const int _tier_requirement[] = { 0, 0, 3, 7, 12, 18, 25 };
	const int _tier_max = static_cast<int>(sizeof(_tier_requirement) / sizeof(_tier_requirement[0])) - 1;

	// Sentinel for SkillDef::target meaning "every weapon in the tree". Spelled
	// out here because WEAP_COUNT reads as a bound rather than as a wildcard at
	// the call site.
	const PLAYER_WEAPON _target_all = WEAP_COUNT;

	// Unused-field filler, so a row's irrelevant half reads as deliberate rather
	// than as something forgotten.
	const WEAPON_STAT _no_stat   = WSTAT_COUNT;
	const int         _no_unlock = -1;

	// =======================================================================
	// THE SKILL TABLE
	// =======================================================================
	//
	// Rows are grouped by tree. Adding a skill is a row here and, if it is an
	// unlock, one `hasUnlock()` test in the weapon.
	//
	// Several rows MAY share an id: that is one skill in the UI, bought once,
	// with more than one effect. It is how a skill both unlocks something and
	// buffs a stat without needing an effect array with a magic bound.
	//
	// `prereq` is looked up globally, so it may name a skill in another tree.
	//
	const SkillDef _skills[] =
	{
	// ---------------------------------------------------------------- LAUNCHER
	//
	// The first tree implemented, and the one the machinery is tested against.
	// It exercises every mechanism exactly once: SOP_MUL, SOP_ADD, SEFF_UNLOCK,
	// a prereq, a multi-rank track and tier gating.

		{
			"launcher_heavier_charge",
			"Heavier Charge",
			"Packs more filler into the shell. Widens the blast radius.",
			STREE_LAUNCHER, 1, nullptr, 3, 1,
			_target_all,
			SEFF_STAT, WSTAT_SPLASH_RADIUS, SOP_MUL, 0.08f,
			_no_unlock
		},
		{
			"launcher_quick_break",
			"Quick Break",
			"Drilled reloads. The barrel comes open and shut faster.",
			STREE_LAUNCHER, 1, nullptr, 2, 1,
			_target_all,
			SEFF_STAT, WSTAT_RELOAD_SPEED, SOP_MUL, 0.10f,
			_no_unlock
		},

		// The unlock. Until this is bought, right mouse fires an ordinary
		// grenade — the launcher is a plain grenade launcher, which is the
		// point.
		{
			"launcher_flak",
			"Flak Canister",
			"Loads the shell as a canister. Alt fire scatters burning shrapnel "
			"instead of lobbing a grenade.",
			STREE_LAUNCHER, 2, nullptr, 1, 3,
			WEAP_LAUNCHER,
			SEFF_UNLOCK, _no_stat, SOP_ADD, 0.0f,
			Weapon_Launcher::UNLOCK_FLAK
		},
		{
			"launcher_flatter_arc",
			"Flatter Arc",
			"A hotter propellant charge. The grenade leaves faster and drops less.",
			STREE_LAUNCHER, 2, nullptr, 2, 1,
			_target_all,
			SEFF_STAT, WSTAT_PROJECTILE_SPEED, SOP_MUL, 0.10f,
			_no_unlock
		},

		// Integer add, and the prereq test. Meaningless without the flak, which
		// is exactly what a prereq is for.
		{
			"launcher_canister_load",
			"Canister Load",
			"More shrapnel packed behind the same charge.",
			STREE_LAUNCHER, 3, "launcher_flak", 2, 2,
			WEAP_LAUNCHER,
			SEFF_STAT, WSTAT_PELLETS, SOP_ADD, 2.0f,
			_no_unlock
		},
		{
			"launcher_shaped_charge",
			"Shaped Charge",
			"Directs the blast forward. A direct hit bites much deeper.",
			STREE_LAUNCHER, 3, nullptr, 3, 2,
			_target_all,
			SEFF_STAT, WSTAT_DAMAGE, SOP_MUL, 0.12f,
			_no_unlock
		},

	// ------------------------------------------------------------------- STAFF
	//
	// The second and, by decision, LAST tree — see the plan's phase 6, which was
	// dropped. The staff earns one because it is the only weapon whose unlocks
	// are a SET rather than a single flag: bit i is spell i, so buying a row here
	// adds a spell to the right-mouse cycle rather than flipping one capability.
	//
	// Tiers are laid out so the two spell unlocks are the tree's landmarks and
	// the stat rows are what you spend on to reach them. The staff ships casting
	// Soul Fire and nothing else.

		{
			"staff_focus",
			"Focus",
			"A steadier channel. Every spell bites harder on a direct hit.",
			STREE_STAFF, 1, nullptr, 3, 1,
			_target_all,
			SEFF_STAT, WSTAT_DAMAGE, SOP_MUL, 0.08f,
			_no_unlock
		},
		{
			// INVERTED. The staff's member is a cooldown in milliseconds, so it
			// is read with statInv() and a bigger multiplier means a SHORTER
			// wait. Written as WSTAT_FIRE_RATE, never as a "cooldown" stat, so
			// the sense convention holds.
			"staff_swift_hand",
			"Swift Hand",
			"The skull answers quicker. Spells come off cooldown sooner.",
			STREE_STAFF, 1, nullptr, 2, 1,
			_target_all,
			SEFF_STAT, WSTAT_FIRE_RATE, SOP_MUL, 0.10f,
			_no_unlock
		},

		// The first spell unlock. Grave Bloom is the heavy arcing bolt, so it
		// reads as a real second option rather than a straight upgrade.
		{
			"staff_grave_bloom",
			"Grave Bloom",
			"Teaches the staff a second spell: a slow, heavy bolt that arcs and "
			"bursts wide.",
			STREE_STAFF, 2, nullptr, 1, 3,
			WEAP_SKULLSTAFF,
			SEFF_UNLOCK, _no_stat, SOP_ADD, 0.0f,
			Weapon_SkullStaff::UNLOCK_GRAVE_BLOOM
		},
		{
			// MULTIPLICATIVE on purpose. An ADD row here would hand Mend — which
			// authors splashDamage 0 — an explosion. See the note in detonate().
			"staff_wider_bloom",
			"Wider Bloom",
			"The burst carries further from where it lands.",
			STREE_STAFF, 2, nullptr, 2, 1,
			_target_all,
			SEFF_STAT, WSTAT_SPLASH_RADIUS, SOP_MUL, 0.10f,
			_no_unlock
		},

		// The second spell unlock, and the utility one. Prereq'd on Grave Bloom
		// so the cycle grows in a fixed order and the player is never choosing
		// between two spells they have not seen.
		{
			"staff_mend",
			"Mend",
			"Teaches the staff to close your own wounds. Cannot be cast at full "
			"health.",
			STREE_STAFF, 3, "staff_grave_bloom", 1, 3,
			WEAP_SKULLSTAFF,
			SEFF_UNLOCK, _no_stat, SOP_ADD, 0.0f,
			Weapon_SkullStaff::UNLOCK_MEND
		},
		{
			"staff_hurled_flame",
			"Hurled Flame",
			"Bolts leave the skull faster and drop less on the way.",
			STREE_STAFF, 3, nullptr, 2, 2,
			_target_all,
			SEFF_STAT, WSTAT_PROJECTILE_SPEED, SOP_MUL, 0.10f,
			_no_unlock
		},

	// ------------------------------------------------------------------------
	// NOTHING ELSE GOES HERE. The plan's phase 6 — converting the remaining
	// twelve weapons — was dropped on 2026-09-06: the Launcher and the Staff are
	// the only two trees, by decision rather than by omission.
	//
	// The other trees still exist in SKILL_TREE and weaponSkillTree() still
	// assigns them, so filling one in later is only ever adding rows. But a row
	// added for a weapon whose call sites still read raw members would resolve
	// correctly and then be read by NOBODY. Convert that weapon first;
	// `skill_validate` reports the mismatch at startup either way.
	// ------------------------------------------------------------------------
	};

	const int _skill_count = static_cast<int>(sizeof(_skills) / sizeof(_skills[0]));
}

SkillSystem* SkillSystem::Get()
{
	// Function-local static rather than a namespace-scope object: it keeps the
	// constructor private (so the singleton cannot be duplicated) and it is
	// constructed on first use rather than during static initialisation.
	//
	// The second half matters here. ammoReserveMax() calls Get(), and that is a
	// free function reachable from anywhere, including very early. Lazy
	// construction means there is no order to get wrong at all.
	static SkillSystem instance;
	return &instance;
}

const SkillDef* SkillSystem::defs()     { return _skills; }
int             SkillSystem::defCount() { return _skill_count; }

SkillSystem::SkillSystem()
{
	// Deliberately NOT resolve(). This runs during static initialisation, and
	// resolve() reads g_PlayerController — a std::unique_ptr global in another
	// translation unit, whose own dynamic initialisation may not have happened
	// yet. Zero-initialisation would make it read as null in practice, but
	// "in practice" is exactly how this project has lost weeks to init order
	// before.
	//
	// So the constructor only puts the caches in their identity state, and
	// WeaponController::init() calls resolve() once the weapons actually exist.
	resetCaches();
}

void SkillSystem::resetCaches()
{
	for (int w = 0; w < WEAP_COUNT; ++w)
	{
		for (int s = 0; s < WSTAT_COUNT; ++s)
		{
			m_stat[w][s].add = 0.0f;
			m_stat[w][s].mul = 1.0f;
		}

		m_unlocks[w] = 0;
	}

	for (int a = 0; a < AMMO_COUNT; ++a)
		m_reserveMul[a] = 1.0f;
}

// ---------------------------------------------------------------------------
// Table lookup
// ---------------------------------------------------------------------------

const SkillDef* SkillSystem::findDef(const char* id) const
{
	if (!id)
		return nullptr;

	for (int i = 0; i < _skill_count; ++i)
	{
		if (std::string(_skills[i].id) == id)
			return &_skills[i];
	}

	return nullptr;
}

void SkillSystem::allSkillIds(std::vector<std::string>& out) const
{
	out.clear();

	for (int i = 0; i < _skill_count; ++i)
	{
		const std::string id = _skills[i].id;

		// Rows sharing an id are one skill — list it once.
		if (std::find(out.begin(), out.end(), id) == out.end())
			out.push_back(id);
	}
}

// The same table canBuy() gates on, exposed rather than duplicated. The panel
// draws a tier's requirement in its header, and a second copy of these numbers
// would drift the first time they were retuned.
int SkillSystem::tierRequirement(int tier)
{
	const int clamped = tier < 1 ? 1 : (tier > _tier_max ? _tier_max : tier);

	return _tier_requirement[clamped];
}

int SkillSystem::maxTier()
{
	return _tier_max;
}

// ---------------------------------------------------------------------------
// Points and ranks
// ---------------------------------------------------------------------------

void SkillSystem::awardPoints(int n)
{
	if (n <= 0)
		return;

	m_points += n;
}

int SkillSystem::rank(const char* id) const
{
	if (!id)
		return 0;

	auto it = m_ranks.find(id);
	return it == m_ranks.end() ? 0 : it->second;
}

int SkillSystem::spentPoints() const
{
	int total = 0;

	for (const auto& entry : m_ranks)
	{
		const SkillDef* def = findDef(entry.first.c_str());
		if (def)
			total += def->costPerRank * entry.second;
	}

	return total;
}

int SkillSystem::spentInTree(SKILL_TREE tree) const
{
	int total = 0;

	for (const auto& entry : m_ranks)
	{
		const SkillDef* def = findDef(entry.first.c_str());
		if (def && def->tree == tree)
			total += def->costPerRank * entry.second;
	}

	return total;
}

bool SkillSystem::canBuy(const char* id, std::string* reason) const
{
	const SkillDef* def = findDef(id);

	if (!def)
	{
		if (reason) *reason = "no such skill";
		return false;
	}

	const int have = rank(id);

	if (have >= def->maxRank)
	{
		if (reason) *reason = "already at max rank";
		return false;
	}

	if (m_points < def->costPerRank)
	{
		if (reason) *reason = "not enough points";
		return false;
	}

	// Tier gate: points already spent in THIS tree, which is what stops a player
	// buying the deep end of every tree at once.
	const int tier = def->tier < 1 ? 1 : (def->tier > _tier_max ? _tier_max : def->tier);
	const int need = _tier_requirement[tier];

	if (spentInTree(def->tree) < need)
	{
		if (reason)
			*reason = "tier " + std::to_string(def->tier) + " needs " +
			          std::to_string(need) + " points spent in the " +
			          skillTreeName(def->tree) + " tree";
		return false;
	}

	// Prereq. Looked up globally on purpose — it may name a skill in a different
	// tree, which is wanted for cross-tree flexibility.
	if (def->prereq && rank(def->prereq) <= 0)
	{
		const SkillDef* pre = findDef(def->prereq);
		if (reason)
			*reason = std::string("requires ") + (pre ? pre->name : def->prereq);
		return false;
	}

	return true;
}

bool SkillSystem::buy(const char* id)
{
	if (!canBuy(id))
		return false;

	const SkillDef* def = findDef(id);

	m_points -= def->costPerRank;
	m_ranks[id] = rank(id) + 1;

	resolve();
	return true;
}

void SkillSystem::setRank(const char* id, int newRank)
{
	const SkillDef* def = findDef(id);
	if (!def)
	{
		// Not an error: an old save may name a skill this build has removed.
		spdlog::warn("SkillSystem: unknown skill id '{}' ignored", id ? id : "(null)");
		return;
	}

	if (newRank <= 0)
		m_ranks.erase(id);
	else
		m_ranks[id] = newRank > def->maxRank ? def->maxRank : newRank;

	resolve();
}

void SkillSystem::resetAll(bool alsoClearPoints)
{
	m_ranks.clear();

	if (alsoClearPoints)
		m_points = 0;

	resolve();
}

// ---------------------------------------------------------------------------
// Resolution — trees are flattened away here and never mentioned again
// ---------------------------------------------------------------------------

bool SkillSystem::rowTouches(const SkillDef& def, PLAYER_WEAPON weapon)
{
	if (weaponSkillTree(weapon) != def.tree)
		return false;

	return def.target == _target_all || def.target == weapon;
}

void SkillSystem::applyRow(const SkillDef& def, int rowRank)
{
	if (rowRank <= 0)
		return;

	for (int w = 0; w < WEAP_COUNT; ++w)
	{
		const PLAYER_WEAPON weapon = static_cast<PLAYER_WEAPON>(w);

		if (!rowTouches(def, weapon))
			continue;

		if (def.effect == SEFF_UNLOCK)
		{
			if (def.unlockBit >= 0 && def.unlockBit < 32)
				m_unlocks[w] |= (1u << def.unlockBit);

			continue;
		}

		if (def.stat < 0 || def.stat >= WSTAT_COUNT)
			continue;

		StatMod& mod = m_stat[w][def.stat];

		// Adds sum and multipliers sum before being applied, so two +10% rows are
		// +20% rather than +21%. Additive stacking is far easier to balance and
		// is what a player reads "+10%" as meaning.
		if (def.op == SOP_ADD)
			mod.add += def.valuePerRank * rowRank;
		else
			mod.mul += def.valuePerRank * rowRank;

		// The reserve cap is not read through the per-weapon cache — it lives in
		// the free function ammoReserveMax(), keyed by pool. Mirror it across.
		if (def.stat == WSTAT_RESERVE_MAX)
		{
			const AMMO_TYPE pool = weaponAmmoType(weapon);

			if (pool > AMMO_NONE && pool < AMMO_COUNT && def.op == SOP_MUL)
				m_reserveMul[pool] += def.valuePerRank * rowRank;
		}
	}
}

void SkillSystem::resolve()
{
	resetCaches();

	// Seed each weapon's starting capabilities BEFORE any row is applied, so a
	// weapon that ships with one fire mode already available keeps it.
	//
	// Read from the live weapons when there are any — defaultUnlocks() is a
	// virtual on the weapon, and there is no player during startup or in the
	// pure editor. With no player, everything starts locked, which is correct:
	// nothing is being fired then either.
	if (g_PlayerController && g_PlayerController->weaponController())
	{
		WeaponController* weapons = g_PlayerController->weaponController();

		for (int w = 0; w < WEAP_COUNT; ++w)
		{
			PlayerWeapon* weapon = weapons->weapon(static_cast<PLAYER_WEAPON>(w));

			if (weapon)
				m_unlocks[w] = weapon->defaultUnlocks();
		}
	}

	for (int i = 0; i < _skill_count; ++i)
		applyRow(_skills[i], rank(_skills[i].id));
}

// ---------------------------------------------------------------------------
// Resolved queries
// ---------------------------------------------------------------------------

float SkillSystem::statMul(PLAYER_WEAPON weapon, WEAPON_STAT stat) const
{
	if (weapon < 0 || weapon >= WEAP_COUNT || stat < 0 || stat >= WSTAT_COUNT)
		return 1.0f;

	// A multiplier driven to zero or below by a hostile row would silently
	// annihilate the stat — and for an inverted stat it would divide by zero.
	// Floor it well clear of both.
	const float mul = m_stat[weapon][stat].mul;
	return mul < 0.01f ? 0.01f : mul;
}

float SkillSystem::statAdd(PLAYER_WEAPON weapon, WEAPON_STAT stat) const
{
	if (weapon < 0 || weapon >= WEAP_COUNT || stat < 0 || stat >= WSTAT_COUNT)
		return 0.0f;

	return m_stat[weapon][stat].add;
}

bool SkillSystem::hasUnlock(PLAYER_WEAPON weapon, int bit) const
{
	if (weapon < 0 || weapon >= WEAP_COUNT || bit < 0 || bit >= 32)
		return false;

	return (m_unlocks[weapon] & (1u << bit)) != 0;
}

float SkillSystem::reserveMulti(AMMO_TYPE type) const
{
	if (type <= AMMO_NONE || type >= AMMO_COUNT)
		return 1.0f;

	const float mul = m_reserveMul[type];
	return mul < 0.01f ? 0.01f : mul;
}

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

void SkillSystem::captureRanks(std::vector<std::pair<std::string, int>>& out) const
{
	out.clear();
	out.reserve(m_ranks.size());

	for (const auto& entry : m_ranks)
		out.push_back(entry);
}

void SkillSystem::applyRanks(const std::vector<std::pair<std::string, int>>& in, int unspent)
{
	m_ranks.clear();
	m_points = unspent < 0 ? 0 : unspent;

	for (const auto& entry : in)
	{
		const SkillDef* def = findDef(entry.first.c_str());

		// An id this build no longer defines is SKIPPED WITH A WARNING, never an
		// error. Removing a skill from the table must not brick an existing save.
		if (!def)
		{
			spdlog::warn("SkillSystem: save names unknown skill '{}'; skipped", entry.first);
			continue;
		}

		if (entry.second > 0)
			m_ranks[entry.first] = entry.second > def->maxRank ? def->maxRank : entry.second;
	}

	resolve();
}

// ---------------------------------------------------------------------------
// Validation — catches an upgrade that can never do anything
// ---------------------------------------------------------------------------

int SkillSystem::validate(std::vector<std::string>& problems) const
{
	problems.clear();

	WeaponController* weapons =
		g_PlayerController ? g_PlayerController->weaponController() : nullptr;

	if (!weapons)
	{
		problems.push_back("no player: cannot check rows against live weapons");
		return 1;
	}

	for (int i = 0; i < _skill_count; ++i)
	{
		const SkillDef& def = _skills[i];
		const std::string where = std::string("'") + def.id + "'";

		if (def.tree == STREE_NONE)
			problems.push_back(where + " sits in STREE_NONE and can never be reached");

		if (def.tier < 1 || def.tier > _tier_max)
			problems.push_back(where + " has tier " + std::to_string(def.tier) +
			                   ", outside 1.." + std::to_string(_tier_max));

		if (def.maxRank < 1)
			problems.push_back(where + " has maxRank < 1");

		if (def.prereq && !findDef(def.prereq))
			problems.push_back(where + " names missing prereq '" + def.prereq + "'");

		// Does any weapon actually receive this row?
		bool touchesAnything = false;

		for (int w = 1; w < WEAP_COUNT; ++w)
		{
			const PLAYER_WEAPON type = static_cast<PLAYER_WEAPON>(w);

			if (!rowTouches(def, type))
				continue;

			touchesAnything = true;

			PlayerWeapon* weapon = weapons->weapon(type);
			if (!weapon)
				continue;

			if (def.effect == SEFF_UNLOCK)
			{
				if (!weapon->unlockName(def.unlockBit))
					problems.push_back(where + " unlocks bit " +
					                   std::to_string(def.unlockBit) + " which " +
					                   weaponDisplayName(type) + " does not name");
			}
			else if ((weapon->supportedStats() & WSTAT_BIT(def.stat)) == 0)
			{
				problems.push_back(where + " modifies " + weaponStatName(def.stat) +
				                   " which " + weaponDisplayName(type) +
				                   " does not read");
			}
		}

		if (!touchesAnything)
			problems.push_back(where + " targets no weapon in the " +
			                   skillTreeName(def.tree) + " tree");
	}

	return static_cast<int>(problems.size());
}

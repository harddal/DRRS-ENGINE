#pragma once

#include <string>
#include <vector>

#include "WeaponSkills.h"
#include "Game/Player/WeaponData.h"

struct SkillDef;

// ---------------------------------------------------------------------------
// SkillPanel — the skill tree UI.
//
// THIS FILE IS DELIBERATELY SPLIT IN TWO HALVES, and the split is the point.
//
//   1. buildView() turns the skill table and the resolved caches into the
//      SkillTreeView structs below. It knows nothing about ImGui. It answers
//      "what should be on screen and what state is each node in".
//
//   2. draw() renders those structs with ImGui, and does nothing else.
//
// ImGui is the layout and testing backend, not the intended final one — RmlUi
// or something with more presentational control is expected to replace it. When
// that happens, half 2 is thrown away and half 1 is kept: the view structs are
// the actual interface between the game's skill state and whatever draws it.
// So resist putting decisions in draw(). If the renderer has to work something
// out — whether a node is affordable, what a row's effect reads as in English —
// that belongs in the view, or the next backend has to re-derive it.
//
// The one thing the view does NOT carry is the input lock. InventoryController
// owns that, because it already owns the mouse cursor and the "player cannot
// look around" flag for the pouch, and two panels writing those globals
// independently would fight over them every frame.
// ---------------------------------------------------------------------------

// What one buyable node looks like right now. One node = one skill id, which
// may be SEVERAL rows in the table — that is how a skill both unlocks something
// and buffs a stat. All of them are summarised in 'effects'.
struct SkillNodeView
{
	std::string id;
	std::string name;
	std::string description;

	int rank    = 0;
	int maxRank = 0;
	int cost    = 0;    // points per rank

	bool owned   = false;  // rank > 0
	bool maxed   = false;  // rank == maxRank
	bool buyable = false;  // canBuy() said yes

	// Filled only when !buyable, straight from canBuy(). The renderer shows it
	// rather than deciding for itself why a node is refused — there is exactly
	// one authority on that and it is not the UI.
	std::string blockedReason;

	// An unlock node is a different SHAPE, not a different colour: a capability
	// is a one-time thing and should not read as a rank track.
	bool isUnlock = false;

	// One line per row sharing this id: "Splash radius  +8% per rank".
	std::vector<std::string> effects;

	// What the stat rows are ACTUALLY doing right now, as the transform the
	// weapon reads through — "x1.240  +0.00". Empty for a pure unlock node.
	//
	// NOT shown as base -> effective, which is what the plan originally asked
	// for. The base lives in the weapon's own private member and is not
	// reachable from here; worse, for the staff it is not even well defined,
	// because directDamage is per SPELL rather than per weapon. Showing the
	// transform is what the console settled on for the same reason, and the two
	// agreeing matters more than either being prettier.
	std::string currentEffect;
};

struct SkillTierView
{
	int  tier           = 0;
	int  pointsRequired = 0;     // spent IN THIS TREE, from SkillSystem::tierRequirement()
	bool open           = false; // the requirement is met

	std::vector<SkillNodeView> nodes;
};

struct SkillTreeView
{
	SKILL_TREE  tree = STREE_NONE;
	std::string name;

	// The weapon whose resolved numbers the stat readouts are taken from. A tree
	// can cover several weapons; this is the one the player actually has, and
	// the held weapon wins if it is in this tree.
	PLAYER_WEAPON representative = WEAP_NONE;
	std::string   representativeName;

	int spent = 0;   // points spent in this tree

	std::vector<SkillTierView> tiers;
};

class SkillPanel
{
public:
	bool isOpen() const { return m_open; }
	void setOpen(bool open) { m_open = open; }
	void toggle() { m_open = !m_open; }

	// ImGui half. Assumes the caller has already decided the panel is open and
	// has dealt with the cursor and the input lock.
	void draw();

	// --- Backend-neutral half -----------------------------------------------
	// Everything the UI needs, derived from the skill table and the live
	// weapons. 'showEmptyTrees' includes trees the player owns no weapon in,
	// which is a testing affordance rather than the shipping behaviour: trees
	// are meant to appear as the run gives you the weapons.
	//
	// Trees with no rows at all are ALWAYS hidden. Eight of the ten trees are
	// assigned-but-empty by design (the plan's phase 6 was dropped), so showing
	// them would be eight blank columns.
	static void buildView(std::vector<SkillTreeView>& out, bool showEmptyTrees);

private:
	bool m_open = false;

	// Testing affordance; see buildView(). Off by default so what is on screen
	// is what a player would see.
	bool m_showEmptyTrees = false;

	// Rebuilt every frame. Cheap at this table size, and a cache would be one
	// more thing to invalidate on every buy, save load and skill_reset.
	std::vector<SkillTreeView> m_view;

	void drawNode(const SkillNodeView& node);
};

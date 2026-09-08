#pragma once

#include <string>

#include "PlayerInventory.h"
#include "Game/Skill/SkillPanel.h"

// ---------------------------------------------------------------------------
// InventoryController — the pouch panel, and the use path.
//
// Replaces a 9x3 drag-and-drop grid with 1H / 2H / spell equip slots. Those
// slots were for a game that no longer runs: WeaponController owns weapons
// outright and the skull staff owns spells. The grid itself was never a grid
// either — sizex/sizey were parsed out of every .item file and never read, and
// ITEM_SLOT_FILLED_ID was never referenced, so it was 27 single-cell slots
// drawn in a rectangle.
//
// What replaces it: one panel, tabs built from the categories present in the
// item database, an icon per stack, and a description with a Use button.
// ---------------------------------------------------------------------------
class InventoryController
{
public:
	InventoryController() = default;

	void init();

	// Input and state only — NO ImGui. Driven from PlayerController::update(),
	// which is inside Engine's FIXED-TIMESTEP loop and therefore runs zero, one
	// or several times per rendered frame.
	//
	// 'otherUiWantsInput' is any OTHER window that needs the mouse cursor and
	// the player's input lock this frame — currently just the F2 viewmodel
	// tuner. It is passed in rather than read from a sibling because
	// PlayerController owns both and is the only place that can see all the
	// contributors; a window reaching for these flags itself is exactly the bug
	// this parameter exists to close (see WeaponController::setViewmodelDebug).
	void update(bool otherUiWantsInput = false);

	// All the ImGui drawing, for both panels. Driven from
	// PlayerController::updateUI(), which runs EXACTLY ONCE per rendered frame.
	//
	// This split is not tidiness. Issuing ImGui commands from the fixed-step
	// loop means that on a frame where the accumulator did not reach a step,
	// no window is submitted at all — and an ImGui window that is not submitted
	// for a frame is simply not drawn that frame. That is the panel flicker, and
	// Engine.cpp's own `logicUpdated` flag is the proof the zero-step frame is
	// real and expected. Anything that draws must live here.
	void updateUI();

	void destroy();

	// True while ANY modal player panel is open — the pouch or the skill tree.
	// The HUD and the look controls read this to know the player is not driving,
	// and they do not care which panel it is.
	bool isInventoryDisplaying() const { return m_displayInventory || m_skillPanel.isOpen(); }

	// The skill tree panel lives here rather than owning itself because this
	// class already owns the two things both panels need and neither can own
	// twice: ImGui's mouse cursor, and InputManager's canProcessInput() lock.
	// Two panels writing those independently would each clobber the other's
	// state every frame — the flags are level-triggered, not requests.
	SkillPanel& skillPanel() { return m_skillPanel; }

	PlayerInventory&       inventory()       { return m_inventory; }
	const PlayerInventory& inventory() const { return m_inventory; }

	// --- Acquisition ---------------------------------------------------------
	// The single entry point for gaining an item, from a pickup or a script.
	//
	// Honours ItemDef::autoUse: an item flagged for it is consumed on the spot
	// rather than stored — but only if the use SUCCEEDS. A medkit walked over at
	// full health reports that it would do nothing and is stored instead, which
	// is the rule medkit_small.asc already hand-rolled for itself.
	//
	// Returns true if the item was taken at all (used or stored).
	bool giveItem(const std::string& id, int count = 1, const std::string& data = std::string());

	// Runs the item's onUse script hook and consumes one on success.
	//
	// An item with NO onUse hook cannot be used at all — it is not "trivially
	// used". A keycard has no script, and consuming one because the player
	// clicked a button would let them destroy the key to the next door.
	// Returns false when there is no hook, or when canUse reports the use would
	// do nothing.
	bool useItem(const std::string& id);

	// Whether a Use button should exist for this item at all. False for anything
	// with no onUse hook — keys, and inert upgrade tokens read by other scripts.
	bool isUsable(const std::string& id) const;

	// Convenience for scripts. Both resolve through PlayerInventory.
	bool hasItem(const std::string& id) const  { return m_inventory.has(id); }
	int  itemCount(const std::string& id) const { return m_inventory.count(id); }

	bool removeItem(const std::string& id, int count = 1) { return m_inventory.remove(id, count) > 0; }

private:
	PlayerInventory m_inventory;
	SkillPanel      m_skillPanel;

	bool m_displayInventory = false;

	// Index into PlayerInventory::stacks(), or SIZE_MAX for nothing selected.
	// Re-validated every frame: using the last of a stack erases it, and a stale
	// index would then read the wrong item or run off the end.
	size_t m_selected = static_cast<size_t>(-1);

	// Which tab is open, by category string rather than by index — the tab set
	// is rebuilt from the database each frame, and an index would silently point
	// at a different category if the set ever changed.
	std::string m_activeCategory;

	void drawPanel();

	// The "would this do anything?" gate. Calls the item's canUse hook if it has
	// one; items without the hook are always usable.
	bool canUseItem(const std::string& id) const;
};

extern bool g_PlayerInventoryIsDisplaying, g_LockPlayerForInput;

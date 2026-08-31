#pragma once

#include <string>

#include "PlayerInventory.h"

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
	void update();
	void destroy();

	bool isInventoryDisplaying() const { return m_displayInventory; }

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

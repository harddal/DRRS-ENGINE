#pragma once

#include <string>
#include <vector>

#include "Game/Item/ItemData.h"

// ---------------------------------------------------------------------------
// PlayerInventory — what the player is carrying.
//
// A flat list of stacks with NO capacity limit. Keys and upgrades gate progress
// rather than compete for space, so a limit would add friction without adding a
// decision — and it would make it possible to be unable to pick up the key that
// opens the next door. Categories, not capacity, are what keep it readable: the
// panel groups by ItemDef::category, so finding one key among eleven stays fast
// however much content is added.
//
// Storage only. Using an item is InventoryController's job (it owns the script
// call), and taking one off the floor is PickupBehavior's.
// ---------------------------------------------------------------------------
class PlayerInventory
{
public:
	// Adds to an existing stack where the definition allows it, otherwise pushes
	// a new one. Returns the number actually added — always 'count' today, since
	// there is no capacity, but callers that report "picked up N" should use the
	// return value so a later cap cannot silently make them lie.
	int add(const std::string& id, int count = 1, const std::string& data = std::string());

	// Removes up to 'count'. Returns how many were actually removed, and drops
	// the stack entirely when it reaches zero.
	int remove(const std::string& id, int count = 1);

	bool has(const std::string& id) const { return count(id) > 0; }
	int  count(const std::string& id) const;

	void clear() { m_stacks.clear(); }

	const std::vector<ItemStack>& stacks() const { return m_stacks; }

	// Stacks whose definition sits in 'category', in the order they were picked
	// up. Returns indices into stacks() rather than copies so the caller can
	// still write through to the stack (the use path edits 'data').
	std::vector<size_t> indicesInCategory(const std::string& category) const;

	// Mutable access for the use path. Returns nullptr for an out-of-range index.
	ItemStack* stackAt(size_t index);

	// --- Save sidecar --------------------------------------------------------
	// Straight in and out of player.sav. Unknown ids are dropped on load with a
	// warning rather than kept as ghosts: an item removed from the content set
	// would otherwise sit in the pouch forever with no name, icon or use.
	std::vector<ItemStack> save() const { return m_stacks; }
	void load(const std::vector<ItemStack>& stacks);

private:
	std::vector<ItemStack> m_stacks;
};

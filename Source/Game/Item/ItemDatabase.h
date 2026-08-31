#pragma once

#include <string>
#include <vector>

#include "ItemData.h"

// Every item TYPE in the game, loaded once from content/item/*.item.
//
// Lookup is by the definition's string id (the filename stem). The old
// numeric-index lookup is gone along with the numeric ids themselves — see the
// note on ItemDef::id.
class ItemDatabase
{
public:
	// Idempotent: after the first successful load this returns immediately.
	// The old version cleared and rebuilt the whole list on every editor->game
	// toggle, which its own comment flagged as a HACK — with stable string ids
	// there is nothing that needs rebuilding.
	static void Load();

	// Forces a reload. For a content-hot-reload command; not the startup path.
	static void Reload();

	// Returns a shared null definition when the id is unknown, so callers can
	// use the result without a null check. Test it with ItemDef::valid().
	static const ItemDef& Get(const std::string& id);

	static bool Exists(const std::string& id);

	static const std::vector<ItemDef>& All();

	// Every distinct category present in the loaded set, in the canonical order
	// (key, upgrade, consumable) with anything unrecognised appended. This is
	// what the inventory panel builds its tabs from, so adding a category to a
	// .item file adds a tab with no code change.
	static std::vector<std::string> Categories();
};

#pragma once

#include <string>
#include <vector>

#include <ITexture.h>

#include "Engine/Engine.h"

// ---------------------------------------------------------------------------
// Items — keys, upgrades and consumables. NOT weapons, ammunition or health:
// those have no inventory presence (no icon to look at, no description to read,
// nothing to select) and live in WeaponController, the reserve pools and
// DamageReceiverComponent respectively. What they share with items is how you
// take them off the floor, which is PickupBehavior's job, not this file's.
//
// The definition and the carried instance are deliberately SEPARATE structs.
// The old Item was both at once, which is how it ended up with twenty-odd
// fields, a hand-written operator= and a copy of every string per pickup.
// ---------------------------------------------------------------------------

// Kept as a string rather than an enum so a new category is a word in a .item
// file and nothing else. The panel builds its tabs from whatever it finds.
namespace ItemCategory
{
	static const char* KEY        = "key";
	static const char* UPGRADE    = "upgrade";
	static const char* CONSUMABLE = "consumable";
}

// Static, loaded once from content/item/*.item. One instance per item TYPE.
struct ItemDef
{
	// THE FILENAME STEM — "keycard", not an index.
	//
	// The old database numbered items by recursive_directory_iterator order
	// (item.id = iter++), so dropping armour.item into the folder renumbered
	// everything alphabetically after it. That was harmless only because nothing
	// ever persisted; with items in player.sav it would silently turn every saved
	// keycard into whatever now sits at that index.
	std::string id;

	std::string name, desc;
	std::string category = ItemCategory::KEY;

	std::string icon;         // texture path, relative to the texture root
	std::string script;       // AngelScript module providing onUse
	std::string pickupSound;

	// There is deliberately no 'entity' field. The old one named the .ent to
	// spawn when the item was dropped, and dropping is gone. The link runs the
	// other way now: an item entity in the world names its item id through
	// ItemComponent, so a key pointing back at an entity would be a second,
	// unread copy of that relationship.

	// Consumed the moment it is picked up rather than stored — a medkit is used,
	// a potion is kept. A property of the ITEM, not of the particular one lying
	// on that floor, which is why it lives here and not on the pickup.
	//
	// The use still has to succeed: an auto-use item whose script reports it
	// would do nothing (a medkit at full health) is stored instead of wasted.
	bool autoUse = false;

	bool stackable = true;
	int  maxStack  = 99;

	// Resolved once at load. Null is survivable — the panel draws a placeholder.
	irr::video::ITexture* iconTexture = nullptr;

	// Compiled once at load, if 'script' is set. Shared by every stack of this
	// item; nothing about a use is per-instance.
	ScriptComponent scriptComponent;

	bool valid() const { return !id.empty(); }
};

// What the player actually carries. This is what goes into player.sav.
struct ItemStack
{
	std::string id;
	int         count = 1;

	// Per-instance state, e.g. a partly-spent charge. Free-form because only the
	// item's own script gives it meaning.
	std::string data;

	ItemStack() = default;
	ItemStack(std::string id, int count) : id(std::move(id)), count(count) {}

	template <class Archive>
	void serialize(Archive& archive)
	{
		archive(CEREAL_NVP(id), CEREAL_NVP(count), CEREAL_NVP(data));
	}
};

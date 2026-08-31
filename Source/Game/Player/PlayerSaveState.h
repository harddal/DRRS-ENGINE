#pragma once

#include <vector>

#include <cereal/cereal.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/string.hpp>
#include <string>

// ---------------------------------------------------------------------------
// PlayerSaveState — everything about the player that does NOT live in the ECS,
// written into a .pak as the "player.sav" member alongside scene.scn.
//
// It is deliberately small. A collected pickup is a KILLED ENTITY, so a scene
// saved after collection simply does not contain it; the same is true of dead
// NPCs and moved movers. "The scene is the save file" gets all of that for
// free, and this only has to carry what has no entity to hang off.
//
// Kept free of any dependency on WeaponData.h so WorldManager can hold one
// without dragging the renderer and the whole weapon stack into its header.
// The translation to and from live weapon state happens on the game side, in
// PlayerController.
// ---------------------------------------------------------------------------

// One weapon's magazine contents. Mirrors WeaponMagState, but keyed by the
// weapon's PLAYER_WEAPON value rather than by its position in
// WeaponController::m_player_weapon — registration order matches that enum
// today only by coincidence, and one weapon registered out of order would shift
// every index under an existing save.
struct WeaponMagRecord
{
	int   weapon = 0;

	int   slot0  = -1;
	int   slot1  = -1;
	int   slot2  = -1;
	int   slot3  = -1;

	float charge = -1.0f;

	template <class Archive>
	void serialize(Archive& ar)
	{
		ar(CEREAL_NVP(weapon),
		   CEREAL_NVP(slot0), CEREAL_NVP(slot1), CEREAL_NVP(slot2), CEREAL_NVP(slot3),
		   CEREAL_NVP(charge));
	}
};

// One carried stack. Mirrors ItemStack; the translation happens on the game side
// in PlayerController, the same way WeaponMagRecord mirrors WeaponMagState.
struct ItemStackRecord
{
	std::string id;
	int         count = 1;
	std::string data;

	template <class Archive>
	void serialize(Archive& ar)
	{
		ar(CEREAL_NVP(id), CEREAL_NVP(count), CEREAL_NVP(data));
	}
};

struct PlayerSaveState
{
	// Bumped whenever the meaning of a field changes or AMMO_TYPE gains a
	// member. A sidecar from a NEWER build than this one is refused rather than
	// misread — see PlayerController::applyPlayerState().
	// v1: health, ammo reserves, weapons, magazines.
	// v2: + carried items. A v1 sidecar loads with an empty pouch rather than
	//     being refused — the version check exists to reject sidecars from a
	//     NEWER build, not to reject older ones.
	static const int CURRENT_VERSION = 2;

	int version = CURRENT_VERSION;

	// DamageReceiverComponent is not in the entity serialize list, so health does
	// not round-trip through scene.scn at all. It has to travel here, and it has
	// to be written back into the component on load: g_PlayerData.currentHealth
	// is only a copy, refreshed from the component every frame.
	int health = 0;

	// Indexed by AMMO_TYPE. Sized at capture; a short vector from an older
	// version leaves the pools it does not mention at whatever they were.
	std::vector<int> reserve;

	// PLAYER_WEAPON values, not slot indices. See WeaponMagRecord.
	std::vector<int> owned;

	int currentWeapon = 0;

	std::vector<WeaponMagRecord> mags;

	// Carried items — keys, upgrades and consumables. Ammunition and health are
	// NOT here: they have no inventory presence and already travel in 'reserve'
	// and 'health' above.
	//
	// Its own record type rather than ItemStack so this header stays free of any
	// game dependency — WorldManager includes it, and pulling ItemData.h in would
	// drag the renderer and the script system into the engine's world header.
	std::vector<ItemStackRecord> items;

	// SPLIT, not a single serialize(), so an older sidecar still loads: cereal's
	// XML archive throws on a named node that is not there, so a v1 file — which
	// has no <items> — would be rejected outright by a symmetric serialize().
	// Reading 'version' first and then only asking for what that version wrote is
	// what makes the version field worth having.
	template <class Archive>
	void save(Archive& ar) const
	{
		ar(CEREAL_NVP(version),
		   CEREAL_NVP(health),
		   CEREAL_NVP(reserve),
		   CEREAL_NVP(owned),
		   CEREAL_NVP(currentWeapon),
		   CEREAL_NVP(mags),
		   CEREAL_NVP(items));
	}

	template <class Archive>
	void load(Archive& ar)
	{
		ar(CEREAL_NVP(version),
		   CEREAL_NVP(health),
		   CEREAL_NVP(reserve),
		   CEREAL_NVP(owned),
		   CEREAL_NVP(currentWeapon),
		   CEREAL_NVP(mags));

		if (version >= 2)
			ar(CEREAL_NVP(items));
	}
};

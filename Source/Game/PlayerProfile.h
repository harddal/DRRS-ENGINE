#pragma once

#include <memory>
#include <string>

class IPlayerController;

// A player prototype: which entity the start marker spawns, how far below the
// marker to put it, and which controller drives it.
//
// This is the ONE place that maps a marker to a controller. It exists so that
// GameplaySystem — which owns the MT_PLAYER_START case — does not have to
// include the header of every controller in the project just to decide which
// one to build.
//
// WHY A STRING AND NOT A MARKER_TYPE ENUM VALUE. MARKER_TYPE is serialised as a
// raw int into every .ent and every scene file. Appending a value is safe;
// inserting one silently reassigns every marker already saved (the same trap as
// the AMMO_TYPE ordering). A string field costs no enum churn at all when
// prototype #3 and #4 arrive, which is the actual point of the exercise.
struct PlayerProfile
{
	// Matched case-sensitively against MarkerComponent::profile.
	const char* name;

	// Passed to _asset_ent(), so this is the path under content/entity with no
	// extension — e.g. "player/player".
	const char* entityAsset;

	// Subtracted from the marker's position to place the entity, and added back
	// when the marker tracks the live player.
	//
	// This is the number that used to be the PLAYER_HEIGHT macro in
	// PlayerController.h. NOTE it is the same 1.75 as CCTSubsystem::init's
	// m_capsuleDesc.height — one height with two homes. A prototype with a
	// different capsule has to change both, and one that changes only this one
	// spawns embedded in the floor.
	float spawnHeight;

	// Builds the controller. Never null.
	std::unique_ptr<IPlayerController> (*create)();
};

// Resolves a MarkerComponent::profile string to a row.
//
// An empty name — which is every playerstart.ent written before profiles
// existed — resolves to the first-person row, so old content is unchanged. An
// unrecognised name also falls back to it, with a warning: a typo in an .ent
// should give you a playable level and a log line, not an unspawnable one.
const PlayerProfile& resolvePlayerProfile(const std::string& name);

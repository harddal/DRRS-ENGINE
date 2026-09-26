#pragma once

#include "anax/Component.hpp"

#include "cereal/cereal.hpp"
#include "cereal/types/string.hpp"

enum MARKER_TYPE
{
	MT_NULL,
	MT_PLAYER_START,
	MT_FREECAMERA,
    MT_WAYPOINT,
    MT_SKY_CAMERA
};

struct MarkerComponent : anax::Component
{
	MARKER_TYPE type;

	bool hasUpdated = false;

	// MT_SKY_CAMERA: parallax scale for the 3D skybox. The sky camera moves
	// 1/skyScale as far as the player, so the miniature reads as skyScale times
	// larger/farther. Ignored by other marker types.
	float skyScale = 16.0f;

	// MT_PLAYER_START: which player prototype to spawn. Resolved through
	// resolvePlayerProfile() (Game/PlayerProfile.h), which maps it to an entity
	// asset, a spawn height and a controller class.
	//
	// Empty means the first-person default, which is what every marker written
	// before this field existed deserialises to. Dragging a different marker
	// .ent into the scene is the whole authoring gesture for picking a
	// prototype; the MARKER_TYPE stays MT_PLAYER_START.
	std::string profile;

	template <class Archive>
	void serialize(Archive& archive)
	{
		archive(CEREAL_NVP(type));
		// Added later; guard so pre-existing marker files still load.
		//
		// APPEND ONLY, and never reorder: each of these is an independent
		// "read it if it is there". A field inserted ahead of one already
		// shipped would be read into the wrong member on every old file.
		try { archive(CEREAL_NVP(skyScale)); } catch (cereal::Exception&) {}
		try { archive(CEREAL_NVP(profile));  } catch (cereal::Exception&) {}
	}
};

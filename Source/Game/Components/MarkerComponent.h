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

	template <class Archive>
	void serialize(Archive& archive)
	{
		archive(CEREAL_NVP(type));
		// Added later; guard so pre-existing marker files still load.
		try { archive(CEREAL_NVP(skyScale)); } catch (cereal::Exception&) {}
	}
};

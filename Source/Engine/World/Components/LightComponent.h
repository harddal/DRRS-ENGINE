#pragma once

#include "anax/Component.hpp"

#include "irrlicht.h"

#include "cereal/cereal.hpp"

enum LIGHT_TYPE
{
	LT_POINT,
	LT_SPOT,
	LT_AREA
};

enum LIGHT_MODE
{
	LM_DYNAMIC,
	LM_DYNAMIC_SHADOW,
	LM_STATIC
};

struct LightComponent : anax::Component
{
	bool update_component_data;

	LIGHT_TYPE type;
	LIGHT_MODE mode;

	bool visible;

	float radius;
	float outerCone;
	float innerCone;
	float falloff;
	
	irr::video::SColorf color_diffuse;

	irr::core::vector3df offset;

	irr::scene::ILightSceneNode* node;
	irr::video::SLight data;

    bool update()
    {
        return update_component_data = true;
    }

	template <class Archive>
	void serialize(Archive& archive)
	{
		archive(
			CEREAL_NVP(type), CEREAL_NVP(mode), CEREAL_NVP(visible), CEREAL_NVP(radius), CEREAL_NVP(outerCone), CEREAL_NVP(innerCone), CEREAL_NVP(falloff),
			CEREAL_NVP(color_diffuse.r), CEREAL_NVP(color_diffuse.g), CEREAL_NVP(color_diffuse.b), CEREAL_NVP(offset.X), CEREAL_NVP(offset.Y), CEREAL_NVP(offset.Z));
	}

    LightComponent() :
        update_component_data(false), type(LT_POINT), mode(LM_DYNAMIC), visible(true), radius(5.0f), outerCone(45.0f),
        innerCone(0.0f), falloff(2.0f), color_diffuse(1.0f, 1.0f, 1.0f), node(nullptr) {}
};

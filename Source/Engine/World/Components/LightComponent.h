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
	float intensity;   // multiplies color_diffuse before upload; >1.0 = overbright (Unreal style)

	bool  showCorona    = false;
	float coronaSize    = 1.5f;
	float coronaFadeNear = 2.0f;   // distance at which corona reaches full brightness
	float coronaFadeFar  = 30.0f;  // distance at which corona begins fading out (fully gone at 2x)

	irr::video::SColorf color_diffuse;

	irr::core::vector3df offset;

	irr::scene::ILightSceneNode*    node;
	irr::scene::IBillboardSceneNode* coronaNode = nullptr;
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
		try { archive(CEREAL_NVP(intensity)); } catch (...) {}
		try { archive(CEREAL_NVP(showCorona)); } catch (...) {}
		try { archive(CEREAL_NVP(coronaSize)); } catch (...) {}
		try { archive(CEREAL_NVP(coronaFadeNear)); } catch (...) {}
		try { archive(CEREAL_NVP(coronaFadeFar)); } catch (...) {}
	}

    LightComponent() :
        update_component_data(false), type(LT_POINT), mode(LM_DYNAMIC), visible(true), radius(5.0f), outerCone(45.0f),
        innerCone(0.0f), falloff(2.0f), intensity(1.0f), showCorona(false), coronaSize(1.5f), coronaFadeNear(2.0f), coronaFadeFar(30.0f),
        color_diffuse(1.0f, 1.0f, 1.0f), node(nullptr), coronaNode(nullptr) {}
};

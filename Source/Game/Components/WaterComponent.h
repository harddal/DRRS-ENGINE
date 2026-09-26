#pragma once

#include "anax/Component.hpp"

#include "cereal/cereal.hpp"
#include "cereal/types/string.hpp"
#include "cereal/types/array.hpp"

#include <string>

// Per-water-body look, modelled on Source's Water shader parameters.
//
// RenderSystem encodes every field below into the node's SMaterial (colours in
// the colour slots, scalars in MaterialTypeParams[0..7]) because there is a
// single global WaterShaderCallback with no per-entity state of its own; the
// material is the only thing that reaches OnSetConstants per draw.
//
// Everything except shallowColor/deepColor is loaded inside a try/catch in
// serialize(), so .ent and scene files written before these fields existed
// still load and simply keep the defaults.
struct WaterComponent : anax::Component
{
	// $refracttint -- tints what you see THROUGH the surface. Near-white keeps
	// the pool bottom's own colour; a pale blue-green is the classic look.
	std::array<float, 3> shallowColor = { 0.72f, 0.88f, 0.86f };

	// $fogcolor -- the colour deep water converges to. This is the single
	// strongest control over the water's perceived colour.
	std::array<float, 3> deepColor = { 0.02f, 0.11f, 0.16f };

	// $reflecttint -- tints the reflected environment. White = untinted.
	std::array<float, 3> reflectColor = { 1.0f, 1.0f, 1.0f };

	// $fogstart / $fogend, in world units of water actually traversed by the
	// view ray. Below fogStart the water is clear; past fogEnd it is solid
	// fogColor. A narrow band gives a sharp murky pool, a wide one a clear lake.
	float fogStart = 0.0f;
	float fogEnd = 12.0f;

	// $refractamount -- screen-space distortion strength. Roughly "fraction of
	// the screen a fully-tilted wave can displace the background by", before
	// distance attenuation.
	float refractAmount = 0.025f;

	// $reflectamount -- multiplier on the fresnel reflection. 1.0 is physical;
	// Source maps commonly run 0.3-0.8 to keep the surface readable.
	float reflectAmount = 0.6f;

	// Normal-map tiles per world unit. 0.3 = one tile every ~3.3 units; the
	// shader samples three layers around that at 1.0x / 0.43x / 2.13x.
	float normalTiling = 0.30f;

	// Wave scroll rate multiplier. 1.0 is a gentle pool; raise for a river.
	float flowSpeed = 1.0f;

	// $fresnelpower. Source's default is 6. Lower turns the surface reflective
	// at steeper angles.
	float fresnelPower = 5.0f;

	// Wave normal XY gain -- "choppiness". 0 is a dead-flat mirror.
	float waveStrength = 1.0f;

	// Surface opacity, used ONLY when the refraction copy is unavailable
	// (preview render targets, or r_water_refract 0). With refraction on, the
	// surface is output opaque exactly like Source's.
	float alpha = 0.8f;

	// $normalmap. Tangent-space, tiling. Empty falls back to the shader's
	// procedural sine-wave normals.
	std::string normalMap = "content/texture/normal/water_normal.png";

	template <class Archive>
	void serialize(Archive& archive)
	{
		archive(
			CEREAL_NVP(shallowColor),
			CEREAL_NVP(deepColor)
		);

		try { archive(CEREAL_NVP(reflectColor)); }  catch (cereal::Exception&) {}
		try { archive(CEREAL_NVP(fogStart));     }  catch (cereal::Exception&) {}
		try { archive(CEREAL_NVP(fogEnd));       }  catch (cereal::Exception&) {}
		try { archive(CEREAL_NVP(refractAmount));}  catch (cereal::Exception&) {}
		try { archive(CEREAL_NVP(reflectAmount));}  catch (cereal::Exception&) {}
		try { archive(CEREAL_NVP(normalTiling)); }  catch (cereal::Exception&) {}
		try { archive(CEREAL_NVP(flowSpeed));    }  catch (cereal::Exception&) {}
		try { archive(CEREAL_NVP(fresnelPower)); }  catch (cereal::Exception&) {}
		try { archive(CEREAL_NVP(waveStrength)); }  catch (cereal::Exception&) {}
		try { archive(CEREAL_NVP(alpha));        }  catch (cereal::Exception&) {}
		try { archive(CEREAL_NVP(normalMap));    }  catch (cereal::Exception&) {}
	}
};

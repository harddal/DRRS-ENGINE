#pragma once

#include "anax/Component.hpp"

#include "cereal/cereal.hpp"
#include "cereal/types/string.hpp"
#include "cereal/types/array.hpp"

struct WaterComponent : anax::Component
{
	// Water color configuration (RGB values 0.0 - 1.0)
	std::array<float, 3> shallowColor = { 0.0f, 0.3f, 0.8f };  // Vibrant blue (default)
	std::array<float, 3> deepColor = { 0.0f, 0.1f, 0.4f };     // Dark blue (default)

	template <class Archive>
	void serialize(Archive& archive) { 
		archive(
			CEREAL_NVP(shallowColor),
			CEREAL_NVP(deepColor)
		);
	}
};
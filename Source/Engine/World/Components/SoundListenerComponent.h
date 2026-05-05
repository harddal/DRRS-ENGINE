#pragma once

#include <string>

#include "anax/Component.hpp"
#include "cereal/cereal.hpp"
#include "cereal/types/vector.hpp"
#include "cereal/types/string.hpp"

struct SoundListenerComponent : anax::Component
{
	template <class Archive>
	void serialize(Archive& archive) {}
};

#pragma once

#include <cstdint>
#include <string>

#include <anax/Component.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/string.hpp>

struct ParticleComponent : anax::Component
{
    // Serialized
    std::string effectPath;     // e.g. "content/particle/fire.psys"
    std::string effectName;     // optional override; derived from effectPath basename if empty
    bool        loop = false;

    // Runtime (not serialized)
    uint32_t handle = 0;        // 0 = not active

    template <class Archive>
    void serialize(Archive& archive)
    {
        archive(CEREAL_NVP(effectPath));
        try { archive(CEREAL_NVP(effectName)); } catch (...) {}
        try { archive(CEREAL_NVP(loop));       } catch (...) {}
    }
};

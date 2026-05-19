#pragma once

#include <string>

#include "SPK.h"

struct ParticleSystemDef;

struct ParticleLoadResult
{
    SPK::SPK_ID baseID    = SPK::NO_ID;
    float       updateRate = 1.0f;
};

class ParticleSystemLoader
{
public:
    // Reads a .psys file and builds a SPARK IRRSystem registered with the SPK factory.
    // The returned baseID is a disabled, invisible template — clone it with SPK_Copy
    // for each active instance and destroy clones with SPK_Destroy when done.
    // Returns {NO_ID, 1.0f} on failure.
    static ParticleLoadResult load(const std::string& path);

    // Serializes a ParticleSystemDef to a .psys XML file.
    // Useful for saving a system designed in the editor or generated in code.
    static bool save(const ParticleSystemDef& def, const std::string& path);
};

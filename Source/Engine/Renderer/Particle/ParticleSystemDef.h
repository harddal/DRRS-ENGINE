#pragma once

#include <string>
#include <vector>

#include "cereal/cereal.hpp"
#include "cereal/types/vector.hpp"
#include "cereal/types/string.hpp"

// A single key point on an interpolator curve.
// When min == max the loader emits a single-value addEntry; otherwise a range addEntry.
struct ParticleInterpolatorEntry
{
    float lifeRatio = 0.0f;
    float min = 0.0f;
    float max = 0.0f;

    template<class Archive>
    void serialize(Archive& ar)
    {
        ar(CEREAL_NVP(lifeRatio), CEREAL_NVP(min), CEREAL_NVP(max));
    }
};

// An interpolator curve for one named parameter (e.g. "ALPHA", "SIZE").
struct ParticleInterpolatorDef
{
    std::string param;
    std::vector<ParticleInterpolatorEntry> entries;

    template<class Archive>
    void serialize(Archive& ar)
    {
        ar(CEREAL_NVP(param), CEREAL_NVP(entries));
    }
};

// A constant, random, or mutable parameter value.
// values.size() == 1  -> setParam(p, v[0])              constant
// values.size() == 2  -> setParam(p, v[0], v[1])        random-only OR mutable-only
// values.size() == 4  -> setParam(p, v[0],v[1],v[2],v[3])  mutable+random (birth range -> end range)
struct ParticleParamDef
{
    std::string name;
    std::vector<float> values;

    template<class Archive>
    void serialize(Archive& ar)
    {
        ar(CEREAL_NVP(name), CEREAL_NVP(values));
    }
};

// Fully describes a SPARK Model (particle blueprint).
// Flag lists use string names: "RED","GREEN","BLUE","ALPHA","SIZE","MASS",
//                              "ANGLE","TEXTURE_INDEX","ROTATION_SPEED"
// Model::create argument order: (enabled, mutable, random, interpolated)
struct ParticleModelDef
{
    float lifetimeMin = 1.0f;
    float lifetimeMax = 1.0f;
    std::vector<std::string> enabledFlags;
    std::vector<std::string> mutableFlags;
    std::vector<std::string> randomFlags;
    std::vector<std::string> interpolatedFlags;
    std::vector<ParticleParamDef> params;
    std::vector<ParticleInterpolatorDef> interpolators;

    template<class Archive>
    void serialize(Archive& ar)
    {
        ar(CEREAL_NVP(lifetimeMin), CEREAL_NVP(lifetimeMax),
           CEREAL_NVP(enabledFlags), CEREAL_NVP(mutableFlags),
           CEREAL_NVP(randomFlags), CEREAL_NVP(interpolatedFlags),
           CEREAL_NVP(params), CEREAL_NVP(interpolators));
    }
};

// Describes the spatial zone used by an emitter.
// type: "sphere", "point", "aabox"
struct ParticleZoneDef
{
    std::string type = "sphere";
    float cx = 0.0f, cy = 0.0f, cz = 0.0f;
    float radius = 0.0f;
    float hx = 0.0f, hy = 0.0f, hz = 0.0f;  // half-extents for "aabox"

    template<class Archive>
    void serialize(Archive& ar)
    {
        ar(CEREAL_NVP(type), CEREAL_NVP(cx), CEREAL_NVP(cy), CEREAL_NVP(cz),
           CEREAL_NVP(radius), CEREAL_NVP(hx), CEREAL_NVP(hy), CEREAL_NVP(hz));
    }
};

// Describes one emitter within a group.
// type: "random", "normal", "static", "straight"
// fullZone: true = spawn anywhere inside zone, false = spawn on zone surface
// flow == -1 means use tank (one-shot burst)
struct ParticleEmitterDef
{
    std::string type = "random";
    ParticleZoneDef zone;
    bool fullZone = true;
    int tank = 1;
    float flow = -1.0f;
    float forceMin = 0.0f;
    float forceMax = 0.0f;

    template<class Archive>
    void serialize(Archive& ar)
    {
        ar(CEREAL_NVP(type), CEREAL_NVP(zone), CEREAL_NVP(fullZone),
           CEREAL_NVP(tank), CEREAL_NVP(flow), CEREAL_NVP(forceMin), CEREAL_NVP(forceMax));
    }
};

// Describes an IRRQuadRenderer.
// blending:    "alpha", "add", "none"
// orientation: "camera_plane", "direction_aligned", "fixed"
// lookX/Y/Z and upX/Y/Z only apply when orientation == "fixed"
struct ParticleRendererDef
{
    std::string texturePath;
    std::string blending     = "alpha";
    std::string orientation  = "camera_plane";
    int   atlasW = 1, atlasH = 1;
    float scaleX = 1.0f, scaleY = 1.0f;
    bool  depthWrite = true;
    bool  alphaTest  = false;
    float alphaTestThreshold = 0.0f;
    float lookX = 0.0f, lookY = 0.0f, lookZ = -1.0f;
    float upX   = 0.0f, upY   = 1.0f, upZ  =  0.0f;

    template<class Archive>
    void serialize(Archive& ar)
    {
        ar(CEREAL_NVP(texturePath), CEREAL_NVP(blending), CEREAL_NVP(orientation),
           CEREAL_NVP(atlasW), CEREAL_NVP(atlasH),
           CEREAL_NVP(scaleX), CEREAL_NVP(scaleY),
           CEREAL_NVP(depthWrite), CEREAL_NVP(alphaTest), CEREAL_NVP(alphaTestThreshold),
           CEREAL_NVP(lookX), CEREAL_NVP(lookY), CEREAL_NVP(lookZ),
           CEREAL_NVP(upX),   CEREAL_NVP(upY),   CEREAL_NVP(upZ));
    }
};

// One particle group: model + renderer + one or more emitters + group physics.
struct ParticleGroupDef
{
    std::string name;
    int capacity = 10;
    float gravityX = 0.0f, gravityY = 0.0f, gravityZ = 0.0f;
    float friction = 0.0f;
    ParticleModelDef model;
    ParticleRendererDef renderer;
    std::vector<ParticleEmitterDef> emitters;

    template<class Archive>
    void serialize(Archive& ar)
    {
        ar(CEREAL_NVP(name), CEREAL_NVP(capacity),
           CEREAL_NVP(gravityX), CEREAL_NVP(gravityY), CEREAL_NVP(gravityZ),
           CEREAL_NVP(friction),
           CEREAL_NVP(model), CEREAL_NVP(renderer), CEREAL_NVP(emitters));
    }
};

// Top-level particle system definition loaded from a .psys file.
// updateRate: caller divides dt by this value before passing to system->update().
struct ParticleSystemDef
{
    float updateRate = 1.0f;
    std::vector<ParticleGroupDef> groups;

    template<class Archive>
    void serialize(Archive& ar)
    {
        ar(CEREAL_NVP(updateRate), CEREAL_NVP(groups));
    }
};

#include "Engine/Renderer/Particle/ParticleSystemLoader.h"
#include "Engine/Renderer/Particle/ParticleSystemDef.h"

#include <fstream>
#include <unordered_set>
#include <string>

#include <spdlog/spdlog.h>

#include "cereal/archives/xml.hpp"

#include "SPK.h"
#include "SPK_IRR.h"

#include "Engine/Renderer/RenderManager.h"

using namespace SPK;
using namespace SPK::IRR;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static int flagBitFromString(const std::string& name)
{
    if (name == "RED")            return FLAG_RED;
    if (name == "GREEN")          return FLAG_GREEN;
    if (name == "BLUE")           return FLAG_BLUE;
    if (name == "ALPHA")          return FLAG_ALPHA;
    if (name == "SIZE")           return FLAG_SIZE;
    if (name == "MASS")           return FLAG_MASS;
    if (name == "ANGLE")          return FLAG_ANGLE;
    if (name == "TEXTURE_INDEX")  return FLAG_TEXTURE_INDEX;
    if (name == "ROTATION_SPEED") return FLAG_ROTATION_SPEED;
    spdlog::warn("[ParticleSystemLoader] Unknown flag name: {}", name);
    return FLAG_NONE;
}

static ModelParam paramFromString(const std::string& name)
{
    if (name == "RED")            return PARAM_RED;
    if (name == "GREEN")          return PARAM_GREEN;
    if (name == "BLUE")           return PARAM_BLUE;
    if (name == "ALPHA")          return PARAM_ALPHA;
    if (name == "SIZE")           return PARAM_SIZE;
    if (name == "MASS")           return PARAM_MASS;
    if (name == "ANGLE")          return PARAM_ANGLE;
    if (name == "TEXTURE_INDEX")  return PARAM_TEXTURE_INDEX;
    if (name == "ROTATION_SPEED") return PARAM_ROTATION_SPEED;
    spdlog::warn("[ParticleSystemLoader] Unknown param name: {}", name);
    return PARAM_RED;
}

static int buildFlagBits(const std::vector<std::string>& flags)
{
    int bits = FLAG_NONE;
    for (const auto& f : flags)
        bits |= flagBitFromString(f);
    return bits;
}

static BlendingMode blendingFromString(const std::string& s)
{
    if (s == "add")   return BLENDING_ADD;
    if (s == "none")  return BLENDING_NONE;
    return BLENDING_ALPHA;  // default
}

// ---------------------------------------------------------------------------
// Builder functions
// ---------------------------------------------------------------------------

static Model* buildModel(const ParticleModelDef& def)
{
    int enabled      = buildFlagBits(def.enabledFlags);
    int mutable_     = buildFlagBits(def.mutableFlags);
    int random       = buildFlagBits(def.randomFlags);
    int interpolated = buildFlagBits(def.interpolatedFlags);

    Model* model = Model::create(enabled, mutable_, random, interpolated);
    model->setLifeTime(def.lifetimeMin, def.lifetimeMax);
    model->setShared(true);

    for (const auto& p : def.params)
    {
        ModelParam param = paramFromString(p.name);
        switch (p.values.size())
        {
        case 1: model->setParam(param, p.values[0]); break;
        case 2: model->setParam(param, p.values[0], p.values[1]); break;
        case 4: model->setParam(param, p.values[0], p.values[1], p.values[2], p.values[3]); break;
        default:
            spdlog::warn("[ParticleSystemLoader] Param '{}' has unexpected value count {}", p.name, p.values.size());
            break;
        }
    }

    for (const auto& interp : def.interpolators)
    {
        Interpolator* iobj = model->getInterpolator(paramFromString(interp.param));
        if (!iobj)
        {
            spdlog::warn("[ParticleSystemLoader] No interpolator for param '{}'", interp.param);
            continue;
        }
        for (const auto& e : interp.entries)
        {
            if (e.min == e.max)
                iobj->addEntry(e.lifeRatio, e.min);
            else
                iobj->addEntry(e.lifeRatio, e.min, e.max);
        }
    }

    return model;
}

static Zone* buildZone(const ParticleZoneDef& def)
{
    Vector3D center(def.cx, def.cy, def.cz);
    if (def.type == "point")
        return Point::create(center);
    if (def.type == "aabox")
        return AABox::create(center, Vector3D(def.hx, def.hy, def.hz));
    // default: sphere
    return Sphere::create(center, def.radius);
}

static Emitter* buildEmitter(const ParticleEmitterDef& def)
{
    Zone* zone = buildZone(def.zone);

    Emitter* emitter = nullptr;
    if (def.type == "normal")
        emitter = NormalEmitter::create();
    else if (def.type == "static")
        emitter = StaticEmitter::create();
    else if (def.type == "straight")
        emitter = StraightEmitter::create(Vector3D(def.dirX, def.dirY, def.dirZ));
    else if (def.type == "spheric")
        emitter = SphericEmitter::create(Vector3D(def.dirX, def.dirY, def.dirZ),
                                         def.angleMin, def.angleMax);
    else
        emitter = RandomEmitter::create();  // default

    emitter->setZone(zone, def.fullZone);
    emitter->setFlow(def.flow);
    emitter->setTank(def.tank);

    if (def.forceMin != 0.0f || def.forceMax != 0.0f)
        emitter->setForce(def.forceMin, def.forceMax);

    return emitter;
}

static IRRQuadRenderer* buildRenderer(const ParticleRendererDef& def,
                                      irr::video::IVideoDriver* driver,
                                      irr::IrrlichtDevice* device)
{
    IRRQuadRenderer* renderer = IRRQuadRenderer::create(device);
    renderer->setTexturingMode(TEXTURE_2D);
    renderer->setShared(true);

    if (!def.texturePath.empty())
        renderer->setTexture(driver->getTexture(def.texturePath.c_str()));

    renderer->setBlending(blendingFromString(def.blending));
    renderer->enableRenderingHint(DEPTH_WRITE, def.depthWrite);

    if (def.alphaTest)
    {
        renderer->enableRenderingHint(ALPHA_TEST, true);
        renderer->setAlphaTestThreshold(def.alphaTestThreshold);
    }

    if (def.atlasW > 1 || def.atlasH > 1)
        renderer->setAtlasDimensions(def.atlasW, def.atlasH);

    renderer->setScale(def.scaleX, def.scaleY);

    if (def.orientation == "direction_aligned")
        renderer->setOrientation(DIRECTION_ALIGNED);
    else if (def.orientation == "fixed")
    {
        renderer->setOrientation(FIXED_ORIENTATION);
        renderer->lookVector.set(def.lookX, def.lookY, def.lookZ);
        renderer->upVector.set(def.upX, def.upY, def.upZ);
    }
    // "camera_plane" is the default — no call needed

    return renderer;
}

static Group* buildGroup(const ParticleGroupDef& def,
                         irr::video::IVideoDriver* driver,
                         irr::IrrlichtDevice* device)
{
    Model*           model    = buildModel(def.model);
    IRRQuadRenderer* renderer = buildRenderer(def.renderer, driver, device);

    Group* group = Group::create(model, def.capacity);
    group->setRenderer(renderer);
    group->setGravity(Vector3D(def.gravityX, def.gravityY, def.gravityZ));
    group->setFriction(def.friction);

    for (const auto& emitterDef : def.emitters)
        group->addEmitter(buildEmitter(emitterDef));

    if (def.useRotator)
        group->addModifier(Rotator::create());

    return group;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

ParticleLoadResult ParticleSystemLoader::load(const std::string& path)
{
    std::ifstream is(path);
    if (!is.good())
    {
        spdlog::error("[ParticleSystemLoader] Cannot open '{}'", path);
        return {};
    }

    ParticleSystemDef def;
    try
    {
        cereal::XMLInputArchive archive(is);
        archive(cereal::make_nvp("ParticleSystem", def));
    }
    catch (const cereal::Exception& e)
    {
        spdlog::error("[ParticleSystemLoader] XML parse error in '{}': {}", path, e.what());
        return {};
    }

    irr::IrrlichtDevice*       device = RenderManager::Get()->device();
    irr::video::IVideoDriver*  driver = RenderManager::Get()->driver();
    irr::scene::ISceneManager* smgr   = RenderManager::Get()->sceneManager();

    IRRSystem* system = IRRSystem::create(smgr->getRootSceneNode(), smgr);

    for (const auto& groupDef : def.groups)
        system->addGroup(buildGroup(groupDef, driver, device));

    system->setAutoUpdateEnabled(false, false);
    system->setVisible(false);

    spdlog::info("[ParticleSystemLoader] Loaded '{}' ({} groups)", path, def.groups.size());
    return { system->getSPKID(), def.updateRate };
}

ParticleLoadResult ParticleSystemLoader::buildFromDef(const ParticleSystemDef& def)
{
    irr::IrrlichtDevice*       device = RenderManager::Get()->device();
    irr::video::IVideoDriver*  driver = RenderManager::Get()->driver();
    irr::scene::ISceneManager* smgr   = RenderManager::Get()->sceneManager();

    IRRSystem* system = IRRSystem::create(smgr->getRootSceneNode(), smgr);
    for (const auto& groupDef : def.groups)
        system->addGroup(buildGroup(groupDef, driver, device));
    system->setAutoUpdateEnabled(false, false);
    system->setVisible(false);

    spdlog::info("[ParticleSystemLoader] buildFromDef: {} group(s)", def.groups.size());
    return { system->getSPKID(), def.updateRate };
}

SPK::System* ParticleSystemLoader::buildForPreview(const ParticleSystemDef& def,
                                                    irr::scene::ISceneManager* smgr)
{
    irr::IrrlichtDevice*      device = RenderManager::Get()->device();
    irr::video::IVideoDriver* driver = RenderManager::Get()->driver();

    IRRSystem* system = IRRSystem::create(smgr->getRootSceneNode(), smgr);
    for (const auto& groupDef : def.groups)
        system->addGroup(buildGroup(groupDef, driver, device));
    system->setAutoUpdateEnabled(false, false);
    return system;
}

bool ParticleSystemLoader::save(const ParticleSystemDef& def, const std::string& path)
{
    std::ofstream os(path);
    if (!os.good())
    {
        spdlog::error("[ParticleSystemLoader] Cannot write '{}'", path);
        return false;
    }

    try
    {
        cereal::XMLOutputArchive archive(os);
        archive(cereal::make_nvp("ParticleSystem", def));
    }
    catch (const cereal::Exception& e)
    {
        spdlog::error("[ParticleSystemLoader] XML write error for '{}': {}", path, e.what());
        return false;
    }

    return true;
}

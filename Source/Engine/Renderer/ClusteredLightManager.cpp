#include "Engine/Renderer/ClusteredLightManager.h"

#include <cmath>
#include <algorithm>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#include <spdlog/spdlog.h>

#include "Engine/Renderer/GLExt.h"

#undef max
#undef min

using namespace irr;

namespace
{
    constexpr int NUM_CLUSTERS = ClusteredLightManager::GRID_X *
                                 ClusteredLightManager::GRID_Y *
                                 ClusteredLightManager::GRID_Z;

    // First cluster slice starts here even if the camera near plane is closer;
    // exponential slicing wastes resolution below ~0.5 units.
    constexpr float CLUSTER_NEAR = 0.5f;
}

ClusteredLightManager::~ClusteredLightManager()
{
    // Textures die with the GL context — RenderManager tears the device down
    // after this destructor runs, so explicit glDeleteTextures is unnecessary
    // (and the context may already be gone in some shutdown orders).
}

void ClusteredLightManager::createTextures()
{
    if (!GLExt::ActiveTexture)
        return;

    GLuint tex[3] = {};
    glGenTextures(3, tex);
    m_texLightData = tex[0];
    m_texGrid      = tex[1];
    m_texIndices   = tex[2];

    auto setup = [](GLuint id, GLint internalFmt, GLenum fmt, int w, int h)
    {
        glBindTexture(GL_TEXTURE_2D, id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, w, h, 0, fmt, GL_FLOAT, nullptr);
    };

    // Do all creation work on unit 8 so Irrlicht's per-unit binding cache
    // (units 0-7) is never invalidated behind its back.
    GLExt::ActiveTexture(GL_TEXTURE0 + 8);
    setup(m_texLightData, GL_RGBA32F, GL_RGBA, MAX_LIGHTS * TEXELS_PER_LIGHT, 1);
    setup(m_texGrid,      GL_RGBA32F, GL_RGBA, GRID_X * GRID_Y, GRID_Z);
    setup(m_texIndices,   GL_R32F,    GL_RED,  INDEX_TEX_W, INDEX_TEX_H);

    // Leave the frame-persistent bindings in place on units 8/9/10.
    glBindTexture(GL_TEXTURE_2D, m_texLightData);
    GLExt::ActiveTexture(GL_TEXTURE0 + 9);
    glBindTexture(GL_TEXTURE_2D, m_texGrid);
    GLExt::ActiveTexture(GL_TEXTURE0 + 10);
    glBindTexture(GL_TEXTURE_2D, m_texIndices);
    GLExt::ActiveTexture(GL_TEXTURE0);

    m_clusterLights.resize(NUM_CLUSTERS);
    m_lightTexData.resize(MAX_LIGHTS * TEXELS_PER_LIGHT * 4);
    m_gridTexData.resize(NUM_CLUSTERS * 4);
    m_indexTexData.resize(MAX_INDICES);

    m_texturesOk = (glGetError() == GL_NO_ERROR);
    if (!m_texturesOk)
        spdlog::error("ClusteredLightManager: texture creation failed");
    else
        spdlog::info("ClusteredLightManager: cluster textures created ({}x{}x{} grid, {} max lights)",
                     GRID_X, GRID_Y, GRID_Z, MAX_LIGHTS);
}

void ClusteredLightManager::update(video::IVideoDriver* driver,
                                   scene::ISceneManager* sceneManager,
                                   const core::dimension2du& viewportSize)
{
    if (!m_enabled)
        return;

    if (!m_texturesOk)
    {
        createTextures();
        if (!m_texturesOk)
        {
            // Driver below the feature floor — degrade to the fallback path.
            m_enabled = false;
            return;
        }
    }

    scene::ICameraSceneNode* cam = sceneManager->getActiveCamera();
    if (!cam)
    {
        m_lightCount = 0;
        return;
    }

    // Force the camera to produce this frame's view matrix now. render() only
    // recomputes the matrix and pushes it to the driver — drawAll() will do the
    // identical computation again later, so this is side-effect free.
    cam->updateAbsolutePosition();
    cam->render();
    const core::matrix4 view = driver->getTransform(video::ETS_VIEW);

    const float zn = CLUSTER_NEAR;
    const float zf = std::max(m_clusterFar, zn * 2.0f);

    // slice = floor(log2(z / zn) * GRID_Z / log2(zf / zn))
    m_sliceScale = static_cast<float>(GRID_Z) / std::log2(zf / zn);
    m_sliceBias  = -std::log2(zn) * m_sliceScale;

    m_tileW = std::max(1.0f, static_cast<float>(viewportSize.Width)  / GRID_X);
    m_tileH = std::max(1.0f, static_cast<float>(viewportSize.Height) / GRID_Y);

    // Frustum extents: view-space x = ndcX * tanX * z (left-handed, z into screen).
    const float tanY = std::tan(cam->getFOV() * 0.5f);
    const float tanX = tanY * cam->getAspectRatio();

    // ---- Gather lights (current-frame transforms, same traversal as drawShadowPass) ----
    core::array<scene::ISceneNode*> lightNodes;
    sceneManager->getSceneNodesFromType(scene::ESNT_LIGHT, lightNodes);

    for (auto& v : m_clusterLights)
        v.clear();

    struct LightRec
    {
        core::vector3df pos, dir;    // view space
        float radius, cosInner, cosOuter;
        video::SColorf color;
        scene::ILightSceneNode* node;
        int shadowSlot;              // -1 = none
    };
    static std::vector<LightRec> recs;   // persistent scratch
    recs.clear();

    for (u32 i = 0; i < lightNodes.size() && (int)recs.size() < MAX_LIGHTS; ++i)
    {
        auto* ln = static_cast<scene::ILightSceneNode*>(lightNodes[i]);
        if (!ln->isTrulyVisible())
            continue;

        ln->updateAbsolutePosition();
        const video::SLight& ld = ln->getLightData();

        LightRec r;
        r.node = ln;
        r.pos  = ln->getAbsolutePosition();
        view.transformVect(r.pos);
        r.radius     = ld.Radius > 0.0f ? ld.Radius : 1000.0f;
        r.color      = ld.DiffuseColor;
        r.dir        = core::vector3df(0.f, 0.f, 1.f);
        r.cosOuter   = -2.0f; // sentinel: not a spot
        r.cosInner   = 0.0f;
        r.shadowSlot = -1;
        if (ld.Type == video::ELT_SPOT)
        {
            // Same derivation as drawShadowPass: rotate local +Z to world, then to view.
            ln->getAbsoluteTransformation().rotateVect(r.dir);
            r.dir.normalize();
            view.rotateVect(r.dir);
            r.dir.normalize();
            r.cosOuter = std::cos(ld.OuterCone * core::DEGTORAD);
            r.cosInner = std::cos(ld.InnerCone * core::DEGTORAD);
        }
        recs.push_back(r);
    }

    // ---- Shadow slot assignment: nearest spotlights to the camera win.
    // The camera sits at the view-space origin, so distance = |pos|.
    m_shadowCasterCount = 0;
    for (int s = 0; s < MAX_SHADOW_CASTERS; ++s)
        m_shadowCasters[s] = nullptr;
    {
        static std::vector<int> spotIdx;
        spotIdx.clear();
        for (int i = 0; i < (int)recs.size(); ++i)
            if (recs[i].cosOuter > -1.5f)
                spotIdx.push_back(i);
        std::sort(spotIdx.begin(), spotIdx.end(), [&](int a, int b) {
            return recs[a].pos.getLengthSQ() < recs[b].pos.getLengthSQ();
        });
        for (int s = 0; s < (int)spotIdx.size() && s < MAX_SHADOW_CASTERS; ++s)
        {
            recs[spotIdx[s]].shadowSlot = s;
            m_shadowCasters[s] = recs[spotIdx[s]].node;
            ++m_shadowCasterCount;
        }
    }

    // ---- Pack texels + froxel assignment ----
    m_lightCount = static_cast<int>(recs.size());

    for (int li = 0; li < m_lightCount; ++li)
    {
        const LightRec& r = recs[li];
        const core::vector3df& pos = r.pos;
        const float radius = r.radius;

        float* t = &m_lightTexData[li * TEXELS_PER_LIGHT * 4];
        t[0]  = pos.X;      t[1]  = pos.Y;      t[2]  = pos.Z;    t[3]  = radius;
        t[4]  = r.color.r;  t[5]  = r.color.g;  t[6]  = r.color.b; t[7]  = r.cosInner;
        t[8]  = r.dir.X;    t[9]  = r.dir.Y;    t[10] = r.dir.Z;  t[11] = r.cosOuter;
        t[12] = static_cast<float>(r.shadowSlot);
        t[13] = 0.0f;       t[14] = 0.0f;       t[15] = 0.0f;

        // ---- Froxel assignment: conservative sphere vs. exponential-z frustum grid ----
        const float lzMin = pos.Z - radius;
        const float lzMax = pos.Z + radius;
        if (lzMax <= zn || lzMin >= zf)
            continue;

        auto zToSlice = [&](float z) {
            return static_cast<int>(std::floor(std::log2(std::max(z, zn)) * m_sliceScale + m_sliceBias));
        };
        const int s0 = core::clamp(zToSlice(lzMin), 0, GRID_Z - 1);
        const int s1 = core::clamp(zToSlice(lzMax), 0, GRID_Z - 1);

        for (int s = s0; s <= s1; ++s)
        {
            const float z0 = zn * std::pow(zf / zn, static_cast<float>(s)     / GRID_Z);
            const float z1 = zn * std::pow(zf / zn, static_cast<float>(s + 1) / GRID_Z);

            // Sphere cross-section radius within this z window.
            float dz = 0.0f;
            if (pos.Z < z0)      dz = z0 - pos.Z;
            else if (pos.Z > z1) dz = pos.Z - z1;
            if (dz >= radius)
                continue;
            const float rEff = std::sqrt(std::max(radius * radius - dz * dz, 0.0f));

            // Conservative NDC range across both slice depths.
            auto ndcRange = [&](float lo, float hi, float tanHalf, float& ndcLo, float& ndcHi)
            {
                const float a0 = lo / (tanHalf * z0), a1 = lo / (tanHalf * z1);
                const float b0 = hi / (tanHalf * z0), b1 = hi / (tanHalf * z1);
                ndcLo = std::min(a0, a1);
                ndcHi = std::max(b0, b1);
            };

            float nx0, nx1, ny0, ny1;
            ndcRange(pos.X - rEff, pos.X + rEff, tanX, nx0, nx1);
            ndcRange(pos.Y - rEff, pos.Y + rEff, tanY, ny0, ny1);
            if (nx1 < -1.0f || nx0 > 1.0f || ny1 < -1.0f || ny0 > 1.0f)
                continue;

            const int ix0 = core::clamp(static_cast<int>(std::floor((nx0 + 1.0f) * 0.5f * GRID_X)), 0, GRID_X - 1);
            const int ix1 = core::clamp(static_cast<int>(std::floor((nx1 + 1.0f) * 0.5f * GRID_X)), 0, GRID_X - 1);
            const int iy0 = core::clamp(static_cast<int>(std::floor((ny0 + 1.0f) * 0.5f * GRID_Y)), 0, GRID_Y - 1);
            const int iy1 = core::clamp(static_cast<int>(std::floor((ny1 + 1.0f) * 0.5f * GRID_Y)), 0, GRID_Y - 1);

            for (int iy = iy0; iy <= iy1; ++iy)
                for (int ix = ix0; ix <= ix1; ++ix)
                    m_clusterLights[ix + iy * GRID_X + s * GRID_X * GRID_Y].push_back(static_cast<u16>(li));
        }
    }

    // ---- Flatten per-cluster lists into the grid + index textures ----
    int cursor = 0;
    for (int c = 0; c < NUM_CLUSTERS; ++c)
    {
        const auto& list = m_clusterLights[c];
        int count = static_cast<int>(list.size());
        if (cursor + count > MAX_INDICES)
        {
            count = MAX_INDICES - cursor;
            if (!m_indexOverflowWarned)
            {
                spdlog::warn("ClusteredLightManager: light index list overflow (>{} entries) — clamping", MAX_INDICES);
                m_indexOverflowWarned = true;
            }
        }

        float* g = &m_gridTexData[c * 4];
        g[0] = static_cast<float>(cursor);
        g[1] = static_cast<float>(count);
        g[2] = 0.0f;
        g[3] = 0.0f;

        for (int k = 0; k < count; ++k)
            m_indexTexData[cursor + k] = static_cast<float>(list[k]);
        cursor += count;
    }

    // ---- Upload (units 8-10 are ours alone; Irrlicht never rebinds them) ----
    GLExt::ActiveTexture(GL_TEXTURE0 + 8);
    glBindTexture(GL_TEXTURE_2D, m_texLightData);
    if (m_lightCount > 0)
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_lightCount * TEXELS_PER_LIGHT, 1, GL_RGBA, GL_FLOAT, m_lightTexData.data());

    GLExt::ActiveTexture(GL_TEXTURE0 + 9);
    glBindTexture(GL_TEXTURE_2D, m_texGrid);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GRID_X * GRID_Y, GRID_Z, GL_RGBA, GL_FLOAT, m_gridTexData.data());

    GLExt::ActiveTexture(GL_TEXTURE0 + 10);
    glBindTexture(GL_TEXTURE_2D, m_texIndices);
    const int rows = (cursor + INDEX_TEX_W - 1) / INDEX_TEX_W;
    if (rows > 0)
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, INDEX_TEX_W, rows, GL_RED, GL_FLOAT, m_indexTexData.data());

    GLExt::ActiveTexture(GL_TEXTURE0);
}

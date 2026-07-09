#include "Engine/Renderer/DecalManager.h"

#include <cmath>
#include <cstdlib>

#include <spdlog/spdlog.h>

#include "Engine/Renderer/RenderManager.h"

using namespace irr;

void DecalShaderCallback::OnSetConstants(video::IMaterialRendererServices* services, s32)
{
    int decalSlot = 0, prepassSlot = 14;
    services->setPixelShaderConstant("tDecal",   &decalSlot,   1);
    services->setPixelShaderConstant("tPrepass", &prepassSlot, 1);

    // Row-major matrix4 uploaded exactly like the engine's other matrices
    // (glUniformMatrix4fv, transpose = false — matches shader-side M * v).
    services->setPixelShaderConstant("uDecalInv", decalInv.pointer(), 16);

    float dir[3] = { dirView.X, dirView.Y, dirView.Z };
    services->setPixelShaderConstant("uDecalDirView", dir, 3);
    services->setPixelShaderConstant("uFade", &fade, 1);

    // Same projection extents SSAO uses for view-space reconstruction.
    float projTan[2] = { 1.0f, 1.0f };
    if (auto* cam = RenderManager::Get()->sceneManager()->getActiveCamera())
    {
        const float ty = tanf(cam->getFOV() * 0.5f);
        projTan[0] = ty * cam->getAspectRatio();
        projTan[1] = ty;
    }
    services->setPixelShaderConstant("uProjTan", projTan, 2);
}

DecalManager::DecalManager()
{
    auto* rm = RenderManager::Get();

    m_callback = new DecalShaderCallback();
    m_material = rm->gpu()->addHighLevelShaderMaterialFromFiles(
        "content/shader/decal.vert", "main", video::EVST_VS_2_0,
        "content/shader/decal.frag", "main", video::EPST_PS_2_0,
        m_callback, video::EMT_ONETEXTURE_BLEND, 0, video::EGSL_DEFAULT);
    if (m_material < 0)
        spdlog::error("DecalManager: decal shader compile failure");

    // Hand-built unit cube (±0.5) so the shader's |local| <= 0.5 volume test is
    // guaranteed to match the rendered box.
    {
        auto* buf = new scene::SMeshBuffer();
        const video::SColor c(255, 255, 255, 255);
        const float h = 0.5f;
        const core::vector3df corners[8] = {
            {-h,-h,-h}, { h,-h,-h}, { h, h,-h}, {-h, h,-h},
            {-h,-h, h}, { h,-h, h}, { h, h, h}, {-h, h, h}
        };
        for (const auto& p : corners)
            buf->Vertices.push_back(video::S3DVertex(p.X, p.Y, p.Z, 0, 0, -1, c, 0, 0));
        const u16 idx[36] = {
            0,2,1, 0,3,2,   // -z
            4,5,6, 4,6,7,   // +z
            0,1,5, 0,5,4,   // -y
            3,6,2, 3,7,6,   // +y
            0,7,3, 0,4,7,   // -x
            1,2,6, 1,6,5    // +x
        };
        for (u16 i : idx)
            buf->Indices.push_back(i);
        buf->recalculateBoundingBox();

        m_boxMesh = new scene::SMesh();
        m_boxMesh->addMeshBuffer(buf);
        buf->drop();
        m_boxMesh->recalculateBoundingBox();
    }
}

DecalManager::~DecalManager()
{
    if (m_boxMesh)
        m_boxMesh->drop();
    delete m_callback;
}

void DecalManager::spawn(const core::vector3df& pos,
                         const core::vector3df& normal,
                         float size,
                         const std::string& texture,
                         float lifetime)
{
    auto* rm  = RenderManager::Get();
    auto* tex = rm->driver()->getTexture(texture.c_str());
    if (!tex)
    {
        spdlog::warn("DecalManager: missing decal texture '{}'", texture);
        return;
    }

    core::vector3df n = normal;
    if (n.getLengthSQ() < 0.0001f)
        n.set(0, 1, 0);
    n.normalize();

    // Basis: box +Z looks INTO the surface (projection direction = -normal),
    // X/Y span the decal face, rolled by a random angle for variety.
    core::vector3df z = -n;
    core::vector3df helper = (fabsf(z.Y) < 0.99f) ? core::vector3df(0, 1, 0)
                                                  : core::vector3df(1, 0, 0);
    core::vector3df x = helper.crossProduct(z).normalize();
    core::vector3df y = z.crossProduct(x).normalize();

    const float roll = static_cast<float>(rand() % 360) * core::DEGTORAD;
    core::vector3df rx = x * cosf(roll) + y * sinf(roll);
    core::vector3df ry = y * cosf(roll) - x * sinf(roll);

    // Row-major with row basis vectors (Irrlicht convention: v * M).
    // Box depth is half the face size — enough surface tolerance without
    // grabbing unrelated geometry behind thin walls.
    Decal d;
    d.world[0] = rx.X * size;        d.world[1] = rx.Y * size;        d.world[2]  = rx.Z * size;        d.world[3]  = 0;
    d.world[4] = ry.X * size;        d.world[5] = ry.Y * size;        d.world[6]  = ry.Z * size;        d.world[7]  = 0;
    d.world[8] = z.X * size * 0.5f;  d.world[9] = z.Y * size * 0.5f;  d.world[10] = z.Z * size * 0.5f;  d.world[11] = 0;
    d.world[12] = pos.X;             d.world[13] = pos.Y;             d.world[14] = pos.Z;              d.world[15] = 1;

    if (!d.world.getInverse(d.invWorld))
        return;

    d.dir     = z;
    d.texture = tex;
    d.life    = lifetime;
    d.maxLife = lifetime;

    if (m_decals.size() >= MAX_DECALS)
        m_decals.erase(m_decals.begin());
    m_decals.push_back(d);
}

void DecalManager::update(float dt)
{
    for (size_t i = 0; i < m_decals.size();)
    {
        m_decals[i].life -= dt;
        if (m_decals[i].life <= 0.0f)
            m_decals.erase(m_decals.begin() + i);
        else
            ++i;
    }
}

void DecalManager::render()
{
    if (m_decals.empty() || m_material < 0 || !m_boxMesh)
        return;

    auto* driver = RenderManager::Get()->driver();
    const core::matrix4 view = driver->getTransform(video::ETS_VIEW);

    for (const auto& d : m_decals)
    {
        m_callback->decalInv = d.invWorld;
        m_callback->fade     = core::clamp(d.life / FADE_WINDOW, 0.0f, 1.0f);
        m_callback->dirView  = d.dir;
        view.rotateVect(m_callback->dirView);
        m_callback->dirView.normalize();

        video::SMaterial mat;
        mat.MaterialType      = static_cast<video::E_MATERIAL_TYPE>(m_material);
        // MODULATE: dst_new = dst * src; the shader outputs 1.0 outside the decal.
        mat.MaterialTypeParam = video::pack_textureBlendFunc(
            video::EBF_DST_COLOR, video::EBF_ZERO, video::EMFN_MODULATE_1X);
        mat.setTexture(0, d.texture);
        mat.ZBuffer          = video::ECFN_NEVER;   // 0 = depth test disabled
        mat.ZWriteEnable     = false;
        mat.Lighting         = false;
        // Back faces only — works with the camera inside the decal volume.
        mat.BackfaceCulling  = false;
        mat.FrontfaceCulling = true;

        driver->setTransform(video::ETS_WORLD, d.world);
        driver->setMaterial(mat);
        driver->drawMeshBuffer(m_boxMesh->getMeshBuffer(0));
    }
}

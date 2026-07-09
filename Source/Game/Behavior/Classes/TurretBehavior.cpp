#include "Engine/Renderer/DecalManager.h"
#include "Game/Behavior/Classes/TurretBehavior.h"

#include "Engine/Engine.h"
#include "Engine/World/Components/TransformComponent.h"
#include "Engine/World/Components/MeshComponent.h"
#include "Engine/World/Components/DescriptorComponent.h"
#include "Game/Components/DamageReceiverComponent.h"

#include <spdlog/spdlog.h>
#include <cmath>

#undef max
#undef min

using namespace irr::core;
using namespace SPK;
using namespace SPK::IRR;

void TurretBehavior::init(anax::Entity& entity)
{
    m_currentYaw      = 0.0f;
    m_currentPitch    = 0.0f;
    m_isFiring        = false;
    m_active          = true;
    m_destroyed       = false;
    m_modelUp         = vector3df(0.0f, 1.0f, 0.0f);
    m_fireCooldown    = 0.0f;
    m_burstCount      = 0;
    m_muzzleFlashTime = MUZZLE_FLASH_DURATION;
    m_smokeHandle     = 0;
    m_coronaNode      = nullptr;

    if (!entity.hasComponent<MeshComponent>()) return;
    auto& mc = entity.getComponent<MeshComponent>();
    if (!mc.node) return;

    m_entityScale = entity.hasComponent<TransformComponent>()
        ? entity.getComponent<TransformComponent>().scale.X
        : 1.0f;

    mc.node->setJointMode(irr::scene::EJUOR_CONTROL);
    m_boneRotY     = mc.node->getJointNode("ROT_Y");
    m_boneRotX     = mc.node->getJointNode("ROT_X");
    m_boneFirespot = mc.node->getJointNode("FIRESPOT");
    m_boneEject    = mc.node->getJointNode("EJECT");
    m_boneLight    = mc.node->getJointNode("LIGHT");

    if (!m_boneRotY)     spdlog::warn("TurretBehavior: bone 'ROT_Y' not found");
    if (!m_boneRotX)     spdlog::warn("TurretBehavior: bone 'ROT_X' not found");
    if (!m_boneFirespot) spdlog::warn("TurretBehavior: bone 'FIRESPOT' not found");
    if (!m_boneEject)    spdlog::warn("TurretBehavior: bone 'EJECT' not found");
    if (!m_boneLight)    spdlog::warn("TurretBehavior: bone 'LIGHT' not found");

    if (m_boneLight)
    {
        float coronaSize = 0.3f * m_entityScale;
        m_coronaNode = RenderManager::Get()->sceneManager()->addBillboardSceneNode(
            m_boneLight, irr::core::dimension2df(coronaSize, coronaSize), irr::core::vector3df(0.0f, 0.0f, 0.0f));
        m_coronaNode->setMaterialTexture(0, RenderManager::Get()->driver()->getTexture("content/texture/particle/flare.png"));
        m_coronaNode->setMaterialFlag(irr::video::EMF_LIGHTING,         false);
        m_coronaNode->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE,    false);
        m_coronaNode->setMaterialFlag(irr::video::EMF_BILINEAR_FILTER,  true);
        m_coronaNode->setMaterialFlag(irr::video::EMF_TRILINEAR_FILTER, true);
        m_coronaNode->setMaterialType(irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL);
        m_coronaNode->setColor(irr::video::SColor(255, 255, 40, 40));
    }

    m_muzzleFlashMaterial = irr::video::EMT_TRANSPARENT_ADD_COLOR;

    SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/weapon/rifle/fire.wav",   true);
    SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/prop/shell1.wav",          true);
    SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/prop/shell2.wav",          true);
    SoundManager::Get()->sound()->addSoundSourceFromFile("content/sound/effect/explosion1.wav",    true);

    ParticleManager::Get()->precache("explosion", "content/particle/explosion.psys");
    ParticleManager::Get()->precache("fire",      "content/particle/fire.psys");

    auto perpixelMat = ShaderMaterialManager::get("phong_perpixel");
    auto* shellMesh  = RenderManager::Get()->sceneManager()->getMesh("content/mesh/prop/shells/shellmedium.obj");
    auto* shellTex   = RenderManager::Get()->driver()->getTexture("content/mesh/prop/shells/shellsColor.png");
    for (int i = 0; i < SHELL_POOL_SIZE; i++)
    {
        m_shellPool[i].node = RenderManager::Get()->sceneManager()->addMeshSceneNode(shellMesh);
        if (m_shellPool[i].node)
        {
			m_shellPool[i].node->setScale(irr::core::vector3df(3.0f * m_entityScale, 3.0f * m_entityScale, 3.0f * m_entityScale));
            m_shellPool[i].node->setMaterialTexture(0, shellTex);
            m_shellPool[i].node->setMaterialFlag(irr::video::EMF_BILINEAR_FILTER,  true);
            m_shellPool[i].node->setMaterialFlag(irr::video::EMF_TRILINEAR_FILTER, true);
            m_shellPool[i].node->setMaterialType(perpixelMat);
            m_shellPool[i].node->setVisible(false);
        }
        m_shellPool[i].active = false;
    }
}

void TurretBehavior::update(anax::Entity& entity, float dt)
{
    if (!m_boneRotY || !m_boneRotX) return;
    if (!entity.hasComponent<TransformComponent>()) return;
    if (!entity.hasComponent<MeshComponent>())     return;
    if (!m_active || m_destroyed) return;

    auto& tc = entity.getComponent<TransformComponent>();
    auto& mc = entity.getComponent<MeshComponent>();

    auto& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
    if (!player.isValid()) return;
    auto& playerTc = player.getComponent<TransformComponent>();

    vector3df toPlayer = playerTc.position - tc.position;
    m_isFiring = toPlayer.getLength() <= m_detectionRange;

    mc.node->updateAbsolutePosition();
    mc.node->animateJoints();

    // Extract model local up each frame — used for orientation-aware pitch, yaw, and shell ejection.
    // Rows [4][5][6] of Irrlicht's row-major matrix4 = local +Y axis in world space.
    {
        const irr::core::matrix4& worldMat = mc.node->getAbsoluteTransformation();
        m_modelUp = vector3df(worldMat[4], worldMat[5], worldMat[6]);
        m_modelUp.normalize();
    }

    if (m_isFiring)
    {
        auto stepAngle = [](float current, float target, float speed, float dt) -> float {
            float delta = target - current;
            while (delta >  180.0f) delta -= 360.0f;
            while (delta < -180.0f) delta += 360.0f;
            float step = speed * (dt / 1000.0f);
            return (fabsf(delta) <= step) ? target : current + (delta > 0.0f ? step : -step);
        };

        irr::core::matrix4 invWorld = mc.node->getAbsoluteTransformation();
        invWorld.setTranslation(vector3df(0.0f, 0.0f, 0.0f));
        invWorld.makeInverse();

        // Yaw: transform full toPlayer into model local space, then zero local Y.
        // Zeroing world Y first (old approach) breaks for ceiling/wall mounts.
        vector3df localDir = toPlayer;
        invWorld.rotateVect(localDir);
        localDir.Y = 0.0f;
        float localHorizDist = sqrtf(localDir.X * localDir.X + localDir.Z * localDir.Z);

        if (localHorizDist > 0.5f)
        {
            float targetYaw = radToDeg(atan2f(localDir.X, localDir.Z));
            m_currentYaw    = stepAngle(m_currentYaw, targetYaw, m_rotateSpeed, dt);
        }

        // Pitch: transform fromPivot into local space using the same invWorld.
        // Using local components makes pitch correct for any mount orientation.
        vector3df fromPivot = m_boneRotX
            ? (playerTc.position - m_boneRotX->getAbsolutePosition())
            : toPlayer;
        invWorld.rotateVect(fromPivot);
        float distHorizLocal = sqrtf(fromPivot.X * fromPivot.X + fromPivot.Z * fromPivot.Z);
        float targetPitch = radToDeg(atan2f(-fromPivot.Y, distHorizLocal)) + 2.0f;
        m_currentPitch    = stepAngle(m_currentPitch, targetPitch, m_rotateSpeed, dt);
    }

    if (m_boneRotY)
    {
        m_boneRotY->setRotation(vector3df(0.0f, m_currentYaw, 0.0f));
        m_boneRotY->updateAbsolutePosition();
    }
    if (m_boneRotX)
    {
        m_boneRotX->setRotation(vector3df(m_currentPitch, 0.0f, 0.0f));
        m_boneRotX->updateAbsolutePosition();
    }
    if (m_boneFirespot)
        m_boneFirespot->updateAbsolutePosition();
    if (m_boneEject)
        m_boneEject->updateAbsolutePosition();

    // LOS check — cast from muzzle to player chest; any hit means geometry is in the way.
    // Using FIRESPOT as origin avoids turret self-hits and unreliable node ID lookups.
    bool canFire = false;
    if (m_isFiring)
    {
        vector3df losOrigin = m_boneFirespot
            ? m_boneFirespot->getAbsolutePosition()
            : tc.position + m_modelUp * 1.5f;
        vector3df losTarget = playerTc.position + vector3df(0.0f, 0.9f, 0.0f);

        RaycastResultData los = RenderManager::Get()->raycastWorldPosition(losOrigin, losTarget, true);
        if (!los.hit)
            canFire = true;
        else if (los.node)
        {
            auto& hitEnt = WorldManager::Get()->managerSystem()->getEntityByID(los.node->getID());
            canFire = hitEnt.isValid() && hitEnt.getComponent<DescriptorComponent>().type == ET_PLAYER;
        }
    }

    m_fireCooldown -= dt;

    if (canFire && m_fireCooldown <= 0.0f)
    {
        vector3df muzzlePos = m_boneFirespot
            ? m_boneFirespot->getAbsolutePosition()
            : tc.position + vector3df(0.0f, 1.0f, 0.0f);

        // Fire along the FIRESPOT bone's own world-space forward axis.
        // Using pivot→tip would include any vertical offset between ROT_X and FIRESPOT
        // in the model, causing the shot to go high/low regardless of aim.
        // Row [8][9][10] = local +Z in world space; swap to [4][5][6] (+Y) if the
        // barrel still doesn't align — depends on how the bone was oriented in Blender.
        vector3df dir;
        if (m_boneFirespot)
        {
            const irr::core::matrix4& mat = m_boneFirespot->getAbsoluteTransformation();
            dir = vector3df(mat[8], mat[9], mat[10]);
            dir.normalize();
        }
        else
            dir = (playerTc.position - muzzlePos).normalize();

        fire(muzzlePos, dir);
        m_burstCount++;
        if (m_burstCount >= 3)
        {
            m_burstCount   = 0;
            m_fireCooldown = m_burstPause;
        }
        else
        {
            m_fireCooldown = 1000.0f / m_fireRate; // fast intra-burst interval
        }
    }

}

void TurretBehavior::persist(anax::Entity& entity, float dt)
{
    if (!m_destroyed && entity.hasComponent<DamageReceiverComponent>())
    {
        if (entity.getComponent<DamageReceiverComponent>().health <= 0)
        {
            m_destroyed = true;

            vector3df deathPos = m_boneFirespot
                ? m_boneFirespot->getAbsolutePosition()
                : entity.getComponent<TransformComponent>().getPosition() + vector3df(0.0f, 1.5f, 0.0f);

            vector3df rotYPos = m_boneRotY
                ? m_boneRotY->getAbsolutePosition()
                : entity.getComponent<TransformComponent>().getPosition();

            uint32_t exHandle = ParticleManager::Get()->spawn("explosion", irr2spk(rotYPos));
            ParticleManager::Get()->setScale(exHandle, m_entityScale);

            m_smokeHandle = ParticleManager::Get()->spawn("fire", irr2spk(rotYPos), true);
            ParticleManager::Get()->setScale(m_smokeHandle, m_entityScale);

            if (m_boneRotX)
                m_boneRotX->setRotation(vector3df(70.0f, 0.0f, 0.0f));

            SoundManager::Get()->sound()->play3D(
                "content/sound/effect/explosion1.wav",
                deathPos, false, false, true, 0, 1.0f);
        }
    }

    if (m_coronaNode)
        m_coronaNode->setVisible(m_active && !m_destroyed);

    updateMuzzleFlash(dt);
    updateTracers(dt);
    updateShells(dt);
}

void TurretBehavior::fire(const vector3df& muzzlePos, const vector3df& dir)
{
    m_lastFiringDir = dir;

    vector3df rayEnd = muzzlePos + dir * 1000.0f;
    RaycastResultData result = RenderManager::Get()->raycastWorldPosition(muzzlePos, rayEnd, true);

    if (result.hit && result.node)
    {
        auto& hitEntity = WorldManager::Get()->managerSystem()->getEntityByID(result.node->getID());
        if (hitEntity.isValid() && hitEntity.hasComponent<DescriptorComponent>())
        {
            auto& hitDesc = hitEntity.getComponent<DescriptorComponent>();
            if (hitDesc.type == ET_STATIC || hitDesc.type == ET_DYNAMIC || hitDesc.type == ET_PLAYER)
            {
                if (hitEntity.hasComponent<DamageReceiverComponent>())
                    hitEntity.getComponent<DamageReceiverComponent>().damageReceived += m_damage;

                if (hitDesc.type != ET_PLAYER)
                {
                    ParticleManager::Get()->setScale(
                        ParticleManager::Get()->spawn("spark", irr2spk(result.point)),
                        m_entityScale);

                    // Bullet hole — screen-space decal along the surface normal
                    RenderManager::Get()->decals()->spawn(
                        result.point, result.normal, 0.2f,
                        "content/texture/fx/bullet_hole.png");
                }
            }
        }
    }

    vector3df tracerEnd = (result.hit && result.node) ? result.point : rayEnd;
    createTracerBeam(muzzlePos, tracerEnd);
    createMuzzleFlash();
    ejectShell();

    SoundManager::Get()->sound()->play3D(
        "content/sound/weapon/rifle/fire.wav",
        muzzlePos, false, false, true, 0, 0.6f
    );
}

void TurretBehavior::createMuzzleFlash()
{
    if (!m_boneFirespot) return;

    if (!m_muzzleStarNode)
    {
        m_muzzleStarNode = RenderManager::Get()->sceneManager()->addBillboardSceneNode(
            m_boneFirespot, irr::core::dimension2df(1.2f * m_entityScale, 1.2f * m_entityScale), vector3df(0.0f, 0.0f, 0.0f));
        m_muzzleStarNode->setMaterialFlag(irr::video::EMF_LIGHTING,      false);
        m_muzzleStarNode->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE, false);
        m_muzzleStarNode->setMaterialFlag(irr::video::EMF_BLEND_OPERATION, true);
        m_muzzleStarNode->setMaterialType(m_muzzleFlashMaterial);
        m_muzzleStarNode->setColor(irr::video::SColor(255, 255, 204, 76));
    }

    auto* tex = RenderManager::Get()->driver()->getTexture("content/texture/particle/star_05.png");
    m_muzzleStarNode->setMaterialTexture(0, tex);
    m_muzzleStarNode->setVisible(true);
    m_muzzleStarNode->updateAbsolutePosition();

    if (!m_muzzleLightNode)
    {
        m_muzzleLightNode = RenderManager::Get()->sceneManager()->addLightSceneNode(
            m_boneFirespot, vector3df(0.0f, 0.0f, 0.0f),
            irr::video::SColorf(1.0f, 0.8f, 0.3f), 3.0f * m_entityScale);
    }
    if (m_muzzleLightNode)
    {
        m_muzzleLightNode->setVisible(true);
        m_muzzleLightNode->updateAbsolutePosition();
    }

    m_muzzleFlashTime = 0.0f;
}

void TurretBehavior::updateMuzzleFlash(float dt)
{
    if (!m_muzzleStarNode) return;

    m_muzzleFlashTime += dt;
    if (m_muzzleFlashTime >= MUZZLE_FLASH_DURATION)
    {
        m_muzzleStarNode->setVisible(false);
        if (m_muzzleLightNode) m_muzzleLightNode->setVisible(false);
    }
    else
    {
        float fade     = 1.0f - (m_muzzleFlashTime / MUZZLE_FLASH_DURATION);
        irr::u32 alpha = static_cast<irr::u32>(fade * 255.0f);
        m_muzzleStarNode->setColor(irr::video::SColor(alpha, 255, 204, 76));
    }
}

void TurretBehavior::createTracerBeam(const vector3df& start, const vector3df& end)
{
    vector3df dir  = end - start;
    float     dist = dir.getLength();
    if (dist < 0.1f) return;
    dir.normalize();

    auto* planeMesh = RenderManager::Get()->sceneManager()->getGeometryCreator()
        ->createPlaneMesh(irr::core::dimension2df(0.1f * m_entityScale, dist), irr::core::dimension2du(1, 1));
    auto* node = RenderManager::Get()->sceneManager()->addMeshSceneNode(planeMesh);
    if (!node) return;

    vector3df rot;
    rot.Y = atan2f(dir.X, dir.Z) * (180.0f / 3.14159265f);
    rot.X = -asinf(dir.Y)        * (180.0f / 3.14159265f);
    rot.Z = 0.0f;
    node->setRotation(rot);
    node->setPosition(start + dir * (dist * 0.5f));

    auto* tex = RenderManager::Get()->driver()->getTexture("content/texture/particle/trace_07.png");
    node->setMaterialTexture(0, tex);
    node->setMaterialFlag(irr::video::EMF_LIGHTING,          false);
    node->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE,     false);
    node->setMaterialFlag(irr::video::EMF_BACK_FACE_CULLING, false);
    node->setMaterialFlag(irr::video::EMF_BLEND_OPERATION,   true);
    node->setMaterialType(m_muzzleFlashMaterial);

    auto* mesh = node->getMesh();
    for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); b++)
    {
        auto* buf   = mesh->getMeshBuffer(b);
        auto* verts = static_cast<irr::video::S3DVertex*>(buf->getVertices());
        for (irr::u32 v = 0; v < buf->getVertexCount(); v++)
            verts[v].Color = irr::video::SColor(255, 255, 200, 100);
        buf->setDirty(irr::scene::EBT_VERTEX);
    }

    TracerBeam tb;
    tb.node      = node;
    tb.spawnTime = Engine::Get()->getCurrentTime();
    m_tracerBeams.push_back(tb);
}

void TurretBehavior::updateTracers(float dt)
{
    float now = Engine::Get()->getCurrentTime();
    for (auto it = m_tracerBeams.begin(); it != m_tracerBeams.end();)
    {
        float elapsed = now - it->spawnTime;
        if (elapsed >= it->lifetime)
        {
            if (it->node) it->node->remove();
            it = m_tracerBeams.erase(it);
        }
        else
        {
            float    fade  = 1.0f - (elapsed / it->lifetime);
            irr::u32 alpha = static_cast<irr::u32>(fade * 255.0f);
            if (it->node)
            {
                auto* mesh = it->node->getMesh();
                for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); b++)
                {
                    auto* buf   = mesh->getMeshBuffer(b);
                    auto* verts = static_cast<irr::video::S3DVertex*>(buf->getVertices());
                    for (irr::u32 v = 0; v < buf->getVertexCount(); v++)
                        verts[v].Color = irr::video::SColor(alpha, 255, 200, 100);
                    buf->setDirty(irr::scene::EBT_VERTEX);
                }
            }
            ++it;
        }
    }
}

void TurretBehavior::ejectShell()
{
    if (!m_boneEject) return;

    ShellCasing* shell = nullptr;
    for (int i = 0; i < SHELL_POOL_SIZE; i++)
    {
        if (!m_shellPool[i].active) { shell = &m_shellPool[i]; break; }
    }
    if (!shell || !shell->node) return;

    vector3df right = m_lastFiringDir.crossProduct(m_modelUp).normalize();
    vector3df rnd(
        Engine::Get()->rng()->getFloat(-0.3f, 0.3f),
        Engine::Get()->rng()->getFloat(-0.3f, 0.3f),
        Engine::Get()->rng()->getFloat(-0.3f, 0.3f)
    );
    vector3df ejectionDir = (-right + m_modelUp * 1.5f + rnd).normalize();
    float     speed       = 8.0f * m_entityScale * Engine::Get()->rng()->getFloat(0.75f, 1.25f);

    // Offset spawn outward so the shell starts clear of the mesh
    vector3df ejectPos = m_boneEject->getAbsolutePosition() + ejectionDir * 0.5f * m_entityScale;

    float yaw   = atan2f(m_lastFiringDir.X, m_lastFiringDir.Z) * (180.0f / 3.14159265f);
    float pitch = asinf(irr::core::clamp(m_lastFiringDir.Y, -1.0f, 1.0f)) * (180.0f / 3.14159265f);

    shell->node->setPosition(ejectPos);
    shell->rotation = vector3df(-pitch, yaw, 0.0f);
    shell->node->setRotation(shell->rotation);
    shell->velocity = ejectionDir * speed;
    shell->angularVelocity = vector3df(
        Engine::Get()->rng()->getFloat(-300.0f, 300.0f),
        Engine::Get()->rng()->getFloat(-300.0f, 300.0f),
        Engine::Get()->rng()->getFloat(-300.0f, 300.0f)
    );
    shell->spawnTime    = static_cast<float>(Engine::Get()->getCurrentTime());
    shell->active       = true;
    shell->physicsActive = true;
    shell->bounceCount  = 0;
    shell->node->setVisible(true);
}

void TurretBehavior::updateShells(float dt)
{
    const float dtSec        = dt * 0.001f;
    const float currentTime  = static_cast<float>(Engine::Get()->getCurrentTime());
    const float shellLifetime = 10000.0f;
    const float shellGravity  = 9.81f;

    for (int i = 0; i < SHELL_POOL_SIZE; i++)
    {
        ShellCasing& shell = m_shellPool[i];
        if (!shell.active) continue;

        if (currentTime - shell.spawnTime >= shellLifetime)
        {
            shell.active = false;
            shell.node->setVisible(false);
            continue;
        }

        if (!shell.physicsActive) continue;

        shell.velocity.Y -= shellGravity * dtSec;

        vector3df pos    = shell.node->getPosition();
        vector3df newPos = pos + shell.velocity * dtSec;

        float speed = shell.velocity.getLength();
        bool  inGrace = (currentTime - shell.spawnTime < 150.0f);
        if (!inGrace && speed > 0.001f)
        {
            vector3df      travelDir = shell.velocity / speed;
            RaycastResultData hit    = RenderManager::Get()->raycastWorldPosition(
                pos, newPos + travelDir * 0.1f, true);

            if (hit.hit)
            {
                vector3df n = hit.normal;
                float     d = shell.velocity.dotProduct(n);
                shell.velocity       = (shell.velocity - n * (2.0f * d)) * 0.45f;
                shell.angularVelocity *= 0.5f;
                newPos = hit.point + n * 0.05f;

                if (shell.bounceCount < 2 && (currentTime - m_lastShellBounceSound) >= 150.0f)
                {
                    std::string snd = "content/sound/prop/shell"
                        + std::to_string(Engine::Get()->rng()->getInt(1, 2)) + ".wav";
                    SoundManager::Get()->sound()->play3D(
                        snd.c_str(), shell.node->getPosition(), false, false, true, 0, 0.5f);
                    m_lastShellBounceSound = currentTime;
                }

                shell.bounceCount++;
                if (shell.bounceCount >= 3)
                {
                    shell.physicsActive = false;
                    shell.velocity      = vector3df(0.0f, 0.0f, 0.0f);
                }
            }
        }

        shell.node->setPosition(newPos);
        shell.rotation += shell.angularVelocity * dtSec;
        shell.node->setRotation(shell.rotation);
    }
}

void TurretBehavior::onLogicSignal(anax::Entity& entity)
{
    m_active = !m_active;
}

void TurretBehavior::destroy(anax::Entity& entity)
{
    if (m_smokeHandle)
        ParticleManager::Get()->destroy(m_smokeHandle);

    for (int i = 0; i < SHELL_POOL_SIZE; i++)
    {
        if (m_shellPool[i].node)
        {
            m_shellPool[i].node->remove();
            m_shellPool[i].node = nullptr;
        }
    }

    for (auto& tb : m_tracerBeams)
        if (tb.node) tb.node->remove();
    m_tracerBeams.clear();

    if (m_muzzleStarNode)  { m_muzzleStarNode->remove();  m_muzzleStarNode  = nullptr; }
    if (m_muzzleLightNode) { m_muzzleLightNode->remove(); m_muzzleLightNode = nullptr; }
    if (m_coronaNode)      { m_coronaNode->remove();      m_coronaNode      = nullptr; }
}

#include "Game/Behavior/Classes/TurretBehavior.h"
#include "Engine/World/Components/TransformComponent.h"
#include "Engine/World/Components/MeshComponent.h"
#include "Engine/World/WorldManager.h"
#include "Engine/Engine.h"
#include "Engine/Renderer/Particle/ParticleManager.h"
#include "Engine/Sound/SoundManager.h"

#include <spdlog/spdlog.h>

#include <cmath>
#include <algorithm>

using namespace irr::core;

// ------------------------------------------------------------------ init ---

void TurretBehavior::init(anax::Entity& entity)
{
    m_fireCooldown = 0.0f;
    m_currentYaw   = 0.0f;
    m_currentPitch = 0.0f;
    m_fireLeft     = true;

    m_tracerMaterial = irr::video::EMT_TRANSPARENT_ADD_COLOR;

    if (!entity.hasComponent<MeshComponent>()) return;
    auto& mc = entity.getComponent<MeshComponent>();
    if (!mc.node) return;

    mc.node->setJointMode(irr::scene::EJUOR_CONTROL);
    m_bodyBone      = mc.node->getJointNode("BODY");
    m_leftBone      = mc.node->getJointNode("LEFT");
    m_rightBone     = mc.node->getJointNode("RIGHT");
    m_leftFirespot  = mc.node->getJointNode("LEFT_FIRESPOT");
    m_rightFirespot = mc.node->getJointNode("RIGHT_FIRESPOT");

    if (!m_bodyBone)      spdlog::warn("TurretBehavior: bone 'BODY' not found");
    if (!m_leftBone)      spdlog::warn("TurretBehavior: bone 'LEFT' not found");
    if (!m_rightBone)     spdlog::warn("TurretBehavior: bone 'RIGHT' not found");
    if (!m_leftFirespot)  spdlog::warn("TurretBehavior: bone 'LEFT_FIRESPOT' not found");
    if (!m_rightFirespot) spdlog::warn("TurretBehavior: bone 'RIGHT_FIRESPOT' not found");
}

// ---------------------------------------------------------------- update ---

void TurretBehavior::update(anax::Entity& entity, float dt)
{
    // Always tick projectiles and tracers even when player is out of range
    updateProjectiles(dt);
    updateTracers(dt);

    if (!m_bodyBone) return;
    if (!entity.hasComponent<TransformComponent>()) return;
    if (!entity.hasComponent<MeshComponent>())     return;

    auto& tc = entity.getComponent<TransformComponent>();
    auto& mc = entity.getComponent<MeshComponent>();

    auto& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
    if (!player.isValid()) return;
    auto& playerTc = player.getComponent<TransformComponent>();

    vector3df toPlayer = playerTc.position - tc.position;
    if (toPlayer.getLength() > m_detectionRange) return;

    // --- Bone tracking ---
    float distXZ      = sqrtf(toPlayer.X * toPlayer.X + toPlayer.Z * toPlayer.Z);
    float targetYaw   = radToDeg(atan2f(toPlayer.X, toPlayer.Z)) - tc.rotation.Y - 90.0f;
    float targetPitch = radToDeg(atan2f(-toPlayer.Y, distXZ));

    auto stepAngle = [](float current, float target, float speed, float dt) -> float {
        float delta = target - current;
        while (delta >  180.0f) delta -= 360.0f;
        while (delta < -180.0f) delta += 360.0f;
        float step = speed * dt;
        return (fabsf(delta) <= step) ? target : current + (delta > 0.0f ? step : -step);
    };

    m_currentYaw   = stepAngle(m_currentYaw,   targetYaw,   m_rotateSpeed, dt);
    m_currentPitch = stepAngle(m_currentPitch, targetPitch, m_rotateSpeed, dt);

    mc.node->updateAbsolutePosition();
    mc.node->animateJoints();

    m_bodyBone->setRotation(vector3df(0.0f, m_currentYaw, 0.0f));
    m_bodyBone->updateAbsolutePosition();

    if (m_leftBone)
    {
        m_leftBone->setRotation(vector3df(0.0f, 0.0f, -m_currentPitch - 20.f));
        m_leftBone->updateAbsolutePosition();
        if (m_leftFirespot) m_leftFirespot->updateAbsolutePosition();
    }
    if (m_rightBone)
    {
        m_rightBone->setRotation(vector3df(0.0f, 0.0f, -m_currentPitch - 20.f));
        m_rightBone->updateAbsolutePosition();
        if (m_rightFirespot) m_rightFirespot->updateAbsolutePosition();
    }

    // --- Fire ---
    m_fireCooldown -= dt;
    if (m_fireCooldown <= 0.0f)
    {
        m_fireCooldown = 1000.0f / m_fireRate;

        irr::scene::IBoneSceneNode* firespotBone = m_fireLeft ? m_leftFirespot  : m_rightFirespot;
        irr::scene::IBoneSceneNode* fallbackBone = m_fireLeft ? m_leftBone      : m_rightBone;
        m_fireLeft = !m_fireLeft;

        irr::scene::IBoneSceneNode* muzzleBone = firespotBone ? firespotBone : fallbackBone;
        vector3df muzzlePos = muzzleBone
            ? muzzleBone->getAbsolutePosition()
            : tc.position + vector3df(0.0f, 1.0f, 0.0f);

        vector3df dir = (playerTc.position - muzzlePos).normalize();

        switch (m_type)
        {
            case 0: fireGun(muzzlePos, dir);     break;
            case 1: fireGrenade(muzzlePos, dir); break;
            case 2: fireRocket(muzzlePos, dir);  break;
            case 3: fireLaser(muzzlePos, dir);   break;
        }
    }
}

// --------------------------------------------------------------- destroy ---

void TurretBehavior::destroy(anax::Entity& entity)
{
    for (auto& proj : m_projectiles)
        if (proj.entity.isValid())
            WorldManager::Get()->killEntityByID(proj.id);
    m_projectiles.clear();

    for (auto& tb : m_tracerBeams)
        if (tb.node) tb.node->remove();
    m_tracerBeams.clear();
}

// ---------------------------------------------------------- fire methods ---

void TurretBehavior::fireGun(const vector3df& muzzlePos, const vector3df& dir)
{
    auto hit = RenderManager::Get()->raycastWorldPosition(muzzlePos, muzzlePos + dir * 500.0f, true);
    if (hit.hit && hit.node)
    {
        auto& e = WorldManager::Get()->managerSystem()->getEntityByID(hit.node->getID());
        if (e.isValid() && e.hasComponent<DamageReceiverComponent>())
            e.getComponent<DamageReceiverComponent>().damageReceived += m_damage;
    }
    vector3df tracerEnd = (hit.hit && hit.node) ? hit.point : muzzlePos + dir * 500.0f;
    createTracerBeam(muzzlePos, tracerEnd);
}

void TurretBehavior::fireGrenade(const vector3df& muzzlePos, const vector3df& dir)
{
    // Arc the launch direction upward slightly
    vector3df arcDir = (dir + vector3df(0.0f, 0.4f, 0.0f)).normalize();
    spawnProjectile(muzzlePos, arcDir * 20.0f, 4000.0f, true);
    SoundManager::Get()->sound()->play3D("content/sound/effect/explosion2.wav", muzzlePos);
}

void TurretBehavior::fireRocket(const vector3df& muzzlePos, const vector3df& dir)
{
    spawnProjectile(muzzlePos, dir * 35.0f, 6000.0f, false);
}

void TurretBehavior::fireLaser(const vector3df& muzzlePos, const vector3df& dir)
{
    auto hit = RenderManager::Get()->raycastWorldPosition(muzzlePos, muzzlePos + dir * 500.0f, true);
    if (hit.hit && hit.node)
    {
        auto& e = WorldManager::Get()->managerSystem()->getEntityByID(hit.node->getID());
        if (e.isValid() && e.hasComponent<DamageReceiverComponent>())
            e.getComponent<DamageReceiverComponent>().damageReceived += m_damage;
        ParticleManager::Get()->spawn("sparks", SPK::IRR::irr2spk(hit.point));
    }
    createTracerBeam(muzzlePos, (hit.hit && hit.node) ? hit.point : muzzlePos + dir * 500.0f);
}

// ---------------------------------------------------- projectile helpers ---

void TurretBehavior::spawnProjectile(const vector3df& pos, const vector3df& velocity,
                                     float maxLifetime, bool hasGravity)
{
    auto& world = *WorldManager::Get()->world();
    auto proj = world.createEntity();

    proj.addComponent<DescriptorComponent>();
    auto& desc   = proj.getComponent<DescriptorComponent>();
    desc.id      = WorldManager::Get()->getNewID();
    desc.name    = "turret_proj_" + std::to_string(desc.id);
    desc.type    = ET_DYNAMIC;
    desc.isSerializable = false;

    proj.addComponent<TransformComponent>();
    proj.getComponent<TransformComponent>().position = pos;

    proj.addComponent<RenderComponent>();
    proj.addComponent<MeshComponent>();
    auto& mesh = proj.getComponent<MeshComponent>();
    mesh.mesh = "content/mesh/prop/missile.obj";
    mesh.textures.emplace_back("content/mesh/prop/missile.png");

    proj.activate();

    TurretProjectile tp;
    tp.entity      = proj;
    tp.id          = desc.id;
    tp.velocity    = velocity;
    tp.maxLifetime = maxLifetime;
    tp.hasGravity  = hasGravity;
    m_projectiles.push_back(tp);
}

void TurretBehavior::updateProjectiles(float dt)
{
    const float dtSec = dt / 1000.0f;

    for (auto& proj : m_projectiles)
    {
        if (!proj.entity.isValid()) { proj.lifetime = proj.maxLifetime + 1.0f; continue; }

        proj.lifetime += dt;
        if (proj.hasGravity)
            proj.velocity.Y -= 9.8f * dtSec;

        auto& ptc    = proj.entity.getComponent<TransformComponent>();
        vector3df newPos = ptc.position + proj.velocity * dtSec;

        auto hit = RenderManager::Get()->raycastWorldPosition(ptc.position, newPos, true);
        if (hit.hit)
        {
            detonateAt(hit.point);
            WorldManager::Get()->killEntityByID(proj.id);
            proj.lifetime = proj.maxLifetime + 1.0f;
            continue;
        }

        ptc.position = newPos;
    }

    m_projectiles.erase(
        std::remove_if(m_projectiles.begin(), m_projectiles.end(),
            [](const TurretProjectile& p) { return p.lifetime > p.maxLifetime; }),
        m_projectiles.end());
}

void TurretBehavior::detonateAt(const vector3df& pos)
{
    SoundManager::Get()->sound()->play3D("content/sound/effect/explosion2.wav", pos);
    ParticleManager::Get()->spawn("explosion", SPK::IRR::irr2spk(pos));

    for (auto& entity : WorldManager::Get()->world()->getEntities())
    {
        if (!entity.isValid()) continue;
        if (!entity.hasComponent<TransformComponent>())    continue;
        if (!entity.hasComponent<DamageReceiverComponent>()) continue;

        float dist = (entity.getComponent<TransformComponent>().position - pos).getLength();
        if (dist >= m_splashRadius) continue;

        float falloff = 1.0f - (dist / m_splashRadius);
        entity.getComponent<DamageReceiverComponent>().damageReceived +=
            static_cast<int>(m_damage * falloff);
    }
}

// --------------------------------------------------------- tracer beams ---

void TurretBehavior::createTracerBeam(const vector3df& start, const vector3df& end)
{
    vector3df dir = end - start;
    float dist = dir.getLength();
    if (dist < 0.1f) return;
    dir.normalize();

    auto* planeMesh = RenderManager::Get()->sceneManager()->getGeometryCreator()
        ->createPlaneMesh(irr::core::dimension2df(0.05f, dist), irr::core::dimension2du(1, 1));
    auto* node = RenderManager::Get()->sceneManager()->addMeshSceneNode(planeMesh);
    planeMesh->drop();
    if (!node) return;

    vector3df rotation;
    rotation.Y = atan2f(dir.X, dir.Z) * (180.0f / irr::core::PI);
    rotation.X = -asinf(clamp(dir.Y, -1.0f, 1.0f)) * (180.0f / irr::core::PI);
    rotation.Z = 0.0f;
    node->setRotation(rotation);
    node->setPosition(start + dir * (dist * 0.5f));

    auto* tex = RenderManager::Get()->driver()->getTexture("content/texture/particle/trace_07.png");
    node->setMaterialTexture(0, tex);
    node->setMaterialFlag(irr::video::EMF_LIGHTING,          false);
    node->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE,     false);
    node->setMaterialFlag(irr::video::EMF_BACK_FACE_CULLING, false);
    node->setMaterialFlag(irr::video::EMF_BLEND_OPERATION,   true);
    node->setMaterialType(m_tracerMaterial);
    node->getMaterial(0).AmbientColor  = irr::video::SColor(255, 255, 200, 100);
    node->getMaterial(0).DiffuseColor  = irr::video::SColor(255, 255, 200, 100);
    node->getMaterial(0).EmissiveColor = irr::video::SColor(255, 255, 200, 100);

    TracerBeam tb;
    tb.node      = node;
    tb.spawnTime = static_cast<float>(Engine::Get()->getCurrentTime());
    m_tracerBeams.push_back(tb);
}

void TurretBehavior::updateTracers(float dt)
{
    float now = static_cast<float>(Engine::Get()->getCurrentTime());
    for (auto it = m_tracerBeams.begin(); it != m_tracerBeams.end();)
    {
        if (now - it->spawnTime >= it->lifetime)
        {
            if (it->node) it->node->remove();
            it = m_tracerBeams.erase(it);
        }
        else ++it;
    }
}

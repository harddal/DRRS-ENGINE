#include "Engine/Navigation/CrowdManager.h"

#include <cmath>
#include <cstring>

#include <spdlog/spdlog.h>

#include "Engine/Navigation/Detour/Include/DetourCommon.h"   // dtVcopy / dtVset
#include "Engine/Navigation/NavigationManager.h"
#include "Engine/Renderer/RenderManager.h"
#include "Engine/World/Components/TransformComponent.h"

using namespace irr::core;

CrowdManager* CrowdManager::s_Instance = nullptr;

namespace
{
    // Fixed pool. dtCrowd allocates the whole thing up front (a 256-ref path
    // corridor per agent), so this is a real memory decision, not a limit that
    // grows.
    const int   k_maxAgents = 128;

    // Sizes the proximity grid (m_grid->init(maxAgents*4, radius*3)) and the
    // placement half-extents addAgent snaps with -- it is NOT just a clamp. An
    // agent fatter than this silently gets degraded neighbour queries, so leave
    // headroom above the 0.35 the navmesh is baked for.
    const float k_maxAgentRadius = 1.0f;

    // Re-issuing requestMoveTarget every frame restarts the plan every frame and
    // the agent never converges on a corridor. Only resubmit once the goal has
    // actually moved this far.
    const float k_goalResubmitDist = 0.5f;

    // Past this, the agent and the real body have stopped describing the same
    // creature -- re-seat the corridor rather than letting movePosition clamp
    // it back forever.
    const float k_maxDivergence = 1.5f;

    const float k_snapExtents[3] = { 2.0f, 4.0f, 2.0f };
}

// ---------------------------------------------------------------------------

CrowdManager::CrowdManager()
{
    if (s_Instance)
    {
        spdlog::error("CrowdManager: duplicate instance");
        return;
    }
    s_Instance = this;
}

CrowdManager::~CrowdManager()
{
    shutdown();
    s_Instance = nullptr;
}

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

bool CrowdManager::ensureInit()
{
    if (m_crowd) return true;
    if (!m_enabled) return false;

    NavigationManager* nav = NavigationManager::Get();
    if (!nav || !nav->isNavMeshBuilt()) return false;

    m_crowd = dtAllocCrowd();
    if (!m_crowd)
    {
        spdlog::error("CrowdManager: dtAllocCrowd failed");
        return false;
    }

    if (!m_crowd->init(k_maxAgents, k_maxAgentRadius, nav->navMesh()))
    {
        spdlog::error("CrowdManager: dtCrowd::init failed");
        dtFreeCrowd(m_crowd);
        m_crowd = nullptr;
        return false;
    }

    // --- Obstacle-avoidance quality ------------------------------------------
    // dtCrowd::init fills all 8 presets identically (DetourCrowd.cpp:408-417),
    // so without this every agent runs Detour's stock adaptive sampling and the
    // tuning is invisible in our source. Overwrite preset 0 -- the only one any
    // agent is assigned -- so the numbers live here.
    //
    // Every field is >= the stock default, which is what "higher quality" has
    // to mean for an adaptive sampler:
    //
    //   velBias       0.4 -> 0.5   sample ring centred further along the desired
    //                              velocity, so a clear lane is preferred over a
    //                              sideways dodge
    //   adaptiveDivs    7 -> 7     (unchanged; already the Recast demo's high)
    //   adaptiveRings   2 -> 3     the demo's high preset -- more speed samples,
    //                              which is what lets an agent choose to SLOW
    //                              rather than only to swerve
    //   adaptiveDepth   5 -> 5     kept at Detour's default, which is already
    //                              above the demo's high (3). Do not lower it to
    //                              "match the demo" -- that is a downgrade.
    {
        dtObstacleAvoidanceParams avoid;
        memset(&avoid, 0, sizeof(avoid));
        avoid.velBias       = 0.5f;
        avoid.weightDesVel  = 2.0f;
        avoid.weightCurVel  = 0.75f;
        avoid.weightSide    = 0.75f;
        avoid.weightToi     = 2.5f;
        avoid.horizTime     = 2.5f;
        avoid.gridSize      = 33;
        avoid.adaptiveDivs  = 7;
        avoid.adaptiveRings = 3;
        avoid.adaptiveDepth = 5;
        m_crowd->setObstacleAvoidanceParams(0, &avoid);
    }

    m_records.assign(static_cast<size_t>(k_maxAgents), Record());
    m_agentCount = 0;

    spdlog::info("CrowdManager: initialised ({} agent slots, generation {})",
                 k_maxAgents, m_generation);
    return true;
}

void CrowdManager::shutdown()
{
    m_records.clear();
    m_agentCount = 0;

    if (!m_crowd) return;

    dtFreeCrowd(m_crowd);
    m_crowd = nullptr;

    // Every outstanding CrowdHandle is now stale. Bumping this is what makes
    // that detectable instead of a stale index quietly aliasing somebody else's
    // agent after the next init.
    ++m_generation;

    spdlog::info("CrowdManager: released (generation now {})", m_generation);
}

void CrowdManager::setOptimizeTopology(bool enabled)
{
    if (m_optimizeTopo == enabled) return;
    m_optimizeTopo = enabled;

    if (!m_crowd) return;   // picked up by registerAgent when the crowd comes up

    // Applied to the agents already registered. updateAgentParameters only
    // copies the params block -- it does not reset the corridor -- so this is
    // safe to flip mid-chase, which is the entire point of having it as a
    // console command: you want to A/B it on the NPC currently misbehaving.
    for (size_t i = 0; i < m_records.size(); ++i)
    {
        if (!m_records[i].used) continue;

        dtCrowdAgent* ag = m_crowd->getEditableAgent(static_cast<int>(i));
        if (!ag || !ag->active) continue;

        dtCrowdAgentParams params = ag->params;
        if (enabled) params.updateFlags |=  DT_CROWD_OPTIMIZE_TOPO;
        else         params.updateFlags &= ~DT_CROWD_OPTIMIZE_TOPO;

        m_crowd->updateAgentParameters(static_cast<int>(i), &params);
    }

    spdlog::info("CrowdManager: path topology optimisation {}",
                 enabled ? "ON" : "OFF");
}

void CrowdManager::setEnabled(bool enabled)
{
    if (m_enabled == enabled) return;
    m_enabled = enabled;

    // Tear down rather than idle: behaviours then fall back to the waypoint
    // walker cleanly, and re-enabling re-registers everyone from scratch.
    if (!m_enabled) shutdown();
}

// ---------------------------------------------------------------------------
// Agents
// ---------------------------------------------------------------------------

bool CrowdManager::isRegistered(const CrowdHandle& handle) const
{
    return m_crowd
        && handle.assigned()
        && handle.generation == m_generation
        && static_cast<size_t>(handle.idx) < m_records.size()
        && m_records[handle.idx].used;
}

bool CrowdManager::registerAgent(const anax::Entity& entity, const CrowdAgentConfig& cfg,
                                 CrowdHandle& handle)
{
    handle.clear();

    if (!ensureInit()) return false;
    if (!entity.isValid() || !entity.hasComponent<TransformComponent>()) return false;

    const vector3df p = entity.getComponent<TransformComponent>().getPosition();
    const float pos[3] = { p.X, p.Y, p.Z };

    dtCrowdAgentParams params;
    memset(&params, 0, sizeof(params));
    params.radius               = cfg.radius;
    params.height               = cfg.height;
    params.maxAcceleration      = cfg.maxAcceleration;
    params.maxSpeed             = cfg.maxSpeed;
    params.collisionQueryRange  = cfg.radius * 12.0f;
    params.pathOptimizationRange= cfg.radius * 30.0f;
    params.separationWeight     = cfg.separationWeight;
    params.updateFlags          = DT_CROWD_ANTICIPATE_TURNS
                                | DT_CROWD_OBSTACLE_AVOIDANCE
                                | DT_CROWD_SEPARATION
                                | DT_CROWD_OPTIMIZE_VIS;

    // OPTIMIZE_VIS raycast-shortcuts WITHIN the corridor the agent already
    // holds, and is always on. OPTIMIZE_TOPO re-plans the corridor TOPOLOGY and
    // is a runtime toggle (ai_topo) that defaults OFF -- see the header for the
    // MAX_ITER=32 partial-finalize reasoning that put it behind a switch.
    if (m_optimizeTopo)
        params.updateFlags |= DT_CROWD_OPTIMIZE_TOPO;

    // dtCrowd::init fills all 8 avoidance presets identically; ensureInit()
    // above overwrites preset 0 with our own numbers.
    params.obstacleAvoidanceType = 0;
    params.queryFilterType       = 0;
    params.userData              = nullptr;

    // addAgent does its own findNearestPoly with m_agentPlacementHalfExtents and
    // returns -1 when the position is not on the navmesh -- which is ordinary,
    // not exceptional: an NPC standing on a mesh nobody flagged 'Cook
    // Navigation' lands here. The caller falls back to the straight-line walker.
    const int idx = m_crowd->addAgent(pos, &params);
    if (idx < 0) return false;

    if (static_cast<size_t>(idx) >= m_records.size())
        m_records.resize(static_cast<size_t>(idx) + 1);

    Record& rec = m_records[idx];
    rec.entity  = entity;
    rec.used    = true;
    rec.hasGoal = false;
    ++m_agentCount;

    handle.idx        = idx;
    handle.generation = m_generation;
    return true;
}

void CrowdManager::unregisterAgent(CrowdHandle& handle)
{
    if (isRegistered(handle))
    {
        m_crowd->removeAgent(handle.idx);

        Record& rec = m_records[handle.idx];
        rec.used    = false;
        rec.hasGoal = false;
        rec.entity  = anax::Entity();
        --m_agentCount;
    }

    // Cleared even when the handle was already stale -- the caller's copy must
    // not keep pointing into a pool it no longer belongs to.
    handle.clear();
}

void CrowdManager::setAgentSpeed(const CrowdHandle& handle, float maxSpeed)
{
    if (!isRegistered(handle)) return;

    dtCrowdAgent* ag = m_crowd->getEditableAgent(handle.idx);
    if (!ag) return;
    if (std::fabs(ag->params.maxSpeed - maxSpeed) < 0.01f) return;

    dtCrowdAgentParams params = ag->params;
    params.maxSpeed        = maxSpeed;
    params.maxAcceleration = maxSpeed * 8.0f;
    m_crowd->updateAgentParameters(handle.idx, &params);
}

// ---------------------------------------------------------------------------
// Steering
// ---------------------------------------------------------------------------

CROWD_STEER CrowdManager::steer(const CrowdHandle& handle, const vector3df& goal,
                                vector3df& outVelocity)
{
    if (!m_enabled || !isRegistered(handle)) return CROWD_STEER_UNAVAILABLE;

    NavigationManager* nav = NavigationManager::Get();
    if (!nav || !nav->navQuery()) return CROWD_STEER_UNAVAILABLE;

    Record& rec = m_records[handle.idx];

    const bool goalMoved = !rec.hasGoal ||
        (goal - rec.lastGoal).getLengthSQ() > (k_goalResubmitDist * k_goalResubmitDist);

    if (goalMoved)
    {
        const float g[3] = { goal.X, goal.Y, goal.Z };
        dtPolyRef ref     = 0;
        float     snap[3] = { 0.0f, 0.0f, 0.0f };

        if (dtStatusSucceed(nav->navQuery()->findNearestPoly(
                g, k_snapExtents, m_crowd->getFilter(0), &ref, snap)) && ref)
        {
            if (m_crowd->requestMoveTarget(handle.idx, ref, snap))
            {
                rec.lastGoal = goal;
                rec.hasGoal  = true;
            }
        }
    }

    const dtCrowdAgent* ag = m_crowd->getAgent(handle.idx);
    if (!ag || !ag->active) return CROWD_STEER_UNAVAILABLE;

    // OFFMESH means it is being animated along a link. INVALID means it fell off
    // the mesh entirely -- that is FAILED, not "wait a moment": there is nothing
    // coming, and the caller must not paper over it by walking at the goal.
    if (ag->state == DT_CROWDAGENT_STATE_INVALID) return CROWD_STEER_FAILED;
    if (ag->state != DT_CROWDAGENT_STATE_WALKING) return CROWD_STEER_PLANNING;

    switch (ag->targetState)
    {
    case DT_CROWDAGENT_TARGET_REQUESTING:
    case DT_CROWDAGENT_TARGET_WAITING_FOR_QUEUE:
    case DT_CROWDAGENT_TARGET_WAITING_FOR_PATH:
        // Transient, resolves in a frame or two.
        return CROWD_STEER_PLANNING;

    case DT_CROWDAGENT_TARGET_FAILED:
    case DT_CROWDAGENT_TARGET_NONE:
        // Detour says there is no route. Beelining here is what sent the
        // suicide bomber through walls.
        return CROWD_STEER_FAILED;

    default:
        break;   // VALID / VELOCITY
    }

    // A path exists but leads somewhere short of the request -- the goal is
    // behind geometry the navmesh does not connect to. The corridor is still
    // worth following (it gets as close as it can), so this is OK, not FAILED.
    // Noted because "partial" is easy to mistake for a failure.

    // nvel, not vel: the desired velocity AFTER obstacle avoidance and
    // separation, before dtCrowd clamps it by its own acceleration limit. The
    // behaviour applies its own limiting (the turn-rate slew), so taking the
    // accelerated value would limit it twice.
    outVelocity.set(ag->nvel[0], ag->nvel[1], ag->nvel[2]);
    return CROWD_STEER_OK;
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void CrowdManager::update(float dtMs)
{
    if (!m_crowd || !m_enabled) return;

    NavigationManager* nav = NavigationManager::Get();
    if (!nav || !nav->isNavMeshBuilt() || !nav->navQuery()) return;

    dtNavMeshQuery*      query  = nav->navQuery();
    const dtQueryFilter* filter = m_crowd->getFilter(0);

    // --- Sync every agent from the body that actually owns its position ------
    //
    // This is the half of the contract that makes the whole design work, and it
    // lives here rather than in followPath on purpose: the zombie's ATTACK state
    // moves the body WITHOUT calling followPath, so a write-back sited in the
    // path helper would leave that agent frozen while the body drifts. Pulling
    // from the transform covers every state and asks nothing of the behaviour.
    for (size_t i = 0; i < m_records.size(); ++i)
    {
        Record& rec = m_records[i];
        if (!rec.used) continue;

        // Behaviours unregister on death and on destroy, but a scene teardown
        // can take an entity out from under us without either running.
        if (!rec.entity.isValid() || !rec.entity.hasComponent<TransformComponent>())
        {
            m_crowd->removeAgent(static_cast<int>(i));
            rec.used    = false;
            rec.hasGoal = false;
            rec.entity  = anax::Entity();
            --m_agentCount;
            continue;
        }

        dtCrowdAgent* ag = m_crowd->getEditableAgent(static_cast<int>(i));
        if (!ag || !ag->active) continue;

        const vector3df p = rec.entity.getComponent<TransformComponent>().getPosition();

        const float dx = p.X - ag->npos[0];
        const float dz = p.Z - ag->npos[2];

        if (dx * dx + dz * dz > k_maxDivergence * k_maxDivergence)
        {
            // PhysX has walked the body somewhere the navmesh does not describe.
            // movePosition would clamp it back to the mesh edge every frame and
            // the two would never reconcile, so re-seat the corridor instead.
            const float wp[3] = { p.X, p.Y, p.Z };
            dtPolyRef   ref     = 0;
            float       snap[3] = { 0.0f, 0.0f, 0.0f };

            if (dtStatusSucceed(query->findNearestPoly(wp, k_snapExtents, filter, &ref, snap)) && ref)
            {
                ag->corridor.reset(ref, snap);
                dtVcopy(ag->npos, snap);
                dtVset(ag->vel,  0.0f, 0.0f, 0.0f);
                dtVset(ag->dvel, 0.0f, 0.0f, 0.0f);
                rec.hasGoal = false;   // the corridor is gone; re-request next steer()
            }
            continue;
        }

        const float np[3] = { p.X, p.Y, p.Z };
        ag->corridor.movePosition(np, query, filter);
        dtVcopy(ag->npos, ag->corridor.getPos());
    }

    // dtCrowd::update takes SECONDS. Every dt in this project is milliseconds.
    // It also documents dt as [Limit: > 0] -- a zero-length fixed step would
    // divide by it.
    if (dtMs > 0.0f)
        m_crowd->update(dtMs * 0.001f, nullptr);
}

// ---------------------------------------------------------------------------
// Debug view
// ---------------------------------------------------------------------------

void CrowdManager::drawDebug()
{
    if (!m_crowd || !m_enabled || !RenderManager::Get()) return;

    const irr::video::SColor colCorridor(255,  90, 200, 255);
    const irr::video::SColor colPartial (255, 255, 160,  60);
    const irr::video::SColor colVel     (255, 255,  80, 255);

    // Lifted clear of the floor, or the corridor z-fights the ground it is
    // drawn on and reads as a dotted line.
    const vector3df lift(0.0f, 0.15f, 0.0f);

    for (size_t i = 0; i < m_records.size(); ++i)
    {
        if (!m_records[i].used) continue;

        const dtCrowdAgent* ag = m_crowd->getAgent(static_cast<int>(i));
        if (!ag || !ag->active) continue;

        // cornerVerts is the agent's LOCAL straight path -- the few corners
        // ahead of it, not the whole corridor. That is deliberately what is
        // drawn: it is the part the steering actually reads.
        vector3df prev(ag->npos[0], ag->npos[1], ag->npos[2]);

        for (int c = 0; c < ag->ncorners; ++c)
        {
            const vector3df corner(ag->cornerVerts[c * 3 + 0],
                                   ag->cornerVerts[c * 3 + 1],
                                   ag->cornerVerts[c * 3 + 2]);

            RenderManager::Get()->renderLine3D(
                Line3D(irr::core::line3df(prev + lift, corner + lift), colCorridor));
            prev = corner;
        }

        // The leg out to the REQUESTED target, in a different colour. A partial
        // path -- one that gets as close as the navmesh allows and stops short
        // -- is otherwise indistinguishable from a complete one, and "partial"
        // is easy to mistake for a failure.
        if (ag->targetState == DT_CROWDAGENT_TARGET_VALID && ag->partial)
        {
            const vector3df tgt(ag->targetPos[0], ag->targetPos[1], ag->targetPos[2]);
            RenderManager::Get()->renderLine3D(
                Line3D(irr::core::line3df(prev + lift, tgt + lift), colPartial));
        }

        // nvel from the chest, scaled to half a second of travel. LENGTH IS THE
        // INFORMATION: this magnitude carries the arrival ramp and the
        // avoidance slowdown, and it used to be normalised away before the
        // behaviour ever saw it.
        const vector3df chest(ag->npos[0], ag->npos[1] + 1.0f, ag->npos[2]);
        const vector3df vel  (ag->nvel[0], ag->nvel[1], ag->nvel[2]);

        RenderManager::Get()->renderLine3D(
            Line3D(irr::core::line3df(chest, chest + vel * 0.5f), colVel));
    }
}

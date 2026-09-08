#pragma once

#include <cstdint>
#include <vector>

#include <anax/Entity.hpp>
#include <irrlicht.h>

#include "Engine/Navigation/DetourCrowd/Include/DetourCrowd.h"

// ---------------------------------------------------------------------------
// CrowdManager — dtCrowd as a STEERING ORACLE, not a position authority.
//
// The distinction is the whole design. dtCrowd normally owns its agents: it
// integrates velocity, resolves collisions and writes npos, and the game reads
// the position back out. That is exactly what this project must NOT do:
//
//   * CharacterBehavior::followPath advances the body along its FACING, not
//     along the steering vector, so a slow turner sweeps a real arc. That is
//     the entire read on the suicide bomber.
//   * Y comes from a PhysX downward raycast, not from the navmesh surface.
//   * With no navmesh baked, followPath falls back to a straight line. dtCrowd
//     has no such mode, and scenes that were never baked still have to work.
//
// So: the crowd computes, the behaviour moves. steer() hands back the
// avoidance-adjusted desired velocity (nvel) and nothing else; the behaviour
// feeds it through its own heading slew and writes the transform itself. This
// class then syncs the REAL position back into the agent every frame, before
// dtCrowd::update, so the crowd keeps simulating from where the body actually
// is.
//
// The crowd's own integration result is therefore computed and discarded every
// frame. That is deliberate and it is the price of the arc turn.
//
// LIFETIME. dtCrowd::init() stores the dtNavMesh* and allocates an internal
// dtNavMeshQuery against it, and it holds both EVEN WITH ZERO AGENTS
// REGISTERED. NavigationManager::destroyNavMesh() frees that mesh, and it runs
// at the top of buildNavMesh() *and* loadNavMesh() -- and importScene() calls
// loadNavMesh on every scene import, so play-in-editor invalidates it twice per
// play session. destroyNavMesh() therefore calls shutdown() unconditionally,
// and the crowd re-inits lazily on the next steer(). Skipping that does not
// fault at the transition: it faults on the first addAgent() in the NEXT scene.
// ---------------------------------------------------------------------------

// Agent handle. A bare int index is not safe across a shutdown -- after a
// re-init a stale index is not merely dangling, it is a VALID index pointing at
// somebody else's agent. The generation makes a stale handle detectable, and
// re-registration transparent.
struct CrowdHandle
{
    int      idx        = -1;
    uint32_t generation = 0;

    bool assigned() const { return idx >= 0; }
    void clear() { idx = -1; generation = 0; }
};

// Why steer() could not give an answer. The distinction between PLANNING and
// FAILED is load-bearing: PLANNING is a transient the caller may paper over by
// heading straight at the goal for a few frames, FAILED means Detour says there
// is NO route and a beeline would walk the NPC straight into whatever is in the
// way. Collapsing them into one bool had the suicide bomber charging through
// walls.
enum CROWD_STEER
{
    CROWD_STEER_UNAVAILABLE = 0,  // no crowd, disabled, or a stale handle
    CROWD_STEER_PLANNING,         // request accepted, no corridor yet
    CROWD_STEER_FAILED,           // no route exists, or the agent fell off the mesh
    CROWD_STEER_OK                // outVelocity is valid -- zero means "hold"
};

struct CrowdAgentConfig
{
    // MUST match the bake, not the behaviour's push radius. NavMeshConfig
    // erodes the walkable area by agentRadius (0.35); a fatter agent gets its
    // avoidance push driven off the navmesh edge in any corridor narrower than
    // twice this, then clamped back by corridor.movePosition, and it jitters
    // against the wall.
    float radius           = 0.35f;
    float height           = 1.8f;
    float maxSpeed         = 3.0f;

    // High on purpose. The behaviour's own turn-rate slew supplies the inertia;
    // letting the crowd also model acceleration makes the body lag its own
    // steering twice over.
    float maxAcceleration  = 24.0f;
    float separationWeight = 2.0f;
};

class CrowdManager
{
public:
    CrowdManager();
    ~CrowdManager();

    static CrowdManager* Get() { return s_Instance; }

    // Drop the crowd and everything in it. Called from
    // NavigationManager::destroyNavMesh() BEFORE the mesh is freed. Bumps the
    // generation, so every outstanding CrowdHandle becomes stale and
    // re-registers on next use.
    void shutdown();

    // Console bypass (ai_crowd). When off, steer() always fails and behaviours
    // fall through to the original waypoint walker -- the bisect switch for
    // "is this the crowd or the behaviour".
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    bool isReady() const { return m_crowd != nullptr; }
    uint32_t generation() const { return m_generation; }
    int  agentCount() const { return m_agentCount; }

    // Registers 'entity' as an agent. Fails (returns false) when there is no
    // navmesh, when the pool is full, or when the entity is not standing on the
    // navmesh -- all three are ordinary, and the caller falls back rather than
    // freezing.
    bool registerAgent(const anax::Entity& entity, const CrowdAgentConfig& cfg,
                       CrowdHandle& handle);
    void unregisterAgent(CrowdHandle& handle);
    bool isRegistered(const CrowdHandle& handle) const;

    // Cheap per-frame update of the one parameter behaviours actually vary.
    void setAgentSpeed(const CrowdHandle& handle, float maxSpeed);

    // Submit 'goal' and read back the avoidance-adjusted desired velocity.
    //
    // CROWD_STEER_OK with a ZERO velocity is a real answer -- "I have a path and
    // the answer is hold", i.e. boxed in or arrived. The caller must respect it
    // by standing still; treating it as failure and heading straight at the goal
    // is how you get the doorway shove this whole system exists to remove.
    //
    // See CROWD_STEER above for why PLANNING and FAILED must not be collapsed.
    CROWD_STEER steer(const CrowdHandle& handle, const irr::core::vector3df& goal,
                      irr::core::vector3df& outVelocity);

    // Syncs every agent from its entity's transform, then ticks dtCrowd once
    // for the whole world. dt is MILLISECONDS, as everywhere else in this
    // project -- dtCrowd::update takes SECONDS and the conversion happens here.
    void update(float dtMs);

    // Draws every agent's local corridor and its nvel. Reached from
    // AICoordinator::drawDebug at ai_debug 3.
    //
    // It earns its place because the two things it draws are otherwise
    // completely invisible: nvel's MAGNITUDE is what the behaviour's speed
    // fraction reads, and the corridor is what OPTIMIZE_TOPO re-plans. A change
    // to either shows up in the game only as "it feels different", which is not
    // something you can bisect.
    void drawDebug();

private:
    // Lazy: the navmesh may not exist yet, and it is replaced out from under us
    // on every scene import.
    bool ensureInit();

    struct Record
    {
        anax::Entity         entity;
        bool                 used     = false;
        bool                 hasGoal  = false;
        irr::core::vector3df lastGoal;
    };

    static CrowdManager* s_Instance;

    dtCrowd*            m_crowd      = nullptr;
    uint32_t            m_generation = 1;
    int                 m_agentCount = 0;
    bool                m_enabled    = true;
    std::vector<Record> m_records;
};

#pragma once

#include "Engine/Types.h"
#include "Engine/Navigation/CrowdManager.h"
#include "Game/AI/AICoordinator.h"
#include "Game/Behavior/EntityBehavior.h"

#include <irrlicht.h>
#include <string>
#include <vector>

struct MeshComponent;
struct TransformComponent;

// Shared floor for the behaviour-driven NPCs.
//
// This is a TOOLBOX, not a pipeline. It is deliberately NOT a rebuild of the
// retired NPCSystem, and the distinction is the whole point of the class:
//
//   NPCSystem::update() ran gravity -> dialog lock -> script dispatch ->
//   separation -> stairs -> wall-slide -> ledge-check over every NPC in a fixed
//   order with policy baked in. An individual NPC could not reorder it, skip a
//   stage, or opt out. The suicide bomber's arc-turn charge -- advancing along
//   its FACING rather than its steering vector, so a slow turner sweeps a real
//   curve -- is exactly the thing that pipeline made impossible.
//
// CharacterBehavior inverts the direction of the call. The base class NEVER
// calls into the subclass. There is no onIdle(), no onChase(), no wantsMovement
// flag the base reads back. Each NPC still writes its own update() top to
// bottom; it calls snapToGround() when it wants ground, and does not when it
// does not.
//
// RULE TO HOLD THE LINE ON: the moment this base class starts invoking a
// virtual that the subclass overrides to supply AI, NPCSystem has been rebuilt
// with extra steps. Helpers only.
//
// It lives beside EntityBehavior rather than in Classes/ on purpose -- it is
// infrastructure, not an instantiable behaviour, and BehaviorFactory must never
// be able to create one.
class CharacterBehavior : public EntityBehavior
{
public:
    IMPACT_SURFACE bloodType() const override { return IMPACT_FLESH; }

    // Releases the crowd agent and the squad slot. Overridden here rather than
    // left to each subclass because forgetting it leaks an agent slot out of a
    // fixed pool of 128 and leaves a phantom in the proximity grid.
    //
    // NOTE FOR ANY FUTURE SUBCLASS: neither shipped NPC overrides destroy(), so
    // this is reached today. A subclass that does override it MUST call
    // CharacterBehavior::destroy(entity).
    void destroy(anax::Entity& entity) override;

protected:
    // ---- Animation --------------------------------------------------------

    // Carries the bomber's re-issue guard: re-issuing setFrameLoop every frame
    // pins a looping clip to its first frame forever, which looks exactly like
    // "the animation is not playing at all". Safe to call every frame.
    void playAnim(MeshComponent& mc, const std::string& name);

    // ---- Perception -------------------------------------------------------

    // Nearest living entity this one is hostile to, or _entity_null_value.
    //
    // Returns an ID, NOT a pointer. world()->getEntities() hands back a
    // const std::vector<Entity>&, and a pointer into it dangles the moment
    // anything spawns and the vector reallocates -- anax defers entity KILLS to
    // end of frame, it does not defer adds. Entity is a cheap handle anyway.
    //
    // Filters on isHostile(), NOT on hasComponent<NPCComponent>(): the player
    // carries no NPCComponent, so an NPC-tag filter would scan straight past the
    // one target that matters. The scan is therefore over all entities, but
    // factionOf() returns NEUTRAL for anything with no opinion and the table
    // rejects it in one array lookup.
    //
    // The full scan is throttled to m_repathInterval and the result cached; a
    // cached target that dies, despawns or turns non-hostile is dropped
    // immediately rather than being chased out the rest of the interval.
    //
    // Call it EVERY frame and pass the frame's dt -- the throttling is internal.
    entityid findTarget(anax::Entity& self, float dt);

    // The entity findTarget() last resolved, for the frames in between scans.
    //
    // Cached as a handle rather than re-looked-up by ID, which is what keeps the
    // O(entities) cost on the throttled path only. It is safe to hold across
    // frames: anax::Entity is a {world, index+counter} pair and EntityIdPool
    // bumps the counter on destruction, so a stale handle reports isValid() ==
    // false instead of dangling. (A POINTER into getEntities() would dangle --
    // that vector reallocates whenever anything spawns, and anax defers entity
    // KILLS to end of frame but does not defer adds.)
    const anax::Entity& target() const { return m_target; }

    // Line of sight from this character's eye line to a world point.
    //
    // A THIN FORWARD to AICoordinator::hasLineOfSight. It used to have its own
    // body and that body was broken -- see the .cpp for what it got wrong and
    // why it never showed up.
    bool canSee(anax::Entity& self, const irr::core::vector3df& p);

    // ---- What the character knows about its target ------------------------
    //
    // Maintained by findTarget() on the sense pass. THE BASE DOES NOT DECIDE
    // WHAT TO DO WITH THEM -- there is no SEARCH state down here, no give-up
    // timer, no virtual for the subclass to fill in. Each behaviour reads these
    // and writes its own state machine, which is the whole toolbox rule.
    //
    // The zombie uses them to walk to where you were and look around; the
    // cultist uses them to keep charging your last position without the
    // wallhack of tracking your live one. Those are opposite policies over the
    // same three fields, which is exactly why the policy is not in here.

    // Where the target was when it was last actually seen. Only meaningful
    // while hasLastKnown() -- it is NOT cleared to zero, it is left stale.
    const irr::core::vector3df& lastKnownPos() const { return m_lastKnownPos; }
    bool  hasLastKnown()  const { return m_hasLastKnown; }

    // Milliseconds since LOS to the current target last held. Zero while it is
    // in view. Meaningless with no target.
    float timeSinceSeen() const { return m_timeSinceSeen; }

    // True while the target is in view right now.
    //
    // TWO sense intervals, not one. m_timeSinceSeen accrues every frame and is
    // only zeroed on the throttled pass, so it sits at just under one full
    // interval immediately before each pass -- a one-interval threshold would
    // flicker false once every 250ms on a target in plain sight, and every
    // caller of this is a state transition.
    bool  hasFreshTarget() const
    {
        return m_targetId != _entity_null_value &&
               m_timeSinceSeen <= m_senseInterval * 2.0f;
    }

    // ---- Locomotion -------------------------------------------------------

    // Pin to the floor. Runs on corpses too, which is why it is separate from
    // the movement helpers.
    //
    // 'dt' (MILLISECONDS) rate-limits the CLIMB the same way followPath does,
    // so a stair tread lifts the body over a couple of frames instead of
    // popping it. A drop larger than the fall threshold still snaps, so falling
    // reads as falling. dt <= 0 restores the old unconditional hard write --
    // that is the default so a caller that has no dt to hand cannot silently
    // get a body that never reaches the floor.
    //
    // IT MUST GET THE SAME TREATMENT AS followPath's ground pin: both NPCs call
    // this every frame BEFORE followPath, so a hard write here would undo
    // followPath's smoothing on the very next frame and the whole thing would
    // look like it did nothing.
    void snapToGround(TransformComponent& tc, float dt = 0.0f);

    // XZ push-away from other living behaviour entities within
    // m_separationRadius.
    irr::core::vector3df calcSeparation(const irr::core::vector3df& pos, anax::Entity& self);

    // Walk toward 'goal'. Returns true if it moved.
    //
    // Steering comes from dtCrowd when there is a navmesh and the crowd is
    // enabled -- RVO obstacle avoidance, neighbour separation and corridor
    // re-planning, all computed against a grid shared with every other agent.
    // The crowd supplies a DIRECTION ONLY; everything below that (the heading
    // slew, the advance-along-facing arc, the speed falloff, the PhysX ground
    // pin) is unchanged and still owned here. See CrowdManager.h.
    //
    // With no navmesh, no crowd, or an agent that could not be registered, it
    // falls back to the original waypoint walker over NavigationManager::
    // findPath -- and with no navmesh at all, to a straight line at the goal.
    // Scenes that were never baked keep working exactly as before.
    //
    // m_turnRate == 0 short-circuits the slew, so a snap-to-facing NPC is
    // expressible without a second code path: the heading jumps to the steering
    // direction, the speed scale collapses to 1, and the motion is identical to
    // moving straight along the steering vector. BEWARE what that means once
    // the crowd is steering: at turn rate 0 the body moves exactly along the
    // crowd's vector, so Detour really is driving the NPC and the avoidance
    // vector's frame-to-frame swings show up raw. Give a crowd-steered NPC a
    // finite turn rate.
    bool followPath(anax::Entity& e, const irr::core::vector3df& goal, float speed, float dt);

    // Where the squad coordinator wants this NPC to go. Safe and cheap to call
    // unconditionally -- with m_squadMode == SQUAD_NONE it hands back
    // 'targetPos' and hasToken = true, which is what every behaviour did before
    // squads existed.
    SquadOrder requestSquadOrder(anax::Entity& self, entityid targetId,
                                 const irr::core::vector3df& targetPos);

    // Snap the body to face an XZ direction, applying m_yawOffset and keeping
    // m_heading in sync so a following followPath() does not spin back.
    void faceTowards(TransformComponent& tc, const irr::core::vector3df& dir);

    // Drop the current route AND the accumulated momentum.
    //
    // CALL THIS INSTEAD OF A BARE m_path.clear() ON ANY STATE CHANGE. Since
    // m_currentSpeed became real state (the accel/decel ramp), a behaviour that
    // only clears the path carries its old speed into the new state -- a zombie
    // leaving ATTACK starts its chase already at a sprint, which is the exact
    // instantaneous locomotion the ramp exists to remove.
    void resetMovement();

    // ---- Salvaged from NPCSystem ------------------------------------------
    //
    // slideAlongWall is NOW LIVE -- followPath calls it on every move. stepUp and
    // hasGroundAhead are still opt-in and uncalled.
    //
    // Wiring the wall slide up stopped being optional once NPCs were seen
    // walking through brush walls: the navmesh is the only thing that has ever
    // kept a body out of geometry, and it goes stale the moment a brush moves
    // (BrushManager.cpp:236). See the call site for the full reasoning.
    //
    // THEIR CONSTANTS ARE NOT PORTABLE AS-IS. The stair window and wall probe
    // were tuned against NPCSystem's `moveSpeedScale * dt` convention, which was
    // units PER FRAME; behaviours work in units per SECOND. Re-tune on first
    // use, do not assume they transfer.

    // Height to add this frame to climb a step ahead, or 0. Casts forward-then-
    // down from head height.
    float stepUp(const irr::core::vector3df& pos, const irr::core::vector3df& dir);

    // 'dir' with any component into a wall ahead removed, so the body slides
    // along the surface instead of grinding into it. Returns 'dir' unchanged
    // for anything whose normal says it is walkable floor (a ramp is not a
    // wall), and does NOT renormalise -- a head-on approach collapses to near
    // zero, which is what actually stops the NPC.
    irr::core::vector3df slideAlongWall(const irr::core::vector3df& pos, irr::core::vector3df dir);

    // False if the step ahead has no floor under it -- a ledge check.
    bool hasGroundAhead(const irr::core::vector3df& pos, const irr::core::vector3df& dir, float dist);

    // ---- Death ------------------------------------------------------------

    // Returns true if the caller should stop (the character is dead).
    //
    // Reads ONLY drc.health and drc.gibbed, both plain non-consuming state.
    //
    // IT MUST NEVER CALL didReceiveDamage() OR didReceiveExplosive(). Those are
    // CONSUMING reads -- DamageReceiverComponent.h:160,173 clear the flag as
    // they return it, so whichever caller runs second gets false. The zombie's
    // hit-reaction sounds read didReceiveDamage(); the bomber's fuse arming
    // reads didReceiveExplosive(). A base class that touched either would
    // silently disable one of those, and the symptom would be "hit sounds
    // stopped working sometimes" with nothing logged and no crash.
    //
    // So hit reactions and fuse arming stay entirely in the subclass. If
    // centralising them is ever wanted, the flag has to be latched once per
    // frame into a non-consuming field first.
    bool handleDeath(anax::Entity& e, const std::string& dieClip, const char* dieSound = nullptr);

    // ---- Properties -------------------------------------------------------

    // Movement/animation tunables shared by every character. A subclass calls
    // this from getProperties() and appends its own rows.
    //
    // getProperties() does NOT compose -- it is a plain virtual returning a
    // vector by value, and applyPropertiesToBehavior matches entries by exact
    // name string. A subclass that forgets to append these does not merely hide
    // the rows: EditorInterface_Components rebuilds the ENTIRE serialized string
    // from getProperties() on every edit, so editing any other property would
    // erase the base tunables from the save data.
    //
    // "Seperation Radius" KEEPS ITS MISSPELLING. It is spelled that way in both
    // shipped NPCs and it is a PERSISTED KEY -- applyPropertiesToBehavior looks
    // up name + "=" in the serialized string. Correcting it would make every
    // saved .ent silently fall back to the default with no error.
    std::vector<BehaviorProperty> baseProperties();

    // ---- Crowd ------------------------------------------------------------

    // Registers the crowd agent on first use and keeps its speed in step.
    // Returns false when the crowd cannot steer this NPC, for any reason.
    //
    // REGISTRATION IS LAZY AND LIVES HERE, reached only from followPath, which
    // only runs in game mode. It must NEVER move into init(): updateEntityQueues
    // runs outside the game-mode gate (WorldManager.cpp:95), so
    // BehaviorSystem::onEntityAdded fires in the editor and init() with it.
    // Registering there would have the editor carrying live crowd agents around
    // while a designer edits and re-bakes, for no benefit at all.
    bool ensureCrowdAgent(anax::Entity& e, float speed);

    // Safe to call repeatedly and on an unregistered NPC.
    void releaseCrowdAgent();

    CrowdHandle m_crowdHandle;

    // Registration failed for this NPC -- almost always "not standing on the
    // navmesh", e.g. on a mesh nobody flagged Cook Navigation. Latched so the
    // attempt (and its log line) does not repeat every frame, and keyed to the
    // crowd generation so a re-bake gives it another chance.
    bool     m_crowdDenied    = false;
    uint32_t m_crowdDeniedGen = 0;

    // Time spent so far heading straight at the goal while the crowd plans.
    // Bounded, and reset on any definite answer -- see followPath.
    float    m_crowdStopgapMs = 0.0f;

    // How much of its top speed dtCrowd wants this agent to be doing, 0..1.
    //
    // nvel's MAGNITUDE carries the crowd's arrival deceleration and its
    // avoidance slowdown; normalising the vector to a pure direction throws
    // both away and leaves every NPC binary -- full speed, or the exact-zero
    // "hold". This is that magnitude, recovered against the speed followPath
    // pushed into the agent, and it is what lets one agent ease off to let
    // another cross instead of shouldering through.
    //
    // The waypoint-walker fallback sets it to 1 so it is unaffected.
    float    m_crowdSpeedFrac = 1.0f;

    // ---- Tunables ---------------------------------------------------------
    float m_separationRadius = 1.5f;

    // Throttle for the WAYPOINT WALKER's repath only. It used to throttle the
    // target scan as well, which made acquisition up to 1.5s late -- an NPC
    // could stand facing you for a second and a half before noticing. The scan
    // is on m_senseInterval now; these two have nothing to do with each other
    // and sharing one number was an accident.
    float m_repathInterval   = 1500.0f;   // ms

    // How often findTarget() runs its O(entities) scan and its LOS refresh.
    // Four times a second: fast enough that acquisition is not visible as a
    // delay, slow enough that the raycasts stay off the per-frame budget.
    float m_senseInterval    = 250.0f;    // ms

    // Total field of view, in degrees, for ACQUIRING a new target. Retention is
    // not gated on it -- once something is your target you may turn away from
    // it without forgetting it exists.
    //
    // 360 means omnidirectional, which is a legitimate design choice for some
    // NPCs (the zombie sets exactly that) rather than a disabled feature.
    float m_visionCone       = 120.0f;

    float m_arrivalRadius    = 0.5f;
    float m_turnRate         = 200.0f;    // deg/sec; 0 = snap instantly

    // Ground-speed ramp, units/sec^2. Without these an NPC is at full sprint on
    // frame one and stops dead, which is most of what "movement doesn't feel
    // natural" actually is.
    //
    // Braking harder than accelerating is deliberate: it is what keeps a
    // charging bomber from overshooting its own trigger radius.
    float m_accel            = 8.0f;
    float m_decel            = 16.0f;

    // The glTF importer mirrors Z (S = diag(1,1,-1,1), see GltfImport.cpp), so a
    // Mixamo character authored facing +Z arrives facing -Z. Every glTF NPC wants
    // the half turn; a .b3d cast wants 0.
    //
    // It has to be +180 and NOT a negated yaw -- negating mirrors the angle about
    // Z rather than turning it around, which looks correct while running along X
    // and stays backwards along Z.
    float m_yawOffset        = 180.0f;

    // Crowd agent body radius. Defaults to NavMeshConfig::agentRadius, which is
    // what the navmesh is ERODED BY at bake time -- a fatter agent has its
    // avoidance push driven off the mesh edge in tight corridors and jitters
    // against the clamp. This is NOT m_separationRadius, which is a push
    // distance for the non-crowd code and is four times larger.
    float m_agentRadius      = 0.35f;

    // How hard the crowd pushes agents apart. Independent of m_separationRadius.
    float m_separationWeight = 2.0f;

    // Squad coordination. SQUAD_NONE reproduces pre-coordinator behaviour
    // exactly, and is the right default for anything that is not a pack animal.
    int   m_squadMode        = SQUAD_NONE;
    int   m_squadTokens      = 3;
    float m_squadRing        = 4.0f;

    // ---- Shared state -----------------------------------------------------
    std::vector<irr::core::vector3df> m_path;
    int   m_pathIndex   = 0;
    float m_repathTimer = 0.0f;

    // Travel direction in degrees, WITHOUT m_yawOffset -- the node rotation is
    // this plus the offset. Kept as state because the body slews toward the
    // steering direction instead of snapping to it.
    float m_heading     = 0.0f;
    bool  m_headingInit = false;
    bool  m_isDead      = false;

    // Actual ground speed, units/sec, ramped toward the wanted speed by
    // m_accel / m_decel. Also drives the animation playback rate, so the feet
    // match the ground. Reset by resetMovement() and by handleDeath().
    float m_currentSpeed = 0.0f;

private:
    anax::Entity m_target;
    entityid     m_targetId    = _entity_null_value;
    float        m_targetTimer = 1.0e9f;   // forces a scan on the first call

    // See the accessors above. Written only by findTarget().
    irr::core::vector3df m_lastKnownPos;
    bool                 m_hasLastKnown  = false;
    float                m_timeSinceSeen = 0.0f;   // ms
};

#include "Game/Behavior/CharacterBehavior.h"

#include "Engine/Navigation/CrowdManager.h"
#include "Engine/Navigation/NavigationManager.h"
#include "Engine/Physics/PhysicsManager.h"
#include "Engine/Renderer/RenderManager.h"
#include "Engine/World/WorldManager.h"
#include "Engine/World/Components/DescriptorComponent.h"
#include "Engine/World/Components/MeshComponent.h"
#include "Engine/World/Components/SoundComponent.h"
#include "Engine/World/Components/TransformComponent.h"
#include "Game/Components/BehaviorComponent.h"
#include "Game/Components/DamageReceiverComponent.h"
#include "Game/Components/Faction.h"
#include "Utility/Utility.h"

#include <spdlog/spdlog.h>
#include <cmath>

// No NOMINMAX in this project, so the Windows macros are live by the time
// <algorithm> would be useful. Kill them before std::max is named.
#undef max
#undef min

#include <algorithm>

using namespace irr::core;

namespace
{
    // How long an NPC may head straight at its goal while dtCrowd is still
    // planning. Long enough to cover a normal re-plan (a frame or two), short
    // enough that a pathological one cannot walk it into a wall.
    const float k_crowdStopgapLimitMs = 500.0f;

    // Forward wall probe distance. Roughly a third of a second of lookahead at
    // the bomber's charge speed, so it begins sliding before it arrives rather
    // than after it is already inside.
    const float k_wallProbeDist = 1.5f;

    // --- Ground probe -------------------------------------------------------
    //
    // Starts ABOVE the feet so a body that has sunk slightly still finds the
    // floor, and reaches well BELOW them so one that has been displaced upward
    // can find its way back down.
    //
    // THE REACH USED TO BE 2.0 FROM 1.0 UP -- one unit of downward travel --
    // and that was a trap with no error and no way back. Anything that ended up
    // more than a unit above the floor could no longer see it: the probe
    // reported no hit, snapToGround wrote nothing, and the body hung there
    // permanently. It is exactly what made corpses float once a player had
    // walked over them, and it would have kept them floating even after the
    // cause was fixed. 4.0 from 1.0 up clears a character's full height with
    // room to spare.
    const float k_groundProbeUp   = 1.0f;
    const float k_groundProbeDist = 4.0f;

    // Groups an NPC's world probes must never see.
    //
    // A CHARACTER IS NOT WORLD SURFACE. The player's controller capsule is
    // tagged RHG_DYNAMIC so weapons hit it, which also put it in the path of
    // every unfiltered probe in here: the downward one read the top of the
    // capsule as ground and stood the NPC on the player's head, and the forward
    // one read its side as a wall and had slideAlongWall refuse to close the
    // last stride into melee range.
    const irr::u32 k_probeExclude = RHG_CHARACTER;

    // --- Ground smoothing ---------------------------------------------------

    // Max rate the feet may be LIFTED, units/sec. An 0.2u stair tread takes
    // ~50ms to climb, which reads as a step instead of a pop. Anything much
    // faster and the smoothing stops being visible; much slower and the body
    // sinks into a ramp it is walking up.
    const float k_climbRate = 4.0f;

    // A drop larger than this is a FALL, not a tread, and snaps. Easing a fall
    // would have bodies float down off ledges.
    const float k_fallSnapDist = 0.5f;

    float smoothGroundY(float fromY, float toY, float dtMs)
    {
        const float dY = toY - fromY;
        if (dY < -k_fallSnapDist) return toY;

        const float maxStep = k_climbRate * (dtMs * 0.001f);
        if (dY >  maxStep) return fromY + maxStep;
        if (dY < -maxStep) return fromY - maxStep;
        return toY;
    }

    // --- Perception ---------------------------------------------------------

    // The incumbent target's squared distance is scaled by this before a
    // challenger is compared against it, so a challenger must be ~22% closer to
    // steal the target. Without it two roughly equidistant hostiles make an NPC
    // flip-flop between them indefinitely.
    const float k_targetHysteresis = 0.6f;

    // How many candidates findTarget() will spend a raycast on per sense pass.
    //
    // THE SCAN IS O(entities) AND USED TO BE PURE ARITHMETIC. A raycast per
    // candidate is a different order of expense -- raycastWorldPosition walks
    // every node from the scene root. So the distance/FOV winner is computed
    // first and only IT is tested, falling through to the runner-up if it
    // fails. Two, not one, because "nearest is behind a pillar" is the ordinary
    // case, not the exotic one.
    const int k_maxLosTests = 2;

    // Playback-rate clamp for the walk cycle. The floor exists so a near-pivot
    // -- where followPath's cos(facingError) scale costs up to 75% of the
    // speed -- does not freeze the legs mid-stride; the ceiling stops a
    // downhill or a speed buff from turning the cycle into a scribble.
    const float k_animRateMin = 0.35f;
    const float k_animRateMax = 1.25f;

    // Below this fraction of nominal speed the body is not walking at all --
    // it is pinned against a wall, or holding for the crowd. THE FLOOR ABOVE
    // MUST FADE OUT HERE. A floor that applied all the way down to zero is what
    // ran the legs under a stationary body; a floor that simply did not apply
    // would freeze them mid-stride instead, which is no better. So the rate
    // ramps 0 -> k_animRateMin across this band and the two meet continuously.
    const float k_animStopFrac = 0.05f;

    // Ramp a speed down toward zero at 'decel'.
    //
    // EVERY early return out of followPath has to do this. The ramp that runs
    // at the bottom of the function is only reached on a frame the NPC
    // actually moves, so a return -- arrival, a crowd "hold", an exhausted
    // waypoint list -- would otherwise freeze m_currentSpeed at whatever it was
    // and resume from there. In a doorway that is precisely the failure the
    // ramp exists to remove: the agent stops dead when told to wait, then
    // resumes at a full sprint the instant it is let through.
    void brake(float& speed, float decel, float dtMs)
    {
        const float step = decel * (dtMs * 0.001f);
        speed = (speed > step) ? (speed - step) : 0.0f;
    }
}

// ---------------------------------------------------------------------------
// Animation
// ---------------------------------------------------------------------------

void CharacterBehavior::playAnim(MeshComponent& mc, const std::string& name)
{
    if (!mc.node) return;

    // Don't restart a clip that is already running. Subclasses call this every
    // frame they are in a state, and re-issuing setFrameLoop each frame pins a
    // looping animation to its first frame forever -- it would look like the
    // animation is not playing at all.
    if (mc.lastPlayedAnimation.name == name) return;

    if (const sAnimationData* a = mc.findAnimation(name))
    {
        mc.node->setLoopMode(a->loop);
        mc.node->setFrameLoop(a->frames.X, a->frames.Y);
        mc.node->setAnimationSpeed(static_cast<irr::f32>(mc.fps));
        mc.lastPlayedAnimation = *a;
    }
    else
    {
        spdlog::warn("CharacterBehavior: mesh has no animation '{}'", name);
    }
}

// ---------------------------------------------------------------------------
// Perception
// ---------------------------------------------------------------------------

entityid CharacterBehavior::findTarget(anax::Entity& self, float dt)
{
    m_targetTimer += dt;

    // Accrues every frame; zeroed only by an actual LOS hit on the sense pass
    // below. Meaningless with no target, so it only runs while there is one.
    if (m_targetId != _entity_null_value)
        m_timeSinceSeen += dt;

    // Validate the cached pick first, every frame and for free. A target that
    // died, despawned or turned non-hostile (pacified mid-fight) drops on the
    // spot instead of being chased for up to a full interval.
    if (m_targetId != _entity_null_value)
    {
        const bool stillGood =
            m_target.isValid() &&
            m_target.hasComponent<DescriptorComponent>() &&
            m_target.getComponent<DescriptorComponent>().isAlive &&
            m_target.hasComponent<TransformComponent>() &&
            isHostile(self, m_target);

        if (!stillGood)
        {
            m_target      = anax::Entity();
            m_targetId    = _entity_null_value;
            m_targetTimer = 1.0e9f;   // force the scan below

            // A target that DIED is not a target that was lost. There is
            // nothing left to go and investigate, so the last-known point goes
            // with it -- otherwise a zombie would trudge over to look at a
            // corpse it made itself.
            m_hasLastKnown  = false;
            m_timeSinceSeen = 0.0f;
        }
    }

    // Throttle on the TIMER, not on "do we have a target". Gating the rescan on
    // an empty result instead would make the no-hostiles-in-the-level case scan
    // every entity every frame for every NPC -- the worst case, not the cheap
    // one.
    //
    // m_senseInterval, NOT m_repathInterval. Those used to be the same number
    // and it made acquisition up to 1.5s late: an NPC could stand facing you
    // for a second and a half before reacting. The repath interval still
    // throttles the waypoint walker, which is a completely different concern.
    if (m_targetTimer < m_senseInterval)
        return m_targetId;

    m_targetTimer = 0.0f;

    // The one O(entities) scan in this class, which is why it is throttled.
    //
    // NOTE: calcSeparation has the same shape and is called EVERY frame by both
    // NPCs. NPCSystem used to build a flat position cache once per frame
    // precisely to avoid that. Not worth doing at ~20 NPCs; this comment is
    // here so the fix is easy to find if it ever profiles.
    vector3df myPos(0.0f, 0.0f, 0.0f);
    vector3df facing(0.0f, 0.0f, 1.0f);

    if (self.hasComponent<TransformComponent>())
    {
        TransformComponent& stc = self.getComponent<TransformComponent>();
        myPos = stc.getPosition();

        // The node rotation is heading + m_yawOffset (see faceTowards), so the
        // offset has to come back off to get the direction the body is actually
        // looking. Reading m_heading instead would be wrong before the first
        // followPath, when it has not been initialised from the placement yet.
        const float facingRad = deg2rad(stc.getRotation().Y - m_yawOffset);
        facing.set(sinf(facingRad), 0.0f, cosf(facingRad));
    }

    // --- Retention: refresh what we know about the target we already have ----
    //
    // This is the ONLY thing that keeps m_lastKnownPos honest, and it runs
    // before the acquisition scan so a target that is still visible re-stamps
    // its position even on a pass where a challenger is being considered.
    //
    // Retention is deliberately NOT gated on FOV or LOS. Losing sight of
    // something does not make you forget it exists; what to DO about it is the
    // behaviour's decision, made from timeSinceSeen().
    if (m_targetId != _entity_null_value && m_target.isValid() &&
        m_target.hasComponent<TransformComponent>())
    {
        const vector3df tp = m_target.getComponent<TransformComponent>().getPosition();
        if (canSee(self, tp))
        {
            m_lastKnownPos  = tp;
            m_hasLastKnown  = true;
            m_timeSinceSeen = 0.0f;
        }
    }

    // --- Acquisition ---------------------------------------------------------
    //
    // Ranked first, gated second. FOV is arithmetic so it is applied inline;
    // LOS is a raycast so at most k_maxLosTests of them are spent, on the
    // nearest survivors only.
    //
    // 360 in the cone gives cos(180) == -1, so the dot test passes for
    // everything -- an omnidirectional NPC costs nothing extra and needs no
    // second code path.
    const float coneCos = std::cos(deg2rad(std::min(360.0f, m_visionCone) * 0.5f));

    // The incumbent is ranked but never GATED -- FOV and LOS are acquisition
    // tests only, and re-testing them on the target you already have is what
    // would make an NPC lose you the instant you stepped behind something. It
    // also gets the hysteresis discount, so a challenger must be meaningfully
    // closer to take the slot.
    float incumbentRank = 1.0e18f;
    bool  haveIncumbent = false;

    anax::Entity topEntity[k_maxLosTests];
    entityid     topId  [k_maxLosTests];
    float        topRank[k_maxLosTests];
    int          topCount = 0;

    for (auto& other : WorldManager::Get()->world()->getEntities())
    {
        if (!other.isValid() || other == self)          continue;
        if (!other.hasComponent<DescriptorComponent>()) continue;
        if (!other.hasComponent<TransformComponent>())  continue;

        const DescriptorComponent& desc = other.getComponent<DescriptorComponent>();
        if (!desc.isAlive) continue;

        if (!isHostile(self, other)) continue;

        vector3df d = other.getComponent<TransformComponent>().getPosition() - myPos;
        const float d2 = d.getLengthSQ();

        if (m_targetId != _entity_null_value && desc.id == m_targetId)
        {
            incumbentRank = d2 * k_targetHysteresis;
            haveIncumbent = true;
            continue;
        }

        // Beaten by the target we already hold -- not worth ranking. This is
        // only an OPTIMISATION and cannot be the whole test: the incumbent may
        // not have been reached yet in this scan, in which case haveIncumbent
        // is still false here. The authoritative comparison is on the LOS loop
        // below, which is order-independent.
        if (haveIncumbent && d2 >= incumbentRank) continue;

        // Facing cone. Flattened to XZ: these NPCs do not look up or down, and
        // including Y would have a target on a balcony fall out of view for no
        // reason the player can see.
        d.Y = 0.0f;
        const float flatLen = d.getLength();
        if (flatLen > 0.001f && coneCos > -0.9999f)
        {
            if ((d / flatLen).dotProduct(facing) < coneCos) continue;
        }

        // Insertion sort into the top-k by distance. k is 2, so a loop is not
        // worth writing -- and a sort over the whole candidate set would mean
        // an allocation per NPC per pass for no gain.
        int slot = topCount;
        while (slot > 0 && d2 < topRank[slot - 1]) --slot;
        if (slot >= k_maxLosTests) continue;

        for (int i = (topCount < k_maxLosTests ? topCount : k_maxLosTests - 1); i > slot; --i)
        {
            topEntity[i] = topEntity[i - 1];
            topId    [i] = topId    [i - 1];
            topRank  [i] = topRank  [i - 1];
        }

        topEntity[slot] = other;
        topId    [slot] = desc.id;
        topRank  [slot] = d2;
        if (topCount < k_maxLosTests) ++topCount;
    }

    // Only now, and at most twice, does this cost a raycast.
    for (int i = 0; i < topCount; ++i)
    {
        // THE HYSTERESIS TEST LIVES HERE, not only in the scan loop. Entity
        // order is arbitrary, so a challenger evaluated BEFORE the incumbent
        // was reached got into the top-k without ever being compared against
        // it -- and would then steal the target off a nearer incumbent purely
        // because of where it happened to sit in the list. That is the
        // flip-flopping this discount exists to stop, reintroduced through the
        // back door.
        if (topRank[i] >= incumbentRank) break;   // sorted, so the rest are worse

        const vector3df tp = topEntity[i].getComponent<TransformComponent>().getPosition();
        if (!canSee(self, tp)) continue;

        m_target        = topEntity[i];
        m_targetId      = topId[i];
        m_lastKnownPos  = tp;      // we are looking straight at it
        m_hasLastKnown  = true;
        m_timeSinceSeen = 0.0f;
        return m_targetId;
    }

    // Nothing new was acquirable. Keep what we had -- the incumbent survives a
    // pass it was not beaten on, sight line or not.
    if (haveIncumbent) return m_targetId;

    m_target        = anax::Entity();
    m_targetId      = _entity_null_value;
    m_hasLastKnown  = false;
    m_timeSinceSeen = 0.0f;
    return m_targetId;
}

bool CharacterBehavior::canSee(anax::Entity& self, const vector3df& p)
{
    // HERE LIES THE OLD BODY OF THIS FUNCTION. It cast from this character's
    // own eye point, and raycastWorldPosition's self-hit skip is ET_PLAYER only
    // (RenderManager.cpp) -- so the ray started inside this NPC's own
    // bounding-box triangle selector and came straight back as a hit on itself.
    // It returned false at every distance, for every NPC, always.
    //
    // Nothing called it, so nothing was visibly broken; but a silently-false
    // LOS test looks EXACTLY like correct cautious behaviour, and wiring the
    // perception work to it would have made every NPC permanently blind with no
    // error anywhere. AICoordinator::hasLineOfSight is the version that has the
    // origin-offset fix (and had to grow it for the same bug), so there is now
    // ONE implementation and this is a forward to it.
    return AICoordinator::hasLineOfSight(self, p);
}

// ---------------------------------------------------------------------------
// Locomotion
// ---------------------------------------------------------------------------

void CharacterBehavior::snapToGround(TransformComponent& tc, float dt)
{
    const vector3df p = tc.getPosition();

    auto ray = PhysicsManager::Get()->raycast(
        p + vector3df(0.0f, k_groundProbeUp, 0.0f),
        vector3df(0.0f, -1.0f, 0.0f), k_groundProbeDist,
        RHG_ANY_HIT, k_probeExclude);

    if (!ray.hit) return;

    const float targetY = ray.data.getAnyHit(0).position.y;

    // Both NPCs call this EVERY FRAME, BEFORE followPath. A hard write here
    // would undo followPath's own climb limiting on the very next frame and the
    // smoothing would look like it had never been implemented -- which is why
    // this takes a dt at all.
    tc.setPosition(vector3df(
        p.X,
        (dt > 0.0f) ? smoothGroundY(p.Y, targetY, dt) : targetY,
        p.Z));
}

vector3df CharacterBehavior::calcSeparation(const vector3df& pos, anax::Entity& self)
{
    vector3df sep(0.0f, 0.0f, 0.0f);

    for (auto& other : WorldManager::Get()->world()->getEntities())
    {
        if (!other.isValid() || other == self)         continue;
        if (!other.hasComponent<BehaviorComponent>())  continue;
        if (!other.hasComponent<TransformComponent>()) continue;

        // Corpses can overlap -- only push away from living NPCs
        if (other.hasComponent<DescriptorComponent>() &&
            !other.getComponent<DescriptorComponent>().isAlive)
            continue;

        vector3df diff = pos - other.getComponent<TransformComponent>().getPosition();
        diff.Y = 0.0f;
        const float d = diff.getLength();
        if (d < m_separationRadius && d > 0.001f)
        {
            diff.normalize();
            sep += diff * ((m_separationRadius - d) / m_separationRadius);
        }
    }

    return sep;
}

void CharacterBehavior::faceTowards(TransformComponent& tc, const vector3df& dir)
{
    vector3df flat(dir.X, 0.0f, dir.Z);
    if (flat.getLength() <= 0.001f) return;

    m_heading     = rad2deg(atan2f(flat.X, flat.Z));
    m_headingInit = true;

    tc.setRotation(vector3df(0.0f, m_heading + m_yawOffset, 0.0f));
}

void CharacterBehavior::applyAnimSpeed(anax::Entity& e, float achieved, float nominal)
{
    if (nominal <= 0.001f) return;
    if (!e.hasComponent<MeshComponent>()) return;

    MeshComponent& mc = e.getComponent<MeshComponent>();
    if (!mc.node) return;

    const float frac = achieved / nominal;

    const float rate = (frac <= k_animStopFrac)
        ? frac * (k_animRateMin / k_animStopFrac)          // fades 0 -> floor
        : std::max(k_animRateMin, std::min(k_animRateMax, frac));

    mc.node->setAnimationSpeed(static_cast<irr::f32>(mc.fps) * rate);
}

void CharacterBehavior::resetMovement()
{
    m_path.clear();
    m_pathIndex    = 0;
    m_currentSpeed = 0.0f;
}

bool CharacterBehavior::followPath(anax::Entity& e, const vector3df& goal, float speed, float dt)
{
    if (!e.hasComponent<TransformComponent>()) return false;

    auto& tc = e.getComponent<TransformComponent>();
    const vector3df myPos = tc.getPosition();

    vector3df steer(0.0f, 0.0f, 0.0f);
    bool      haveSteer = false;

    // --- Crowd steering (preferred) -----------------------------------------
    if (ensureCrowdAgent(e, speed))
    {
        // Arrival. The crowd has no waypoint list to run out of, so the
        // "reached the end of the path" return the fallback branch relies on
        // has to be expressed as a distance here.
        vector3df toGoal = goal - myPos;
        toGoal.Y = 0.0f;
        const float goalDist = toGoal.getLength();
        if (goalDist <= m_arrivalRadius)
        {
            brake(m_currentSpeed, m_decel, dt);
            applyAnimSpeed(e, 0.0f, speed);
            return false;
        }

        vector3df crowdVel;
        const CROWD_STEER result = CrowdManager::Get()->steer(m_crowdHandle, goal, crowdVel);

        if (result == CROWD_STEER_OK)
        {
            m_crowdStopgapMs = 0.0f;

            crowdVel.Y = 0.0f;
            const float len = crowdVel.getLength();

            // A zero velocity from a planned agent is the crowd saying "hold" --
            // boxed in behind other agents, most often in the doorway this is
            // all for. Stand still and let the jam clear; do NOT fall through to
            // the stopgap below, which would shove into it.
            if (len <= 0.001f)
            {
                brake(m_currentSpeed, m_decel, dt);
                applyAnimSpeed(e, 0.0f, speed);
                return false;
            }

            steer = crowdVel / len;

            // KEEP THE MAGNITUDE, not just the direction. 'speed' is what
            // ensureCrowdAgent just pushed into the agent as its maxSpeed, so
            // len/speed is exactly the fraction of top speed Detour is asking
            // for -- it carries the crowd's arrival deceleration and its
            // avoidance slowdown, both of which were being thrown away by the
            // normalise. Binning it is what made every NPC binary: full speed,
            // or the exact-zero hold above, with nothing in between.
            m_crowdSpeedFrac = (speed > 0.001f)
                             ? std::min(1.0f, len / speed)
                             : 1.0f;
            haveSteer = true;
        }
        else if (result == CROWD_STEER_PLANNING &&
                 m_crowdStopgapMs < k_crowdStopgapLimitMs &&
                 goalDist > 0.001f)
        {
            // The request is genuinely still being planned. Head straight at the
            // goal for those few frames rather than standing still, which would
            // read as a hitch every time the target moves far enough to trigger
            // a re-plan.
            //
            // TIME-BOUNDED, and PLANNING ONLY. An unbounded version of this that
            // also fired on FAILED is what had the suicide bomber running at the
            // player through a wall: Detour kept reporting no route, and every
            // frame this branch pointed it straight at the target anyway. Once
            // the budget is spent, fall through to the waypoint walker below --
            // which will also fail to find a path and correctly stop the NPC.
            m_crowdStopgapMs += dt;
            steer            = toGoal / goalDist;
            m_crowdSpeedFrac = 1.0f;   // no crowd velocity to read a fraction off
            haveSteer        = true;
        }
        else if (result != CROWD_STEER_PLANNING)
        {
            m_crowdStopgapMs = 0.0f;
        }
    }

    // --- Fallback: the original waypoint walker ------------------------------
    // Reached when there is no navmesh, the crowd is off (ai_crowd 0), or this
    // NPC could not be registered as an agent.
    if (!haveSteer)
    {
        m_repathTimer += dt;

        if (m_repathTimer >= m_repathInterval || m_path.empty() ||
            m_pathIndex >= static_cast<int>(m_path.size()))
        {
            if (NavigationManager::Get() && NavigationManager::Get()->isNavMeshBuilt())
                m_path = NavigationManager::Get()->findPath(myPos, goal);
            else
                m_path.assign(1, goal);

            m_pathIndex   = 0;
            m_repathTimer = 0.0f;
        }

        // Advance past reached waypoints (XZ only so slope height doesn't stall
        // advancement)
        while (m_pathIndex < static_cast<int>(m_path.size()))
        {
            const vector3df toWp = m_path[m_pathIndex] - myPos;
            if (std::sqrtf(toWp.X * toWp.X + toWp.Z * toWp.Z) <= m_arrivalRadius)
                ++m_pathIndex;
            else
                break;
        }

        if (m_pathIndex >= static_cast<int>(m_path.size()))
        {
            brake(m_currentSpeed, m_decel, dt);
            applyAnimSpeed(e, 0.0f, speed);
            return false;
        }

        vector3df toWpXZ = m_path[m_pathIndex] - myPos;
        toWpXZ.Y = 0.0f;
        const float wpDistXZ = toWpXZ.getLength();
        if (wpDistXZ <= 0.01f)
        {
            brake(m_currentSpeed, m_decel, dt);
            applyAnimSpeed(e, 0.0f, speed);
            return false;
        }

        const vector3df dir = toWpXZ / wpDistXZ;

        // calcSeparation belongs to THIS branch only. The crowd already folds
        // neighbour separation into nvel, and adding this on top would push
        // twice.
        const vector3df sep = calcSeparation(myPos, e);

        steer = dir + sep;
        const float steerLen = steer.getLength();
        if (steerLen > 0.001f) steer /= steerLen;

        // The walker has no notion of a desired speed, only a direction, so
        // this branch is unaffected by the crowd's magnitude term.
        m_crowdSpeedFrac = 1.0f;
        haveSteer        = true;
    }

    // --- Turn toward the steering direction at a limited rate ----------------
    const float desiredHeading = rad2deg(atan2f(steer.X, steer.Z));

    if (!m_headingInit)
    {
        // Start from however it was placed, so a fresh NPC doesn't spin on its
        // first frame.
        m_heading     = tc.getRotation().Y - m_yawOffset;
        m_headingInit = true;
    }

    float facingError = 0.0f;

    if (m_turnRate <= 0.0f)
    {
        // Snap. The heading jumps to the steering direction, facingError stays
        // 0 so speedScale is 1, and 'forward' below comes out equal to 'steer'
        // -- which is exactly the old snap-facing motion, with no second code
        // path to keep in sync.
        m_heading = desiredHeading;
    }
    else
    {
        float delta = desiredHeading - m_heading;
        while (delta >  180.0f) delta -= 360.0f;
        while (delta < -180.0f) delta += 360.0f;

        facingError = std::fabs(delta);   // 0..180, before the clamp

        const float maxStep = m_turnRate * (dt * 0.001f);
        if (delta >  maxStep) delta =  maxStep;
        if (delta < -maxStep) delta = -maxStep;

        m_heading += delta;
    }

    while (m_heading >  180.0f) m_heading -= 360.0f;
    while (m_heading < -180.0f) m_heading += 360.0f;

    tc.setRotation(vector3df(0.0f, m_heading + m_yawOffset, 0.0f));

    // --- Move along the FACING, not the steering direction --------------------
    // This is what makes the turn an arc. Advancing along 'steer' while the yaw
    // lags behind would slide the model sideways -- it reads as skating, and the
    // path is still a corner, just with a rotating model on it. Going where the
    // body actually points sweeps a real curve.
    const float headingRad = deg2rad(m_heading);
    vector3df forward(sinf(headingRad), 0.0f, cosf(headingRad));

    // --- Wall slide ----------------------------------------------------------
    // NOTHING ELSE STOPS AN NPC ENTERING GEOMETRY. followPath writes the
    // transform directly and only casts DOWN for the floor; the navmesh has
    // always been the sole thing keeping bodies out of walls, and it is not
    // enough on its own for two independent reasons:
    //
    //   * A navmesh goes STALE the moment brush geometry changes
    //     (BrushManager.cpp:236) and stays stale until somebody clicks Bake.
    //     Walls built after the last bake do not exist as far as pathing is
    //     concerned, and the NPC walks straight through them.
    //   * Even against a perfect navmesh, the body advances along its FACING,
    //     not along the steering vector. At the bomber's 200 deg/s and 4.5 u/s
    //     that is a ~1.3 unit turn radius, so it CUTS CORNERS and clips wall
    //     edges by design.
    //
    // Brush geometry has PhysX static collision (BrushManager.cpp:451-458), so a
    // forward probe sees it whether or not it was ever baked into a navmesh.
    // This does not replace the navmesh -- an NPC sliding along a wall is still
    // lost -- it just stops "lost" from meaning "inside the level".
    forward = slideAlongWall(myPos, forward);

    // Speed falls off as the facing error grows: a near-pivot when the target is
    // behind, full speed once lined up. Without this, a slow turner at full
    // speed can orbit its goal forever and never close on it -- and it is also
    // just how a person takes a hard corner.
    const float errorRad   = deg2rad(facingError);
    const float speedScale = std::max(0.25f, std::cos(errorRad));

    // --- Ramp toward the wanted speed rather than assuming it ----------------
    //
    // THE FACING FALLOFF IS DELIBERATELY NOT IN HERE. It used to be, and that
    // was wrong: speedScale is a STEERING term, not a locomotion one. Ramping
    // it made a corner cost the hard brake down to the falloff AND the slow
    // climb back out of it, on top of the turn the falloff already represents.
    // At m_moveSpeed 3 a 90-degree corner cost roughly 0.4s of that, every
    // corner -- which in a maze is continuous, and is what "they slow down a
    // lot when they turn" actually was.
    //
    // So the ramp governs the LOCOMOTION target only (start, stop, and the
    // crowd's own arrival/avoidance easing, which is already smooth), and the
    // falloff multiplies in afterwards where it responds instantly in both
    // directions exactly as it did before any of this.
    const float wanted = speed * m_crowdSpeedFrac;

    {
        const float rate = (wanted > m_currentSpeed) ? m_accel : m_decel;
        const float step = rate * (dt * 0.001f);

        if (wanted > m_currentSpeed) m_currentSpeed = std::min(wanted, m_currentSpeed + step);
        else                         m_currentSpeed = std::max(wanted, m_currentSpeed - step);
    }

    const float applied = m_currentSpeed * speedScale;

    vector3df newPos = myPos + forward * applied * (dt * 0.001f);

    auto gndRay = PhysicsManager::Get()->raycast(
        newPos + vector3df(0.0f, k_groundProbeUp, 0.0f),
        vector3df(0.0f, -1.0f, 0.0f), k_groundProbeDist,
        RHG_ANY_HIT, k_probeExclude);

    // Rate-limited up, snapped on a real drop. A hard write here popped the
    // whole body tread by tread up a staircase; see smoothGroundY. snapToGround
    // has the same treatment or it would undo this every frame.
    const float targetY = gndRay.hit ? gndRay.data.getAnyHit(0).position.y : myPos.Y;
    newPos.Y = smoothGroundY(myPos.Y, targetY, dt);

    tc.setPosition(newPos);

    // --- Foot speed ----------------------------------------------------------
    // playAnim sets the playback rate ONCE, at clip change, and the body's
    // actual ground speed then varies underneath it -- worst on a hard corner,
    // where the cos(facingError) scale costs up to 75% of the speed and the eye
    // is drawn straight to the feet.
    //
    // MEASURED FROM THE REAL DISPLACEMENT, not from m_currentSpeed. Those two
    // diverge whenever slideAlongWall shortens 'forward' -- which is constantly,
    // in any corridor -- and reading the intended speed instead of the achieved
    // one ran the legs at full rate under a body pinned motionless against a
    // wall. XZ only: the ground pin's vertical component is not locomotion.
    //
    // This must run AFTER any playAnim() in the same frame, which is how both
    // NPCs are written: the clip change happens in the state transition, the
    // move happens here. A clip change re-issues setAnimationSpeed(fps) and
    // would stomp this if the order were reversed.
    //
    // Unrelated to ITimer::setSpeed -- that is the global virtual-timer time
    // scale, and it composes with this multiplicatively, which is correct.
    const float movedXZ = std::sqrtf((newPos.X - myPos.X) * (newPos.X - myPos.X) +
                                     (newPos.Z - myPos.Z) * (newPos.Z - myPos.Z));

    applyAnimSpeed(e, movedXZ / (dt * 0.001f), speed);

    return true;
}

// ---------------------------------------------------------------------------
// Crowd
// ---------------------------------------------------------------------------

bool CharacterBehavior::ensureCrowdAgent(anax::Entity& e, float speed)
{
    CrowdManager* crowd = CrowdManager::Get();
    if (!crowd || !crowd->isEnabled()) return false;

    // No navmesh means no crowd -- and that is a supported configuration, not an
    // error. The caller falls through to the straight-line walker, which is what
    // every unbaked scene relies on.
    if (!NavigationManager::Get() || !NavigationManager::Get()->isNavMeshBuilt())
        return false;

    if (crowd->isRegistered(m_crowdHandle))
    {
        crowd->setAgentSpeed(m_crowdHandle, speed);
        return true;
    }

    // A previous attempt failed. Retry only once the crowd has been rebuilt --
    // that is the one event that can change the answer (a re-bake may now cover
    // where this NPC is standing).
    if (m_crowdDenied && m_crowdDeniedGen == crowd->generation())
        return false;

    CrowdAgentConfig cfg;
    cfg.radius           = m_agentRadius;
    cfg.maxSpeed         = speed;
    cfg.maxAcceleration  = speed * 8.0f;
    cfg.separationWeight = m_separationWeight;

    if (!crowd->registerAgent(e, cfg, m_crowdHandle))
    {
        m_crowdDenied    = true;
        m_crowdDeniedGen = crowd->generation();

        // Once per NPC per bake, not once per frame.
        std::string name = "?";
        if (e.hasComponent<DescriptorComponent>())
            name = e.getComponent<DescriptorComponent>().name;

        spdlog::warn("CharacterBehavior: '{}' could not join the crowd "
                     "(not on the navmesh, or the agent pool is full) -- "
                     "falling back to direct pathing", name);
        return false;
    }

    m_crowdDenied = false;
    return true;
}

void CharacterBehavior::releaseCrowdAgent()
{
    if (CrowdManager::Get())
        CrowdManager::Get()->unregisterAgent(m_crowdHandle);
    else
        m_crowdHandle.clear();
}

void CharacterBehavior::destroy(anax::Entity& entity)
{
    releaseCrowdAgent();

    if (entity.isValid() && entity.hasComponent<DescriptorComponent>())
        AICoordinator::Get()->release(entity.getComponent<DescriptorComponent>().id);
}

// ---------------------------------------------------------------------------
// Squad
// ---------------------------------------------------------------------------

SquadOrder CharacterBehavior::requestSquadOrder(anax::Entity& self, entityid targetId,
                                                const vector3df& targetPos)
{
    SquadRequest request;
    request.mode   = m_squadMode;
    request.tokens = m_squadTokens;
    request.ring   = m_squadRing;

    return AICoordinator::Get()->requestOrder(self, targetId, targetPos, request);
}

// ---------------------------------------------------------------------------
// Salvaged from NPCSystem -- see the header for why the constants below are NOT
// safe to trust without re-tuning.
// ---------------------------------------------------------------------------

float CharacterBehavior::stepUp(const vector3df& pos, const vector3df& dir)
{
    const vector3df origin = pos + vector3df(0.0f, 1.5f, 0.0f) + dir * 0.7f;

    // Uncalled today, but fixed alongside the live probes rather than left as a
    // trap for whoever wires it up.
    auto ray = PhysicsManager::Get()->raycast(origin, vector3df(0.0f, -1.0f, 0.0f), 1.6f,
                                              RHG_ANY_HIT, k_probeExclude);
    if (!ray.hit) return 0.0f;

    const float heightDiff = ray.data.getAnyHit(0).position.y - pos.Y;

    // Window: too small and it fights the ground pin, too large and the body
    // teleports up walls. Tuned per-frame in NPCSystem; re-tune per-second.
    if (heightDiff > 0.025f && heightDiff < 0.75f)
        return heightDiff * 0.4f;

    return 0.0f;
}

vector3df CharacterBehavior::slideAlongWall(const vector3df& pos, vector3df dir)
{
    // Characters excluded: the player is not a wall. Un-excluded, the probe hit
    // the player's capsule from 1.5 units out -- further than the zombie's 1.25
    // attack range -- and the head-on collapse below then stopped it dead just
    // outside the range it needed to reach to bite.
    auto ray = PhysicsManager::Get()->raycast(pos + vector3df(0.0f, 0.5f, 0.0f), dir,
                                              k_wallProbeDist, RHG_ANY_HIT, k_probeExclude);
    if (!ray.hit) return dir;

    const auto pn = ray.data.getAnyHit(0).normal;
    const vector3df wallNorm(pn.x, pn.y, pn.z);

    // A RAMP IS NOT A WALL. The probe is horizontal at knee height, so anything
    // rising ahead -- a ramp, a stair nosing, a sloped floor -- gets hit by it.
    // Sliding along those would make every walkable slope in the level
    // impassable, while the navmesh (agentMaxSlope 45 degrees) says to walk
    // straight up them.
    //
    // cos(45 deg) == 0.707, so a surface whose normal has more Y than that is
    // floor by the same rule the bake used, and is ignored here.
    if (wallNorm.Y > 0.707f) return dir;

    const float dot = dir.dotProduct(wallNorm);
    if (dot < 0.0f)
        dir -= wallNorm * dot;   // slide along the wall surface

    // Length is deliberately NOT restored. The component removed is the one
    // heading into the wall, so a glancing approach barely slows and a head-on
    // one collapses to near zero, which is what stops the NPC rather than having
    // it grind along at full speed.
    return dir;
}

bool CharacterBehavior::hasGroundAhead(const vector3df& pos, const vector3df& dir, float dist)
{
    const vector3df probe = pos + dir * dist + vector3df(0.0f, 2.0f, 0.0f);
    return PhysicsManager::Get()->raycast(probe, vector3df(0.0f, -1.0f, 0.0f), 4.0f,
                                          RHG_ANY_HIT, k_probeExclude).hit;
}

// ---------------------------------------------------------------------------
// Death
// ---------------------------------------------------------------------------

bool CharacterBehavior::handleDeath(anax::Entity& e, const std::string& dieClip, const char* dieSound)
{
    if (m_isDead) return true;

    if (!e.hasComponent<DamageReceiverComponent>()) return false;

    // health and gibbed ONLY. See the header: didReceiveDamage() and
    // didReceiveExplosive() are consuming reads and must not be touched here.
    auto& drc = e.getComponent<DamageReceiverComponent>();
    if (drc.health > 0) return false;

    m_isDead = true;

    // resetMovement(), not a bare m_path.clear(): the momentum has to go too,
    // or a corpse that is later revived (or a pooled behaviour re-used) starts
    // already at a run.
    resetMovement();

    // Leave the crowd HERE, on death, not just in destroy(). handleDeath does
    // not remove the entity -- corpses persist, keeping their transform. An
    // agent left registered would keep occupying the proximity grid and living
    // NPCs would start walking AROUND corpses, which calcSeparation deliberately
    // does not do (see its "Corpses can overlap" filter). That is a visible
    // regression, not a neutral difference.
    releaseCrowdAgent();

    if (e.hasComponent<DescriptorComponent>())
        AICoordinator::Get()->release(e.getComponent<DescriptorComponent>().id);

    // A gibbed body is already hidden and queued for removal -- playing a death
    // animation and a death cry over the top would be a corpse performing for a
    // frame after it stopped existing.
    if (drc.gibbed) return true;

    if (!dieClip.empty() && e.hasComponent<MeshComponent>())
        playAnim(e.getComponent<MeshComponent>(), dieClip);

    if (dieSound && e.hasComponent<SoundComponent>())
        e.getComponent<SoundComponent>().play(dieSound);

    return true;
}

// ---------------------------------------------------------------------------
// Properties
// ---------------------------------------------------------------------------

std::vector<BehaviorProperty> CharacterBehavior::baseProperties()
{
    std::vector<BehaviorProperty> props;
    props.push_back({ "Seperation Radius", BehaviorPropType::FLOAT, &m_separationRadius });
    props.push_back({ "Repath Interval",   BehaviorPropType::FLOAT, &m_repathInterval   });
    props.push_back({ "Arrival Radius",    BehaviorPropType::FLOAT, &m_arrivalRadius    });
    props.push_back({ "Turn Rate",         BehaviorPropType::FLOAT, &m_turnRate         });
    props.push_back({ "Yaw Offset",        BehaviorPropType::FLOAT, &m_yawOffset        });

    // Appended AFTER the existing rows. Key strings are matched by name, so
    // adding to the end cannot disturb anything already saved -- a .ent with no
    // value for these simply keeps the default.
    props.push_back({ "Agent Radius",      BehaviorPropType::FLOAT, &m_agentRadius      });
    props.push_back({ "Separation Weight", BehaviorPropType::FLOAT, &m_separationWeight });
    props.push_back({ "Squad Mode",        BehaviorPropType::INT,   &m_squadMode        });
    props.push_back({ "Squad Tokens",      BehaviorPropType::INT,   &m_squadTokens      });
    props.push_back({ "Squad Ring",        BehaviorPropType::FLOAT, &m_squadRing        });

    // Appended again, for the same reason as the block above -- a .ent saved
    // before these existed simply keeps the defaults. DO NOT REORDER OR RENAME
    // ANY ROW IN THIS FUNCTION: applyPropertiesToBehavior matches on the exact
    // name string, so a rename silently reverts every saved .ent to the default
    // with no error. ("Seperation Radius" keeps its misspelling for exactly
    // this reason.)
    props.push_back({ "Sense Interval",    BehaviorPropType::FLOAT, &m_senseInterval    });
    props.push_back({ "Vision Cone",       BehaviorPropType::FLOAT, &m_visionCone       });
    props.push_back({ "Accel",             BehaviorPropType::FLOAT, &m_accel            });
    props.push_back({ "Decel",             BehaviorPropType::FLOAT, &m_decel            });
    return props;
}

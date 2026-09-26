#pragma once

#include "Engine/Animation/AnimPose.h"

#include "irrlicht.h"

#include <vector>

// How the IK asks the world where the floor is.
//
// An interface rather than a direct PhysicsManager call, because the animation
// module has no business knowing PhysX exists and the NPCs will want a probe
// with different filtering from the player's. AnimationSystem installs the
// concrete one.
class IAnimGroundProbe
{
public:
    virtual ~IAnimGroundProbe() {}

    // Straight down from 'fromWorld', at most 'downDistance'. Returns false if
    // nothing was hit, which is a normal answer - over a ledge, off the map.
    virtual bool probeGround(const irr::core::vector3df& fromWorld, float downDistance,
                             irr::core::vector3df& hitWorld,
                             irr::core::vector3df& normalWorld) = 0;
};

struct AnimFootIKConfig
{
    // Ankle-to-sole distance, in world units: the probe returns the FLOOR, and
    // the ankle has to sit this far above it.
    //
    // 0 MEANS MEASURE IT, which is the default and what you want. It is a
    // property of the rig, not a tuning value, and bind() reads it off the bind
    // pose - the ankle's height in the rest pose IS the ankle-to-sole distance,
    // because the rest pose stands on the ground. Measured on the paladin:
    // 0.1313, and its planted ankle sits at exactly that height in every single
    // clip. A hand-picked 0.11 sank both feet by two centimetres.
    float soleOffset = 0.0f;

    // The probe starts this far above the animated ankle and runs this far
    // below it.
    //
    // A SHORT PROBE IS A ONE-WAY TRAP: a foot lifted higher than the probe
    // reaches never finds floor again, so the offset decays to zero and the foot
    // stays wherever the clip put it - which looks like the IK switching itself
    // off mid-stride. Generous on both ends, deliberately.
    float probeUp   = 0.60f;
    float probeDown = 1.20f;

    // How far above its planted height the clip may lift a foot before LOWERING
    // is faded out entirely.
    //
    // THE ASYMMETRY IS THE WHOLE POINT, and getting it wrong is what broke the
    // first version of this. Raising a foot is always right: a foot inside the
    // floor is wrong in every phase of every clip. LOWERING a foot is only right
    // for a foot the clip has PLANTED - a foot in swing is above the ground on
    // purpose, and pulling it down replaces the stride with a shuffle and drags
    // the hips with it.
    //
    // Stance is judged from the foot's height in MESH space, not its height
    // above the world floor. Mesh space is what the ANIMATION intends, and it is
    // independent of where the ground happens to be - which is exactly what lets
    // a planted foot still reach down a step, while a swing foot at the same
    // height above the ground is left alone. Judging it against the world floor
    // cannot tell those two apart.
    float stanceFalloff = 0.10f;

    // Separate caps, because raising and lowering are different operations.
    float maxRaise = 0.50f;
    float maxLower = 0.50f;

    // How far the hips may drop so the legs do not over-extend on stairs.
    float maxHipDrop = 0.45f;

    // Exponential approach rate, 1/sec. DAMPING IS NOT OPTIONAL: the probe
    // result steps discontinuously at a stair edge, and an undamped solution
    // snaps the whole leg on the frame the ray crosses the nosing.
    float adaptRate = 10.0f;

    // How hard the foot rolls to match the surface. 0 disables the pitch and
    // leaves only the vertical placement.
    float pitchWeight = 0.85f;

    // Cap on that roll. A downward ray that lands exactly on a stair nosing can
    // come back with the RISER's normal, which is horizontal; un-capped, the
    // foot then rotates most of a right angle into the step. The cap is what
    // makes a bad hit look like a slightly wrong foot instead of a broken one.
    float maxPitchDeg = 35.0f;

    // A probe hit is only floor if its normal is at least this vertical.
    // Rejecting the rest is the other half of the stair-nosing guard: a hit on a
    // riser reports a height somewhere UP the face of the step, which is not a
    // surface anything can stand on.
    float minGroundNormalY = 0.35f;

    // Probe under the TOE as well as the ankle, and place the foot on whichever
    // of the two contacts is HIGHER.
    //
    // ONE RAY CANNOT SEE A STEP EDGE. The paladin's foot is about 0.14 long, and
    // a stair tread is not much deeper than that, so a foot straddling a nosing
    // has its ankle over one tread and its toe over the other. Placed from the
    // ankle alone it is dropped onto the lower tread and the front half of the
    // foot disappears into the upper one - which is exactly the "the foot off
    // the stair doesn't lie on either step" symptom. Taking the higher of the
    // two requirements rests the foot ON the nosing instead, which is what a
    // real foot does.
    bool toeProbe = true;
};

// Two-bone analytic foot IK, applied to the FINAL pose.
//
// Runs after everything else - base blend, layers, the lot - because it is a
// correction to where the feet ended up, and anything that ran after it would
// undo it.
//
// WHAT IT FIXES, AND WHAT IT DOES NOT. It fixes feet floating above a slope and
// sinking into a stair. It fixes none of the transition or blending problems the
// earlier phases dealt with, and it is not a substitute for any of them.
class AnimFootIK
{
public:
    bool bind(irr::scene::ISkinnedMesh* mesh);
    bool valid() const { return m_valid; }

    void setProbe(IAnimGroundProbe* probe) { m_probe = probe; }
    void setConfig(const AnimFootIKConfig& cfg) { m_cfg = cfg; }

    // Drop all smoothed state. Call on a teleport, or the character spends a
    // quarter of a second dragging its feet from where it used to be.
    void reset();

    // An extra vertical offset applied to the PELVIS, in world units, on top of
    // whatever hip drop the solver decides for itself. Already-smoothed: this
    // is not damped again.
    //
    // WHAT IT IS FOR. A character climbing a step is moved up it instantly by
    // the capsule controller, and softening that jolt by lagging the whole body
    // transform is wrong once foot IK exists - it desynchronises the mesh from
    // the floor the feet are being placed against, for as long as the lag lasts,
    // which is precisely the moment anyone is looking at the feet. Put the body
    // on the true capsule position and hand the lag to the PELVIS instead: the
    // upper body still eases up the step, and the feet stay where the ground is.
    void setPelvisOffset(float worldY) { m_pelvisOffset = worldY; }

    // 'weightTarget' is 0..1 and is RAMPED internally, so the caller can flip it
    // between 0 and 1 on a state change without producing a snap. Feed 0 while
    // airborne and during a landing.
    void solve(AnimPose& pose, const irr::core::matrix4& nodeToWorld,
               float weightTarget, float seconds);

    // --- Debug ---------------------------------------------------------------
    // What the last solve() actually SAW, per foot, in world space.
    //
    // Kept because foot placement cannot be debugged from numbers. Every hard
    // case so far has been a probe landing on a surface nobody expected - the
    // far side of a step, a riser, the character's own collision - and a foot in
    // the wrong place looks identical whichever of those it was. Drawn by the
    // 'anim_ik_draw' cvar.
    struct Probe
    {
        irr::core::vector3df from;
        irr::core::vector3df hit;
        irr::core::vector3df normal;
        float                length = 0.0f;
        bool                 fired  = false;   // the probe was cast at all
        bool                 valid  = false;   // ... and something was hit
        bool                 used   = false;   // ... and its normal was vertical enough
    };

    struct FootDebug
    {
        Probe ankle;
        Probe toe;
        irr::core::vector3df ankleWorld;
        irr::core::vector3df toeWorld;
        irr::core::vector3df targetWorld;
    };

    const FootDebug& footDebug(int side) const
    {
        return m_dbg[(side == 1) ? 1 : 0];
    }

    float weight() const { return m_weight; }
    float hipDrop() const { return m_hipDrop; }
    float hipExtra() const { return m_hipExtra; }
    float soleOffset() const { return m_soleOffset; }
    float toeOffset() const { return m_toeOffset; }
    bool  footInfo(int side, float& offset, bool& grounded, float& stance) const;

private:
    struct Leg
    {
        int upLeg = -1;   // thigh
        int leg   = -1;   // shin
        int foot  = -1;   // ankle
        int toe   = -1;
    };

    void composeGlobals(const AnimPose& pose);

    // Shift the pelvis by a WORLD-space vertical and rebuild the globals. The
    // rebuild is not optional: the legs are solved against absolute targets, so
    // a pelvis that moved after the globals were composed leaves every leg
    // reaching for a hip that is no longer there.
    void moveHips(AnimPose& pose, const irr::core::matrix4& worldToNode, float worldY);

    // World-space distance the chain can cover, and what it is covering in the
    // pose as it stands. Done in WORLD space because the targets are, and
    // because converting a reach through the node transform would have to
    // assume its scale is uniform.
    void legReach(const Leg& leg, const irr::core::matrix4& nodeToWorld,
                  irr::core::vector3df& hipWorld, float& reach) const;

    bool solveLeg(AnimPose& pose, const Leg& leg,
                  const irr::core::vector3df& targetModel,
                  const irr::core::vector3df& normalModel, float w,
                  irr::core::vector3df& bendCache);

    IAnimGroundProbe* m_probe = nullptr;
    AnimFootIKConfig  m_cfg;

    // Parent indices and a parents-before-children order, both resolved once.
    std::vector<int> m_parent;
    std::vector<int> m_order;

    std::vector<irr::core::quaternion> m_gRot;
    std::vector<irr::core::vector3df>  m_gPos;
    std::vector<float>                 m_gScale;

    int m_hips = -1;
    Leg m_legs[2];   // 0 = left, 1 = right

    float                m_offset[2];
    float                m_stance[2];
    bool                 m_grounded[2];
    irr::core::vector3df m_normalWorld[2];
    irr::core::vector3df m_targetModel[2];
    FootDebug            m_dbg[2];

    // Last accepted bend plane per leg, so a leg that passes through straight
    // borrows the direction it had a frame ago rather than picking one out of
    // rounding noise.
    irr::core::vector3df m_bend[2];

    float m_hipDrop      = 0.0f;
    float m_hipExtra     = 0.0f;   // extra drop bought to keep both legs in reach
    float m_pelvisOffset = 0.0f;   // external, see setPelvisOffset
    float m_weight       = 0.0f;
    float m_soleOffset   = 0.0f;   // resolved at bind, from the config or the rig
    float m_toeOffset    = 0.0f;   // bind-pose toe height, the toe's own sole offset

    bool m_valid = false;
};

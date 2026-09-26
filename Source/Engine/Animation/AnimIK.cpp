#include "Engine/Animation/AnimIK.h"

#include "spdlog/spdlog.h"

#include <cmath>
#include <cstring>

using namespace irr;

namespace
{
    // Joint name tails, matched the same way AnimMask matches them.
    const char* k_hipsName = "Hips";
    const char* k_legNames[2][4] =
    {
        { "LeftUpLeg",  "LeftLeg",  "LeftFoot",  "LeftToeBase"  },
        { "RightUpLeg", "RightLeg", "RightFoot", "RightToeBase" },
    };

    bool nameEndsWith(const char* name, const char* tail)
    {
        if (!name || !tail)
            return false;

        const size_t n = strlen(name);
        const size_t t = strlen(tail);
        return (t <= n) && (strcmp(name + (n - t), tail) == 0);
    }

    int findJoint(scene::ISkinnedMesh* mesh, const char* tail)
    {
        const core::array<scene::ISkinnedMesh::SJoint*>& joints = mesh->getAllJoints();
        for (u32 i = 0; i < joints.size(); ++i)
            if (nameEndsWith(joints[i]->Name.c_str(), tail))
                return static_cast<int>(i);

        return -1;
    }

    float clampf(float v, float lo, float hi)
    {
        return (v < lo) ? lo : ((v > hi) ? hi : v);
    }

    // How much of the chain's length the solver is willing to span.
    //
    // THIS IS A DEGENERACY GUARD, NOT A POSE CHOICE, so it sits as close to 1 as
    // the maths allows. It used to be 0.995, and on this rig that was a bug: the
    // paladin's bind leg spans 0.99986 of its chain and its IDLE leg reaches
    // 0.993, so a 0.995 ceiling was BELOW the pose's own extension and quietly
    // shortened a standing leg by half a percent - six millimetres of lift, on
    // exactly the frames the foot was supposed to be nailed down. The pose's own
    // span is honoured on top of this; see solveLeg.
    const float k_reachSafety = 0.9995f;

    // Exponential approach, frame-rate independent. 1 - exp(-rate*dt) rather
    // than rate*dt, so a long frame cannot overshoot past the target.
    float approach(float current, float target, float rate, float seconds)
    {
        if (rate <= 0.0f)
            return target;

        const float t = 1.0f - expf(-rate * seconds);
        return current + (target - current) * t;
    }
}

bool AnimFootIK::bind(scene::ISkinnedMesh* mesh)
{
    m_valid = false;
    m_parent.clear();
    m_order.clear();
    reset();

    if (!mesh || mesh->getJointCount() == 0)
        return false;

    const core::array<scene::ISkinnedMesh::SJoint*>& joints = mesh->getAllJoints();
    const u32 count = joints.size();

    m_parent.assign(count, -1);
    for (u32 i = 0; i < count; ++i)
    {
        for (u32 c = 0; c < joints[i]->Children.size(); ++c)
        {
            for (u32 j = 0; j < count; ++j)
            {
                if (joints[j] == joints[i]->Children[c])
                {
                    m_parent[j] = static_cast<int>(i);
                    break;
                }
            }
        }
    }

    // Parents before children. getAllJoints() is usually already in that order,
    // but "usually" is not a property to compose global transforms on.
    m_order.reserve(count);
    {
        std::vector<bool> done(count, false);
        bool progress = true;

        while (progress)
        {
            progress = false;
            for (u32 i = 0; i < count; ++i)
            {
                if (done[i])
                    continue;

                const int p = m_parent[i];
                if (p < 0 || done[p])
                {
                    done[i] = true;
                    m_order.push_back(static_cast<int>(i));
                    progress = true;
                }
            }
        }

        if (m_order.size() != count)
        {
            spdlog::warn("AnimFootIK: joint hierarchy has a cycle - foot IK disabled");
            return false;
        }
    }

    m_gRot.assign(count, core::quaternion());
    m_gPos.assign(count, core::vector3df(0.0f, 0.0f, 0.0f));
    m_gScale.assign(count, 1.0f);

    m_hips = findJoint(mesh, k_hipsName);

    for (int s = 0; s < 2; ++s)
    {
        m_legs[s].upLeg = findJoint(mesh, k_legNames[s][0]);
        m_legs[s].leg   = findJoint(mesh, k_legNames[s][1]);
        m_legs[s].foot  = findJoint(mesh, k_legNames[s][2]);
        m_legs[s].toe   = findJoint(mesh, k_legNames[s][3]);

        if (m_legs[s].upLeg < 0 || m_legs[s].leg < 0 || m_legs[s].foot < 0)
        {
            // Not a rig we know how to solve. Refuse rather than guessing at
            // indices and folding a character's legs backwards.
            spdlog::warn("AnimFootIK: '{}' chain not found - foot IK disabled for this mesh",
                         k_legNames[s][0]);
            return false;
        }
    }

    // --- Sole offsets, measured off the rig ----------------------------------
    // The bind pose stands on the ground, so a joint's height in it IS that
    // joint's distance above the sole. Composed from SJoint::LocalMatrix, which
    // is the bind pose by definition.
    //
    // The TOE gets one of its own, and it is not the ankle's: the toe joint sits
    // near the front of the sole, much lower than the ankle (paladin 0.0189
    // against 0.1313). The toe probe needs it to say where the ankle must be for
    // the BALL of the foot to rest on what the toe found.
    {
        std::vector<core::matrix4> bind(count);
        for (size_t k = 0; k < m_order.size(); ++k)
        {
            const int i = m_order[k];
            const int p = m_parent[i];
            bind[i] = (p < 0) ? joints[i]->LocalMatrix
                              : (bind[p] * joints[i]->LocalMatrix);
        }

        m_soleOffset = m_cfg.soleOffset;

        if (m_soleOffset <= 0.0f)
        {
            const float l = bind[m_legs[0].foot].getTranslation().Y;
            const float r = bind[m_legs[1].foot].getTranslation().Y;

            m_soleOffset = (l + r) * 0.5f;

            if (m_soleOffset <= 0.0f)
            {
                // A rig whose bind pose is not standing on the ground. Refuse to
                // guess: a wrong sole offset sinks or floats the character
                // permanently, which is worse than no IK at all.
                spdlog::warn("AnimFootIK: bind-pose ankle height is {:.4f} - cannot derive a "
                             "sole offset, foot IK disabled", m_soleOffset);
                return false;
            }
        }

        m_toeOffset = 0.0f;

        if (m_legs[0].toe >= 0 && m_legs[1].toe >= 0)
        {
            const float lt = bind[m_legs[0].toe].getTranslation().Y;
            const float rt = bind[m_legs[1].toe].getTranslation().Y;

            m_toeOffset = (lt + rt) * 0.5f;
        }

        // A toe at or above the ankle is not a toe this reasoning applies to.
        // Drop to the ankle-only probe rather than placing feet from a number
        // that means something else on this rig.
        if (m_toeOffset <= 0.0f || m_toeOffset >= m_soleOffset)
        {
            spdlog::warn("AnimFootIK: bind-pose toe height {:.4f} is not below the ankle's "
                         "{:.4f} - toe probe disabled for this mesh",
                         m_toeOffset, m_soleOffset);
            m_toeOffset = 0.0f;
        }
    }

    m_valid = true;
    return true;
}

void AnimFootIK::reset()
{
    for (int s = 0; s < 2; ++s)
    {
        m_offset[s]   = 0.0f;
        m_stance[s]   = 0.0f;
        m_grounded[s] = false;
        m_normalWorld[s].set(0.0f, 1.0f, 0.0f);
        m_targetModel[s].set(0.0f, 0.0f, 0.0f);
        m_bend[s].set(0.0f, 0.0f, 0.0f);
    }

    m_hipDrop      = 0.0f;
    m_hipExtra     = 0.0f;
    m_pelvisOffset = 0.0f;
    m_weight       = 0.0f;
}

// Model-space global transforms from the local pose.
//
// Rotation composes as global = local * parentGlobal. That is NOT a typo for the
// textbook order: Irrlicht's quaternion operator* applies its LEFT operand
// first, which was confirmed by experiment rather than assumed. Position and
// (uniform) scale compose the ordinary way.
void AnimFootIK::composeGlobals(const AnimPose& pose)
{
    for (size_t k = 0; k < m_order.size(); ++k)
    {
        const int i = m_order[k];
        const int p = m_parent[i];

        const scene::SJointPose& local = pose.joints[i];

        if (p < 0)
        {
            m_gRot[i]   = local.Rotation;
            m_gRot[i].normalize();
            m_gPos[i]   = local.Position;
            m_gScale[i] = local.Scale.X;
        }
        else
        {
            m_gRot[i]   = local.Rotation * m_gRot[p];
            m_gRot[i].normalize();
            m_gPos[i]   = m_gPos[p] + m_gRot[p] * (local.Position * m_gScale[p]);
            m_gScale[i] = m_gScale[p] * local.Scale.X;
        }
    }
}

// Shift the pelvis by a world-space vertical and rebuild the globals.
//
// Incremental: it ADDS to whatever the hips already carry, so it can be called
// more than once in a frame. The rebuild at the end is not optional - the legs
// are solved against absolute targets, and a pelvis that moved after the globals
// were composed leaves every leg reaching for a hip that is no longer there.
void AnimFootIK::moveHips(AnimPose& pose, const core::matrix4& worldToNode, float worldY)
{
    if (m_hips < 0 || fabsf(worldY) < 1e-6f)
        return;

    // The shift is a WORLD-space vertical, so it has to be expressed in the
    // hips' parent space before it can be added to a local position.
    core::vector3df modelDelta;
    worldToNode.rotateVect(modelDelta, core::vector3df(0.0f, worldY, 0.0f));

    const int p = m_parent[m_hips];
    if (p >= 0)
    {
        core::quaternion inv = m_gRot[p];
        inv.makeInverse();
        modelDelta = inv * modelDelta;

        if (m_gScale[p] > 1e-6f)
            modelDelta /= m_gScale[p];
    }

    pose.joints[m_hips].Position += modelDelta;

    composeGlobals(pose);
}

// The hip joint in world space, and how far that chain can actually stretch
// from it.
void AnimFootIK::legReach(const Leg& leg, const core::matrix4& nodeToWorld,
                          core::vector3df& hipWorld, float& reach) const
{
    core::vector3df B, C;
    nodeToWorld.transformVect(hipWorld, m_gPos[leg.upLeg]);
    nodeToWorld.transformVect(B, m_gPos[leg.leg]);
    nodeToWorld.transformVect(C, m_gPos[leg.foot]);

    reach = ((B - hipWorld).getLength() + (C - B).getLength()) * k_reachSafety;

    // Never claim less than the pose is already spanning. See k_reachSafety.
    const float current = (C - hipWorld).getLength();
    if (current > reach)
        reach = current;
}

// Analytic two-bone solve, done entirely in POSITIONS first and converted to
// rotations at the end.
//
// Solving for the new knee and ankle POSITIONS is pure vector maths with no
// rotation convention to get wrong; only the final step touches quaternions, and
// it does so through rotationFromTo, which is unambiguous. The alternative -
// composing law-of-cosines angles about axes expressed in each joint's local
// frame - is the standard formulation and is far easier to get subtly backwards.
bool AnimFootIK::solveLeg(AnimPose& pose, const Leg& leg,
                          const core::vector3df& targetModel,
                          const core::vector3df& normalModel, float w,
                          core::vector3df& bendCache)
{
    const core::vector3df A = m_gPos[leg.upLeg];
    const core::vector3df B = m_gPos[leg.leg];
    const core::vector3df C = m_gPos[leg.foot];

    core::vector3df ab = B - A;
    core::vector3df bc = C - B;

    const float l1 = ab.getLength();
    const float l2 = bc.getLength();
    if (l1 < 1e-5f || l2 < 1e-5f)
        return false;

    core::vector3df at = targetModel - A;
    float d = at.getLength();
    if (d < 1e-5f)
        return false;

    // Never let the chain reach its exact limit: at full extension the bend
    // plane is undefined and the knee pops to whichever side rounding chooses.
    //
    // THE POSE'S OWN SPAN IS THE FLOOR, and leaving it out was a bug. A standing
    // leg is straight - the paladin's spans 0.9999 of its chain at bind and
    // 0.993 while idling - so a fixed ceiling below that shortens a leg the
    // animation had every right to straighten. Because this solver is otherwise
    // the exact identity when the target equals the current ankle position, that
    // clamp was the ONE thing that made a standing foot move at all, and it did
    // so only on the frames the leg happened to be most extended: an
    // intermittent few millimetres of lift under an idle that never moves its
    // feet. Honour what the pose is already doing and only refuse to go beyond
    // it.
    float reach = (l1 + l2) * k_reachSafety;

    const float dCur = (C - A).getLength();
    if (dCur > reach)
        reach = (dCur < l1 + l2) ? dCur : (l1 + l2);

    if (d > reach)
    {
        at *= reach / d;
        d = reach;
    }

    const core::vector3df dir = at / d;

    // The bend plane comes from where the knee ALREADY is, so the solve keeps
    // the pose's own knee direction instead of inventing one. Falling back to a
    // fixed axis would flip the knee inside out whenever the leg passed through
    // straight.
    // The threshold is RELATIVE to the chain, not an absolute epsilon: it is a
    // length in model units and model units are whatever the asset was exported
    // at. Below it the direction is rounding noise, and since the foot's final
    // orientation is derived from where the knee ended up, a noisy bend plane
    // does not merely wobble the knee - it swivels the foot on the floor.
    const float perpMin = 1e-3f * (l1 + l2);

    core::vector3df perp = ab - dir * ab.dotProduct(dir);
    float perpLen = perp.getLength();

    if (perpLen < perpMin)
    {
        // Straight leg: no bend plane left in the pose. Borrow the one this leg
        // had a frame ago, so passing THROUGH straight keeps the knee on the
        // side it was already on instead of choosing again at the bottom.
        perp = bendCache - dir * bendCache.dotProduct(dir);
        perpLen = perp.getLength();
    }

    if (perpLen < perpMin)
    {
        // Nothing to borrow either - first frame, or a leg that started
        // straight. Push the knee toward the foot's forward direction, which for
        // any bipedal rig is the way a knee bends.
        core::vector3df fwd = (leg.toe >= 0) ? (m_gPos[leg.toe] - C) : core::vector3df(0, 0, 1);
        perp = fwd - dir * fwd.dotProduct(dir);
        perpLen = perp.getLength();

        if (perpLen < 1e-5f)
            return false;
    }

    const core::vector3df bend = perp / perpLen;
    bendCache = bend;

    // Law of cosines for the angle at the hip between the target direction and
    // the thigh.
    const float cosA = clampf((l1 * l1 + d * d - l2 * l2) / (2.0f * l1 * d), -1.0f, 1.0f);
    const float sinA = sqrtf(1.0f - cosA * cosA);

    const core::vector3df kneeNew = A + dir * (l1 * cosA) + bend * (l1 * sinA);
    const core::vector3df footNew = A + dir * d;

    // --- Convert the new bone directions into joint rotations ----------------
    core::vector3df abOld = ab / l1;
    core::vector3df abNew = kneeNew - A;
    abNew.normalize();

    core::quaternion dThigh;
    dThigh.rotationFromTo(abOld, abNew);
    dThigh.normalize();

    // Blend the correction in by weight rather than the target position, so a
    // partially-weighted IK is a partial ROTATION and the bone lengths stay
    // exactly right at every weight.
    // NORMALISE AFTER EVERY SLERP AND EVERY PRODUCT. core::quaternion::slerp
    // degenerates to an unnormalised component lerp when its inputs are within
    // 0.05 of parallel - which is exactly the case at a small IK weight - and
    // quaternion::operator*(vector3df) SCALES by roughly the squared norm. An
    // un-normalised rotation therefore does not merely aim the bone wrongly, it
    // changes its LENGTH. Measured before this was added: the shin came out 0.4%
    // short at half weight, steadily, which on a leg is a visible sink.
    core::quaternion dThighW;
    dThighW.slerp(core::quaternion(), dThigh, w);
    dThighW.normalize();

    core::quaternion upLegGlobalNew = m_gRot[leg.upLeg] * dThighW;
    upLegGlobalNew.normalize();

    // The shin's old direction, carried through the thigh correction.
    core::vector3df bcOld = bc / l2;
    core::vector3df bcCarried = dThighW * bcOld;

    core::vector3df bcNew = footNew - kneeNew;
    if (bcNew.getLength() < 1e-5f)
        return false;
    bcNew.normalize();

    core::quaternion dShin;
    dShin.rotationFromTo(bcCarried, bcNew);
    dShin.normalize();

    core::quaternion dShinW;
    dShinW.slerp(core::quaternion(), dShin, w);
    dShinW.normalize();

    core::quaternion legGlobalNew = (m_gRot[leg.leg] * dThighW) * dShinW;
    legGlobalNew.normalize();

    // --- Foot roll -----------------------------------------------------------
    // Rotate the foot as if the GROUND had tilted: the delta that takes model
    // up onto the surface normal. Rig-agnostic on purpose - it needs no
    // assumption about which of the foot joint's local axes points up, and
    // Mixamo's do not point anywhere convenient.
    core::quaternion footGlobalNew = (m_gRot[leg.foot] * dThighW) * dShinW;

    if (m_cfg.pitchWeight > 0.0f && normalModel.getLengthSQ() > 1e-6f)
    {
        core::vector3df up(0.0f, 1.0f, 0.0f);
        core::vector3df n = normalModel;
        n.normalize();

        core::quaternion dPitch;
        dPitch.rotationFromTo(up, n);

        // Cap the roll. slerp(identity, dPitch, t) turns through exactly t of the
        // full angle, so scaling the weight by maxAngle/angle IS the cap - no
        // second rotation to build. A downward ray that lands on a stair nosing
        // can come back with the riser's normal, and un-capped that lays the
        // foot on its side against the step.
        float scale = 1.0f;

        const float angle = acosf(clampf(up.dotProduct(n), -1.0f, 1.0f));
        const float limit = m_cfg.maxPitchDeg * 3.14159265f / 180.0f;

        if (angle > limit && angle > 1e-6f)
            scale = limit / angle;

        core::quaternion dPitchW;
        dPitchW.slerp(core::quaternion(), dPitch, w * m_cfg.pitchWeight * scale);
        dPitchW.normalize();

        footGlobalNew = footGlobalNew * dPitchW;
    }

    footGlobalNew.normalize();

    // --- Back to local -------------------------------------------------------
    // local = global * inverse(parentGlobal), which follows from
    // global = local * parentGlobal above. Each joint uses its parent's UPDATED
    // global, or the correction would be applied twice down the chain.
    core::quaternion invParent;

    const int upLegParent = m_parent[leg.upLeg];
    invParent = (upLegParent >= 0) ? m_gRot[upLegParent] : core::quaternion();
    invParent.makeInverse();
    pose.joints[leg.upLeg].Rotation = upLegGlobalNew * invParent;
    pose.joints[leg.upLeg].Rotation.normalize();

    invParent = upLegGlobalNew;
    invParent.makeInverse();
    pose.joints[leg.leg].Rotation = legGlobalNew * invParent;
    pose.joints[leg.leg].Rotation.normalize();

    invParent = legGlobalNew;
    invParent.makeInverse();
    pose.joints[leg.foot].Rotation = footGlobalNew * invParent;
    pose.joints[leg.foot].Rotation.normalize();

    return true;
}

void AnimFootIK::solve(AnimPose& pose, const core::matrix4& nodeToWorld,
                       float weightTarget, float seconds)
{
    if (!m_valid || pose.size() != m_parent.size())
        return;

    m_weight = approach(m_weight, clampf(weightTarget, 0.0f, 1.0f), m_cfg.adaptRate, seconds);

    // Below this the correction is invisible, but the smoothed offsets must keep
    // decaying toward zero or re-enabling the IK snaps from wherever it stopped.
    const bool contributing = (m_weight > 0.002f) && (m_probe != nullptr);

    if (!contributing)
    {
        for (int s = 0; s < 2; ++s)
        {
            m_offset[s]   = approach(m_offset[s], 0.0f, m_cfg.adaptRate, seconds);
            m_grounded[s] = false;
        }
        m_hipDrop = approach(m_hipDrop, 0.0f, m_cfg.adaptRate, seconds);
        return;
    }

    core::matrix4 worldToNode;
    if (!nodeToWorld.getInverse(worldToNode))
        return;

    composeGlobals(pose);

    // --- Probe ---------------------------------------------------------------
    core::vector3df targetWorld[2];

    for (int s = 0; s < 2; ++s)
    {
        const Leg& leg = m_legs[s];

        core::vector3df ankleWorld;
        nodeToWorld.transformVect(ankleWorld, m_gPos[leg.foot]);

        const bool useToe = m_cfg.toeProbe && (leg.toe >= 0) && (m_toeOffset > 0.0f);

        core::vector3df toeWorld = ankleWorld;
        if (useToe)
            nodeToWorld.transformVect(toeWorld, m_gPos[leg.toe]);

        // How far above its PLANTED height this clip has lifted the foot, in
        // mesh space. Ground-independent on purpose: it says what the animation
        // intends, so a planted foot reaching down a step and a swing foot at
        // the same height above the ground can be told apart. They cannot be,
        // measured against the world floor.
        const float clipLift = m_gPos[leg.foot].Y - m_soleOffset;
        const float stance   = 1.0f - clampf(clipLift / m_cfg.stanceFalloff, 0.0f, 1.0f);

        m_stance[s] = stance;

        // Where the ankle has to end up, in world Y, for the foot to rest on
        // what was found. TWO CONTACTS, AND THE HIGHER ONE WINS: one ray cannot
        // see a step edge, and a foot straddling a stair nosing placed from its
        // ankle alone is dropped onto the lower tread with its front half inside
        // the upper one.
        float requiredY = 0.0f;
        bool  found     = false;

        core::vector3df groundNormal(0.0f, 1.0f, 0.0f);

        core::vector3df hit, normal;

        FootDebug& dbg = m_dbg[s];
        dbg = FootDebug();
        dbg.ankleWorld = ankleWorld;
        dbg.toeWorld   = useToe ? toeWorld : ankleWorld;

        const float probeLen = m_cfg.probeUp + m_cfg.probeDown;

        // Ankle: the ankle sits its own sole offset above whatever is under it.
        {
            core::vector3df from = ankleWorld;
            from.Y += m_cfg.probeUp;

            dbg.ankle.from   = from;
            dbg.ankle.length = probeLen;
            dbg.ankle.fired  = true;

            if (m_probe->probeGround(from, probeLen, hit, normal))
            {
                dbg.ankle.valid  = true;
                dbg.ankle.hit    = hit;
                dbg.ankle.normal = normal;

                if (normal.Y >= m_cfg.minGroundNormalY)
                {
                    dbg.ankle.used = true;

                    requiredY    = hit.Y + m_soleOffset;
                    groundNormal = normal;
                    found        = true;
                }
            }
        }

        // Toe: where the ankle has to be for the BALL of the foot to rest on
        // what the toe found, with the foot kept at the pitch the clip gave it.
        // (ankleWorld.Y - toeWorld.Y) is that pitch, carried across unchanged.
        if (useToe)
        {
            core::vector3df from = toeWorld;
            from.Y += m_cfg.probeUp;

            dbg.toe.from   = from;
            dbg.toe.length = probeLen;
            dbg.toe.fired  = true;

            if (m_probe->probeGround(from, probeLen, hit, normal))
            {
                dbg.toe.valid  = true;
                dbg.toe.hit    = hit;
                dbg.toe.normal = normal;

                if (normal.Y >= m_cfg.minGroundNormalY)
                {
                    dbg.toe.used = true;

                    const float want = hit.Y + m_toeOffset + (ankleWorld.Y - toeWorld.Y);

                    if (!found || want > requiredY)
                    {
                        requiredY    = want;
                        groundNormal = normal;
                    }

                    found = true;
                }
            }
        }

        m_grounded[s] = found;

        float raw = 0.0f;

        if (found)
        {
            raw = requiredY - ankleWorld.Y;

            if (raw > 0.0f)
            {
                // Through the floor. Always lift it out - a foot inside geometry
                // is wrong in every phase of every clip.
                raw = clampf(raw, 0.0f, m_cfg.maxRaise);
            }
            else
            {
                // Above the floor. Only a PLANTED foot should be pulled down.
                raw = clampf(raw, -m_cfg.maxLower, 0.0f) * stance;
            }

            m_normalWorld[s] = groundNormal;
        }

        m_offset[s] = approach(m_offset[s], raw, m_cfg.adaptRate, seconds);

        targetWorld[s] = ankleWorld;
        targetWorld[s].Y += m_offset[s] * m_weight;

        dbg.targetWorld = targetWorld[s];

        worldToNode.transformVect(m_targetModel[s], targetWorld[s]);
    }

    // --- Hip drop ------------------------------------------------------------
    // Only ever downward, and only as far as the LOWER foot needs. Going up
    // would lift the character off the floor it is standing on; the legs
    // straightening is the correct answer there.
    // The offsets are already stance-weighted, so a swing foot contributes
    // nothing here. That matters more than it sounds: before the weighting, a
    // mid-stride foot passing through the band above the floor pulled the whole
    // pelvis down with it, and the PLANTED leg then had to fold to keep its own
    // (absolute) target. One foot through the floor, the other folded up behind
    // - which is exactly how the bug presented.
    float wantDrop = 0.0f;
    if (m_offset[0] < wantDrop) wantDrop = m_offset[0];
    if (m_offset[1] < wantDrop) wantDrop = m_offset[1];
    wantDrop = clampf(wantDrop, -m_cfg.maxHipDrop, 0.0f);

    m_hipDrop = approach(m_hipDrop, wantDrop, m_cfg.adaptRate, seconds);

    // The external pelvis offset rides along with it and is NOT damped again -
    // whatever produced it has already done that. It is the only term here that
    // may be positive.
    moveHips(pose, worldToNode, m_hipDrop * m_weight + m_pelvisOffset);

    // --- Buy enough drop for BOTH legs to reach ------------------------------
    // min(offset) gives the deepest foot exactly the room it needs and every
    // other foot nothing. That is fine when the legs have slack, and this rig
    // has almost none: the paladin stands at 96-99% of its leg length, so a
    // centimetre of shortfall on the shallower foot is already past full
    // extension, and the solver answers an unreachable target by pulling the
    // foot UP the line to the hip. Read on the floor that is a foot that
    // intermittently lifts for no visible reason.
    //
    // Dropping the pelvis by d shortens each hip-to-target span by very nearly
    // d, the targets being fixed in the world, so one pass gets within a
    // rounding error of the answer.
    float extra = 0.0f;

    for (int s = 0; s < 2; ++s)
    {
        core::vector3df hipWorld;
        float reach = 0.0f;

        legReach(m_legs[s], nodeToWorld, hipWorld, reach);

        const float need = (targetWorld[s] - hipWorld).getLength();

        if (need - reach > extra)
            extra = need - reach;
    }

    extra = clampf(extra, 0.0f, m_cfg.maxHipDrop);
    m_hipExtra = extra;

    moveHips(pose, worldToNode, -extra * m_weight);

    // --- Solve ---------------------------------------------------------------
    for (int s = 0; s < 2; ++s)
    {
        core::vector3df normalModel(0.0f, 1.0f, 0.0f);
        if (m_grounded[s])
            worldToNode.rotateVect(normalModel, m_normalWorld[s]);

        solveLeg(pose, m_legs[s], m_targetModel[s], normalModel, m_weight, m_bend[s]);
    }
}

bool AnimFootIK::footInfo(int side, float& offset, bool& grounded, float& stance) const
{
    if (side < 0 || side > 1 || !m_valid)
        return false;

    stance = m_stance[side];

    offset   = m_offset[side];
    grounded = m_grounded[side];
    return true;
}

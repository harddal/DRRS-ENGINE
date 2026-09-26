#pragma once

#include "Engine/Animation/AnimMask.h"

#include "irrlicht.h"

// A complete skeletal pose: one LOCAL TRS per joint, in
// ISkinnedMesh::getAllJoints() order.
//
// TRS WITH A REAL QUATERNION, NEVER MATRICES. Every operation this system grows
// — cross-fade, blend space, additive layer — is a per-joint lerp/slerp, and a
// matrix cannot be slerped. It is also exactly what CSkinnedMesh stores in
// SJoint::Animated*, so handing a pose to the mesh is a copy rather than a
// decomposition.
//
// irr::core::array rather than std::vector for one reason only: the finished
// pose goes straight into IAnimatedMeshSceneNode::setExternalPose() (engine
// fork #6) with no per-frame container conversion.
struct AnimPose
{
    irr::core::array<irr::scene::SJointPose> joints;

    // core::array::set_used() leaves the new slots RAW — it only reallocates,
    // it does not construct. push_back does construct, so the pose is built
    // that way and every entry is a real SJointPose from the first frame.
    void reset(irr::u32 count)
    {
        joints.clear();
        joints.reallocate(count);
        for (irr::u32 i = 0; i < count; ++i)
            joints.push_back(irr::scene::SJointPose());
    }

    irr::u32 size() const { return joints.size(); }
    bool     empty() const { return joints.size() == 0; }
};

// NORMALISE AFTER EVERY SLERP.
//
// core::quaternion::slerp falls back to a plain component lerp when the two
// inputs are within its threshold of parallel, and that lerp does NOT
// renormalise. A quaternion that drifts off unit length is not merely an
// inaccurate rotation - quaternion::operator*(vector3df) SCALES the vector by
// roughly its squared norm, so the bone it rotates changes length. Measured on a
// two-bone chain at half weight: 0.4% shorter, systematically, for as long as
// the blend is partial. It is small, it is free to fix, and it compounds down a
// chain.

// Fold 'src' into 'dst' at parameter t, per joint, IN LOCAL SPACE.
//
// This is the incremental normalised blend: to average N poses with weights
// w0..wN-1, start with pose 0 and fold each subsequent one in at
// t = wK / (sum of weights so far + wK). Two poses is just t = w1/(w0+w1),
// which is the ordinary cross-fade.
//
// Rotations go through core::quaternion::slerp, which negates one input when
// the dot product is negative — so the SHORTEST ARC is always taken. Miss that
// and a joint occasionally takes the long way round, which reads as a single
// glitched frame rather than as a blend bug.
//
// Never blend global/absolute transforms: the composition down the chain is
// what makes a blended pose look like a pose rather than like a pile of limbs.
inline void blendPoseInto(AnimPose& dst, const AnimPose& src, float t)
{
    if (t <= 0.0f || dst.size() != src.size())
        return;

    if (t >= 1.0f)
    {
        dst.joints = src.joints;
        return;
    }

    const irr::u32 count = dst.size();
    for (irr::u32 i = 0; i < count; ++i)
    {
        irr::scene::SJointPose&       a = dst.joints[i];
        const irr::scene::SJointPose& b = src.joints[i];

        a.Position = irr::core::lerp(a.Position, b.Position, t);
        a.Scale    = irr::core::lerp(a.Scale,    b.Scale,    t);

        const irr::core::quaternion from = a.Rotation;
        a.Rotation.slerp(from, b.Rotation, t);
        a.Rotation.normalize();
    }
}

// Per-joint masked override: fold 'src' into 'dst' at weight * mask[joint].
//
// This is what a LAYER does. The scalar blendPoseInto above cannot express it -
// a layer's whole purpose is that it reaches some joints fully, some partly and
// some not at all.
inline void overlayPoseMasked(AnimPose& dst, const AnimPose& src,
                              const AnimMask& mask, float weight)
{
    if (weight <= 0.0f || dst.size() != src.size())
        return;

    const irr::u32 count = dst.size();
    for (irr::u32 i = 0; i < count; ++i)
    {
        const float t = weight * mask.weight(i);
        if (t <= 0.0001f)
            continue;

        irr::scene::SJointPose&       a = dst.joints[i];
        const irr::scene::SJointPose& b = src.joints[i];

        if (t >= 0.9999f)
        {
            a = b;
            continue;
        }

        a.Position = irr::core::lerp(a.Position, b.Position, t);
        a.Scale    = irr::core::lerp(a.Scale,    b.Scale,    t);

        const irr::core::quaternion from = a.Rotation;
        a.Rotation.slerp(from, b.Rotation, t);
        a.Rotation.normalize();
    }
}

// Per-joint ADDITIVE layer: add (src - reference) on top of dst.
//
// An additive layer does not say "be this pose", it says "be whatever you were,
// plus this much lean / recoil / aim". The delta is taken against a REFERENCE
// FRAME of the layer's own clip - usually its first - so an additive clip
// authored from a neutral stance adds nothing at that frame and departs from it
// over time.
//
// Rotation composes, position and scale add. The rotation delta is
// inverse(reference) * current, scaled by slerping it away from identity; note
// that reduces to 'current' exactly when the base pose equals the reference,
// which holds under either of Irrlicht's two possible quaternion multiply
// orderings because the inverse cancels adjacently in both readings. For the
// small deltas an additive layer actually carries, the difference between the
// two readings is second order.
inline void addPoseMasked(AnimPose& dst, const AnimPose& src, const AnimPose& reference,
                          const AnimMask& mask, float weight)
{
    if (weight <= 0.0f || dst.size() != src.size() || dst.size() != reference.size())
        return;

    const irr::u32 count = dst.size();
    for (irr::u32 i = 0; i < count; ++i)
    {
        const float t = weight * mask.weight(i);
        if (t <= 0.0001f)
            continue;

        irr::scene::SJointPose&       a = dst.joints[i];
        const irr::scene::SJointPose& b = src.joints[i];
        const irr::scene::SJointPose& r = reference.joints[i];

        a.Position += (b.Position - r.Position) * t;
        a.Scale    += (b.Scale    - r.Scale)    * t;

        irr::core::quaternion inverse = r.Rotation;
        inverse.makeInverse();

        irr::core::quaternion delta = inverse * b.Rotation;

        irr::core::quaternion scaled;
        scaled.slerp(irr::core::quaternion(), delta, t);
        scaled.normalize();

        a.Rotation = a.Rotation * scaled;
        a.Rotation.normalize();
    }
}

// Ease a 0..1 blend weight. A LINEAR ramp reads as a slight slide, because the
// pose starts and stops moving instantaneously; smoothstep has zero derivative
// at both ends and reads as a transition.
inline float animEase(float t)
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

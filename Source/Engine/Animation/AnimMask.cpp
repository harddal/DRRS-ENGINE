#include "Engine/Animation/AnimMask.h"

#include "spdlog/spdlog.h"

#include <cstring>

using namespace irr;

namespace AnimMaskName
{
    const char* k_full      = "full";
    const char* k_upperBody = "upper_body";
    const char* k_lowerBody = "lower_body";
    const char* k_noArms    = "no_arms";
    const char* k_spineUp   = "spine_up";
    const char* k_arms      = "arms";
    const char* k_leftArm   = "left_arm";
    const char* k_rightArm  = "right_arm";
    const char* k_head      = "head";
    const char* k_legs      = "legs";
    const char* k_rightFingers = "right_fingers";
    const char* k_fingers      = "fingers";
}

namespace
{
    // Joint name tails. A rig whose joints are not named this way produces empty
    // masks; see the AnimMaskSet comment for why that fails loudly-but-safely.
    const char* k_rootHips[]   = { "Hips", nullptr };
    const char* k_rootSpine[]  = { "Spine", nullptr };
    const char* k_rootArms[]   = { "LeftShoulder", "RightShoulder", nullptr };
    const char* k_rootLeft[]   = { "LeftShoulder", nullptr };
    const char* k_rootRight[]  = { "RightShoulder", nullptr };
    const char* k_rootNeck[]   = { "Neck", nullptr };
    const char* k_rootLegs[]   = { "LeftUpLeg", "RightUpLeg", nullptr };
    const char* k_none[]       = { nullptr };

    // All five chains are named even though the paladin carries only two of
    // them. Mixamo's "no fingers" skeleton option does not remove the fingers -
    // it weights all four to the INDEX chain and drops the other three roots -
    // so one grip pose closes the whole hand on that rig and still works joint
    // for joint on a full one. build() needs only one root to match.
    const char* k_rootRightFingers[] = { "RightHandThumb1", "RightHandIndex1",
                                         "RightHandMiddle1", "RightHandRing1",
                                         "RightHandPinky1", nullptr };
    const char* k_rootFingers[]      = { "RightHandThumb1", "RightHandIndex1",
                                         "RightHandMiddle1", "RightHandRing1",
                                         "RightHandPinky1",
                                         "LeftHandThumb1", "LeftHandIndex1",
                                         "LeftHandMiddle1", "LeftHandRing1",
                                         "LeftHandPinky1", nullptr };

    const AnimMask::Def k_defs[] =
    {
        // Feather 2 in on anything rooted at the spine: that is the classic
        // 0.33 / 0.67 / 1.0 ramp down the vertebrae, and it is the difference
        // between an upper-body layer that blends and one that creases.
        { AnimMaskName::k_upperBody, k_rootSpine, k_none,     2, 0 },
        { AnimMaskName::k_spineUp,   k_rootSpine, k_rootArms, 2, 1 },

        // Rooted at the hips, so no feather in - the lower body wants the
        // layer's full authority on the joints it covers. The feather OUT
        // softens the one joint at the boundary so the shoulders do not snap.
        { AnimMaskName::k_lowerBody, k_rootHips,  k_rootSpine, 0, 1 },
        { AnimMaskName::k_noArms,    k_rootHips,  k_rootArms,  0, 1 },

        { AnimMaskName::k_arms,      k_rootArms,  k_none, 1, 0 },
        { AnimMaskName::k_leftArm,   k_rootLeft,  k_none, 1, 0 },
        { AnimMaskName::k_rightArm,  k_rootRight, k_none, 1, 0 },
        { AnimMaskName::k_head,      k_rootNeck,  k_none, 1, 0 },
        { AnimMaskName::k_legs,      k_rootLegs,  k_none, 0, 0 },

        // NO FEATHER IN. Every other mask ramps so the boundary joint does not
        // crease, but the boundary here is the wrist, and a wrist at even 0.5
        // of a grip pose drags the weapon parented to it out of the hand. The
        // crease a hard edge would cause has nowhere to show: the joint below it
        // is a knuckle, and a knuckle is meant to be a hinge.
        { AnimMaskName::k_rightFingers, k_rootRightFingers, k_none, 0, 0 },
        { AnimMaskName::k_fingers,      k_rootFingers,      k_none, 0, 0 },
    };

    bool nameEndsWith(const char* name, const char* tail)
    {
        if (!name || !tail)
            return false;

        const size_t n = strlen(name);
        const size_t t = strlen(tail);
        return (t <= n) && (strcmp(name + (n - t), tail) == 0);
    }

    // The ramp a joint 'depth' levels below an include root gets.
    float featherInWeight(int depth, int feather)
    {
        if (feather <= 0)
            return 1.0f;
        if (depth >= feather)
            return 1.0f;

        return static_cast<float>(depth + 1) / static_cast<float>(feather + 1);
    }

    // The ramp a joint 'depth' levels into an excluded subtree keeps.
    float featherOutWeight(int depth, int feather)
    {
        if (feather <= 0)
            return 0.0f;
        if (depth >= feather)
            return 0.0f;

        return static_cast<float>(feather - depth) / static_cast<float>(feather + 1);
    }
}

bool AnimMask::build(scene::ISkinnedMesh* mesh, const Def& def)
{
    m_name = def.name ? def.name : "";
    m_weights.clear();

    if (!mesh)
        return false;

    const core::array<scene::ISkinnedMesh::SJoint*>& joints = mesh->getAllJoints();
    const u32 count = joints.size();
    if (count == 0)
        return false;

    m_weights.assign(count, 0.0f);

    // SJoint::Children holds pointers, and everything else here is an index, so
    // the lookup is built once rather than searched per joint.
    std::vector<const scene::ISkinnedMesh::SJoint*> byIndex(count);
    for (u32 i = 0; i < count; ++i)
        byIndex[i] = joints[i];

    struct Local
    {
        static int indexOf(const std::vector<const scene::ISkinnedMesh::SJoint*>& all,
                           const scene::ISkinnedMesh::SJoint* j)
        {
            for (size_t i = 0; i < all.size(); ++i)
                if (all[i] == j)
                    return static_cast<int>(i);
            return -1;
        }

        // Walks a subtree writing the ramped weight at each level. Iterative
        // with an explicit stack: a rig is shallow, but a corrupt one with a
        // cycle would take a recursive version down with it.
        static void paint(const std::vector<const scene::ISkinnedMesh::SJoint*>& all,
                          std::vector<float>& out, int root, int feather, bool rampIn)
        {
            if (root < 0)
                return;

            std::vector<std::pair<int, int> > stack;   // joint, depth
            stack.push_back(std::make_pair(root, 0));

            while (!stack.empty())
            {
                const int index = stack.back().first;
                const int depth = stack.back().second;
                stack.pop_back();

                const float w = rampIn ? featherInWeight(depth, feather)
                                       : featherOutWeight(depth, feather);

                if (rampIn)
                {
                    // Overlapping include roots take the strongest claim.
                    if (w > out[index])
                        out[index] = w;
                }
                else
                {
                    // An exclude can only ever remove weight.
                    if (w < out[index])
                        out[index] = w;
                }

                const scene::ISkinnedMesh::SJoint* joint = all[index];
                for (u32 c = 0; c < joint->Children.size(); ++c)
                {
                    const int child = indexOf(all, joint->Children[c]);
                    if (child >= 0)
                        stack.push_back(std::make_pair(child, depth + 1));
                }
            }
        }
    };

    bool anyRoot = false;

    for (const char* const* n = def.include; n && *n; ++n)
    {
        for (u32 i = 0; i < count; ++i)
        {
            if (!nameEndsWith(joints[i]->Name.c_str(), *n))
                continue;

            Local::paint(byIndex, m_weights, static_cast<int>(i), def.featherIn, true);
            anyRoot = true;
        }
    }

    if (!anyRoot)
    {
        // Nothing matched. Refuse rather than hand back an all-zero mask, which
        // would silently delete whatever the layer was supposed to drive.
        m_weights.clear();
        return false;
    }

    for (const char* const* n = def.exclude; n && *n; ++n)
        for (u32 i = 0; i < count; ++i)
            if (nameEndsWith(joints[i]->Name.c_str(), *n))
                Local::paint(byIndex, m_weights, static_cast<int>(i), def.featherOut, false);

    return true;
}

bool AnimMask::buildFull(scene::ISkinnedMesh* mesh, const char* name)
{
    m_name = name ? name : "";
    m_weights.clear();

    if (!mesh || mesh->getJointCount() == 0)
        return false;

    m_weights.assign(mesh->getJointCount(), 1.0f);
    return true;
}

bool AnimMaskSet::build(scene::ISkinnedMesh* mesh)
{
    m_masks.clear();

    // "full" is built by joint COUNT, not by walking a named root. Measured on
    // paladin.glb: the Hips subtree is 41 joints and matches the skin exactly
    // today, but tying the default mask to a rig's topology is the kind of
    // assumption that silently drops joints on the next asset.
    {
        AnimMask full;
        if (full.buildFull(mesh, AnimMaskName::k_full))
            m_masks.push_back(full);
    }

    const int defCount = static_cast<int>(sizeof(k_defs) / sizeof(k_defs[0]));

    for (int i = 0; i < defCount; ++i)
    {
        AnimMask mask;
        if (mask.build(mesh, k_defs[i]))
            m_masks.push_back(mask);
    }

    if (m_masks.empty())
    {
        spdlog::warn("AnimMaskSet: no mask matched this skeleton - joints are not "
                     "named like a Mixamo rig, so masked layers will be ignored");
        return false;
    }

    return true;
}

const AnimMask* AnimMaskSet::find(const char* name) const
{
    if (!name || !*name)
        return nullptr;

    for (size_t i = 0; i < m_masks.size(); ++i)
        if (m_masks[i].name() == name)
            return &m_masks[i];

    return nullptr;
}

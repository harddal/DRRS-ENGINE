#pragma once

#include "irrlicht.h"

#include <string>
#include <vector>

// A per-joint weight array: how much of a layer reaches each joint.
//
// Resolved ONCE, at bind, from joint NAMES. Joint indices are a property of the
// loaded mesh and mean nothing across assets, so a mask is described by the
// names of the subtree roots it covers and looked up by a stable label
// ("upper_body", "left_arm") everywhere else.
//
// FEATHERED, NOT BINARY. A mask that steps from 0 to 1 between two adjacent
// joints reads as a hinge: the character visibly creases at whichever vertebra
// the boundary landed on. Weights therefore ramp over a couple of joints at each
// edge, which is the same thing a hand-authored mask does (Spine 0.33, Spine1
// 0.67, Spine2 1.0) without anyone having to author it.
class AnimMask
{
public:
    // Named roots are matched by NAME TAIL, so "Spine" finds "mixamorig:Spine"
    // and, correctly, does not find "mixamorig:Spine1" - the subtree walk picks
    // that up instead.
    struct Def
    {
        const char* name;

        // Subtree roots that the mask covers, and subtree roots carved back out
        // of it. Null-terminated arrays.
        const char* const* include;
        const char* const* exclude;

        // Levels over which the weight ramps up from an include root (1/(n+1)
        // at the root, reaching 1.0 n levels down) and down into an exclude
        // root. 0 for a hard edge.
        int featherIn;
        int featherOut;
    };

    bool build(irr::scene::ISkinnedMesh* mesh, const Def& def);

    // Every joint at 1.0, with no dependence on joint names or rig topology.
    // The default mask has to be correct on ANY skeleton, including one whose
    // joint list holds nodes outside the skinned hierarchy.
    bool buildFull(irr::scene::ISkinnedMesh* mesh, const char* name);

    bool valid() const { return !m_weights.empty(); }
    irr::u32 size() const { return static_cast<irr::u32>(m_weights.size()); }

    float weight(irr::u32 joint) const
    {
        return (joint < m_weights.size()) ? m_weights[joint] : 0.0f;
    }

    const std::string& name() const { return m_name; }

private:
    std::string        m_name;
    std::vector<float> m_weights;
};

// The standard masks, built for whatever skeleton a character binds to.
//
// The definitions are MIXAMO-SHAPED (Hips / Spine / LeftShoulder / Neck /
// LeftUpLeg). Every skinned character in this project comes through
// Tools/mixamo_to_glb.py or Tools/monsters_to_glb.py and carries those names.
// A rig that does not will silently produce empty masks - build() returns false
// and the layer simply does not apply, rather than masking to nothing and
// deleting the character's upper body.
class AnimMaskSet
{
public:
    bool build(irr::scene::ISkinnedMesh* mesh);
    void clear() { m_masks.clear(); }

    // Null for an unknown name, which callers treat as "full body".
    const AnimMask* find(const char* name) const;

    int  count() const { return static_cast<int>(m_masks.size()); }
    const AnimMask& at(int i) const { return m_masks[i]; }

private:
    std::vector<AnimMask> m_masks;
};

// Stable labels. Not an enum, because these cross into cvars and eventually into
// .ent data, where a string survives a reorder and an integer does not.
namespace AnimMaskName
{
    extern const char* k_full;        // every joint at 1.0; an unmasked layer
    extern const char* k_upperBody;   // spine and everything above it, incl. arms
    extern const char* k_lowerBody;   // hips and legs, nothing above the spine
    extern const char* k_noArms;      // everything except the two arm chains
    extern const char* k_spineUp;     // spine, neck and head, WITHOUT the arms
    extern const char* k_arms;        // both arm chains
    extern const char* k_leftArm;
    extern const char* k_rightArm;
    extern const char* k_head;
    extern const char* k_legs;

    // The finger chains only, with a HARD edge at the wrist. A grip pose must
    // not reach the hand joint itself: a weapon is parented to that joint, so
    // any weight there rotates the weapon instead of closing the hand round it.
    extern const char* k_rightFingers;
    extern const char* k_fingers;      // both hands
}

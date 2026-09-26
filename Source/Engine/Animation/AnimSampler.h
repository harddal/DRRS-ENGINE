#pragma once

#include "Engine/Animation/AnimPose.h"

#include "irrlicht.h"

#include <vector>

// Samples one skinned mesh's keyframes into an AnimPose.
//
// WHY THIS IS NOT CSkinnedMesh::getFrameData
//
//  1. It is private, and more importantly its search hints live on SJoint —
//     ONE set, shared by every caller. Two clip players reading distant regions
//     of the concatenated timeline make every hint miss, and Irrlicht's miss
//     path is a full linear scan of the entire key array, per joint, per call.
//     This sampler owns its hints, so each player keeps its own place.
//
//  2. Irrlicht searches ALL keys. Every clip in a glTF import shares one
//     concatenated timeline, so sampling near a clip's end interpolates into
//     the NEXT clip's first keyframe. That is masked today only because
//     playback stops at EndFrame, and it stops being masked the moment two
//     clips are blended. The search here is clamped to the clip's own frame
//     window.
//
//     This is also the one place where phase 1 is deliberately NOT bit-identical
//     to the old path. The glTF importer backfills a single static key for any
//     channel a clip does not animate (GltfImport.cpp, the targetedChannels
//     pass), so a rotation-only joint has exactly one position key inside its
//     clip — and Irrlicht would happily interpolate that toward a key belonging
//     to a different clip entirely. Clamping is the fix, not a behaviour change
//     anyone asked for.
//
//  3. A looping clip wraps against its OWN first key rather than against
//     whatever follows it on the timeline. Irrlicht sidesteps this by requiring
//     "the last frame must be identical to the first one".
class AnimSampler
{
public:
    // Caches the joint list, allocates this sampler's own hints, and captures
    // the bind pose as the per-joint fallback for channels with no keys.
    bool bind(irr::scene::ISkinnedMesh* mesh);

    bool     isBound() const { return m_mesh != nullptr; }
    irr::u32 jointCount() const { return static_cast<irr::u32>(m_hints.size()); }

    // Fill 'out' with the pose at 'frame'. Only keys inside
    // [firstFrame, lastFrame] are considered; 'loop' decides whether the tail of
    // the window interpolates back toward its first key or simply clamps.
    //
    // 'out' is resized if it does not already match the joint count.
    void sample(float frame, float firstFrame, float lastFrame, bool loop, AnimPose& out);

private:
    // Index of the first and last key of each channel that falls inside the
    // current clip window, plus this sampler's own search cursor.
    //
    // lo/hi are recomputed only when the window changes, which for a locomotion
    // clip is once per clip change rather than once per frame.
    struct JointChannel
    {
        irr::s32 lo   = 0;
        irr::s32 hi   = -1;   // hi < lo means "no keys in this window"
        irr::s32 hint = 0;
    };

    struct JointHints
    {
        JointChannel position;
        JointChannel rotation;
        JointChannel scale;
    };

    void rebuildWindows(float firstFrame, float lastFrame);

    irr::scene::ISkinnedMesh* m_mesh = nullptr;

    std::vector<JointHints> m_hints;

    // Per-joint fallback, decomposed from SJoint::LocalMatrix at bind time.
    // Only ever visible for a channel with no keys at all inside the window —
    // and for a joint with no keys anywhere CSkinnedMesh ignores the pose and
    // uses LocalMatrix directly, so this is belt and braces.
    AnimPose m_bindPose;

    float m_windowFirst = 1.0f;   // deliberately first > last: forces the
    float m_windowLast  = 0.0f;   // first sample() call to build the windows
};

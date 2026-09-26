#include "Engine/Animation/AnimSampler.h"

using namespace irr;

namespace
{
    // Last index in [lo,hi] whose key frame is <= 'frame', found by walking from
    // the previous answer.
    //
    // A walk rather than a binary search on purpose: a cursor advancing one
    // frame at a time moves the answer by at most one step, so the steady-state
    // cost is a single comparison. The walk is bounded by the CLIP WINDOW (tens
    // of keys), not by the full concatenated timeline (thousands), so even a
    // hard seek to the far end of a clip is cheap.
    template <typename KeyT>
    s32 advanceHint(const core::array<KeyT>& keys, float frame, s32 lo, s32 hi, s32& hint)
    {
        s32 i = hint;
        if (i < lo || i > hi)
            i = lo;

        while (i < hi && keys[i + 1].frame <= frame) ++i;
        while (i > lo && keys[i].frame     >  frame) --i;

        hint = i;
        return i;
    }

    // First index whose key frame is >= value (std::lower_bound over core::array).
    template <typename KeyT>
    s32 lowerBound(const core::array<KeyT>& keys, float value)
    {
        s32 lo = 0;
        s32 hi = static_cast<s32>(keys.size());
        while (lo < hi)
        {
            const s32 mid = lo + (hi - lo) / 2;
            if (keys[mid].frame < value) lo = mid + 1;
            else                         hi = mid;
        }
        return lo;
    }

    // Last index whose key frame is <= value, or -1 when there is none.
    template <typename KeyT>
    s32 upperBoundMinusOne(const core::array<KeyT>& keys, float value)
    {
        s32 lo = 0;
        s32 hi = static_cast<s32>(keys.size());
        while (lo < hi)
        {
            const s32 mid = lo + (hi - lo) / 2;
            if (keys[mid].frame <= value) lo = mid + 1;
            else                          hi = mid;
        }
        return lo - 1;
    }

    struct Bracket
    {
        s32   a = 0;
        s32   b = 0;
        float t = 0.0f;
    };

    // The two keys straddling 'frame', plus the parameter between them.
    //
    // When 'a' is the last key in the window and the clip loops, the successor
    // is the window's FIRST key one period later. That is what keeps a looping
    // clip interpolating against itself rather than against whatever clip
    // happens to sit next on the shared timeline.
    template <typename KeyT>
    Bracket bracketHinted(const core::array<KeyT>& keys, float frame,
                          s32 lo, s32 hi, s32& hint, bool loop, float period)
    {
        Bracket br;
        br.a = advanceHint(keys, frame, lo, hi, hint);

        if (br.a < hi)
        {
            br.b = br.a + 1;
            const float span = keys[br.b].frame - keys[br.a].frame;
            br.t = (span > 0.0f) ? (frame - keys[br.a].frame) / span : 0.0f;
        }
        else if (loop && hi > lo)
        {
            br.b = lo;
            const float span = (keys[lo].frame + period) - keys[hi].frame;
            br.t = (span > 0.0f) ? (frame - keys[hi].frame) / span : 0.0f;
        }
        else
        {
            br.b = br.a;
            br.t = 0.0f;
        }

        if (br.t < 0.0f) br.t = 0.0f;
        if (br.t > 1.0f) br.t = 1.0f;
        return br;
    }

    // Index range of the keys inside [first,last]. When a channel has no key at
    // all in the window - which the glTF importer's static backfill makes
    // unlikely but not impossible - fall back to the single nearest key, so the
    // joint holds a real transform instead of collapsing to the bind pose.
    template <typename KeyT>
    void windowFor(const core::array<KeyT>& keys, float first, float last, s32& lo, s32& hi)
    {
        if (keys.size() == 0) { lo = 0; hi = -1; return; }

        lo = lowerBound(keys, first);
        hi = upperBoundMinusOne(keys, last);

        if (lo > hi)
        {
            s32 nearest = upperBoundMinusOne(keys, first);
            if (nearest < 0) nearest = 0;
            if (nearest >= static_cast<s32>(keys.size())) nearest = static_cast<s32>(keys.size()) - 1;
            lo = hi = nearest;
        }
    }
}

bool AnimSampler::bind(scene::ISkinnedMesh* mesh)
{
    m_mesh = nullptr;
    m_hints.clear();
    m_bindPose.reset(0);
    m_windowFirst = 1.0f;
    m_windowLast  = 0.0f;

    if (!mesh || mesh->getJointCount() == 0)
        return false;

    m_mesh = mesh;

    const u32 count = mesh->getJointCount();
    m_hints.resize(count);
    m_bindPose.reset(count);

    const core::array<scene::ISkinnedMesh::SJoint*>& joints = mesh->getAllJoints();
    for (u32 i = 0; i < count; ++i)
    {
        const core::matrix4& local = joints[i]->LocalMatrix;

        m_bindPose.joints[i].Position = local.getTranslation();
        m_bindPose.joints[i].Scale    = local.getScale();
        m_bindPose.joints[i].Rotation = core::quaternion(local);
    }

    return true;
}

void AnimSampler::rebuildWindows(float firstFrame, float lastFrame)
{
    const core::array<scene::ISkinnedMesh::SJoint*>& joints = m_mesh->getAllJoints();

    for (u32 i = 0; i < m_hints.size(); ++i)
    {
        // Read the keys through getAnimationSource(): finalize() points every
        // joint at itself, but useAnimationFrom() (the .psk/.psa path) re-points
        // it at another mesh's joint, and getFrameData does exactly the same.
        const scene::ISkinnedMesh::SJoint* src = joints[i]->getAnimationSource();
        JointHints& h = m_hints[i];

        windowFor(src->PositionKeys, firstFrame, lastFrame, h.position.lo, h.position.hi);
        windowFor(src->RotationKeys, firstFrame, lastFrame, h.rotation.lo, h.rotation.hi);
        windowFor(src->ScaleKeys,    firstFrame, lastFrame, h.scale.lo,    h.scale.hi);

        h.position.hint = h.position.lo;
        h.rotation.hint = h.rotation.lo;
        h.scale.hint    = h.scale.lo;
    }

    m_windowFirst = firstFrame;
    m_windowLast  = lastFrame;
}

void AnimSampler::sample(float frame, float firstFrame, float lastFrame, bool loop, AnimPose& out)
{
    if (!m_mesh)
        return;

    const u32 count = static_cast<u32>(m_hints.size());
    if (out.size() != count)
        out.reset(count);

    if (firstFrame != m_windowFirst || lastFrame != m_windowLast)
        rebuildWindows(firstFrame, lastFrame);

    const float period = lastFrame - firstFrame;

    const core::array<scene::ISkinnedMesh::SJoint*>& joints = m_mesh->getAllJoints();

    for (u32 i = 0; i < count; ++i)
    {
        const scene::ISkinnedMesh::SJoint* src = joints[i]->getAnimationSource();
        JointHints& h = m_hints[i];

        scene::SJointPose& p = out.joints[i];
        p = m_bindPose.joints[i];

        if (h.position.hi >= h.position.lo && src->PositionKeys.size())
        {
            const Bracket br = bracketHinted(src->PositionKeys, frame,
                                             h.position.lo, h.position.hi, h.position.hint,
                                             loop, period);
            p.Position = core::lerp(src->PositionKeys[br.a].position,
                                    src->PositionKeys[br.b].position, br.t);
        }

        if (h.rotation.hi >= h.rotation.lo && src->RotationKeys.size())
        {
            const Bracket br = bracketHinted(src->RotationKeys, frame,
                                             h.rotation.lo, h.rotation.hi, h.rotation.hint,
                                             loop, period);
            // core::quaternion::slerp already negates one input when the dot
            // product is negative, so the shortest arc is taken and no single
            // bone takes the long way round.
            p.Rotation.slerp(src->RotationKeys[br.a].rotation,
                             src->RotationKeys[br.b].rotation, br.t);
        }

        if (h.scale.hi >= h.scale.lo && src->ScaleKeys.size())
        {
            const Bracket br = bracketHinted(src->ScaleKeys, frame,
                                             h.scale.lo, h.scale.hi, h.scale.hint,
                                             loop, period);
            p.Scale = core::lerp(src->ScaleKeys[br.a].scale,
                                 src->ScaleKeys[br.b].scale, br.t);
        }
    }
}

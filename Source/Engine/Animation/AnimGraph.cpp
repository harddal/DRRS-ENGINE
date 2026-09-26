#include "Engine/Animation/AnimGraph.h"

#include "Engine/World/Components/MeshComponent.h"

#include "spdlog/spdlog.h"

#include <cmath>

using namespace irr;

bool AnimGraph::s_blendEnabled = true;

namespace
{
    // Below this a slot or a state contributes nothing visible and is retired.
    // Not zero: a weight decaying toward zero over a fixed ramp reaches it
    // exactly, but floating-point accumulation can leave a hair behind and the
    // slot would never free.
    const float k_weightEpsilon = 0.0005f;

    // Safety rails on the derived playback rate, NOT a feel knob.
    //
    // The rate falls out of planarSpeed / blended stride length, so at any speed
    // the character can actually reach it lands near 1.0 by construction (walk
    // 2.2 -> 1.04, run 6.0 -> 1.31). These only bite when something outside the
    // locomotion model moves the body - being shoved, a conveyor, a scripted
    // slide - where an unclamped rate would either freeze the legs or blur them.
    const float k_rateMin = 0.50f;
    const float k_rateMax = 1.75f;

    float fract01(float v)
    {
        v -= std::floor(v);
        return (v < 0.0f) ? 0.0f : (v >= 1.0f ? 0.0f : v);
    }
}

bool AnimGraph::bind(MeshComponent& mc)
{
    if (!mc.node || !mc.isAnimated)
    {
        m_bound = false;
        return false;
    }

    // Already bound to this exact node and mesh: nothing to do. The mesh
    // pointer is compared too, because setMesh() on a live node would leave the
    // samplers holding another skeleton's joint list.
    scene::IAnimatedMesh* animated = mc.node->getMesh();
    if (m_bound && m_node == mc.node && animated == static_cast<scene::IAnimatedMesh*>(m_skinned))
        return true;

    m_bound   = false;
    m_node    = nullptr;
    m_skinned = nullptr;

    if (!animated || animated->getMeshType() != scene::EAMT_SKINNED)
        return false;

    scene::ISkinnedMesh* skinned = static_cast<scene::ISkinnedMesh*>(animated);

    const u32 jointCount = skinned->getJointCount();

    for (int i = 0; i < k_maxPlayers; ++i)
    {
        // EVERY SLOT GETS ITS OWN SAMPLER. That is the whole reason the sampler
        // exists rather than CSkinnedMesh::getFrameData: the built-in search
        // hints live on SJoint, one set shared by every caller, so cursors in
        // distant regions of the concatenated timeline would make each other's
        // hint miss and fall back to a full linear scan.
        if (!m_slots[i].sampler.bind(skinned))
            return false;

        m_slots[i].pose.reset(jointCount);
        m_slots[i].active  = false;
        m_slots[i].touched = false;
        m_slots[i].state   = -1;
        m_slots[i].share   = 0.0f;
        m_slots[i].clipName.clear();
    }

    for (int i = 0; i < k_maxStates; ++i)
        m_states[i] = State();

    for (int i = 0; i < k_maxLayers; ++i)
    {
        if (!m_layers[i].sampler.bind(skinned))
            return false;

        m_layers[i].pose.reset(jointCount);
        m_layers[i].reference.reset(jointCount);
        m_layers[i].active = false;
        m_layers[i].weight = 0.0f;
        m_layers[i].target = 0.0f;
        m_layers[i].mask   = nullptr;
        m_layers[i].clipName.clear();
        m_layers[i].maskName.clear();
    }

    // Masks are per-MESH: they are joint-index arrays, and joint indices mean
    // nothing across assets. Built once here, looked up by name thereafter.
    m_masks.build(skinned);

    // Same for the IK: joint indices, a parent table and a topological order,
    // all resolved once. bind() returns false on a rig it does not recognise and
    // the pass then does nothing, which is the right failure for a correction.
    m_footIK.bind(skinned);
    m_footIK.reset();

    m_node    = mc.node;
    m_skinned = skinned;
    m_fps     = (mc.fps > 0) ? static_cast<float>(mc.fps) : 30.0f;

    m_pose.reset(jointCount);

    m_resolved  = AnimClipRef();
    m_current   = -1;
    m_phase     = 0.0f;
    m_phaseRate = 0.0f;
    m_playing   = false;
    m_bound     = true;

    return true;
}

// Resolve a clip name into m_resolved, caching the last answer.
// MeshComponent::findAnimation is a linear scan with string compares and this is
// called every frame, for every member of the blend set.
bool AnimGraph::resolve(MeshComponent& mc, const char* clipName)
{
    if (m_resolved.name == clipName)
        return true;

    const sAnimationData* clip = mc.findAnimation(clipName);
    if (!clip)
    {
        // Warn once per name. Unguarded this fires every frame, and the clip
        // that is missing is by definition the one the character wants to be
        // playing right now. Leaves the existing pose alone: a missing clip must
        // not also stop whatever IS playing.
        if (m_missingClip != clipName)
        {
            spdlog::warn("AnimGraph: mesh '{}' has no animation '{}'", mc.mesh, clipName);
            m_missingClip = clipName;
        }
        return false;
    }

    m_resolved.name      = clipName;
    m_resolved.baseFrame = clip->frames.X;
    m_resolved.lastFrame = clip->frames.Y;
    m_resolved.loop      = clip->loop;
    return true;
}

// Keep the component's own record in step: Bindings_Render exposes
// lastPlayedAnimation.name to script, and CharacterBehavior::playAnim reads it
// as its re-issue guard.
//
// NOT done inside resolve(). playBlend resolves every member of the set once
// per frame, so doing it there would leave the record naming whichever clip
// happened to be last in the array rather than the one actually dominating.
void AnimGraph::publishActive(MeshComponent& mc, const char* clipName)
{
    // Guarded because playBlend runs this every fixed step and findAnimation is
    // a linear scan with string compares.
    if (mc.lastPlayedAnimation.name == clipName)
        return;

    const sAnimationData* clip = mc.findAnimation(clipName);
    if (clip)
        mc.lastPlayedAnimation = *clip;
}

// Start a new state and set every other one fading out. A zero fade is a hard
// cut: the new state takes the whole pose immediately and the rest are dropped.
int AnimGraph::newState(bool blend, float fadeSeconds)
{
    const float fade = blendEnabled() ? fadeSeconds : 0.0f;

    int slot = -1;
    for (int i = 0; i < k_maxStates; ++i)
    {
        if (!m_states[i].active) { slot = i; break; }
    }

    if (slot < 0)
    {
        // Every state busy. Evict the lightest one that is not current; its
        // contribution is by definition the least visible.
        int worst = -1;
        for (int i = 0; i < k_maxStates; ++i)
        {
            if (i == m_current) continue;
            if (worst < 0 || m_states[i].weight < m_states[worst].weight)
                worst = i;
        }
        slot = (worst >= 0) ? worst : 0;

        for (int i = 0; i < k_maxPlayers; ++i)
            if (m_slots[i].state == slot)
                retireSlot(m_slots[i]);
    }

    m_states[slot]        = State();
    m_states[slot].active = true;
    m_states[slot].blend  = blend;
    m_states[slot].target = 1.0f;
    m_states[slot].weight = 0.0f;

    if (fade <= 0.0f)
    {
        for (int i = 0; i < k_maxStates; ++i)
        {
            if (i == slot) continue;
            for (int j = 0; j < k_maxPlayers; ++j)
                if (m_slots[j].state == i)
                    retireSlot(m_slots[j]);
            m_states[i] = State();
        }

        m_states[slot].weight = 1.0f;
        m_states[slot].rate   = 0.0f;
    }
    else
    {
        // ONE ramp rate for the whole transition. The outgoing states must reach
        // zero as the incoming reaches one, or the normalisation quietly
        // rescales the result part-way through the fade.
        const float ramp = 1.0f / fade;

        m_states[slot].rate = ramp;
        for (int i = 0; i < k_maxStates; ++i)
        {
            if (i == slot || !m_states[i].active) continue;
            m_states[i].target = 0.0f;
            m_states[i].rate   = ramp;
        }
    }

    m_current = slot;
    m_playing = true;
    return slot;
}

void AnimGraph::retireSlot(Slot& s)
{
    s.active  = false;
    s.touched = false;
    s.state   = -1;
    s.share   = 0.0f;
}

// Find the slot in 'state' already playing this segment, or take a free one.
//
// Reusing rather than restarting is what makes an interrupted transition free:
// walk -> idle -> walk inside 0.2s finds the same slot and keeps BOTH its weight
// and its cursor, so there is no pop and no phase reset.
int AnimGraph::acquireSlot(int state, int begin, int end, bool loop, bool reverse)
{
    for (int i = 0; i < k_maxPlayers; ++i)
    {
        const Slot& s = m_slots[i];
        if (s.active && s.state == state &&
            s.player.begin == begin && s.player.end == end &&
            s.player.loop == loop && s.reverse == reverse)
            return i;
    }

    for (int i = 0; i < k_maxPlayers; ++i)
        if (!m_slots[i].active)
            return i;

    // Full. Drop the lightest slot that is not in the current state.
    int worst = -1;
    for (int i = 0; i < k_maxPlayers; ++i)
    {
        if (m_slots[i].state == m_current) continue;
        if (worst < 0 || m_slots[i].share < m_slots[worst].share)
            worst = i;
    }

    return (worst >= 0) ? worst : 0;
}

bool AnimGraph::play(MeshComponent& mc, const char* clipName,
                     int localBegin, int localEnd, bool loop, float rate,
                     float fadeSeconds)
{
    if (!clipName)
        return false;

    // Bind on demand. The controller runs in the fixed-step loop and
    // AnimationSystem in the pre-draw pass, so the first play() of a scene
    // arrives before the system has ever seen this entity - and the scene node
    // itself is created by RenderSystem some frames after the component exists.
    if (!m_bound && !bind(mc))
        return false;

    if (!resolve(mc, clipName))
        return false;

    const int begin = m_resolved.baseFrame + localBegin;
    const int end   = (localEnd < 0) ? m_resolved.lastFrame : (m_resolved.baseFrame + localEnd);

    // Already in this exact single-clip state: only the rate can have moved.
    // THIS IS THE COMMON CASE - the controller calls play() every fixed step -
    // and it must not restart anything.
    if (m_current >= 0 && m_states[m_current].active && !m_states[m_current].blend)
    {
        for (int i = 0; i < k_maxPlayers; ++i)
        {
            Slot& s = m_slots[i];
            if (s.active && s.state == m_current &&
                s.player.begin == begin && s.player.end == end && s.player.loop == loop)
            {
                s.player.rate = rate;
                s.clipName    = clipName;
                s.baseFrame   = m_resolved.baseFrame;
                s.lastFrame   = m_resolved.lastFrame;
                m_playing     = true;
                return true;   // already published on the frame this state began
            }
        }
    }

    const int state = newState(false, fadeSeconds);
    const int slot  = acquireSlot(state, begin, end, loop, false);

    Slot& s = m_slots[slot];
    s.player = AnimClipPlayer();
    s.player.play(begin, end, loop, rate);
    s.clipName    = clipName;
    s.baseFrame   = m_resolved.baseFrame;
    s.lastFrame   = m_resolved.lastFrame;
    s.state       = state;
    s.share       = 1.0f;
    s.phaseOffset = -1.0f;
    s.reverse     = false;
    s.synced      = false;
    s.active      = true;
    s.touched     = true;

    publishActive(mc, clipName);
    return true;
}

bool AnimGraph::playBlend(MeshComponent& mc, const AnimBlendEntry* entries, int count,
                          float planarSpeed, float fadeSeconds)
{
    if (!entries || count <= 0)
        return false;

    if (!m_bound && !bind(mc))
        return false;

    // anim_blend 0 is the whole-feature off switch, so it collapses the set to
    // its single heaviest member as well as hard-cutting every state transition
    // and unsyncing the phase. That reproduces the pre-blend behaviour: one clip
    // at a time, chosen by a threshold, snapping when the threshold is crossed.
    //
    // It will FLAP at a blend-space boundary where the old code did not, because
    // the tier hysteresis and the quadrant dwell that used to hide that are gone.
    // That is not a regression in the switch - it is the demonstration of what
    // those two constants were paying for.
    AnimBlendEntry dominant;
    if (!blendEnabled())
    {
        int best = -1;
        for (int e = 0; e < count; ++e)
            if (entries[e].clip && (best < 0 || entries[e].share > entries[best].share))
                best = e;

        if (best < 0)
            return false;

        dominant       = entries[best];
        dominant.share = 1.0f;
        entries        = &dominant;
        count          = 1;
    }

    // Enter the blend state only if we are not already in one. From then on the
    // set is updated in place: the SHARES are what move, continuously, so a clip
    // joining the set joins at share ~0 and needs no fade of its own.
    const bool alreadyBlending = (m_current >= 0 && m_states[m_current].active &&
                                  m_states[m_current].blend);

    const int state = alreadyBlending ? m_current : newState(true, fadeSeconds);

    for (int i = 0; i < k_maxPlayers; ++i)
        if (m_slots[i].state == state)
            m_slots[i].touched = false;

    // --- Install / update every member --------------------------------------
    float shareSum  = 0.0f;   // synced members only
    float strideSum = 0.0f;   // share-weighted world units per cycle
    float cycleSum  = 0.0f;   // share-weighted cycle length in seconds

    for (int e = 0; e < count; ++e)
    {
        const AnimBlendEntry& entry = entries[e];

        // share > 0 and finite. The blend space is caller-computed, and a share
        // that is NaN or absurd does not merely look wrong - it poisons
        // strideSum, which poisons the shared phase, which sends every cursor in
        // the group to NaN. Skipping it here keeps one bad coordinate from
        // taking the whole character down with it.
        if (!entry.clip || !(entry.share > 0.0f) || entry.share > 1000.0f)
            continue;

        if (!resolve(mc, entry.clip))
            continue;

        const int begin = m_resolved.baseFrame;
        const int end   = m_resolved.lastFrame;
        if (end <= begin)
            continue;

        const int   index = acquireSlot(state, begin, end, true, entry.reverse);
        Slot&       s     = m_slots[index];
        const bool  fresh = !(s.active && s.state == state &&
                              s.player.begin == begin && s.player.end == end &&
                              s.reverse == entry.reverse);

        if (fresh)
        {
            s.player = AnimClipPlayer();
            s.player.play(begin, end, true, 1.0f);
        }

        s.clipName    = entry.clip;
        s.baseFrame   = m_resolved.baseFrame;
        s.lastFrame   = m_resolved.lastFrame;
        s.state       = state;
        s.share       = entry.share;
        s.phaseOffset = entry.phaseOffset;
        s.reverse     = entry.reverse;
        s.synced      = (entry.phaseOffset >= 0.0f) && blendEnabled();
        s.active      = true;
        s.touched     = true;

        if (s.synced && entry.naturalSpeed > 0.0f)
        {
            const float cycle = static_cast<float>(end - begin) / m_fps;

            shareSum  += entry.share;
            cycleSum  += entry.share * cycle;
            strideSum += entry.share * entry.naturalSpeed * cycle;
        }
    }

    // Anything left in this state that the caller did not name this frame has
    // had its share taken to zero by the blend space itself, so it can simply go.
    for (int i = 0; i < k_maxPlayers; ++i)
        if (m_slots[i].active && m_slots[i].state == state && !m_slots[i].touched)
            retireSlot(m_slots[i]);

    // --- Shared phase rate ---------------------------------------------------
    // The stride length - world units covered per cycle - is what a shared phase
    // accumulator advances against:
    //
    //     dPhase/dt = planarSpeed / strideBlended
    //
    // which is exactly the old rate = speed / naturalSpeed, re-derived through
    // the blended pair instead of through whichever single clip a threshold had
    // picked. Walk at 2.2 still comes out at 1.04 and run at 6.0 at 1.31, so the
    // feet stay planted at every speed in between as well - which is the whole
    // point of replacing the tier switch.
    if (shareSum > 0.0f && strideSum > 0.0f)
    {
        // Starting to move from a standstill. The phase froze wherever the
        // character stopped, and resuming there would start the gait at an
        // arbitrary point; seeding it at 0 starts every walk on a left-foot
        // plant. Invisible when it happens, because the moving clip's share is
        // still ~0 at that instant - which is the whole reason it can be done
        // here at all rather than needing a transition.
        if (m_phaseRate <= 0.0f)
            m_phase = 0.0f;

        const float stride = strideSum / shareSum;
        const float cycle  = cycleSum / shareSum;

        float rate = (cycle > 0.0f) ? (planarSpeed * cycle / stride) : 1.0f;
        if (rate < k_rateMin) rate = k_rateMin;
        if (rate > k_rateMax) rate = k_rateMax;

        m_phaseRate = (cycle > 0.0f) ? (rate / cycle) : 0.0f;
    }
    else
    {
        m_phaseRate = 0.0f;
    }

    // Publish the heaviest member, which is what "what is this character
    // playing" means for a set whose shares move every frame.
    {
        const int dom = dominantSlot();
        if (dom >= 0 && !m_slots[dom].clipName.empty())
            publishActive(mc, m_slots[dom].clipName.c_str());
    }

    m_playing = true;
    return true;
}

bool AnimGraph::playLayer(MeshComponent& mc, int index, const char* clipName,
                          const char* maskName, AnimLayerMode mode,
                          bool loop, float rate, float fadeSeconds,
                          int referenceFrame,
                          int localBegin, int localEnd)
{
    if (index < 0 || index >= k_maxLayers || !clipName)
        return false;

    if (!m_bound && !bind(mc))
        return false;

    if (!resolve(mc, clipName))
        return false;

    // Same mapping play() uses, in the same one place: everything a caller hands
    // around is a LOCAL offset inside the clip, and it becomes an absolute mesh
    // frame here and nowhere else.
    const int begin = m_resolved.baseFrame + localBegin;
    const int end   = (localEnd < 0) ? m_resolved.lastFrame
                                     : (m_resolved.baseFrame + localEnd);

    Layer& layer = m_layers[index];

    // An unmatched mask name means FULL BODY, not an all-zero mask. A rig whose
    // joints are not named as expected must lose the masking, never lose the
    // joints the layer was meant to drive.
    const AnimMask* mask = maskName ? m_masks.find(maskName) : nullptr;
    if (!mask)
        mask = m_masks.find(AnimMaskName::k_full);
    const bool sameSetup = layer.active &&
                           layer.clipName == clipName &&
                           layer.mode == mode &&
                           layer.mask == mask &&
                           layer.player.begin == begin &&
                           layer.player.end   == end;

    if (sameSetup)
    {
        // Already running this exact layer: only the rate and the fade target
        // can have moved, so nothing restarts and the cursor is untouched.
        layer.player.rate = rate;
        layer.target      = 1.0f;
        if (fadeSeconds > 0.0f && blendEnabled())
            layer.rate = 1.0f / fadeSeconds;
        return true;
    }

    layer.player = AnimClipPlayer();
    layer.player.play(begin, end, loop, rate);

    layer.clipName = clipName;
    layer.maskName = maskName ? maskName : "";
    layer.mask     = mask;
    layer.mode     = mode;
    layer.active   = true;
    layer.target   = 1.0f;

    const float fade = blendEnabled() ? fadeSeconds : 0.0f;
    if (fade <= 0.0f)
    {
        layer.weight = 1.0f;
        layer.rate   = 0.0f;
    }
    else
    {
        layer.rate = 1.0f / fade;
    }

    if (mode == ALM_ADDITIVE)
    {
        // Snapshot the reference ONCE, here. Sampling it per frame would cost a
        // second full pass for a pose that by definition never changes, and
        // would silently start moving if the clip range were ever retargeted.
        // Relative to the CLIP's base, not to the sub-range: "the frame of this
        // clip that adds nothing" does not move because a caller chose to play
        // part of it. -1 still means the first frame actually being played.
        const float refFrame = (referenceFrame < 0)
                                   ? static_cast<float>(begin)
                                   : static_cast<float>(m_resolved.baseFrame + referenceFrame);

        layer.sampler.sample(refFrame,
                             static_cast<float>(begin),
                             static_cast<float>(end),
                             false,
                             layer.reference);
    }

    return true;
}

void AnimGraph::restartLayer(int index)
{
    if (index < 0 || index >= k_maxLayers)
        return;

    Layer& layer = m_layers[index];
    if (!layer.active)
        return;

    // The seed follows the sign of the rate, for the same reason
    // AnimClipPlayer::play does: a reversed clip re-seeded to its beginning has
    // nowhere left to run.
    layer.player.seek(static_cast<float>((layer.player.rate < 0.0f) ? layer.player.end
                                                                    : layer.player.begin));
}

void AnimGraph::stopLayer(int index, float fadeSeconds)
{
    if (index < 0 || index >= k_maxLayers)
        return;

    Layer& layer = m_layers[index];
    if (!layer.active)
        return;

    layer.target = 0.0f;

    const float fade = blendEnabled() ? fadeSeconds : 0.0f;
    if (fade <= 0.0f)
    {
        layer.active = false;
        layer.weight = 0.0f;
        layer.rate   = 0.0f;
    }
    else
    {
        layer.rate = 1.0f / fade;
    }
}

bool AnimGraph::layerActive(int index) const
{
    return (index >= 0 && index < k_maxLayers) && m_layers[index].active;
}

void AnimGraph::update(float seconds)
{
    if (!m_bound || !m_playing || !m_node)
        return;

    // --- Shared cyclic phase -------------------------------------------------
    m_phase = fract01(m_phase + m_phaseRate * seconds);

    // --- Advance state weights ----------------------------------------------
    for (int i = 0; i < k_maxStates; ++i)
    {
        State& st = m_states[i];
        if (!st.active)
            continue;

        if (st.rate <= 0.0f)
        {
            st.weight = st.target;
        }
        else
        {
            const float step = st.rate * seconds;
            if (st.weight < st.target)
                st.weight = (st.weight + step > st.target) ? st.target : st.weight + step;
            else if (st.weight > st.target)
                st.weight = (st.weight - step < st.target) ? st.target : st.weight - step;
        }

        if (st.target <= 0.0f && st.weight <= k_weightEpsilon)
        {
            for (int j = 0; j < k_maxPlayers; ++j)
                if (m_slots[j].state == i)
                    retireSlot(m_slots[j]);

            st = State();
        }
    }

    // --- Advance cursors -----------------------------------------------------
    // EVERY player advances, including the ones on their way out. That is the
    // entire difference between this and Irrlicht's setTransitionTime, which
    // blends from a frozen snapshot: a walk fading into a run has to keep
    // walking, or the legs stop mid-stride for the length of the fade.
    for (int i = 0; i < k_maxPlayers; ++i)
    {
        Slot& s = m_slots[i];
        if (!s.active)
            continue;

        if (s.synced)
        {
            // Driven from the group, not from its own clock. 'phaseOffset' is
            // the clip's own left-foot contact phase, so phase 0 is left-foot
            // contact in EVERY member of the group and a cross-fade can never
            // average one foot's stance against the other's swing.
            float u = fract01(m_phase + s.phaseOffset);

            // A reversed clip walks its own cycle backwards. Its foot-plant
            // alignment against the forward clips is then approximate - playing
            // a stride backwards swaps which foot is planting - but it is
            // continuous, which is what actually prevents the pop. Forward and
            // back are never both active anyway: they are opposite cardinals in
            // the directional blend and only adjacent pairs ever mix.
            if (s.reverse)
                u = 1.0f - u;

            const float span = static_cast<float>(s.player.end - s.player.begin);
            s.player.cursor = static_cast<float>(s.player.begin) + u * span;
        }
        else
        {
            s.player.advance(seconds, m_fps);
        }
    }

    // --- Sample and blend ----------------------------------------------------
    float accumulated = 0.0f;
    bool  haveAny     = false;

    for (int i = 0; i < k_maxPlayers; ++i)
    {
        Slot& s = m_slots[i];
        if (!s.active || s.state < 0)
            continue;

        // Ease the STATE weight, not the share. A linear 0->1 state ramp reads
        // as a slight slide; the shares, by contrast, are blend-space
        // coordinates and easing those would bend the space itself.
        const float w = animEase(m_states[s.state].weight) * s.share;
        if (w <= k_weightEpsilon && haveAny)
            continue;

        s.sampler.sample(s.player.cursor,
                         static_cast<float>(s.player.begin),
                         static_cast<float>(s.player.end),
                         s.player.loop,
                         s.pose);

        if (!haveAny)
        {
            m_pose.joints = s.pose.joints;
            accumulated   = (w > k_weightEpsilon) ? w : k_weightEpsilon;
            haveAny       = true;
            continue;
        }

        // Incremental normalised blend: fold each pose in at its share of the
        // running total. The weights never have to be normalised up front, so a
        // set that is momentarily out of balance still produces a properly
        // weighted average rather than a scaled one.
        blendPoseInto(m_pose, s.pose, w / (accumulated + w));
        accumulated += w;
    }

    if (!haveAny)
        return;

    // --- Layers, in order, on top of the base --------------------------------
    // The base is one full-body pose by the time we get here. Each layer then
    // reaches only the joints its mask names, either replacing what is there
    // (override) or composing with it (additive).
    for (int i = 0; i < k_maxLayers; ++i)
    {
        Layer& layer = m_layers[i];
        if (!layer.active)
            continue;

        layer.player.advance(seconds, m_fps);

        if (layer.rate <= 0.0f)
        {
            layer.weight = layer.target;
        }
        else
        {
            const float step = layer.rate * seconds;
            if (layer.weight < layer.target)
                layer.weight = (layer.weight + step > layer.target) ? layer.target
                                                                   : layer.weight + step;
            else if (layer.weight > layer.target)
                layer.weight = (layer.weight - step < layer.target) ? layer.target
                                                                    : layer.weight - step;
        }

        if (layer.target <= 0.0f && layer.weight <= k_weightEpsilon)
        {
            layer.active = false;
            layer.weight = 0.0f;
            continue;
        }

        const float w = animEase(layer.weight);
        if (w <= k_weightEpsilon)
            continue;

        layer.sampler.sample(layer.player.cursor,
                             static_cast<float>(layer.player.begin),
                             static_cast<float>(layer.player.end),
                             layer.player.loop,
                             layer.pose);

        // playLayer resolves an unknown or absent mask name to the "full" mask,
        // so there is one code path rather than two. It can still be null if the
        // rig defeated AnimMaskSet entirely, in which case the layer is dropped
        // rather than applied to nothing.
        if (!layer.mask)
            continue;

        const AnimMask& mask = *layer.mask;

        if (layer.mode == ALM_ADDITIVE)
            addPoseMasked(m_pose, layer.pose, layer.reference, mask, w);
        else
            overlayPoseMasked(m_pose, layer.pose, mask, w);
    }

    // --- Foot IK, last -------------------------------------------------------
    // After the base and after every layer, because it is a correction to where
    // the feet actually ended up and anything running afterwards would undo it.
    //
    // updateAbsolutePosition() first: the IK works in WORLD space (that is where
    // the floor is), and the node's absolute transform is otherwise only
    // refreshed during drawAll(), i.e. after this. Reading it stale would place
    // the feet against last frame's position, which on a moving character is a
    // permanent lag rather than a one-frame one.
    if (m_footIK.valid())
    {
        m_node->updateAbsolutePosition();
        m_footIK.solve(m_pose, m_node->getAbsoluteTransformation(), m_footIKWeight, seconds);
    }

    // ENGINE FORK #6. The node copies the array and applies it immediately
    // before its own skinMesh(), which is what makes it safe when several nodes
    // share one cached CSkinnedMesh.
    m_node->setExternalPose(m_pose.joints);
}

void AnimGraph::release()
{
    if (!m_playing)
        return;

    if (m_node)
        m_node->clearExternalPose();

    for (int i = 0; i < k_maxPlayers; ++i)
        retireSlot(m_slots[i]);

    for (int i = 0; i < k_maxStates; ++i)
        m_states[i] = State();

    for (int i = 0; i < k_maxLayers; ++i)
    {
        m_layers[i].active = false;
        m_layers[i].weight = 0.0f;
        m_layers[i].target = 0.0f;
    }

    m_current   = -1;
    m_phaseRate = 0.0f;
    m_playing   = false;
}

void AnimGraph::detach()
{
    m_node    = nullptr;
    m_skinned = nullptr;
    m_bound   = false;
    m_playing = false;
    m_current = -1;
}

// The heaviest slot of the current state. For a single-clip state that is the
// clip; for a blend set it is whichever member currently dominates, which is
// what a state machine asking "what am I playing" means by the question.
int AnimGraph::dominantSlot() const
{
    int   best  = -1;
    float bestW = -1.0f;

    for (int i = 0; i < k_maxPlayers; ++i)
    {
        const Slot& s = m_slots[i];
        if (!s.active || s.state != m_current)
            continue;

        if (s.share > bestW)
        {
            bestW = s.share;
            best  = i;
        }
    }

    return best;
}

float AnimGraph::frame() const
{
    const int i = dominantSlot();
    return (i >= 0) ? m_slots[i].player.cursor : 0.0f;
}

bool AnimGraph::finished() const
{
    const int i = dominantSlot();
    return (i >= 0) ? m_slots[i].player.finished : false;
}

const std::string& AnimGraph::activeClip() const
{
    static const std::string empty;
    const int i = dominantSlot();
    return (i >= 0) ? m_slots[i].clipName : empty;
}

int AnimGraph::activeClipBase() const
{
    const int i = dominantSlot();
    return (i >= 0) ? m_slots[i].baseFrame : 0;
}

int AnimGraph::activeClipLast() const
{
    const int i = dominantSlot();
    return (i >= 0) ? m_slots[i].lastFrame : 0;
}

bool AnimGraph::debugLayer(int index, AnimLayerDebug& out) const
{
    if (index < 0 || index >= k_maxLayers || !m_layers[index].active)
        return false;

    const Layer& layer = m_layers[index];

    out.clip     = layer.clipName;
    out.mask     = layer.maskName.empty() ? std::string("full") : layer.maskName;
    out.cursor   = layer.player.cursor;
    out.weight   = animEase(layer.weight);
    out.target   = layer.target;
    out.additive = (layer.mode == ALM_ADDITIVE);

    return true;
}

bool AnimGraph::debugPlayer(int index, AnimPlayerDebug& out) const
{
    if (index < 0 || index >= k_maxPlayers)
        return false;

    const Slot& s = m_slots[index];
    if (!s.active || s.state < 0)
        return false;

    out.clip    = s.clipName;
    out.cursor  = s.player.cursor;
    out.begin   = s.player.begin;
    out.end     = s.player.end;
    out.rate    = s.player.rate;
    out.share   = s.share;
    out.weight  = animEase(m_states[s.state].weight) * s.share;
    out.loop    = s.player.loop;
    out.synced  = s.synced;
    out.current = (s.state == m_current);

    return true;
}

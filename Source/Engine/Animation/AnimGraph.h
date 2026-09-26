#pragma once

#include "Engine/Animation/AnimClip.h"
#include "Engine/Animation/AnimIK.h"
#include "Engine/Animation/AnimMask.h"
#include "Engine/Animation/AnimClipPlayer.h"
#include "Engine/Animation/AnimPose.h"
#include "Engine/Animation/AnimSampler.h"

#include "irrlicht.h"

#include <string>

struct MeshComponent;

// One clip's contribution to a continuous blend.
//
// The caller recomputes the whole set EVERY FRAME from its blend-space
// coordinates and hands it over; shares move continuously, so a clip entering
// the set enters at share ~0 and leaving is the same in reverse. That is why a
// blend set needs no cross-fade machinery of its own - only transitions between
// genuinely different STATES (locomotion / in-place turn / jump) do.
struct AnimBlendEntry
{
    const char* clip = nullptr;

    // Weight within the set. The set is normalised at evaluation, so these need
    // not sum to exactly 1.
    float share = 0.0f;

    // The fraction of this clip's cycle at which the LEFT foot plants, measured
    // by Tools/measure_clip_phase.py. Every synced clip in the set is driven
    // from one shared phase accumulator offset by this, so left-foot contact
    // lines up across all of them and a walk->run cross-fade cannot scissor.
    //
    // NEGATIVE means the clip is not cyclic and free-runs on its own cursor -
    // that is what idle is.
    float phaseOffset = -1.0f;

    // World units per second this clip covers at playback rate 1.0. Used with
    // the clip's own measured cycle length to derive the shared phase rate, so
    // the blended playback rate stays near 1.0 and the feet stay planted.
    float naturalSpeed = 0.0f;

    // Sample the cycle backwards. The clip set has no backpedal clip: travelling
    // backwards is the forward clip reversed, which is the RIGHT answer for the
    // feet (a body moving backwards needs its planted foot to travel forward
    // relative to the hips) and wrong only for the arm swing.
    bool reverse = false;
};

// How a layer combines with everything under it.
enum AnimLayerMode
{
    // "Be this pose, on these joints." An upper-body hit reaction, an aim pose,
    // the footwork of an in-place turn.
    ALM_OVERRIDE = 0,

    // "Be whatever you were, PLUS this much." The layer's pose is read as a
    // delta against a reference frame of its own clip, so it composes with the
    // locomotion underneath rather than replacing it: lean, breathing, recoil.
    ALM_ADDITIVE
};

// One layer's worth of state, for the anim_debug overlay.
struct AnimLayerDebug
{
    std::string clip;
    std::string mask;
    float cursor   = 0.0f;
    float weight   = 0.0f;
    float target   = 0.0f;
    bool  additive = false;
};

// One player's worth of state, for the anim_debug overlay.
struct AnimPlayerDebug
{
    std::string clip;
    float cursor  = 0.0f;
    int   begin   = 0;
    int   end     = 0;
    float rate    = 0.0f;
    float share   = 0.0f;   // weight within its state
    float weight  = 0.0f;   // final contribution: state weight * share
    bool  loop    = false;
    bool  synced  = false;
    bool  current = false;
};

// Per-character animation state: what is playing, where its cursors are, and
// the blended pose that comes out.
//
// WHY THIS IS NOT IN THE CONTROLLER. CharacterBehavior::playAnim drives every
// NPC through the identical snap path the player controller used to. Put the
// cursors and the pose in PlayerControllerTPS and they get written a second
// time for the NPCs, and the second one diverges.
//
// THE MODEL, AS OF PHASE 3
//
//   * A STATE is what the character is doing: locomotion, an in-place turn, the
//     rise of a jump, a landing. States cross-fade against each other, each with
//     its own duration.
//   * A state holds one or more SLOTS. A single-clip state has one. A blend
//     state has as many as its blend space needs, each with a continuously
//     varying share.
//   * A slot's final weight is its state's (eased) weight times its share.
//   * Every cyclic slot is driven from ONE shared phase accumulator, offset by
//     its own measured foot-plant phase. That is what stops a walk->run fade
//     from averaging left-foot-down against right-foot-down.
//
// AND AS OF PHASE 4, LAYERS. Everything above is the BASE: one full-body pose.
// Layers stack on top of it in order, each with a bone mask and either override
// or additive semantics. A layer is one clip, not a blend set - the base is
// where continuous blending lives, and a layer is "also do this, here".
//
// This is the piece that makes a third-person character with weapons tractable:
// the legs keep running from the base while an arm chain does something else
// entirely, instead of the whole body being replaced by whatever the weapon
// wanted.
//
// AND FOOT IK, last of all, as a correction to where the feet ended up rather
// than a pose in its own right. Anything that ran after it would undo it.
class AnimGraph
{
public:
    // Eight, because the locomotion blend space can legitimately want five at
    // once - idle, plus two adjacent directions at two speed tiers - and a jump
    // or a turn can still be fading out underneath.
    static const int k_maxPlayers = 8;
    static const int k_maxStates  = 4;

    // Layer 0 is the base and is not in this array, so this is the number of
    // layers ON TOP. Four, because a character carrying a weapon needs three of
    // them AT ONCE and none is optional: the in-place turn drives the legs, the
    // grip pose holds the fingers closed, and an attack drives the upper body -
    // and the console test harness has to be able to sit alongside all three or
    // it cannot be used to diagnose them. The cost of an unused one is a bool
    // test.
    static const int k_maxLayers = 4;

    // Resolve the node's skinned mesh and size the pose buffers. Safe to call
    // every frame; it only does work the first time and after a mesh swap.
    bool bind(MeshComponent& mc);

    bool bound() const { return m_bound; }

    // --- Single-clip state ---------------------------------------------------
    // 'localBegin'/'localEnd' are offsets INSIDE the clip, not absolute mesh
    // frames - the jump clips are played as measured sub-ranges. Pass 0 and -1
    // for the whole clip.
    //
    // 'fadeSeconds' is PER TRANSITION and not a global: settling into idle wants
    // 0.25-0.35s, ordinary locomotion 0.15-0.25s, and anything with an impact in
    // it (a jump takeoff, a hit reaction) 0.08-0.10s or it reads as mush. Pass 0
    // for a hard cut.
    //
    // Re-issuing the state already playing is cheap and expected: it only
    // updates the playback rate, so a caller may call this every frame.
    //
    // Returns false if the mesh has no such clip (warned once per name).
    bool play(MeshComponent& mc, const char* clipName,
              int localBegin, int localEnd, bool loop, float rate,
              float fadeSeconds = 0.0f);

    // --- Continuous blend state ----------------------------------------------
    // Call EVERY frame with freshly computed shares. It starts a cross-fade only
    // when the character was not already in a blend state; from then on the set
    // is updated in place, because the shares themselves are what move.
    //
    // 'planarSpeed' is the character's actual ground speed. The shared phase rate
    // is derived from it and the blended stride length, which is what keeps the
    // feet planted across the whole speed range without a rate constant anywhere.
    bool playBlend(MeshComponent& mc, const AnimBlendEntry* entries, int count,
                   float planarSpeed, float fadeSeconds = 0.0f);

    // --- Layers --------------------------------------------------------------
    // Drive layer 'index' (0 .. k_maxLayers-1, stacked in that order ON TOP of
    // the base). Call every frame or once; either works, because re-requesting
    // the clip and mask already running only updates the rate.
    //
    // 'maskName' is one of AnimMaskName's labels; an unknown or null name means
    // full body. An unmatched mask is IGNORED rather than applied as all-zero,
    // so a rig with unexpected joint names loses the layer instead of losing its
    // upper body.
    //
    // For ALM_ADDITIVE, 'referenceFrame' is the local frame of the clip that
    // counts as "adds nothing"; -1 means the clip's first frame, which is what
    // an additive clip authored from a neutral stance wants.
    // 'localBegin'/'localEnd' are offsets INSIDE the clip, exactly as play()
    // takes them: 0 and -1 mean the whole clip. A multi-hit combo authored as
    // one long clip is played as one sub-range per hit, which is what lets a
    // second click cut straight to the next swing instead of waiting out the
    // first one's follow-through.
    bool playLayer(MeshComponent& mc, int index, const char* clipName,
                   const char* maskName, AnimLayerMode mode,
                   bool loop, float rate, float fadeSeconds,
                   int referenceFrame = -1,
                   int localBegin = 0, int localEnd = -1);

    // Fade a layer out. Its clip keeps playing while it does.
    void stopLayer(int index, float fadeSeconds = 0.0f);

    // Re-seed a running layer's cursor to the start of its clip, WITHOUT
    // re-fading it in.
    //
    // WHY THIS IS NOT JUST ANOTHER playLayer CALL. Re-issuing the clip already
    // running is deliberately a no-op for the cursor (see AnimClipPlayer::play),
    // which is exactly right for a held pose - a grip re-requested every frame
    // must not keep restarting - and exactly wrong for a one-shot fired twice.
    // Without this, a second click during the fade-out of the first swing finds
    // the layer still active on the same clip, changes nothing, and reads to the
    // player as the input having been dropped.
    void restartLayer(int index);

    bool layerActive(int index) const;

    // --- Foot IK -------------------------------------------------------------
    // The probe is how the IK asks the world where the floor is; without one the
    // pass does nothing. AnimationSystem installs it.
    void setGroundProbe(IAnimGroundProbe* probe) { m_footIK.setProbe(probe); }

    // 0..1, RAMPED internally, so a caller may flip it on a state change. Feed 0
    // while airborne and through a landing: there is no floor to stand on in the
    // air, and a landing is the one moment the feet are supposed to be moving
    // independently of it.
    void setFootIKWeight(float weight) { m_footIKWeight = weight; }

    // Drop the IK's smoothed state. On a teleport, or the character spends a
    // quarter of a second dragging its feet from wherever it used to be.
    void resetFootIK() { m_footIK.reset(); }

    // An extra vertical offset for the PELVIS, in world units, on top of the hip
    // drop the IK decides for itself. Feed a controller's step smoothing through
    // here rather than lagging the body transform: see AnimFootIK.
    void setPelvisOffset(float worldY) { m_footIK.setPelvisOffset(worldY); }

    const AnimFootIK& footIK() const { return m_footIK; }

    // Advance every cursor and every weight, sample, blend, and hand the result
    // to the node.
    //
    // MUST run once per RENDERED frame, not inside the fixed-step loop - see
    // project_imgui_fixed_step_flicker for the same class of bug. 'seconds' is
    // already time-scaled by the caller; a node on the external-pose path no
    // longer rides Irrlicht's virtual timer.
    void update(float seconds);

    // Stop driving the node and hand it back to setFrameLoop playback. The node
    // must still be alive - this touches it.
    void release();

    // Forget the node WITHOUT touching it. For teardown, where RenderSystem may
    // already have destroyed the scene node: system removal callbacks have no
    // guaranteed order, so release() there would be a use-after-free.
    void detach();

    // --- Queries the controller needs ---------------------------------------
    // These describe the DOMINANT slot of the current state - the heaviest clip
    // of whatever the character is doing now. Anything fading out is still
    // playing and still contributing to the pose, but it is not what the state
    // machine thinks is happening.
    //
    // frame() replaces IAnimatedMeshSceneNode::getFrameNr() for a migrated
    // character: the node's own frame counter still ticks, but nothing reads it.
    float frame() const;
    bool  finished() const;

    const std::string& activeClip() const;

    // The dominant slot's CLIP range: the first and last absolute frame of the
    // whole clip it was resolved from, not of the sub-range it happens to be
    // playing. A caller holding local offsets into that clip adds them to
    // activeClipBase().
    //
    // THESE USED TO READ m_resolved, AND THAT WAS A BUG. m_resolved is a
    // name -> range lookup CACHE shared by everything that resolves a clip,
    // layers included, so it holds whatever was asked for last rather than what
    // the character is playing. A grip layer re-requesting its clip every frame
    // - which is exactly what a held weapon pose does - left these reporting the
    // grip clip's range while frame() and activeClip() reported the base state's,
    // and any caller comparing one against the other silently never terminated.
    int   activeClipBase() const;
    int   activeClipLast() const;

    // Shared cyclic phase, [0,1). Phase 5's foot IK will want it to know which
    // foot is meant to be planted.
    float phase() const { return m_phase; }
    float phaseRate() const { return m_phaseRate; }

    // --- Debug --------------------------------------------------------------
    int  debugPlayerCount() const { return k_maxPlayers; }
    bool debugPlayer(int index, AnimPlayerDebug& out) const;

    int  debugLayerCount() const { return k_maxLayers; }
    bool debugLayer(int index, AnimLayerDebug& out) const;

    // anim_blend 0 is the whole-feature off switch: hard cuts between states, no
    // cyclic sync, and a blend set collapsed to its single heaviest member. That
    // reproduces one-clip-at-a-time playback, so a suspected artifact can be
    // bisected at runtime without a rebuild. Global because it is a debug
    // switch, not a per-character property.
    static void setBlendEnabled(bool on) { s_blendEnabled = on; }
    static bool blendEnabled() { return s_blendEnabled; }

private:
    struct Slot
    {
        AnimClipPlayer player;
        AnimSampler    sampler;
        AnimPose       pose;

        std::string clipName;

        // The CLIP this slot was resolved from, as absolute mesh frames.
        // Distinct from player.begin/end, which are the SUB-RANGE being played -
        // the jump clips are played as measured segments inside their clip.
        int   baseFrame   = 0;
        int   lastFrame   = 0;

        int   state       = -1;
        float share       = 0.0f;
        float phaseOffset = -1.0f;
        bool  reverse     = false;
        bool  synced      = false;

        bool touched = false;   // named in this frame's blend set
        bool active  = false;
    };

    struct State
    {
        float weight = 0.0f;
        float target = 0.0f;
        float rate   = 0.0f;   // weight units per second; 0 means instant
        bool  blend  = false;  // a continuous set rather than a single clip
        bool  active = false;
    };

    struct Layer
    {
        AnimClipPlayer player;
        AnimSampler    sampler;
        AnimPose       pose;
        AnimPose       reference;   // additive only: the "adds nothing" pose

        std::string clipName;
        std::string maskName;

        const AnimMask* mask = nullptr;   // null = full body
        AnimLayerMode   mode = ALM_OVERRIDE;

        float weight = 0.0f;
        float target = 0.0f;
        float rate   = 0.0f;

        bool active = false;
    };

    bool resolve(MeshComponent& mc, const char* clipName);
    void publishActive(MeshComponent& mc, const char* clipName);
    int  newState(bool blend, float fadeSeconds);
    int  acquireSlot(int state, int begin, int end, bool loop, bool reverse);
    int  dominantSlot() const;
    void retireSlot(Slot& s);

    irr::scene::IAnimatedMeshSceneNode* m_node    = nullptr;
    irr::scene::ISkinnedMesh*           m_skinned = nullptr;

    // Frames per second of the clip timeline, taken from MeshComponent::fps
    // (the .anim sidecar's <fps>, 30 for the paladin).
    float m_fps = 30.0f;

    Slot     m_slots[k_maxPlayers];
    State    m_states[k_maxStates];
    Layer    m_layers[k_maxLayers];

    AnimMaskSet m_masks;
    AnimFootIK  m_footIK;
    float       m_footIKWeight = 0.0f;

    int      m_current = -1;   // state being faded TOWARD; -1 before the first play
    AnimPose m_pose;           // the blended result handed to the node

    // The sync group. One accumulator in [0,1) for every cyclic clip in the
    // blend set; each samples its own frame range at (phase + its own offset).
    float m_phase     = 0.0f;
    float m_phaseRate = 0.0f;   // cycles per second

    // Cache of the last name -> frame-range resolution. One entry is enough for
    // a single-clip state; a blend set resolves each member once per frame, but
    // its membership changes rarely and findAnimation is a short linear scan.
    AnimClipRef m_resolved;
    std::string m_missingClip;   // warn-once guard; a bad name is asked for every frame

    bool m_bound   = false;
    bool m_playing = false;   // a state has been requested at least once

    static bool s_blendEnabled;
};

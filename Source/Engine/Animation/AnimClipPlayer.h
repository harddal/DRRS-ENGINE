#pragma once

#include "Engine/Animation/AnimClip.h"

#include <cmath>

// One playback cursor over one clip segment.
//
// Replaces IAnimatedMeshSceneNode's StartFrame/EndFrame/CurrentFrameNr for a
// migrated character, and deliberately mirrors CAnimatedMeshSceneNode::
// buildFrameNr's wrap/clamp rules so phase 1 is a like-for-like port.
//
// TWO DIFFERENCES FROM THE NODE, BOTH INTENTIONAL:
//
//  * Playing the same segment again does NOT re-seed the cursor. setFrameLoop
//    always re-seeds to StartFrame (or EndFrame at negative speed - the sign
//    trap), which is why the old controller had to guard every call with an
//    "is this already the active segment" comparison. Here, play() on the
//    segment already running is a no-op for the cursor and only updates the
//    rate, so a caller may issue it every frame.
//
//  * The cursor advances on the ENGINE clock, not on Irrlicht's virtual timer.
//    See reference_irrlicht_virtual_timer: a node on the external-pose path no
//    longer rides ITimer::setSpeed(), so the caller must pass an already
//    time-scaled delta.
struct AnimClipPlayer
{
    // Absolute mesh frames. 'end' may equal 'begin' for a single-frame hold.
    int   begin = 0;
    int   end   = 0;
    bool  loop  = false;

    float cursor = 0.0f;   // absolute mesh frames
    float rate   = 1.0f;   // multiplier on the mesh fps; NEGATIVE plays backwards

    bool  finished = false;   // non-looping segment reached its far end

    bool active() const { return end != begin || cursor != 0.0f; }

    // Point at a segment. Re-seeds the cursor ONLY when the segment actually
    // changes; the seed follows the sign of the rate for the same reason
    // setFrameLoop does - a reversed clip started at its beginning has nowhere
    // to go.
    void play(int beginFrame, int endFrame, bool looping, float playRate)
    {
        rate = playRate;

        if (beginFrame == begin && endFrame == end && looping == loop)
            return;

        begin    = beginFrame;
        end      = endFrame;
        loop     = looping;
        finished = false;
        cursor   = (playRate < 0.0f) ? static_cast<float>(endFrame)
                                     : static_cast<float>(beginFrame);
    }

    // Force the cursor somewhere inside the segment. Phase 3's sync groups are
    // what will really want this; it exists now so a cross-fade can be tested
    // against a known phase.
    void seek(float frame) { cursor = frame; finished = false; }

    void advance(float seconds, float fps)
    {
        if (end == begin)
        {
            cursor = static_cast<float>(begin);
            return;
        }

        cursor += seconds * fps * rate;

        const float lo = static_cast<float>(begin);
        const float hi = static_cast<float>(end);
        const float span = hi - lo;

        if (loop)
        {
            // fmodf, not a while-loop: a large time-scale spike or a hitch can
            // carry the cursor several cycles past the end in one step.
            if (cursor > hi)      cursor = lo + fmodf(cursor - lo, span);
            else if (cursor < lo) cursor = hi - fmodf(hi - cursor, span);
        }
        else
        {
            if (rate >= 0.0f)
            {
                if (cursor >= hi) { cursor = hi; finished = true; }
            }
            else
            {
                if (cursor <= lo) { cursor = lo; finished = true; }
            }
        }
    }
};

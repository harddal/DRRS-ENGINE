#pragma once

#include <string>

// A clip resolved out of MeshComponent::animationList ONCE.
//
// findAnimation is a linear scan with string compares over the whole list, and
// a locomotion controller asks for a clip every single frame — so the answer is
// cached here and only re-resolved when the name actually changes.
//
// 'baseFrame' and 'lastFrame' are ABSOLUTE mesh frames on the one concatenated
// timeline every clip shares (see paladin.anim: Idle is 0..532, Walk is
// 1453..1483). Everything the controller passes around is a LOCAL offset inside
// the clip; the conversion happens in exactly one place, AnimClipPlayer::play.
struct AnimClipRef
{
    std::string name;
    int   baseFrame = 0;
    int   lastFrame = 0;
    bool  loop      = false;

    // The clip's own natural ground speed in world units/sec, when it has been
    // measured. 0 means "not measured" — the caller supplies the rate itself.
    // Unused in phase 1; the 1D speed blend space is what wants it.
    float naturalSpeed = 0.0f;

    bool valid() const { return lastFrame >= baseFrame && !name.empty(); }
};

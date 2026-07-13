#pragma once

#include <vector>

#include "Engine/Brush/BrushData.h"

struct BrushChunk;

// ---------------------------------------------------------------------------
// BrushCompiler — turns a chunk's member brushes into renderable geometry.
//
// One CMeshBuffer<S3DVertex2TCoords> per material+shader bucket (split past
// the u16 index limit), UV1 from the face's Valve-220 axes, UV2 = UV1 until
// the lightmap baker overwrites it.  Chunk vertices are in WORLD space; the
// chunk node sits at the origin, so its recalculated AABB drives Irrlicht's
// per-node EAC_BOX frustum culling — chunk culling for free in drawAll(),
// the shadow pass and the depth pre-pass.
// ---------------------------------------------------------------------------

namespace BrushCompiler
{
    // Build a world-space SMesh from the given brushes.  Faces flagged
    // FACE_NODRAW are skipped unless includeNoDraw (collision meshes want
    // them).  Returns nullptr when there is nothing to emit; caller drops.
    irr::scene::SMesh* buildChunkMesh(const std::vector<const Brush*>& brushes,
                                      bool includeNoDraw);

    // Create/refresh/destroy the chunk's scene node from its member brushes,
    // using the rebuild-in-place pattern (new SAnimatedMesh + setMesh, then
    // material flags re-applied).  Does NOT touch collision or selectors —
    // those are the "heavy" path in BrushManager, deferred during drags.
    void updateChunkNode(BrushChunk& chunk, const std::vector<const Brush*>& brushes);
}

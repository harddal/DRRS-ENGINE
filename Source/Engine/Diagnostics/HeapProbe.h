#pragma once

#include <cstddef>
#include <string>

// ---------------------------------------------------------------------------
// Heap-corruption localisation scaffold.
//
// Temporary diagnostic for the unsolved intermittent crash where simdjson's
// parse tape comes back clobbered (fastgltf then reserves on a garbage count
// and throws std::bad_alloc).  Two independent detectors:
//
//   arm()/check()  - pattern-filled canary blocks.  check() reports the first
//                    damaged word per block WITH THE VALUE THAT OVERWROTE IT,
//                    which is usually the strongest clue about the writer
//                    (a user-space pointer, a pair of floats, ASCII, ...).
//   gltfProbe()    - parses a .glb purely as a corruption detector and evicts
//                    it from the mesh cache so the next probe re-parses.
//
// Sprinkle check()/gltfProbe() through startup; the first checkpoint that
// reports damage brackets the subsystem responsible.  Delete this file and its
// call sites once the writer is found.
// ---------------------------------------------------------------------------

#define ENGINE_HEAP_PROBE 1

// The asset every gltfProbe() call re-parses. Point it at whichever .glb
// reproduces the crash most reliably.
extern std::string g_heapProbeGltfAsset;

namespace HeapProbe
{
    // Allocate a generation of canary blocks.  Call at more than one point:
    // later generations reuse memory freed by earlier subsystems, which is
    // where a stale-pointer write lands.
    void arm(const char* label, std::size_t blocks = 48, std::size_t blockBytes = 64 * 1024);

    // Verify every armed block.  Returns the number of damaged blocks (0 = clean).
    int check(const char* label);

    // Parse a glTF/GLB as a corruption test.  Returns true on success.
    bool gltfProbe(const char* label, const std::string& path);

    void release();
}

#if ENGINE_HEAP_PROBE
    #define HEAP_PROBE_ARM(label)          ::HeapProbe::arm(label)
    #define HEAP_PROBE_CHECK(label)        ::HeapProbe::check(label)
    #define HEAP_PROBE_GLTF(label, path)   ::HeapProbe::gltfProbe(label, path)
#else
    #define HEAP_PROBE_ARM(label)          ((void)0)
    #define HEAP_PROBE_CHECK(label)        (0)
    #define HEAP_PROBE_GLTF(label, path)   (true)
#endif

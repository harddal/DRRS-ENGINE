#pragma once

#include <vector>
#include <cstdint>
#include <string>

#include "irrlicht.h"

#include "Engine/Types.h"

// ---------------------------------------------------------------------------
// MeshLightmap
// Holds all lightmap data for a single static mesh entity.
// Populated in stages:
//   Phase 1 (unwrapMesh)            - buffers, width, height
//   Phase 3 (rasterize)             - texels coverage + world-space lookup
//   Phase 4 (bake)                  - pixels
//   Phase 5 (dilate/upload)         - dilated pixels + GPU texture
//   Phase 5 (applyLightmapUVsToNode)- new EVT_2TCOORDS mesh set on scene node
// ---------------------------------------------------------------------------
struct MeshLightmap
{
    uint32_t width  = 0;
    uint32_t height = 0;

    // One vertex in the xatlas-remapped mesh.
    // May be more than the input vertex count (seam splits).
    struct Vertex
    {
        // Object-space data — needed to rebuild the GPU mesh with UV2.
        irr::core::vector3df objPos;
        irr::core::vector3df objNormal;

        // World-space data — needed for shadow ray casting during bake.
        irr::core::vector3df worldPos;
        irr::core::vector3df worldNormal;

        irr::core::vector2df uv1;  // original diffuse UV
        irr::core::vector2df uv2;  // lightmap UV, normalised [0,1]
    };

    // One entry per source mesh buffer (one per material slot).
    struct BufferData
    {
        std::vector<Vertex>   vertices;
        std::vector<uint32_t> indices;
    };
    std::vector<BufferData> buffers;

    // One entry per lightmap texel, row-major [y * width + x].
    // Filled by rasterizeTexels().
    struct Texel
    {
        irr::core::vector3df worldPos;
        irr::core::vector3df worldNormal;
        bool covered     = false;
        int  bufferIndex = -1;
    };
    std::vector<Texel> texels;

    // Baked RGB values, row-major [3 * (y * width + x)].
    std::vector<float> pixels;

    // GPU texture — null until uploadLightmap() is called.
    irr::video::ITexture* texture = nullptr;
};

// ---------------------------------------------------------------------------
// LightmapBaker
// ---------------------------------------------------------------------------
class LightmapBaker
{
public:
    struct BakeSettings
    {
        uint32_t resolution     = 256;   // target lightmap resolution
        uint32_t padding        = 2;     // chart padding in texels
        float    shadowBias     = 0.05f; // world-unit ray offset to avoid self-hits
        uint32_t dilationPasses = 2;     // outward-fill iterations for seam texels
        uint32_t blurRadius     = 0;     // Gaussian blur radius in texels applied after baking; 1–3 smooths shadow edges
    };

    // Compact light descriptor used during baking.
    struct BakeLight
    {
        irr::core::vector3df position;   // world-space
        irr::core::vector3df direction;  // spot: normalised forward, unused for point
        irr::video::SColorf  color;
        float                radius;
        float                outerCone;  // spot half-angle in degrees (point = 180)
        int                  type;       // mirrors LIGHT_TYPE: 0=point,1=spot,2=area
    };

    // Phase 1: UV unwrap via xatlas.
    static bool unwrapMesh(
        irr::scene::IAnimatedMesh*  animMesh,
        const irr::core::matrix4&   worldTransform,
        const BakeSettings&         settings,
        MeshLightmap&               outLightmap);

    // Phase 3: Rasterize triangles into lightmap texels.
    static void rasterizeTexels(MeshLightmap& lightmap);

    // Phase 4: Compute direct lighting per texel via shadow rays.
    // sceneTriangles must contain all world-space scene triangles (built once in bakeScene).
    // The vector is read-only so bake() can be called from multiple threads simultaneously.
    static void bake(
        MeshLightmap&                              lightmap,
        const std::vector<BakeLight>&              lights,
        const std::vector<irr::core::triangle3df>& sceneTriangles,
        const BakeSettings&                        settings);

    // Phase 4b: Gaussian blur — smooths shadow edges in texel space.
    // Only blurs covered texels; seam and uncovered texels are untouched.
    static void blurLightmap(MeshLightmap& lightmap, uint32_t radius);

    // Phase 5a: Dilation — fill chart-border texels to prevent seam gaps.
    static void dilate(MeshLightmap& lightmap, uint32_t passes);

    // Phase 5b: Upload float pixel buffer to an ITexture.
    static irr::video::ITexture* uploadLightmap(
        irr::video::IVideoDriver* driver,
        MeshLightmap&             lightmap,
        const std::string&        name);

    // Phase 5c: Rebuild the scene node's mesh with S3DVertex2TCoords so that
    // UV2 (lightmap coords) reach the vertex shader via gl_MultiTexCoord1.
    // Also applies the lightmap texture to material slot 1.
    // Must be called after uploadLightmap().
    static void applyLightmapUVsToNode(
        const MeshLightmap&                  lightmap,
        irr::scene::IAnimatedMeshSceneNode*  node);

    // Full pipeline: iterate ET_STATIC mesh entities, bake, and apply.
    static void bakeScene(const BakeSettings& settings = BakeSettings());

    // Async variant: CPU work runs on a background thread.
    // Call flushPendingUploads(driver) every frame from the main thread until
    // both isBaking() and hasPendingUploads() return false.
    static void bakeSceneAsync(const BakeSettings& settings = BakeSettings());
    static void flushPendingUploads(irr::video::IVideoDriver* driver);

    static float getBakeProgress();   // 0..1 — fraction of entities CPU-baked
    static bool  isBaking();          // true while background thread is running
    static bool  hasPendingUploads(); // true while GPU uploads are still queued

    // ---------------------------------------------------------------------------
    // Serialization helpers — called by WorldManager during scene save/load.
    // ---------------------------------------------------------------------------

    // In-memory representation of all lightmap files for one baked entity.
    // pngBytes    — PNG-encoded lightmap texture (ready to add to a zip).
    // uvmeshBytes — binary UV2 mesh data (LMSV format, see LightmapBaker.cpp).
    struct LightmapFiles
    {
        entityid             id;
        std::vector<uint8_t> pngBytes;
        std::vector<uint8_t> uvmeshBytes;
    };

    // Collect lightmap data for every baked entity into memory buffers.
    // tempDir must be a writable directory; it is used briefly to encode PNGs
    // via Irrlicht's image writer — all temporary files are deleted on return.
    static std::vector<LightmapFiles> collectLightmapFiles(
        irr::video::IVideoDriver* driver,
        const std::string&        tempDir);

    // Read and apply baked lightmaps for all ET_STATIC mesh entities.
    // The zip must already be mounted on `fs` (addFileArchive) before calling;
    // it is NOT unmounted here — that is the caller's responsibility.
    static void loadLightmaps(
        irr::io::IFileSystem*     fs,
        irr::video::IVideoDriver* driver);

private:
    static bool barycentricCoords(
        const irr::core::vector2df& p,
        const irr::core::vector2df& a,
        const irr::core::vector2df& b,
        const irr::core::vector2df& c,
        float& u, float& v, float& w);
};

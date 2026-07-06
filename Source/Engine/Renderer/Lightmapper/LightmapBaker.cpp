#include "LightmapBaker.h"
#include "xatlas.h"

#include <algorithm>
#include <cmath>
#include <cstdio>   // std::remove
#include <fstream>  // std::ifstream (PNG temp read-back)
#include <atomic>
#include <mutex>
#include <numeric>
#include <thread>
#include <spdlog/spdlog.h>

// bakeScene() needs world access
#include "Engine/World/WorldManager.h"
#include "Engine/World/Components/MeshComponent.h"
#include "Engine/World/Components/LightComponent.h"
#include "Engine/World/Components/TransformComponent.h"
#include "Engine/World/Components/DescriptorComponent.h"
#include "Engine/Renderer/RenderManager.h"
#include "Engine/Prop/PropManager.h"

#include "irrlicht.h"

using namespace irr;
using namespace core;
using namespace scene;
using namespace video;

// ---------------------------------------------------------------------------
// Per-texel bake progress — declared here so bake() can increment them.
// Initialised in bakeSceneAsync() before the background thread starts.
// ---------------------------------------------------------------------------
static std::atomic<int> g_texelsDone{0};
static std::atomic<int> g_texelsTotal{0};

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Extract flat position/normal/uv/index arrays from one Irrlicht mesh buffer.
// Indices are always promoted to uint32 for xatlas.
static bool extractBufferData(
    IMeshBuffer*            buf,
    std::vector<float>&     outPos,     // 3 floats per vertex
    std::vector<float>&     outNormal,  // 3 floats per vertex
    std::vector<float>&     outUV,      // 2 floats per vertex
    std::vector<uint32_t>&  outIndices)
{
    const u32 vertCount  = buf->getVertexCount();
    const u32 indexCount = buf->getIndexCount();

    if (vertCount == 0 || indexCount == 0)
        return false;

    outPos.resize(vertCount * 3);
    outNormal.resize(vertCount * 3);
    outUV.resize(vertCount * 2);
    outIndices.resize(indexCount);

    // --- Vertices ---
    E_VERTEX_TYPE vtype = buf->getVertexType();
    for (u32 i = 0; i < vertCount; i++)
    {
        vector3df pos, normal;
        vector2df uv;

        switch (vtype)
        {
        case EVT_STANDARD:
        {
            const S3DVertex& v = static_cast<const S3DVertex*>(buf->getVertices())[i];
            pos    = v.Pos;
            normal = v.Normal;
            uv     = v.TCoords;
            break;
        }
        case EVT_2TCOORDS:
        {
            const S3DVertex2TCoords& v = static_cast<const S3DVertex2TCoords*>(buf->getVertices())[i];
            pos    = v.Pos;
            normal = v.Normal;
            uv     = v.TCoords;
            break;
        }
        case EVT_TANGENTS:
        {
            const S3DVertexTangents& v = static_cast<const S3DVertexTangents*>(buf->getVertices())[i];
            pos    = v.Pos;
            normal = v.Normal;
            uv     = v.TCoords;
            break;
        }
        default:
            spdlog::warn("LightmapBaker: unsupported vertex type {}, skipping buffer", (int)vtype);
            return false;
        }

        outPos[i*3+0] = pos.X;
        outPos[i*3+1] = pos.Y;
        outPos[i*3+2] = pos.Z;

        outNormal[i*3+0] = normal.X;
        outNormal[i*3+1] = normal.Y;
        outNormal[i*3+2] = normal.Z;

        outUV[i*2+0] = uv.X;
        outUV[i*2+1] = uv.Y;
    }

    // --- Indices (promote u16 -> u32) ---
    if (buf->getIndexType() == EIT_16BIT)
    {
        const u16* src = static_cast<const u16*>(buf->getIndices());
        for (u32 i = 0; i < indexCount; i++)
            outIndices[i] = static_cast<uint32_t>(src[i]);
    }
    else
    {
        const u32* src = reinterpret_cast<const u32*>(buf->getIndices());
        for (u32 i = 0; i < indexCount; i++)
            outIndices[i] = src[i];
    }

    return true;
}

// ---------------------------------------------------------------------------
// Phase 1: unwrapMesh
// ---------------------------------------------------------------------------
bool LightmapBaker::unwrapMesh(
    IAnimatedMesh*      animMesh,
    const matrix4&      worldTransform,
    const BakeSettings& settings,
    MeshLightmap&       outLightmap)
{
    IMesh* mesh = animMesh->getMesh(0); // frame 0 — static geometry
    if (!mesh)
    {
        spdlog::error("LightmapBaker::unwrapMesh: animMesh->getMesh(0) returned null");
        return false;
    }

    const u32 bufferCount = mesh->getMeshBufferCount();
    if (bufferCount == 0)
    {
        spdlog::error("LightmapBaker::unwrapMesh: mesh has no buffers");
        return false;
    }

    // -----------------------------------------------------------------------
    // Build per-buffer source arrays and feed to xatlas
    // -----------------------------------------------------------------------
    xatlas::Atlas* atlas = xatlas::Create();

    // Keep source arrays alive until xatlas::Generate() finishes (it copies
    // data internally but we hold them through AddMesh at minimum).
    std::vector<std::vector<float>>    allPos(bufferCount);
    std::vector<std::vector<float>>    allNormal(bufferCount);
    std::vector<std::vector<float>>    allUV(bufferCount);
    std::vector<std::vector<uint32_t>> allIndices(bufferCount);

    uint32_t successfulBuffers = 0;
    for (u32 b = 0; b < bufferCount; b++)
    {
        IMeshBuffer* buf = mesh->getMeshBuffer(b);

        if (!extractBufferData(buf, allPos[b], allNormal[b], allUV[b], allIndices[b]))
        {
            spdlog::warn("LightmapBaker::unwrapMesh: skipping buffer {} (empty or unsupported)", b);
            // Still call AddMesh with dummy data so mesh indices stay aligned
            // with buffer indices. Use a degenerate single-triangle placeholder.
            static const float     dummyPos[9]  = {};
            static const float     dummyNorm[9] = {};
            static const uint32_t  dummyIdx[3]  = {0,1,2};
            xatlas::MeshDecl decl;
            decl.vertexPositionData   = dummyPos;
            decl.vertexPositionStride = sizeof(float) * 3;
            decl.vertexCount          = 3;
            decl.indexData            = dummyIdx;
            decl.indexCount           = 3;
            decl.indexFormat          = xatlas::IndexFormat::UInt32;
            xatlas::AddMesh(atlas, decl, bufferCount);
            continue;
        }

        xatlas::MeshDecl decl;
        decl.vertexPositionData   = allPos[b].data();
        decl.vertexPositionStride = sizeof(float) * 3;
        decl.vertexNormalData     = allNormal[b].data();
        decl.vertexNormalStride   = sizeof(float) * 3;
        decl.vertexUvData         = allUV[b].data();
        decl.vertexUvStride       = sizeof(float) * 2;
        decl.vertexCount          = static_cast<uint32_t>(allPos[b].size() / 3);
        decl.indexData            = allIndices[b].data();
        decl.indexCount           = static_cast<uint32_t>(allIndices[b].size());
        decl.indexFormat          = xatlas::IndexFormat::UInt32;

        xatlas::AddMeshError err = xatlas::AddMesh(atlas, decl, bufferCount);
        if (err != xatlas::AddMeshError::Success)
        {
            spdlog::error("LightmapBaker::unwrapMesh: xatlas AddMesh failed for buffer {}: {}",
                          b, xatlas::StringForEnum(err));
            xatlas::Destroy(atlas);
            return false;
        }
        ++successfulBuffers;
    }

    if (successfulBuffers == 0)
    {
        spdlog::error("LightmapBaker::unwrapMesh: all buffers failed extraction");
        xatlas::Destroy(atlas);
        return false;
    }

    // -----------------------------------------------------------------------
    // Generate lightmap UVs
    // -----------------------------------------------------------------------
    xatlas::PackOptions packOptions;
    packOptions.resolution = settings.resolution; // target size; actual may vary
    packOptions.padding    = settings.padding;
    packOptions.bilinear   = true;

    xatlas::Generate(atlas, xatlas::ChartOptions(), packOptions);

    if (atlas->width == 0 || atlas->height == 0)
    {
        spdlog::error("LightmapBaker::unwrapMesh: xatlas produced an empty atlas");
        xatlas::Destroy(atlas);
        return false;
    }

    spdlog::info("LightmapBaker: atlas {}x{}, {} charts across {} meshes",
                 atlas->width, atlas->height, atlas->chartCount, atlas->meshCount);

    // -----------------------------------------------------------------------
    // Build MeshLightmap from xatlas output
    // -----------------------------------------------------------------------
    outLightmap.width  = atlas->width;
    outLightmap.height = atlas->height;
    outLightmap.buffers.resize(bufferCount);

    // Normal matrix: transpose of inverse of world transform.
    // For transforms without non-uniform shear this is just the rotation part,
    // but we compute it properly to handle scaled objects correctly.
    matrix4 normalMatrix = worldTransform;
    normalMatrix.makeInverse();
    normalMatrix = normalMatrix.getTransposed();

    const float invW = 1.0f / static_cast<float>(atlas->width);
    const float invH = 1.0f / static_cast<float>(atlas->height);

    for (u32 b = 0; b < bufferCount; b++)
    {
        const xatlas::Mesh& xmesh   = atlas->meshes[b];
        MeshLightmap::BufferData& bufData = outLightmap.buffers[b];

        bufData.vertices.resize(xmesh.vertexCount);
        bufData.indices.resize(xmesh.indexCount);

        const float* srcPos    = allPos[b].empty()    ? nullptr : allPos[b].data();
        const float* srcNormal = allNormal[b].empty() ? nullptr : allNormal[b].data();
        const float* srcUV     = allUV[b].empty()     ? nullptr : allUV[b].data();

        for (u32 vi = 0; vi < xmesh.vertexCount; vi++)
        {
            const xatlas::Vertex& xv = xmesh.vertexArray[vi];
            const uint32_t orig = xv.xref; // maps back to the original vertex index

            MeshLightmap::Vertex& v = bufData.vertices[vi];

            if (srcPos)
            {
                vector3df op(srcPos[orig*3], srcPos[orig*3+1], srcPos[orig*3+2]);
                v.objPos = op;                                      // object-space (GPU mesh)
                worldTransform.transformVect(v.worldPos, op);       // world-space  (baking)
            }

            if (srcNormal)
            {
                vector3df on(srcNormal[orig*3], srcNormal[orig*3+1], srcNormal[orig*3+2]);
                v.objNormal = on;                                   // object-space (GPU mesh)
                normalMatrix.rotateVect(v.worldNormal, on);
                v.worldNormal.normalize();
            }

            if (srcUV)
                v.uv1 = vector2df(srcUV[orig*2], srcUV[orig*2+1]);

            // xatlas UVs are in pixel space [0..width, 0..height] — normalise to [0,1]
            v.uv2 = vector2df(xv.uv[0] * invW, xv.uv[1] * invH);
        }

        for (u32 ii = 0; ii < xmesh.indexCount; ii++)
            bufData.indices[ii] = xmesh.indexArray[ii];
    }

    // Pre-allocate pixel and texel storage (zeroed)
    const uint32_t texelCount = outLightmap.width * outLightmap.height;
    outLightmap.pixels.assign(texelCount * 3, 0.0f);
    outLightmap.texels.resize(texelCount);

    xatlas::Destroy(atlas);
    return true;
}

// ---------------------------------------------------------------------------
// Phase 3: Texel rasterization helpers
// ---------------------------------------------------------------------------

// Compute barycentric coordinates of point p with respect to triangle (a,b,c).
// Returns false if the triangle is degenerate.
// u,v,w are all >= -epsilon when p is inside (or on the edge of) the triangle.
bool LightmapBaker::barycentricCoords(
    const vector2df& p,
    const vector2df& a,
    const vector2df& b,
    const vector2df& c,
    float& u, float& v, float& w)
{
    const vector2df v0 = b - a;
    const vector2df v1 = c - a;
    const vector2df v2 = p - a;

    // Cramer's rule via dot products
    const float d00 = v0.dotProduct(v0);
    const float d01 = v0.dotProduct(v1);
    const float d11 = v1.dotProduct(v1);
    const float d20 = v2.dotProduct(v0);
    const float d21 = v2.dotProduct(v1);

    const float denom = d00 * d11 - d01 * d01;
    if (std::fabs(denom) < 1e-7f)
        return false;

    const float invDenom = 1.0f / denom;
    v = (d11 * d20 - d01 * d21) * invDenom;
    w = (d00 * d21 - d01 * d20) * invDenom;
    u = 1.0f - v - w;

    return true;
}

// ---------------------------------------------------------------------------
// Phase 3: rasterizeTexels
// ---------------------------------------------------------------------------
void LightmapBaker::rasterizeTexels(MeshLightmap& lightmap)
{
    const uint32_t W = lightmap.width;
    const uint32_t H = lightmap.height;

    // Texels are already default-constructed (covered=false).
    for (auto& t : lightmap.texels)
        t = MeshLightmap::Texel{};

    for (int b = 0; b < static_cast<int>(lightmap.buffers.size()); b++)
    {
        const MeshLightmap::BufferData& buf = lightmap.buffers[b];
        const uint32_t triCount = static_cast<uint32_t>(buf.indices.size()) / 3;

        for (uint32_t t = 0; t < triCount; t++)
        {
            const uint32_t i0 = buf.indices[t*3+0];
            const uint32_t i1 = buf.indices[t*3+1];
            const uint32_t i2 = buf.indices[t*3+2];

            const MeshLightmap::Vertex& v0 = buf.vertices[i0];
            const MeshLightmap::Vertex& v1 = buf.vertices[i1];
            const MeshLightmap::Vertex& v2 = buf.vertices[i2];

            // UV2 in pixel space
            const vector2df p0(v0.uv2.X * W, v0.uv2.Y * H);
            const vector2df p1(v1.uv2.X * W, v1.uv2.Y * H);
            const vector2df p2(v2.uv2.X * W, v2.uv2.Y * H);

            // Axis-aligned bounding box of triangle in pixel space, clamped to atlas
            const int minX = std::max(0, static_cast<int>(std::floor(std::min({p0.X, p1.X, p2.X}))));
            const int minY = std::max(0, static_cast<int>(std::floor(std::min({p0.Y, p1.Y, p2.Y}))));
            const int maxX = std::min(static_cast<int>(W) - 1, static_cast<int>(std::ceil(std::max({p0.X, p1.X, p2.X}))));
            const int maxY = std::min(static_cast<int>(H) - 1, static_cast<int>(std::ceil(std::max({p0.Y, p1.Y, p2.Y}))));

            for (int py = minY; py <= maxY; py++)
            {
                for (int px = minX; px <= maxX; px++)
                {
                    // Sample at texel centre
                    const vector2df sample(px + 0.5f, py + 0.5f);

                    float u, v, w;
                    if (!barycentricCoords(sample, p0, p1, p2, u, v, w))
                        continue;

                    // Small epsilon to allow texels just on the triangle edge
                    const float eps = 0.001f;
                    if (u < -eps || v < -eps || w < -eps)
                        continue;

                    // Interpolate world-space attributes
                    const vector3df worldPos =
                        v0.worldPos    * u + v1.worldPos    * v + v2.worldPos    * w;
                    const vector3df worldNormal =
                        v0.worldNormal * u + v1.worldNormal * v + v2.worldNormal * w;

                    MeshLightmap::Texel& texel = lightmap.texels[py * W + px];

                    // Don't overwrite a texel already covered by a closer (or any) triangle.
                    // First-write wins; for correct baking any covered texel is fine.
                    if (!texel.covered)
                    {
                        texel.worldPos    = worldPos;
                        vector3df n = worldNormal;
                        n.normalize();
                        texel.worldNormal = n;
                        texel.covered     = true;
                        texel.bufferIndex = b;
                    }
                }
            }
        }
    }

    // Count for diagnostics
    uint32_t coveredCount = 0;
    for (const auto& tx : lightmap.texels)
        if (tx.covered) ++coveredCount;

    spdlog::info("LightmapBaker: rasterized {} / {} texels covered ({:.1f}%)",
                 coveredCount, lightmap.texels.size(),
                 100.0f * coveredCount / std::max<uint32_t>(1, static_cast<uint32_t>(lightmap.texels.size())));
}

// ---------------------------------------------------------------------------
// Phase 4b: blurLightmap — separable Gaussian blur over covered texels.
// Smooths hard shadow edges in texel space without touching seam/uncovered
// texels, so dilation still fills chart borders cleanly afterwards.
// ---------------------------------------------------------------------------
void LightmapBaker::blurLightmap(MeshLightmap& lightmap, uint32_t radius)
{
    if (radius == 0) return;

    const uint32_t W = lightmap.width;
    const uint32_t H = lightmap.height;

    // Build a 1-D Gaussian kernel of size (2*radius+1).
    const int kSize = (int)(radius * 2) + 1;
    std::vector<float> kernel(kSize);
    const float sigma = (float)radius * 0.5f;
    float kSum = 0.0f;
    for (int k = 0; k < kSize; k++)
    {
        const float d = (float)(k - (int)radius);
        kernel[k] = std::exp(-d * d / (2.0f * sigma * sigma));
        kSum += kernel[k];
    }
    for (float& w : kernel) w /= kSum;

    std::vector<float> temp(W * H * 3, 0.0f);

    // Horizontal pass: pixels → temp
    for (uint32_t y = 0; y < H; y++)
    {
        for (uint32_t x = 0; x < W; x++)
        {
            if (!lightmap.texels[y * W + x].covered) continue;
            float r = 0.0f, g = 0.0f, b = 0.0f, wTotal = 0.0f;
            for (int k = 0; k < kSize; k++)
            {
                const int sx = (int)x + k - (int)radius;
                if (sx < 0 || sx >= (int)W) continue;
                if (!lightmap.texels[y * W + sx].covered) continue;
                const float w = kernel[k];
                r += lightmap.pixels[(y * W + sx) * 3 + 0] * w;
                g += lightmap.pixels[(y * W + sx) * 3 + 1] * w;
                b += lightmap.pixels[(y * W + sx) * 3 + 2] * w;
                wTotal += w;
            }
            if (wTotal > 0.0f)
            {
                temp[(y * W + x) * 3 + 0] = r / wTotal;
                temp[(y * W + x) * 3 + 1] = g / wTotal;
                temp[(y * W + x) * 3 + 2] = b / wTotal;
            }
        }
    }

    // Vertical pass: temp → pixels
    for (uint32_t y = 0; y < H; y++)
    {
        for (uint32_t x = 0; x < W; x++)
        {
            if (!lightmap.texels[y * W + x].covered) continue;
            float r = 0.0f, g = 0.0f, b = 0.0f, wTotal = 0.0f;
            for (int k = 0; k < kSize; k++)
            {
                const int sy = (int)y + k - (int)radius;
                if (sy < 0 || sy >= (int)H) continue;
                if (!lightmap.texels[sy * W + x].covered) continue;
                const float w = kernel[k];
                r += temp[(sy * W + x) * 3 + 0] * w;
                g += temp[(sy * W + x) * 3 + 1] * w;
                b += temp[(sy * W + x) * 3 + 2] * w;
                wTotal += w;
            }
            if (wTotal > 0.0f)
            {
                lightmap.pixels[(y * W + x) * 3 + 0] = r / wTotal;
                lightmap.pixels[(y * W + x) * 3 + 1] = g / wTotal;
                lightmap.pixels[(y * W + x) * 3 + 2] = b / wTotal;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Phase 5: Dilation
// Expands covered texels into uncovered neighbours to eliminate seam gaps.
// ---------------------------------------------------------------------------
void LightmapBaker::dilate(MeshLightmap& lightmap, uint32_t passes)
{
    const uint32_t W = lightmap.width;
    const uint32_t H = lightmap.height;

    static const int dx[] = { 1, -1,  0,  0 };
    static const int dy[] = { 0,  0,  1, -1 };

    for (uint32_t pass = 0; pass < passes; pass++)
    {
        std::vector<MeshLightmap::Texel> next = lightmap.texels;
        std::vector<float> nextPix = lightmap.pixels;

        for (uint32_t y = 0; y < H; y++)
        {
            for (uint32_t x = 0; x < W; x++)
            {
                if (lightmap.texels[y * W + x].covered)
                    continue;

                // Look for a covered neighbour
                for (int d = 0; d < 4; d++)
                {
                    const int nx = static_cast<int>(x) + dx[d];
                    const int ny = static_cast<int>(y) + dy[d];
                    if (nx < 0 || ny < 0 || nx >= static_cast<int>(W) || ny >= static_cast<int>(H))
                        continue;

                    const uint32_t nIdx = static_cast<uint32_t>(ny) * W + static_cast<uint32_t>(nx);
                    if (!lightmap.texels[nIdx].covered)
                        continue;

                    const uint32_t dstIdx = y * W + x;
                    next[dstIdx]          = lightmap.texels[nIdx];
                    next[dstIdx].covered  = false; // mark as dilated, not baked

                    nextPix[dstIdx * 3 + 0] = lightmap.pixels[nIdx * 3 + 0];
                    nextPix[dstIdx * 3 + 1] = lightmap.pixels[nIdx * 3 + 1];
                    nextPix[dstIdx * 3 + 2] = lightmap.pixels[nIdx * 3 + 2];
                    break;
                }
            }
        }

        lightmap.texels = std::move(next);
        lightmap.pixels = std::move(nextPix);
    }
}

// ---------------------------------------------------------------------------
// Phase 5: Upload to GPU
// ---------------------------------------------------------------------------
irr::video::ITexture* LightmapBaker::uploadLightmap(
    IVideoDriver*       driver,
    MeshLightmap&       lightmap,
    const std::string&  name)
{
    const uint32_t W = lightmap.width;
    const uint32_t H = lightmap.height;

    if (W == 0 || H == 0 || lightmap.pixels.empty())
    {
        spdlog::error("LightmapBaker::uploadLightmap: lightmap has no pixel data");
        return nullptr;
    }

    // Create a software image (RGBA8) and copy float pixels into it
    IImage* img = driver->createImage(
        ECF_A8R8G8B8,
        dimension2du(W, H));

    if (!img)
    {
        spdlog::error("LightmapBaker::uploadLightmap: failed to create IImage");
        return nullptr;
    }

    for (uint32_t y = 0; y < H; y++)
    {
        for (uint32_t x = 0; x < W; x++)
        {
            const uint32_t idx = y * W + x;
            const float r = std::min(lightmap.pixels[idx * 3 + 0], 1.0f);
            const float g = std::min(lightmap.pixels[idx * 3 + 1], 1.0f);
            const float b = std::min(lightmap.pixels[idx * 3 + 2], 1.0f);

            img->setPixel(x, y,
                SColor(
                    255,
                    static_cast<u32>(r * 255.0f),
                    static_cast<u32>(g * 255.0f),
                    static_cast<u32>(b * 255.0f)));
        }
    }

    ITexture* tex = driver->addTexture(name.c_str(), img);
    img->drop();

    if (!tex)
        spdlog::error("LightmapBaker::uploadLightmap: driver->addTexture failed for '{}'", name);
    else
        spdlog::info("LightmapBaker::uploadLightmap: uploaded '{}' ({}x{})", name, W, H);

    lightmap.texture = tex;
    return tex;
}

// ---------------------------------------------------------------------------
// Shadow ray helpers — thread-safe, no Irrlicht state involved
// ---------------------------------------------------------------------------

// Return the world-space position of vertex vi from any supported buffer type.
static vector3df getVertexWorldPos(IMeshBuffer* buf, u32 vi, const matrix4& worldT)
{
    vector3df p;
    switch (buf->getVertexType())
    {
    case EVT_STANDARD:
        p = static_cast<const S3DVertex*>(buf->getVertices())[vi].Pos; break;
    case EVT_2TCOORDS:
        p = static_cast<const S3DVertex2TCoords*>(buf->getVertices())[vi].Pos; break;
    case EVT_TANGENTS:
        p = static_cast<const S3DVertexTangents*>(buf->getVertices())[vi].Pos; break;
    default: break;
    }
    worldT.transformVect(p);
    return p;
}

// Möller–Trumbore ray-triangle intersection.
// Returns true if the ray from `orig` along unit `dir` hits `tri` at distance (0, maxDist).
static bool rayHitsTriangle(
    const vector3df&  orig,
    const vector3df&  dir,
    float             maxDist,
    const triangle3df& tri)
{
    const vector3df edge1 = tri.pointB - tri.pointA;
    const vector3df edge2 = tri.pointC - tri.pointA;
    const vector3df h     = dir.crossProduct(edge2);
    const float     a     = edge1.dotProduct(h);

    if (std::fabs(a) < 1e-7f) return false; // ray parallel to triangle

    const float     f = 1.0f / a;
    const vector3df s = orig - tri.pointA;
    const float     u = f * s.dotProduct(h);
    if (u < 0.0f || u > 1.0f) return false;

    const vector3df q = s.crossProduct(edge1);
    const float     v = f * dir.dotProduct(q);
    if (v < 0.0f || u + v > 1.0f) return false;

    const float t = f * edge2.dotProduct(q);
    return t > 1e-4f && t < maxDist;
}

// ---------------------------------------------------------------------------
// BVH for O(log N) shadow ray testing
// ---------------------------------------------------------------------------

struct BvhNode
{
    vector3df aabbMin;
    vector3df aabbMax;
    int       leftChild  = -1; // >= 0 on internal nodes
    int       rightChild = -1;
    int       triFirst   =  0; // leaf: first index into BvhAccel::triIdx
    int       triCount   =  0; // > 0 on leaves, 0 on internal nodes
};

struct BvhAccel
{
    std::vector<BvhNode> nodes;
    std::vector<int>     triIdx; // reordered indices into sceneTriangles
};

static int buildBvhNode(
    BvhAccel&                       accel,
    const std::vector<triangle3df>& tris,
    int start, int end)
{
    const int nodeIdx = static_cast<int>(accel.nodes.size());
    accel.nodes.push_back(BvhNode{});

    // Compute AABB of all triangles in [start, end)
    vector3df aabbMin( 1e30f,  1e30f,  1e30f);
    vector3df aabbMax(-1e30f, -1e30f, -1e30f);
    for (int i = start; i < end; i++)
    {
        const triangle3df& t = tris[accel.triIdx[i]];
        aabbMin.X = std::min({aabbMin.X, t.pointA.X, t.pointB.X, t.pointC.X});
        aabbMin.Y = std::min({aabbMin.Y, t.pointA.Y, t.pointB.Y, t.pointC.Y});
        aabbMin.Z = std::min({aabbMin.Z, t.pointA.Z, t.pointB.Z, t.pointC.Z});
        aabbMax.X = std::max({aabbMax.X, t.pointA.X, t.pointB.X, t.pointC.X});
        aabbMax.Y = std::max({aabbMax.Y, t.pointA.Y, t.pointB.Y, t.pointC.Y});
        aabbMax.Z = std::max({aabbMax.Z, t.pointA.Z, t.pointB.Z, t.pointC.Z});
    }
    accel.nodes[nodeIdx].aabbMin = aabbMin;
    accel.nodes[nodeIdx].aabbMax = aabbMax;

    const int count = end - start;
    if (count <= 8) // leaf
    {
        accel.nodes[nodeIdx].triFirst = start;
        accel.nodes[nodeIdx].triCount = count;
        return nodeIdx;
    }

    // Split along the longest AABB axis at the centroid median
    const vector3df extent = aabbMax - aabbMin;
    int axis = 0;
    if (extent.Y > extent.X) axis = 1;
    if (extent.Z > (axis == 0 ? extent.X : extent.Y)) axis = 2;

    const int mid = (start + end) / 2;
    std::nth_element(
        accel.triIdx.begin() + start,
        accel.triIdx.begin() + mid,
        accel.triIdx.begin() + end,
        [&](int a, int b)
        {
            const triangle3df& ta = tris[a];
            const triangle3df& tb = tris[b];
            const float ca = axis == 0 ? (ta.pointA.X + ta.pointB.X + ta.pointC.X)
                           : axis == 1 ? (ta.pointA.Y + ta.pointB.Y + ta.pointC.Y)
                                       : (ta.pointA.Z + ta.pointB.Z + ta.pointC.Z);
            const float cb = axis == 0 ? (tb.pointA.X + tb.pointB.X + tb.pointC.X)
                           : axis == 1 ? (tb.pointA.Y + tb.pointB.Y + tb.pointC.Y)
                                       : (tb.pointA.Z + tb.pointB.Z + tb.pointC.Z);
            return ca < cb;
        });

    const int left  = buildBvhNode(accel, tris, start, mid);
    const int right = buildBvhNode(accel, tris, mid,   end);
    accel.nodes[nodeIdx].leftChild  = left;
    accel.nodes[nodeIdx].rightChild = right;
    // triCount stays 0 (internal node marker)
    return nodeIdx;
}

static BvhAccel buildBvh(const std::vector<triangle3df>& tris)
{
    BvhAccel accel;
    if (tris.empty()) return accel;

    const int N = static_cast<int>(tris.size());
    accel.nodes.reserve(N * 2); // 2N-1 nodes worst case
    accel.triIdx.resize(N);
    std::iota(accel.triIdx.begin(), accel.triIdx.end(), 0);

    buildBvhNode(accel, tris, 0, N);
    return accel;
}

// Ray-AABB slab test using precomputed inverse direction.
static bool rayHitsAABB(
    const vector3df& orig, const vector3df& invDir, float maxDist,
    const vector3df& aabbMin, const vector3df& aabbMax)
{
    float tmin = 0.0f, tmax = maxDist;

    auto testSlab = [&](float o, float id, float lo, float hi)
    {
        float t0 = (lo - o) * id, t1 = (hi - o) * id;
        if (t0 > t1) std::swap(t0, t1);
        tmin = std::max(tmin, t0);
        tmax = std::min(tmax, t1);
    };
    testSlab(orig.X, invDir.X, aabbMin.X, aabbMax.X);
    testSlab(orig.Y, invDir.Y, aabbMin.Y, aabbMax.Y);
    testSlab(orig.Z, invDir.Z, aabbMin.Z, aabbMax.Z);
    return tmin <= tmax;
}

// BVH traversal — returns true if any triangle occludes the path orig→light.
static bool rayHitsScene(
    const vector3df&                orig,
    const vector3df&                dir,
    float                           maxDist,
    const BvhAccel&                 accel,
    const std::vector<triangle3df>& tris)
{
    if (accel.nodes.empty()) return false;

    const vector3df invDir(
        dir.X != 0.0f ? 1.0f / dir.X : 1e30f,
        dir.Y != 0.0f ? 1.0f / dir.Y : 1e30f,
        dir.Z != 0.0f ? 1.0f / dir.Z : 1e30f);

    int stack[64], top = 0;
    stack[top++] = 0; // root

    while (top > 0)
    {
        const BvhNode& node = accel.nodes[stack[--top]];

        if (!rayHitsAABB(orig, invDir, maxDist, node.aabbMin, node.aabbMax))
            continue;

        if (node.triCount > 0) // leaf
        {
            for (int i = 0; i < node.triCount; i++)
                if (rayHitsTriangle(orig, dir, maxDist, tris[accel.triIdx[node.triFirst + i]]))
                    return true;
        }
        else // internal
        {
            stack[top++] = node.leftChild;
            stack[top++] = node.rightChild;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Phase 4: bake — direct lighting per texel
// ---------------------------------------------------------------------------
void LightmapBaker::bake(
    MeshLightmap&                              lightmap,
    const std::vector<BakeLight>&              lights,
    const std::vector<triangle3df>&            sceneTriangles,
    const BakeSettings&                        settings)
{
    const uint32_t texelCount = static_cast<uint32_t>(lightmap.texels.size());

    // Build BVH once before spawning threads; all threads share it read-only.
    const BvhAccel bvh = buildBvh(sceneTriangles);

    // Split texels evenly across hardware threads.
    // Each thread writes to a disjoint slice of lightmap.pixels (indices [begin*3, end*3))
    // and only reads bvh/sceneTriangles/lights — no locks needed.
    const uint32_t threadCount = std::max(1u, std::thread::hardware_concurrency());
    const uint32_t chunk       = (texelCount + threadCount - 1) / threadCount;

    auto bakeRange = [&](uint32_t begin, uint32_t end)
    {
        for (uint32_t i = begin; i < end; i++)
        {
            const MeshLightmap::Texel& texel = lightmap.texels[i];
            if (!texel.covered)
                continue;

            // Offset start point along normal to avoid self-intersection
            const vector3df rayOrigin = texel.worldPos + texel.worldNormal * settings.shadowBias;

            float accumR = 0.0f, accumG = 0.0f, accumB = 0.0f;

            for (const BakeLight& light : lights)
            {
                // Directional area light: uniform parallel rays, no distance falloff
                if (light.type == 2) // LT_AREA
                {
                    const vector3df L = -light.direction; // rays travel opposite to facing dir
                    const float NdotL = texel.worldNormal.dotProduct(L);
                    if (NdotL <= 0.0f)
                        continue;
                    if (rayHitsScene(rayOrigin, L, 1e6f, bvh, sceneTriangles))
                        continue;
                    accumR += light.color.r * NdotL;
                    accumG += light.color.g * NdotL;
                    accumB += light.color.b * NdotL;
                    continue;
                }

                const vector3df toLight = light.position - rayOrigin;
                const float     distSq  = toLight.getLengthSQ();
                const float     dist    = std::sqrt(distSq);

                // Outside light radius — skip
                const float radSq = light.radius * light.radius;
                if (distSq >= radSq)
                    continue;

                const vector3df L = toLight / dist; // normalised direction to light

                // N·L — back-face check
                const float NdotL = texel.worldNormal.dotProduct(L);
                if (NdotL <= 0.0f)
                    continue;

                // Spot-light cone check (type 1 = LT_SPOT)
                if (light.type == 1)
                {
                    const float cosOuter = std::cos(irr::core::DEGTORAD * light.outerCone);
                    const vector3df fromLight = -L;
                    if (fromLight.dotProduct(light.direction) < cosOuter)
                        continue;
                }

                if (rayHitsScene(rayOrigin, L, dist, bvh, sceneTriangles))
                    continue; // texel is in shadow for this light

                // Quadratic attenuation matching the dynamic shader
                const float atten = irr::core::clamp(1.0f - distSq / radSq, 0.0f, 1.0f);

                accumR += light.color.r * NdotL * atten;
                accumG += light.color.g * NdotL * atten;
                accumB += light.color.b * NdotL * atten;
            }

            lightmap.pixels[i * 3 + 0] = accumR;
            lightmap.pixels[i * 3 + 1] = accumG;
            lightmap.pixels[i * 3 + 2] = accumB;
            g_texelsDone.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    for (uint32_t t = 0; t < threadCount; t++)
    {
        const uint32_t begin = t * chunk;
        const uint32_t end   = std::min(begin + chunk, texelCount);
        if (begin >= texelCount) break;
        threads.emplace_back(bakeRange, begin, end);
    }

    for (auto& th : threads)
        th.join();

    spdlog::info("LightmapBaker::bake: {} texels, {} threads, BVH {} nodes / {} tris",
                 texelCount, threads.size(), bvh.nodes.size(), sceneTriangles.size());
}

// ---------------------------------------------------------------------------
// Phase 5c: applyLightmapUVsToNode
// Rebuilds the scene node's mesh as EVT_2TCOORDS so that the GPU receives
// the xatlas-generated UV2 in gl_MultiTexCoord1.  The original diffuse
// texture in slot 0 is preserved; the lightmap texture is set in slot 1.
// ---------------------------------------------------------------------------
void LightmapBaker::applyLightmapUVsToNode(
    const MeshLightmap&         lightmap,
    IAnimatedMeshSceneNode*     node)
{
    // Save existing materials so the diffuse texture survives setMesh().
    const u32 origMatCount = node->getMaterialCount();
    std::vector<video::SMaterial> savedMats(origMatCount);
    for (u32 m = 0; m < origMatCount; m++)
        savedMats[m] = node->getMaterial(m);

    // Build a new SMesh — one CMeshBuffer<S3DVertex2TCoords> per buffer.
    scene::SMesh* newMesh = new scene::SMesh();

    for (u32 bi = 0; bi < static_cast<u32>(lightmap.buffers.size()); ++bi)
    {
        const MeshLightmap::BufferData& bufData = lightmap.buffers[bi];
        auto* newBuf = new scene::CMeshBuffer<video::S3DVertex2TCoords>();
        newBuf->Vertices.reallocate(static_cast<u32>(bufData.vertices.size()));
        newBuf->Indices.reallocate(static_cast<u32>(bufData.indices.size()));

        // Preserve the original buffer material (diffuse texture, material type, etc.)
        // so that code reading buf->getMaterial().getTexture(0) still works after the bake.
        if (bi < origMatCount)
            newBuf->getMaterial() = savedMats[bi];

        for (const MeshLightmap::Vertex& v : bufData.vertices)
        {
            video::S3DVertex2TCoords vert;
            vert.Pos      = v.objPos;
            vert.Normal   = v.objNormal;
            vert.Color    = video::SColor(255, 255, 255, 255);
            vert.TCoords  = v.uv1;   // diffuse UV  → gl_MultiTexCoord0
            vert.TCoords2 = v.uv2;   // lightmap UV → gl_MultiTexCoord1
            newBuf->Vertices.push_back(vert);
        }

        for (uint32_t idx : bufData.indices)
        {
            // Warn and clamp if the mesh exceeds u16 range (>65535 verts).
            if (idx > 0xFFFF)
            {
                spdlog::warn("LightmapBaker::applyLightmapUVsToNode: index {} overflows u16, clamping", idx);
                idx = 0xFFFF;
            }
            newBuf->Indices.push_back(static_cast<u16>(idx));
        }

        newBuf->recalculateBoundingBox();
        newMesh->addMeshBuffer(newBuf);
        newBuf->drop();
    }

    newMesh->recalculateBoundingBox();

    auto* animMesh = new scene::SAnimatedMesh(newMesh);
    newMesh->drop();

    // Rebuilt mesh replaces the original (which had EHM_STATIC from RenderSystem);
    // re-apply the hint so lightmapped geometry also renders from GPU buffers.
    animMesh->setHardwareMappingHint(scene::EHM_STATIC);

    node->setMesh(animMesh);
    animMesh->drop();

    // Restore materials (setMesh may resize the material array).
    const u32 newMatCount = node->getMaterialCount();
    for (u32 m = 0; m < newMatCount && m < origMatCount; m++)
        node->getMaterial(m) = savedMats[m];

    // Apply lightmap to slot 1 on all materials.
    if (lightmap.texture)
    {
        for (u32 m = 0; m < node->getMaterialCount(); m++)
            node->getMaterial(m).setTexture(1, lightmap.texture);
    }
}

// ---------------------------------------------------------------------------
// BinWriter — simple byte-buffer builder used by collectLightmapFiles.
// Defined at file scope because MSVC (C2892) prohibits member templates on
// local classes (classes defined inside a function body).
// ---------------------------------------------------------------------------
struct BinWriter
{
    std::vector<uint8_t> data;

    void writeBytes(const void* src, size_t n)
    {
        const uint8_t* p = static_cast<const uint8_t*>(src);
        data.insert(data.end(), p, p + n);
    }

    template<class T>
    void write(T v) { writeBytes(&v, sizeof(v)); }
};

// ---------------------------------------------------------------------------
// collectLightmapFiles — gather all baked lightmap data into memory buffers
// ---------------------------------------------------------------------------
// LMSV binary format (uvmesh):
//   uint32 magic   = 0x4C4D5356
//   uint32 version = 1
//   uint32 bufferCount
//   per buffer:
//     uint32 vertexCount
//     uint32 indexCount
//     per vertex: float[3] objPos, float[3] objNormal, float[2] uv1, float[2] uv2
//     per index:  uint16
// ---------------------------------------------------------------------------
std::vector<LightmapBaker::LightmapFiles> LightmapBaker::collectLightmapFiles(
    IVideoDriver*       driver,
    const std::string&  tempDir)
{
    auto* world    = WorldManager::Get()->world();
    auto  entities = world->getEntities();

    std::vector<LightmapFiles> result;

    for (auto& ent : entities)
    {
        if (!ent.hasComponent<MeshComponent>() || !ent.hasComponent<DescriptorComponent>())
            continue;

        auto& mc = ent.getComponent<MeshComponent>();
        auto& dc = ent.getComponent<DescriptorComponent>();

        if (!mc.lightmap || !mc.lightmap->texture || !mc.node)
            continue;

        LightmapFiles lf;
        lf.id = dc.id;

        // --- PNG: write to a temp file via Irrlicht, read back as bytes ---
        {
            ITexture* tex = mc.lightmap->texture;
            // createImage(ITexture*, pos, size) extracts a CPU-side software image.
            IImage* img = driver->createImage(
                tex,
                core::position2di(0, 0),
                tex->getSize());

            if (img)
            {
                const std::string tmpPath =
                    tempDir + "/lightmap_" + std::to_string(dc.id) + "_tmp.png";

                driver->writeImageToFile(img, tmpPath.c_str());
                img->drop();

                std::ifstream ifs(tmpPath, std::ios::binary);
                if (ifs)
                {
                    lf.pngBytes.assign(
                        std::istreambuf_iterator<char>(ifs),
                        std::istreambuf_iterator<char>());
                }
                std::remove(tmpPath.c_str());
            }
        }

        // --- UVMesh: extract from the scene node's EVT_2TCOORDS mesh ---
        {
            IMesh* mesh = mc.node->getMesh()->getMesh(0);
            if (!mesh)
            {
                spdlog::warn("LightmapBaker::collectLightmapFiles: no mesh on node for '{}'", dc.name);
                continue;
            }

            const u32 bufCount = mesh->getMeshBufferCount();

            BinWriter bw;

            bw.write(static_cast<uint32_t>(0x4C4D5356)); // magic 'LMSV'
            bw.write(static_cast<uint32_t>(1));           // version
            bw.write(static_cast<uint32_t>(bufCount));

            for (u32 b = 0; b < bufCount; b++)
            {
                IMeshBuffer* mbuf = mesh->getMeshBuffer(b);
                const u32 vertCount  = mbuf->getVertexCount();
                const u32 indexCount = mbuf->getIndexCount();

                bw.write(static_cast<uint32_t>(vertCount));
                bw.write(static_cast<uint32_t>(indexCount));

                const S3DVertex2TCoords* verts =
                    static_cast<const S3DVertex2TCoords*>(mbuf->getVertices());

                for (u32 i = 0; i < vertCount; i++)
                {
                    bw.write(verts[i].Pos.X);      bw.write(verts[i].Pos.Y);      bw.write(verts[i].Pos.Z);
                    bw.write(verts[i].Normal.X);   bw.write(verts[i].Normal.Y);   bw.write(verts[i].Normal.Z);
                    bw.write(verts[i].TCoords.X);  bw.write(verts[i].TCoords.Y);
                    bw.write(verts[i].TCoords2.X); bw.write(verts[i].TCoords2.Y);
                }

                // applyLightmapUVsToNode always writes u16 indices
                const u16* indices = static_cast<const u16*>(mbuf->getIndices());
                for (u32 i = 0; i < indexCount; i++)
                    bw.write(static_cast<uint16_t>(indices[i]));
            }

            lf.uvmeshBytes = std::move(bw.data);
        }

        if (!lf.pngBytes.empty() && !lf.uvmeshBytes.empty())
        {
            spdlog::info("LightmapBaker::collectLightmapFiles: collected '{}' ({} PNG bytes, {} uvmesh bytes)",
                dc.name, lf.pngBytes.size(), lf.uvmeshBytes.size());
            result.push_back(std::move(lf));
        }
        else
        {
            spdlog::warn("LightmapBaker::collectLightmapFiles: skipping '{}' — PNG or uvmesh data empty", dc.name);
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// loadLightmaps — restore baked lightmaps from a mounted zip
// ---------------------------------------------------------------------------
void LightmapBaker::loadLightmaps(io::IFileSystem* fs, IVideoDriver* driver)
{
    auto* world    = WorldManager::Get()->world();
    auto  entities = world->getEntities();

    for (auto& ent : entities)
    {
        if (!ent.hasComponent<MeshComponent>() || !ent.hasComponent<DescriptorComponent>())
            continue;

        auto& dc = ent.getComponent<DescriptorComponent>();
        if (dc.type != ET_STATIC)
            continue;

        auto& mc = ent.getComponent<MeshComponent>();
        if (!mc.node)
            continue;

        const std::string uvmeshName = "lightmap_" + std::to_string(dc.id) + ".uvmesh";
        irr::io::IReadFile* uvFile = fs->createAndOpenFile(uvmeshName.c_str());
        if (!uvFile)
            continue; // no lightmap baked for this entity — skip silently

        // Read binary blob
        std::vector<uint8_t> uvData(static_cast<size_t>(uvFile->getSize()));
        uvFile->read(uvData.data(), static_cast<u32>(uvData.size()));
        uvFile->drop();

        // ---- Parse LMSV binary ----
        const uint8_t* ptr = uvData.data();
        const uint8_t* end = ptr + uvData.size();

        auto readU32 = [&]() -> uint32_t { uint32_t v; memcpy(&v, ptr, 4); ptr += 4; return v; };
        auto readU16 = [&]() -> uint16_t { uint16_t v; memcpy(&v, ptr, 2); ptr += 2; return v; };
        auto readF32 = [&]() -> float    { float    v; memcpy(&v, ptr, 4); ptr += 4; return v; };

        const uint32_t magic = readU32();
        if (magic != 0x4C4D5356)
        {
            spdlog::warn("LightmapBaker::loadLightmaps: bad magic in '{}', skipping", uvmeshName);
            continue;
        }
        /*version =*/ readU32();
        const uint32_t bufCount = readU32();

        MeshLightmap* lm = new MeshLightmap();
        lm->buffers.resize(bufCount);

        for (uint32_t b = 0; b < bufCount && ptr < end; b++)
        {
            const uint32_t vertCount  = readU32();
            const uint32_t indexCount = readU32();

            lm->buffers[b].vertices.resize(vertCount);
            lm->buffers[b].indices.resize(indexCount);

            for (uint32_t vi = 0; vi < vertCount && ptr < end; vi++)
            {
                MeshLightmap::Vertex& v = lm->buffers[b].vertices[vi];
                v.objPos.X    = readF32(); v.objPos.Y    = readF32(); v.objPos.Z    = readF32();
                v.objNormal.X = readF32(); v.objNormal.Y = readF32(); v.objNormal.Z = readF32();
                v.uv1.X = readF32(); v.uv1.Y = readF32();
                v.uv2.X = readF32(); v.uv2.Y = readF32();
            }
            for (uint32_t ii = 0; ii < indexCount && ptr < end; ii++)
                lm->buffers[b].indices[ii] = readU16();
        }

        // Load the PNG texture — driver->getTexture goes through the mounted zip
        const std::string pngName = "lightmap_" + std::to_string(dc.id) + ".png";
        lm->texture = driver->getTexture(pngName.c_str());
        if (!lm->texture)
        {
            spdlog::warn("LightmapBaker::loadLightmaps: texture '{}' not found in archive", pngName);
            delete lm;
            continue;
        }
        lm->width  = lm->texture->getSize().Width;
        lm->height = lm->texture->getSize().Height;

        // Remove any existing lightmap on this entity (re-load scenario)
        if (mc.lightmap)
        {
            if (mc.lightmap->texture)
            {
                for (u32 m = 0; m < mc.node->getMaterialCount(); m++)
                    if (mc.node->getMaterial(m).TextureLayer[1].Texture == mc.lightmap->texture)
                        mc.node->getMaterial(m).setTexture(1, nullptr);
                driver->removeTexture(mc.lightmap->texture);
            }
            delete mc.lightmap;
        }
        mc.lightmap = lm;

        // Rebuild the scene node with EVT_2TCOORDS and apply the lightmap texture
        applyLightmapUVsToNode(*mc.lightmap, mc.node);

        // Free the CPU-side vertex data — it has been uploaded to the GPU mesh
        { std::vector<MeshLightmap::BufferData>().swap(mc.lightmap->buffers); }

        spdlog::info("LightmapBaker::loadLightmaps: applied lightmap to '{}'", dc.name);
    }
}

// ---------------------------------------------------------------------------
// Async bake state
// ---------------------------------------------------------------------------
namespace {

struct PendingUpload
{
    MeshComponent* mc;
    MeshLightmap*  lightmap; // owned; transferred to mc->lightmap on the main thread
    std::string    texName;
};

std::atomic<bool>          g_baking{false};
std::mutex                 g_uploadMutex;
std::vector<PendingUpload> g_pendingUploads;

} // namespace

float LightmapBaker::getBakeProgress()
{
    const int total = g_texelsTotal.load(std::memory_order_relaxed);
    return total > 0 ? static_cast<float>(g_texelsDone.load(std::memory_order_relaxed)) / static_cast<float>(total) : 0.0f;
}

bool LightmapBaker::isBaking()          { return g_baking.load(); }
bool LightmapBaker::hasPendingUploads() { std::lock_guard<std::mutex> lk(g_uploadMutex); return !g_pendingUploads.empty(); }

void LightmapBaker::flushPendingUploads(IVideoDriver* driver)
{
    std::vector<PendingUpload> batch;
    {
        std::lock_guard<std::mutex> lk(g_uploadMutex);
        batch = std::move(g_pendingUploads);
    }

    for (auto& upload : batch)
    {
        MeshComponent* mc = upload.mc;

        // Remove old lightmap — GPU and CPU
        if (mc->lightmap)
        {
            if (mc->lightmap->texture)
            {
                for (u32 m = 0; m < mc->node->getMaterialCount(); m++)
                    if (mc->node->getMaterial(m).TextureLayer[1].Texture == mc->lightmap->texture)
                        mc->node->getMaterial(m).setTexture(1, nullptr);
                driver->removeTexture(mc->lightmap->texture);
            }
            delete mc->lightmap;
        }
        mc->lightmap = upload.lightmap;

        // GPU upload + scene node rebuild (must be main thread)
        uploadLightmap(driver, *mc->lightmap, upload.texName);
        applyLightmapUVsToNode(*mc->lightmap, mc->node);

        // Free CPU-side intermediates — data is now on the GPU
        { std::vector<MeshLightmap::BufferData>().swap(mc->lightmap->buffers); }
        { std::vector<MeshLightmap::Texel>().swap(mc->lightmap->texels); }
        { std::vector<float>().swap(mc->lightmap->pixels); }
    }
}

void LightmapBaker::bakeSceneAsync(const BakeSettings& settings)
{
    if (g_baking.load())
    {
        spdlog::warn("LightmapBaker::bakeSceneAsync: bake already in progress, ignoring");
        return;
    }

    auto* world    = WorldManager::Get()->world();
    auto* smgr     = RenderManager::Get()->sceneManager();
    auto  entities = world->getEntities();

    // ------------------------------------------------------------------
    // Collect everything we need from the main thread before launching.
    // This avoids any concurrent access to Irrlicht scene objects.
    // ------------------------------------------------------------------

    // Scene triangles — only ET_STATIC entities cast shadows
    std::vector<triangle3df> sceneTriangles;
    for (auto& ent : entities)
    {
        if (!ent.hasComponent<MeshComponent>() || !ent.hasComponent<TransformComponent>() || !ent.hasComponent<DescriptorComponent>())
            continue;
        if (ent.getComponent<DescriptorComponent>().type != ET_STATIC) continue;
        auto& mc = ent.getComponent<MeshComponent>();
        if (!mc.trimesh || !mc.node) continue;
        const matrix4& worldT = mc.node->getAbsoluteTransformation();
        IMesh* mesh = mc.trimesh->getMesh(0);
        for (u32 b = 0; b < mesh->getMeshBufferCount(); b++)
        {
            IMeshBuffer* buf = mesh->getMeshBuffer(b);
            const u32 indexCount = buf->getIndexCount();
            auto addTri = [&](u32 i0, u32 i1, u32 i2) {
                sceneTriangles.push_back({
                    getVertexWorldPos(buf, i0, worldT),
                    getVertexWorldPos(buf, i1, worldT),
                    getVertexWorldPos(buf, i2, worldT) });
            };
            if (buf->getIndexType() == EIT_16BIT) {
                const u16* idx = static_cast<const u16*>(buf->getIndices());
                for (u32 i = 0; i + 2 < indexCount; i += 3) addTri(idx[i], idx[i+1], idx[i+2]);
            } else {
                const u32* idx = reinterpret_cast<const u32*>(buf->getIndices());
                for (u32 i = 0; i + 2 < indexCount; i += 3) addTri(idx[i], idx[i+1], idx[i+2]);
            }
        }
    }

    // Static lights
    std::vector<BakeLight> lights;
    for (auto& ent : entities)
    {
        if (!ent.hasComponent<LightComponent>() || !ent.hasComponent<TransformComponent>()) continue;
        auto& lc = ent.getComponent<LightComponent>();
        if (lc.mode != LM_STATIC || !lc.visible) continue;
        auto& tc = ent.getComponent<TransformComponent>();
        if (!tc.node) continue;
        BakeLight bl;
        bl.position  = tc.node->getAbsolutePosition() + lc.offset;
        bl.color     = lc.color_diffuse;
        bl.radius    = lc.radius;
        bl.type      = static_cast<int>(lc.type);
        bl.outerCone = lc.outerCone;
        if (lc.type == LT_SPOT || lc.type == LT_AREA) {
            matrix4 rotMat;
            rotMat.setRotationDegrees(tc.node->getAbsoluteTransformation().getRotationDegrees());
            vector3df dir(0.0f, 0.0f, -1.0f);
            rotMat.rotateVect(dir);
            bl.direction = dir.normalize();
        }
        lights.push_back(bl);
    }

    // Entities to bake — capture pointers and world transforms
    struct EntityToBake { MeshComponent* mc; matrix4 worldTransform; std::string texName; };
    std::vector<EntityToBake> toBake;
    for (auto& ent : entities)
    {
        if (!ent.hasComponent<MeshComponent>() || !ent.hasComponent<TransformComponent>()) continue;
        auto& dc = ent.getComponent<DescriptorComponent>();
        if (dc.type != ET_STATIC) continue;
        auto& mc = ent.getComponent<MeshComponent>();
        if (!mc.trimesh || !mc.node) continue;
        auto& tc = ent.getComponent<TransformComponent>();
        if (!tc.node) continue;
        toBake.push_back({ &mc, tc.node->getAbsoluteTransformation(),
                           "lightmap_" + std::to_string(dc.id) });
    }

    // Reset progress — texel totals are filled after Phase 1 in the background thread
    g_texelsDone  = 0;
    g_texelsTotal = 0;
    g_baking      = true;
    { std::lock_guard<std::mutex> lk(g_uploadMutex); g_pendingUploads.clear(); }

    spdlog::info("LightmapBaker::bakeSceneAsync: {} entities, {} tris, {} lights",
                 toBake.size(), sceneTriangles.size(), lights.size());

    // ------------------------------------------------------------------
    // Background thread: all CPU work only
    // ------------------------------------------------------------------
    std::thread([settings,
                 sceneTriangles = std::move(sceneTriangles),
                 lights         = std::move(lights),
                 toBake         = std::move(toBake)]()
    {
        // Phase 1: unwrap + rasterize all entities to discover total covered texels.
        struct ReadyEntity { MeshComponent* mc; MeshLightmap* lm; std::string texName; };
        std::vector<ReadyEntity> ready;
        ready.reserve(toBake.size());

        int totalCovered = 0;
        for (const auto& ed : toBake)
        {
            auto* lm = new MeshLightmap();
            if (!unwrapMesh(ed.mc->trimesh, ed.worldTransform, settings, *lm))
            {
                spdlog::warn("LightmapBaker: unwrapMesh failed for '{}', skipping", ed.texName);
                delete lm;
                continue;
            }
            rasterizeTexels(*lm);
            for (const auto& t : lm->texels)
                if (t.covered) totalCovered++;
            ready.push_back({ ed.mc, lm, ed.texName });
        }

        // Now we know total work; reset per-texel counters before baking.
        g_texelsDone.store(0, std::memory_order_relaxed);
        g_texelsTotal.store(totalCovered, std::memory_order_relaxed);

        // Phase 2: bake (increments g_texelsDone per covered texel) + dilate + queue.
        for (auto& r : ready)
        {
            spdlog::info("LightmapBaker: baking '{}'...", r.texName);
            bake(*r.lm, lights, sceneTriangles, settings);
            blurLightmap(*r.lm, settings.blurRadius);
            dilate(*r.lm, settings.dilationPasses);

            {
                std::lock_guard<std::mutex> lk(g_uploadMutex);
                g_pendingUploads.push_back({ r.mc, r.lm, r.texName });
            }
        }

        g_baking = false;
        spdlog::info("LightmapBaker::bakeSceneAsync: CPU bake complete");
    }).detach();
}

// ---------------------------------------------------------------------------
// bakeScene — full pipeline orchestrator
// ---------------------------------------------------------------------------
void LightmapBaker::bakeScene(const BakeSettings& settings)
{
    auto* world  = WorldManager::Get()->world();
    auto* driver = RenderManager::Get()->driver();

    auto entities = world->getEntities();

    // -----------------------------------------------------------------------
    // 1. Extract all scene geometry into a flat world-space triangle array.
    //    Only ET_STATIC entities cast shadows.
    // -----------------------------------------------------------------------
    std::vector<triangle3df> sceneTriangles;

    for (auto& ent : entities)
    {
        if (!ent.hasComponent<MeshComponent>() || !ent.hasComponent<TransformComponent>() || !ent.hasComponent<DescriptorComponent>())
            continue;

        if (ent.getComponent<DescriptorComponent>().type != ET_STATIC)
            continue;

        auto& mc = ent.getComponent<MeshComponent>();
        if (!mc.trimesh || !mc.node)
            continue;

        const matrix4& worldT = mc.node->getAbsoluteTransformation();
        IMesh* mesh = mc.trimesh->getMesh(0);

        for (u32 b = 0; b < mesh->getMeshBufferCount(); b++)
        {
            IMeshBuffer* buf = mesh->getMeshBuffer(b);
            const u32 indexCount = buf->getIndexCount();

            auto addTri = [&](u32 i0, u32 i1, u32 i2)
            {
                sceneTriangles.push_back({
                    getVertexWorldPos(buf, i0, worldT),
                    getVertexWorldPos(buf, i1, worldT),
                    getVertexWorldPos(buf, i2, worldT)
                });
            };

            if (buf->getIndexType() == EIT_16BIT)
            {
                const u16* idx = static_cast<const u16*>(buf->getIndices());
                for (u32 i = 0; i + 2 < indexCount; i += 3)
                    addTri(idx[i], idx[i+1], idx[i+2]);
            }
            else
            {
                const u32* idx = reinterpret_cast<const u32*>(buf->getIndices());
                for (u32 i = 0; i + 2 < indexCount; i += 3)
                    addTri(idx[i], idx[i+1], idx[i+2]);
            }
        }
    }

    // Also add prop geometry for shadow casting (castShadows==true props block light
    // even if they are not themselves baked).
    if (PropManager::Get())
    {
        for (auto& prop : PropManager::Get()->getAllProps())
        {
            if (!prop.castShadows || !prop.trimesh || !prop.node) continue;

            const matrix4& worldT = prop.node->getAbsoluteTransformation();
            IMesh* mesh = prop.trimesh->getMesh(0);

            for (u32 b = 0; b < mesh->getMeshBufferCount(); b++)
            {
                IMeshBuffer* buf = mesh->getMeshBuffer(b);
                const u32 indexCount = buf->getIndexCount();

                auto addTri = [&](u32 i0, u32 i1, u32 i2)
                {
                    sceneTriangles.push_back({
                        getVertexWorldPos(buf, i0, worldT),
                        getVertexWorldPos(buf, i1, worldT),
                        getVertexWorldPos(buf, i2, worldT)
                    });
                };

                if (buf->getIndexType() == EIT_16BIT)
                {
                    const u16* idx = static_cast<const u16*>(buf->getIndices());
                    for (u32 i = 0; i + 2 < indexCount; i += 3)
                        addTri(idx[i], idx[i+1], idx[i+2]);
                }
                else
                {
                    const u32* idx = reinterpret_cast<const u32*>(buf->getIndices());
                    for (u32 i = 0; i + 2 < indexCount; i += 3)
                        addTri(idx[i], idx[i+1], idx[i+2]);
                }
            }
        }
    }

    spdlog::info("LightmapBaker: {} scene triangles extracted for shadow testing", sceneTriangles.size());

    // -----------------------------------------------------------------------
    // 2. Collect all static lights
    // -----------------------------------------------------------------------
    std::vector<BakeLight> lights;

    for (auto& ent : entities)
    {
        if (!ent.hasComponent<LightComponent>() || !ent.hasComponent<TransformComponent>())
            continue;

        auto& lc = ent.getComponent<LightComponent>();
        if (lc.mode != LM_STATIC || !lc.visible)
            continue;

        auto& tc = ent.getComponent<TransformComponent>();
        if (!tc.node)
            continue;

        BakeLight bl;
        bl.position = tc.node->getAbsolutePosition() + lc.offset;
        bl.color    = lc.color_diffuse;
        bl.radius   = lc.radius;
        bl.type     = static_cast<int>(lc.type);
        bl.outerCone = lc.outerCone;

        // Spot/area direction: the node's local -Z axis in world space
        if (lc.type == LT_SPOT || lc.type == LT_AREA)
        {
            matrix4 rotMat;
            rotMat.setRotationDegrees(tc.node->getAbsoluteTransformation().getRotationDegrees());
            vector3df dir(0.0f, 0.0f, -1.0f);
            rotMat.rotateVect(dir);
            bl.direction = dir.normalize();
        }

        lights.push_back(bl);
    }

    spdlog::info("LightmapBaker::bakeScene: {} static lights found", lights.size());

    // -----------------------------------------------------------------------
    // 3. Bake every mesh entity
    // -----------------------------------------------------------------------
    int bakeCount = 0;

    for (auto& ent : entities)
    {
        if (!ent.hasComponent<MeshComponent>() || !ent.hasComponent<TransformComponent>())
            continue;

        // Only bake ET_STATIC entities — dynamic/player/marker entities are excluded.
        auto& dc2 = ent.getComponent<DescriptorComponent>();
        if (dc2.type != ET_STATIC)
            continue;

        auto& mc = ent.getComponent<MeshComponent>();
        if (!mc.trimesh || !mc.node)
            continue;

        auto& tc = ent.getComponent<TransformComponent>();
        if (!tc.node)
            continue;

        spdlog::info("LightmapBaker: baking '{}'...", dc2.name);

        const matrix4 worldTransform = tc.node->getAbsoluteTransformation();

        // Delete any previous lightmap for this entity.
        // Must remove the GPU texture from the driver first — driver->addTexture()
        // creates a new texture object every call and does not replace the old one,
        // so the old ITexture would leak inside the driver forever otherwise.
        if (mc.lightmap)
        {
            if (mc.lightmap->texture)
            {
                // Clear the texture from the node's material slots before removal so
                // that the driver doesn't free memory that's still bound.
                for (u32 m = 0; m < mc.node->getMaterialCount(); m++)
                {
                    if (mc.node->getMaterial(m).TextureLayer[1].Texture == mc.lightmap->texture)
                        mc.node->getMaterial(m).setTexture(1, nullptr);
                }
                driver->removeTexture(mc.lightmap->texture);
            }
            delete mc.lightmap;
            mc.lightmap = nullptr;
        }

        mc.lightmap = new MeshLightmap();

        // Phase 1: UV unwrap
        if (!unwrapMesh(mc.trimesh, worldTransform, settings, *mc.lightmap))
        {
            spdlog::warn("LightmapBaker: unwrapMesh failed for '{}', skipping", dc2.name);
            delete mc.lightmap;
            mc.lightmap = nullptr;
            continue;
        }

        // Phase 3: Texel rasterization
        rasterizeTexels(*mc.lightmap);

        // Phase 4: Direct lighting
        bake(*mc.lightmap, lights, sceneTriangles, settings);

        // Phase 4b: Blur
        blurLightmap(*mc.lightmap, settings.blurRadius);

        // Phase 5a: Dilation
        dilate(*mc.lightmap, settings.dilationPasses);

        // Phase 5b: Upload to GPU
        const std::string texName = "lightmap_" + std::to_string(dc2.id);
        uploadLightmap(driver, *mc.lightmap, texName);

        // Phase 5c: Rebuild node mesh with EVT_2TCOORDS and apply lightmap texture.
        // This is what actually gets UV2 to the vertex shader via gl_MultiTexCoord1.
        applyLightmapUVsToNode(*mc.lightmap, mc.node);

        // Free the large CPU-side intermediate buffers — all data is now on the GPU.
        // buffers  : vertex data copied into the new EVT_2TCOORDS mesh buffers
        // texels   : world-space lookup table used only during bake()
        // pixels   : float RGB values uploaded to the ITexture
        // Keeping them would waste ~3 MB per baked entity until the next bake.
        { std::vector<MeshLightmap::BufferData>().swap(mc.lightmap->buffers); }
        { std::vector<MeshLightmap::Texel>().swap(mc.lightmap->texels); }
        { std::vector<float>().swap(mc.lightmap->pixels); }

        ++bakeCount;
    }

    spdlog::info("LightmapBaker::bakeScene: {} entity meshes baked.", bakeCount);

    // -----------------------------------------------------------------------
    // 4. Bake static props that have receivesLightmap == true
    // -----------------------------------------------------------------------
    int propBakeCount = 0;

    if (PropManager::Get())
    {
        for (auto& bakeTarget : PropManager::Get()->getBakeTargets())
        {
            StaticProp* prop = bakeTarget.prop;
            if (!prop->trimesh || !prop->node) continue;

            spdlog::info("LightmapBaker: baking prop {}...", prop->id);

            const matrix4 worldTransform = bakeTarget.worldTransform;

            // Clean up any previous lightmap
            if (prop->lightmap)
            {
                if (prop->lightmap->texture)
                {
                    for (u32 m = 0; m < prop->node->getMaterialCount(); m++)
                    {
                        if (prop->node->getMaterial(m).TextureLayer[1].Texture == prop->lightmap->texture)
                            prop->node->getMaterial(m).setTexture(1, nullptr);
                    }
                    driver->removeTexture(prop->lightmap->texture);
                }
                delete prop->lightmap;
                prop->lightmap = nullptr;
            }

            prop->lightmap = new MeshLightmap();

            if (!unwrapMesh(prop->trimesh, worldTransform, settings, *prop->lightmap))
            {
                spdlog::warn("LightmapBaker: unwrapMesh failed for prop {}, skipping", prop->id);
                delete prop->lightmap;
                prop->lightmap = nullptr;
                continue;
            }

            rasterizeTexels(*prop->lightmap);
            bake(*prop->lightmap, lights, sceneTriangles, settings);
            blurLightmap(*prop->lightmap, settings.blurRadius);
            dilate(*prop->lightmap, settings.dilationPasses);

            const std::string texName = "prop_lightmap_" + std::to_string(prop->id);
            uploadLightmap(driver, *prop->lightmap, texName);
            applyLightmapUVsToNode(*prop->lightmap, prop->node);

            { std::vector<MeshLightmap::BufferData>().swap(prop->lightmap->buffers); }
            { std::vector<MeshLightmap::Texel>().swap(prop->lightmap->texels); }
            { std::vector<float>().swap(prop->lightmap->pixels); }

            ++propBakeCount;
        }
    }

    spdlog::info("LightmapBaker::bakeScene: complete. {} entity meshes + {} props baked.",
        bakeCount, propBakeCount);
}

# Terrain System Implementation Plan

## Overview

Load heightmap PNGs and detail textures, generate a chunked textured mesh, support splatmap blending of up to 4 ground types, and be compatible with the existing LightmapBaker.

---

## Prerequisites

- [ ] Recompile Irrlicht with `_IRR_MATERIAL_MAX_TEXTURES_ = 8` in `IrrCompileConfig.h`

---

## New Files

```
Source/Engine/Terrain/
├── TerrainConfig.h
├── TerrainData.h
├── TerrainLoader.h / .cpp
├── TerrainMeshGenerator.h / .cpp
└── TerrainManager.h / .cpp

Binaries/content/shader/
├── terrain.vert
└── terrain.frag
```

---

## Texture Slot Layout

| Slot | Contents |
|------|----------|
| 0 | *(unused — white placeholder)* |
| 1 | Lightmap (written by LightmapBaker) |
| 2 | Splatmap |
| 3 | Detail texture 0 (e.g. grass) |
| 4 | Detail texture 1 (e.g. dirt) |
| 5 | Detail texture 2 (e.g. rock) |
| 6 | Detail texture 3 (e.g. snow) |
| 7 | *(reserved)* |

---

## UV Layout

| Channel | Value | Notes |
|---------|-------|-------|
| `TCoords` (slot 0) | flat `(x/(W-1), z/(H-1))` | splatmap UV; never touched by xatlas |
| `TCoords2` (slot 1) | flat initially, xatlas UV after bake | lightmap UV post-bake |

In the terrain shader:
```glsl
vec2 splatUV  = gl_TexCoord[0].xy;
vec2 detailUV = gl_TexCoord[0].xy * uUVTile;
vec2 lmUV     = gl_TexCoord[1].xy;
```

---

## Chunking

- Terrain is split into **64×64 cell chunks**
- Each chunk = 65×65 vertices = 4,225 vertices — safely under u16 max (65,535)
- Each chunk is a separate `CMeshBuffer<S3DVertex2TCoords>` on a single `IAnimatedMeshSceneNode`
- Irrlicht frustum-culls per mesh buffer automatically
- LightmapBaker processes each chunk buffer independently via xatlas

---

## Step-by-Step Tasks

### Step 1 — Data structures
- [ ] Write `TerrainConfig.h`
  - `heightmap_path`, `splatmap_path`, `detail_textures[4]`
  - `cell_size` (world units per pixel), `max_height`, `uv_tile`
- [ ] Write `TerrainData.h`
  - `heights[]` (float, normalized 0..1), `normals[]` (vector3df)
  - `width`, `depth`

### Step 2 — Loader
- [ ] Write `TerrainLoader.cpp`
  - `loadHeights(path) -> TerrainData`
    - `driver->createImageFromFile()`, read red channel / 255.0f
    - compute per-vertex normals via finite differences (cross product of ∂h/∂x and ∂h/∂z)
    - `img->drop()`
  - `loadDetailTexture(path) -> ITexture*`
    - `driver->getTexture(path)` (driver cache handles dedup)
  - `loadSplatmap(path) -> ITexture*`
    - `driver->getTexture(path)`

### Step 3 — Mesh generator (single LOD)
- [ ] Write `TerrainMeshGenerator.cpp`
  - Split heightmap into 64×64 cell chunks
  - For each chunk, create `CMeshBuffer<S3DVertex2TCoords>`:
    - `Pos    = (x * cell_size, height * max_height, z * cell_size)`
    - `Normal = normals[x,z]`
    - `TCoords  = (x / (W-1), z / (H-1))` — flat splatmap UV
    - `TCoords2 = (x / (W-1), z / (H-1))` — same initially; xatlas overwrites post-bake
    - Indices: two CCW triangles per cell quad
  - Call `recalculateBoundingBox()` per buffer and on the final `SMesh`
  - Return `SAnimatedMesh*`

### Step 4 — Get a mesh on screen
- [ ] Write `TerrainManager.cpp` (skeleton)
  - Singleton, `loadTerrain(TerrainConfig)`
  - Call loader → mesh generator → `sceneManager->addAnimatedMeshSceneNode()`
  - Set material type to `EMT_SOLID`, bind a single white texture to slot 0
  - Add `update()` call in `WorldManager::update()`
- [ ] Verify chunked mesh appears in scene with correct shape and normals

### Step 5 — Terrain shader (single detail texture)
- [ ] Write `terrain.vert` (copy of `phong_perpixel.vert` verbatim)
- [ ] Write `terrain.frag`
  - Sample one detail texture at `detailUV = gl_TexCoord[0].xy * uUVTile`
  - Plug result into the phong lighting calc from `phong_perpixel.frag`
  - No splatmap blending yet
- [ ] Write `TerrainShaderCallback` (subclass of `ShaderConstantSetCallBack`)
  - Set additional uniforms: `tSplat`, `tDetail0..3`, `uUVTile`
- [ ] Register shader as `"terrain"` in `RenderManager::createDefaultShaders()`
- [ ] Assign shader to terrain node in `TerrainManager::loadTerrain()`

### Step 6 — Splatmap blending
- [ ] Extend `terrain.frag` to blend 4 detail textures by splatmap RGBA:
  ```glsl
  vec4 w = texture2D(tSplat, splatUV);
  w /= max(w.r + w.g + w.b + w.a, 0.0001);
  vec4 color = texture2D(tDetail0, detailUV) * w.r
             + texture2D(tDetail1, detailUV) * w.g
             + texture2D(tDetail2, detailUV) * w.b
             + texture2D(tDetail3, detailUV) * w.a;
  ```
- [ ] Bind splatmap to slot 2, detail textures to slots 3–6 in `TerrainManager`

### Step 7 — Lightmap bake compatibility
- [ ] Verify `LightmapBaker::applyLightmapUVsToNode()` runs on the terrain node without index overflow warnings (confirms chunking is correct)
- [ ] Verify lightmap appears correctly in slot 1 after bake
- [ ] Verify splatmap UV (`TCoords` / `gl_TexCoord[0]`) is unaffected by xatlas

### Step 8 — Collision
- [ ] After mesh generation, create octree triangle selector on the terrain node:
  ```cpp
  auto* sel = sceneManager->createOctreeTriangleSelector(node->getMesh(), node, 128);
  node->setTriangleSelector(sel);
  sel->drop();
  ```
- [ ] Register with PhysicsManager if needed

### Step 9 — Unload
- [ ] `TerrainManager::unloadTerrain(handle)`
  - Remove scene node
  - Drop textures if no other references

---

## Key Reference Files

| File | Relevant for |
|------|-------------|
| [Engine/Renderer/Lightmapper/LightmapBaker.cpp:854](Engine/Renderer/Lightmapper/LightmapBaker.cpp#L854) | `applyLightmapUVsToNode` — slot 1 write, TCoords2 overwrite |
| [Engine/Renderer/Lightmapper/LightmapBaker.cpp:504](Engine/Renderer/Lightmapper/LightmapBaker.cpp#L504) | pixel data → ITexture pattern |
| [Engine/Renderer/Extensions/IQuadSceneNode.cpp](Engine/Renderer/Extensions/IQuadSceneNode.cpp) | custom scene node / SMeshBuffer template |
| [Engine/Prop/PropManager.cpp:188](Engine/Prop/PropManager.cpp#L188) | non-ECS scene node lifecycle |
| [Engine/World/Systems/RenderSystem.cpp:19](Engine/World/Systems/RenderSystem.cpp#L19) | material flags, shader assignment pattern |
| [Binaries/content/shader/phong_perpixel.vert](../Binaries/content/shader/phong_perpixel.vert) | vertex shader to copy for terrain.vert |
| [Binaries/content/shader/phong_perpixel.frag](../Binaries/content/shader/phong_perpixel.frag) | lighting calc to port into terrain.frag |

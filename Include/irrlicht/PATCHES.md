# Engine-specific patches to vendored Irrlicht 1.8.5

Divergences from upstream Irrlicht 1.8.5, kept deliberately small and marked
with `ENGINE FORK` comments at each site.

## 1. Skip built-in transparent pass for mesh nodes (2026-07)

**Files:** `source/Irrlicht/CSceneManager.h`, `source/Irrlicht/CSceneManager.cpp`

The engine renders transparent mesh buffers itself in a back-to-front sorted
per-buffer pass (`RenderManager::drawTransparentPass`). Vanilla `drawAll()` was
*also* rendering them via `ESNRP_TRANSPARENT`, so every transparent mesh surface
was blended twice.

When a scene manager's parameter `"Engine_SkipMeshTransparent"` is `true`
(cached into `SkipMeshTransparentRegistration` at the top of `drawAll()`),
`registerNodeForRendering(..., ESNRP_TRANSPARENT)` skips nodes of type
`ESNT_MESH`, `ESNT_OCTREE`, and `ESNT_ANIMATED_MESH`. Non-mesh transparents
(billboards, particle systems, custom effect scene nodes) are unaffected.

The attribute is set only on the main scene manager (`RenderManager` ctor);
preview/off-screen scene managers keep vanilla behavior because nothing else
renders their transparents.

## 2. PerFrame uniform block binding (2026-07)

**File:** `source/Irrlicht/COpenGLSLMaterialRenderer.cpp` (`linkProgram`, GL2 path)

After a successful link, if the program declares a uniform block named
`"PerFrame"`, it is bound to uniform binding point 0. The engine keeps a
std140 UBO permanently attached there (`RenderManager::updatePerFrameUBO`)
holding all per-frame shader constants (time, camera basis, fog, ambient,
shadow params, cluster params). `glGetUniformBlockIndex` /
`glUniformBlockBinding` are resolved locally via `wglGetProcAddress` because
this Irrlicht version predates UBO support. Programs without the block are
unaffected (GL_INVALID_INDEX check).

## 3. ITexture::getNativeHandle (2026-07)

**Files:** `include/ITexture.h`, `source/Irrlicht/COpenGLTexture.h`

New virtual `u32 ITexture::getNativeHandle() const` (default 0) returning the
GL texture name in `COpenGLTexture`. The engine uses it to bind frame-constant
textures (shadow map RTT → unit 11, env map → unit 12) to raw texture units
above Irrlicht's 8 material slots, replacing the old pattern of smuggling them
in through material texture layers via `setMaterial()` inside shader callbacks.

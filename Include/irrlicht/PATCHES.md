# Engine-specific patches to vendored Irrlicht 1.8.5

Divergences from upstream Irrlicht 1.8.5, kept deliberately small and marked
with `ENGINE FORK` comments at each site.

---

## READ FIRST — rebuilding Irrlicht after editing anything here

**`Game Engine.vcxproj` compiles ZERO Irrlicht sources.** It statically links
(`_IRR_STATIC_LIB_`) a prebuilt `Irrlicht.lib` produced by a **separate solution**
that is not part of `GameEngine.sln` and has no `ProjectReference` pointing at it.

**Editing a file in this directory and rebuilding the engine changes nothing.**
The patch must be compiled into the archive first:

```
MSBuild "Include\irrlicht\source\Irrlicht\Irrlicht9.0.sln" -t:Build -p:Configuration=Release -p:Platform=x64
MSBuild "Include\irrlicht\source\Irrlicht\Irrlicht9.0.sln" -t:Build -p:Configuration=Debug   -p:Platform=x64
```

Then rebuild the engine normally — MSBuild tracks the `.lib` as a link input and
relinks even though no symbol changed.

Config mapping (also declared as `$(IrrConfig)` in `Game Engine.vcxproj`):

| Engine configuration | Irrlicht configuration | Links |
|---|---|---|
| `RelWithDebInfo\|x64` | `Release\|x64` | `x64\Release\Irrlicht.lib` |
| `Release\|x64` | `Release\|x64` | `x64\Release\Irrlicht.lib` |
| `Debug\|x64` | `Debug\|x64` | `x64\Debug\Irrlicht.lib` |

**Pass `-p:Platform=x64` explicitly — it is not optional.** `ObjectFileName` is
hard-coded to `..\obj\IrrRelease/` and `..\obj\IrrDebug/` instead of `$(IntDir)`,
so Win32 and x64 share one object directory. Building any Win32 configuration
poisons it and the next x64 link fails with
`LNK1112: module machine type 'x86' conflicts with target machine type 'x64'`.
Also: build **`Release|x64`**, never `Static lib - Release|x64` — that upstream
configuration writes elsewhere and would leave the engine linking the old lib.

Close Visual Studio first (`source/Irrlicht/.vs/` can hold locks), and do **not**
open `Irrlicht9.0.sln` in VS2022 — it will offer to retarget the v141 toolset and
rewrite the project.

### Verifying a patch actually made it into the binary

Timestamps are not proof. Use the import table — pick a Win32 API the patch newly
calls and check the exe imports it. For patch #5 (`TranslateMessage`):

```
dumpbin -imports Binaries\Engine.exe | findstr /I TranslateMessage
```

A hit means the patched code linked. (From Git Bash use `-imports`, not
`/imports`, which MSYS mangles into a path.)

`Game Engine.vcxproj` also carries a `VerifyIrrlichtLibIsCurrent` target that
fails the engine build if anything here is newer than the `.lib` being linked.
It exists because this exact mistake has already been made twice — see #5.

---

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

## 4. Re-apply base material on MaterialTypeParam change (2026-07)

**File:** `source/Irrlicht/COpenGLSLMaterialRenderer.cpp` (`OnSetMaterial`)

`COpenGLSLMaterialRenderer::OnSetMaterial` only invoked the base material's
state setup when the material *type* changed. Shader materials built over
`EMT_ONETEXTURE_BLEND` carry their blend factors packed in `MaterialTypeParam`
(the engine's soft-particle material: SPARK packs additive vs alpha per group),
so consecutive draws sharing the shader type but differing in params silently
kept the previous draw's blend state. The condition now also compares
`MaterialTypeParam` — mirroring why vanilla `ONETEXTURE_BLEND`'s own change
check is commented out upstream.

**Ordering amendment (2026-07):** the vanilla function tail ran
`setBasicRenderStates(material, lastMaterial, …)` *after* the base material's
`OnSetMaterial`. Its `BlendOperation` branch (`COpenGLDriver.cpp` ~3005) issues
`glDisable(GL_BLEND)` when transitioning `EBO_x → EBO_NONE` — so a draw whose
material sets `EMF_BLEND_OPERATION` (e.g. a muzzle-flash billboard) followed by
a GLSL-over-blend-base draw (SPARK soft particles, `BlendOperation` default
`EBO_NONE`) had its blending disabled *after* the base material enabled it:
particles rendered opaque for as long as a flash was visible. The function now
applies textures + `setBasicRenderStates` **first** and the base material's
blend state **last**, mirroring the builtin renderers' ordering.

## 5. TranslateMessage for non-Irrlicht windows (2026-08)

**File:** `source/Irrlicht/CIrrDeviceWin32.cpp` (`handleSystemMessages`)

Vanilla skips `TranslateMessage` entirely, with the comment "we don't use WM_CHAR
and it would conflict with our deadkey handling" — `WndProc` decodes characters
itself into `SEvent::SKeyInput::Char`. Correct for Irrlicht, but it means **no
`WM_CHAR` is ever generated anywhere in the process**.

That became a problem once Dear ImGui multi-viewport was enabled: torn-off editor
panels are real OS windows with their own window class, and `imgui_impl_win32`'s
text path is driven by `WM_CHAR`. Without translation, typing into a floating
Script Editor did nothing.

The pump now translates only messages **not** addressed to Irrlicht's own `HWnd`:

```cpp
if (msg.hwnd && msg.hwnd != HWnd)
    TranslateMessage(&msg);
```

Irrlicht's main window keeps its original untranslated path, so the deadkey
handling the comment was protecting is untouched. ImGui receives characters for the
main window from `IrrImGuiEventReceiver` (forwarding `KeyInput.Char`) and for
torn-off windows from the backend — the hwnd split is what keeps those from
double-feeding.

### This patch spent a week doing nothing — read the section at the top of this file

Written, committed and documented on 2026-08-25, it had **no effect whatsoever** until
2026-08-25 17:32, because `Irrlicht.lib` was never rebuilt. The linked archive dated
from 2026-07-08, seven weeks before the patch. `dumpbin -imports Binaries\Engine.exe`
showed `DispatchMessageA` and `PeekMessageA` but no `TranslateMessage` — a one-bit proof,
since `CIrrDeviceWin32.cpp` holds the only compiled call site in this link.

An earlier revision of this section claimed the patch was "necessary but not sufficient"
and blamed `TextEditor::handleKeyboardInputs()`'s `ImGui::IsWindowFocused()` gate. **That
was wrong** — it reasoned about a binary the patch had never been compiled into, and it
could not explain plain `ImGui::InputText`/`InputFloat` failing too (those have no focus
gate). The lesson is to verify what was *built*, not what was *written*.

The same omission left `x64\Debug\Irrlicht.lib` (2026-05-09) older than the
`virtual u32 getNativeHandle()` added to `include/ITexture.h` by patch #3 on 2026-07-05,
while `RenderManager.cpp` calls it at 7 sites — a vtable-layout mismatch, i.e. undefined
behaviour in every Debug build for months. Both libs were rebuilt together.

### Two things that remain true regardless

**Never translate messages for Irrlicht's own `HWnd`.** It is the obvious "fix" if
characters ever stop arriving, and it re-breaks the dead-key handling the original
upstream comment protects.

**Dead-key state is shared.** `CIrrDeviceWin32.cpp:805` uses `ToAsciiEx(...)`, which
shares per-thread/per-HKL dead-key state with `TranslateMessage`. The `hwnd` split means
only one of the two runs per keystroke and the two windows cannot hold focus at once, so
this is safe — but a dead key pressed in a torn-off panel leaves pending state that
carries into the main window. Known and bounded; not worth code to avoid.

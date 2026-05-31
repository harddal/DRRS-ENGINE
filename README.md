# DRRS Engine

A custom 3D game engine for Windows, featuring an integrated level editor, PBR shading, baked lightmaps, PhysX physics, AngelScript scripting, and a data-driven particle system.

![License](https://img.shields.io/badge/license-Unlicense-blue)
![Platform](https://img.shields.io/badge/platform-Windows-lightgrey)
![Language](https://img.shields.io/badge/language-C%2B%2B17-informational)

---

## Features

**Rendering**
- Per-pixel PBR lighting — GGX/Cook-Torrance BRDF, normal maps, specular
- Spot-light shadow mapping with slope-scale bias (2048×2048 ECF_R32F)
- CPU lightmap baking (xatlas UV unwrap → ray-traced irradiance → dilated texture)
- SPARK particle system integration — data-driven `.psys` effect files
- Splatmap terrain blending
- ImGui-based rendering backend (OpenGL 3+)

**Editor**
- In-engine level editor with entity hierarchy, component inspector, and asset browser
- Gizmo-based transform controls (translate, rotate, scale)
- Texture painter and vegetation/prop placement painter
- Built-in particle effect designer
- Integrated script editor with diff view
- One-click lightmap bake from the scene properties panel

**Physics & Navigation**
- NVIDIA PhysX integration via `PhysicsManager`
- Recast & Detour navmesh generation, pathfinding, crowd simulation, and dynamic tile cache

**Scripting**
- AngelScript VM managed by `ScriptManager`
- Compiled `.asc` scripts for NPCs, logic triggers, interactables, and objects
- Script reference auto-generated to `Binaries/script_reference.html`

**Audio**
- SoLoud audio engine — 3D positional sound, rapid-fire voice management
- `sound::play2d` / `sound::play3d` script bindings

**ECS Architecture**
- Anax entity-component-system
- Components: `TransformComponent`, `MeshComponent`, `LightComponent`, `NPCComponent`, `DamageReceiverComponent`, `TriggerZoneComponent`, and 15+ more
- Fixed 60 Hz physics tick (Gaffer on Games accumulator pattern)

---

## Screenshots

![Editor](/.github/screenshots/editor_main.png)

---

## Tech Stack

| Library | Role |
|---|---|
| [Irrlicht](https://irrlicht.sourceforge.io/) | 3D rendering, scene graph, windowing |
| [ImGui](https://github.com/ocornut/imgui) | Editor and debug UI |
| [Anax](https://github.com/miguelmartin75/anax) | Entity-component-system |
| [PhysX](https://developer.nvidia.com/physx-sdk) | Rigid body physics |
| [Recast & Detour](https://github.com/recastnavigation/recastnavigation) | Navmesh & pathfinding |
| [AngelScript](https://www.angelcode.com/angelscript/) | Embedded scripting |
| [SoLoud](https://solhsa.com/soloud/) | Audio engine |
| [SPARK](https://github.com/Synxis/SPARK) | Particle system |
| [Assimp](https://github.com/assimp/assimp) | Model importing |
| [xatlas](https://github.com/jpcy/xatlas) | Lightmap UV unwrapping |
| [spdlog](https://github.com/gabime/spdlog) | Logging |
| [Cereal](https://uscilab.github.io/cereal/) | Serialization |
| [GLM](https://github.com/g-truc/glm) | Math |
| [FreeType](https://freetype.org/) | Font rasterization |

---

## Getting Started

### Requirements

- Windows 10/11 (x64)
- Visual Studio 2019 or later (MSVC, C++17)
- Third-party dependencies (see Tech Stack table above)

### Build

1. Open `GameEngine.sln` in Visual Studio.
2. Select **Release | x64** (or **Debug | x64**).
3. Build the solution (`Ctrl+Shift+B`).
4. The output executable is written to `Binaries/Engine.exe`.
5. Run from `Binaries/` so relative content paths resolve correctly.

### Controls (in-game)

| Key | Action |
|---|---|
| `F1` | Toggle editor mode |
| `F2` | Toggle debug console |
| `WASD` | Move |
| `Mouse` | Look |
| `LMB` | Primary fire |
| `RMB` | Alternate fire / aim |

---

## Project Structure

```
Source/
├── Engine/
│   ├── Renderer/       # RenderManager, lightmapper, particle system
│   ├── Physics/        # PhysX integration
│   ├── Navigation/     # Recast/Detour
│   ├── World/          # WorldManager, ECS components & systems
│   ├── Script/         # AngelScript VM
│   ├── Sound/          # SoLoud integration
│   └── Input/          # Keyboard, mouse, gamepad
├── Game/
│   ├── Player/         # Camera, HUD, weapons, interaction
│   ├── NPC/            # AI system
│   ├── Item/           # Item database & inventory
│   └── Components/     # Game-specific ECS components
└── Editor/
    ├── Interface/      # All ImGui panels
    └── TextEditor/     # Built-in script/text editor
Binaries/
└── content/
    ├── shader/         # GLSL vertex & fragment shaders
    ├── script/         # Compiled AngelScript (.asc)
    ├── mesh/           # Models (player, props, weapons, NPCs)
    ├── texture/        # Textures and lightmaps
    └── map/            # Level files
```

---

## Scripting

Entity behavior is scripted in AngelScript, compiled to `.asc` files in `Binaries/content/script/`. The full API surface is documented in `Binaries/script_reference.html`.

Example — `object/torch_light.asc`, a flickering light that exposes tunable parameters to the editor:

```angelscript
#include "../entity.asc"

[export] float g_baseRadius       = 5.0;
[export] float g_flickerSpeed     = 1.0;
[export] float g_flickerIntensity = 0.5; // 0 = no flicker, 1 = wild

[export] float g_colorR = 1.0;
[export] float g_colorG = 0.45;
[export] float g_colorB = 0.08;

void init(entityid self)
{
    light::radius(self, g_baseRadius);
    light::color(self, vector3d(g_colorR, g_colorG, g_colorB));
}

void update(entityid self)
{
    float t = float(time::get()) * 0.001 * g_flickerSpeed;

    // Three sine layers at prime-ish frequencies produce the irregular feel of a real flame
    float drift   = sin(t * 1.8)  * 0.30;
    float flicker = sin(t * 9.4)  * 0.15;
    float flutter = sin(t * 23.0) * 0.07;
    float noise   = (float(math::random(100)) - 50.0) * 0.002;

    float brightness = 1.0 + (drift + flicker + flutter + noise) * g_flickerIntensity;
    if (brightness < 0.35) brightness = 0.35;
    if (brightness > 1.20) brightness = 1.20;

    light::radius(self, g_baseRadius * brightness);
    light::color(self, vector3d(
        g_colorR * brightness,
        g_colorG * (brightness * 0.85 + 0.15),
        g_colorB * (brightness * 0.70 + 0.30)
    ));
}
```

---

## License

This project is released into the public domain under the [Unlicense](LICENSE).

#version 330 compatibility
// Underwater camera pass — linear HDR, between bloom_composite and tonemap.
//
// Fogs the stretch of each view ray that lies INSIDE the water volume, using
// the same Source fog term water.frag applies when you look into the water
// from above, so a pool reads the same from both sides of its surface:
//
//     linear  = pow(authored sRGB, 2.2)
//     colour  = mix(scene * shallowTint, deepColor, fogT)
//     fogT    = clamp((waterDistance - fogStart) / (fogEnd - fogStart))
//
// plus a per-channel absorption (red dies first) that water.frag does not
// have, scaled to the pool's own fogEnd so every body of water keeps its
// authored look.
//
// PER-PIXEL WATERLINE. Each ray starts at its point on the near plane. A pixel
// whose near-plane point is outside the volume is left untouched — it is
// looking out through air, and if it looks down into the water, water.frag's
// front face already fogs it. That is what splits a half-submerged camera at
// the surface instead of flipping the whole screen on and off. The split line
// ripples slightly and carries a thin dark meniscus band on both sides.
//
// RAY LENGTH. The prepass stores depth along the view AXIS, so the ray is
// parameterised by view depth (dirV has z = 1) and the world distance is
// t * length(dirV) — the same correction as water.frag's rayScale. The ray is
// also clipped where it leaves the box: looking up at the surface, whatever is
// above the water is air, and only the underwater part of the ray may fog.
// water.frag zeroes its own fog on the surface's back face for the same reason
// ("submersion itself is the camera's business"), so nothing is fogged twice.

uniform sampler2D tScene;         // chain input, linear HDR
uniform sampler2D tPrepass;       // unit 14: rgb = view normal, a = linear view depth (0 = sky)
uniform float     uPrepassValid;

uniform mat4  uInvView;           // main camera view inverse (column-major, LH, +z forward)
uniform vec2  uProjTan;           // (tan(fovX/2), tan(fovY/2))
uniform float uNear;

uniform vec3  uBoxMin;
uniform vec3  uBoxMax;
uniform vec3  uShallowColor;      // authored sRGB ($refracttint)
uniform vec3  uDeepColor;         // authored sRGB ($fogcolor)
uniform float uFogStart;
uniform float uFogEnd;

uniform float uAbsorb;            // 0 = no colour loss
uniform float uDistortion;        // wobble amplitude in UV units
uniform float uRipple;            // waterline ripple, fraction of screen height
uniform float uTime;              // seconds

varying vec2 vTexCoord;

// Relative extinction per channel at distance == fogEnd, before uAbsorb.
// e^-2.0 / e^-0.6 / e^-0.25: red is nearly gone by the fog wall, blue barely dims.
const vec3 kAbsorbRGB = vec3(2.0, 0.6, 0.25);

void main()
{
    vec2 uv = vTexCoord;

    vec3  dirV = vec3((uv * 2.0 - 1.0) * uProjTan, 1.0);   // view ray, z = 1
    vec3  camW = uInvView[3].xyz;
    vec3  dirW = mat3(uInvView) * dirV;                     // t along this == view depth

    // ------------------------------------------------------------- waterline
    // The surface the lens sees is not quite flat: a small travelling ripple
    // so the split line laps across the screen instead of cutting it with a
    // ruler. Varies with SCREEN x and is sized as a fraction of the near
    // plane's height — the near plane is ~0.15 units tall, so any world-space
    // amplitude or frequency would be either invisible or fill the screen.
    // Peak is 1.6 * uRipple; updateUnderwaterPass() lifts the box top by that
    // much so the pass is on before a crest can reach the screen.
    vec3  startW   = camW + dirW * uNear;
    float nearH    = 2.0 * uProjTan.y * uNear;
    float rippleY  = uRipple * nearH *
                     (sin(uv.x * 9.0 + uTime * 2.2) + 0.6 * sin(uv.x * 17.0 - uTime * 1.6));
    float hSurf    = startW.y - (uBoxMax.y + rippleY);             // > 0 above the water

    bool inFootprint = all(greaterThanEqual(startW.xz, uBoxMin.xz)) &&
                       all(lessThanEqual   (startW.xz, uBoxMax.xz)) &&
                       startW.y >= uBoxMin.y;
    bool inWater = inFootprint && hSurf <= 0.0;

    // Meniscus: a thin dark band where the near plane cuts the surface. Width
    // is a fraction of the screen HEIGHT in pixels, via the screen gradient of
    // hSurf — a world-unit width would swell to fill the screen when looking
    // straight down (hSurf is then almost constant across the near plane).
    const float kMeniscusScreen = 0.004;
    float hPerPx   = max(fwidth(hSurf), 1e-7);
    float bandPx   = kMeniscusScreen * float(textureSize(tScene, 0).y);
    float meniscus = inFootprint ? 1.0 - smoothstep(0.0, bandPx * hPerPx, abs(hSurf)) : 0.0;
    float edgeDim  = mix(1.0, 0.25, meniscus);

    if (!inWater)
    {
        gl_FragColor = vec4(texture(tScene, uv).rgb * edgeDim, 1.0);
        return;
    }

    // ------------------------------------------------------- ray exit (slab)
    // The ray starts inside the box, so the exit is the nearest far slab. Keep
    // near-zero components away from 0 so the divide cannot produce 0 * inf.
    vec3 safeDir = mix(dirW, vec3(1e-6), lessThan(abs(dirW), vec3(1e-6)));
    vec3 inv     = 1.0 / safeDir;
    vec3 tFar    = max((uBoxMin - camW) * inv, (uBoxMax - camW) * inv);
    float tExit  = min(min(tFar.x, tFar.y), tFar.z);

    // Opaque geometry along the ray. Depth is read at the UNDISTORTED position,
    // for the same reason water.frag does: fog must not follow the wobble.
    float sceneZ = (uPrepassValid > 0.5) ? texture(tPrepass, uv).a : 0.0;
    float tOpaque = (sceneZ <= 0.001) ? 1e6 : sceneZ;

    float tWater = max(min(tOpaque, tExit) - uNear, 0.0);
    float dist   = tWater * length(dirV);

    // ------------------------------------------------------------ distortion
    // UV units, so it looks the same at any resolution. Scaled down with water
    // distance so the viewmodel and anything right against the lens stay put.
    vec2 wobble = vec2(
        sin(uv.y * 23.0 + uTime * 1.7) + 0.5 * sin(uv.y * 57.0 - uTime * 2.3),
        cos(uv.x * 19.0 + uTime * 1.3) + 0.5 * cos(uv.x * 43.0 + uTime * 2.9));
    float wobbleFade = clamp(dist * 0.5, 0.0, 1.0);
    vec2  texel      = 1.0 / vec2(textureSize(tScene, 0));
    vec2  suv        = clamp(uv + wobble * (uDistortion / 1.5) * wobbleFade, texel, 1.0 - texel);
    vec3  scene      = texture(tScene, suv).rgb;

    // ----------------------------------------------------------------- fog
    vec3 shallowTint = pow(uShallowColor, vec3(2.2));
    vec3 deep        = pow(uDeepColor,    vec3(2.2));

    float fogRange = max(uFogEnd - uFogStart, 1e-4);
    float fogT     = clamp((dist - uFogStart) / fogRange, 0.0, 1.0);

    vec3 absorb = exp(-kAbsorbRGB * uAbsorb * (dist / max(uFogEnd, 1e-4)));

    vec3 color = mix(scene * shallowTint * absorb, deep, fogT);
    gl_FragColor = vec4(color * edgeDim, 1.0);
}

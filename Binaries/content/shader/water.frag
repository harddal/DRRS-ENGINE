#version 330 compatibility
// Source-engine-style water.
//
// This reproduces the four things that actually make Source's water read as
// water, rather than as a scrolling blue texture:
//
//   1. REFRACTION   ($refract / $refractamount / $refracttint)
//      The scene behind the surface is re-sampled from a copy of the opaque
//      frame, offset in screen space by the wave normal. Because the water is
//      output OPAQUE, the refraction is the only thing you see through -- same
//      as Source. That is what lets the distortion be per-pixel instead of a
//      flat alpha blend.
//
//   2. DEPTH FOG    ($fogenable / $fogstart / $fogend / $fogcolor)
//      The refracted colour is lerped toward the water's fog colour by how far
//      the view ray travels through water before hitting geometry. Shallow
//      edges stay nearly clear, deep areas go solid. This single term is the
//      dominant part of the Source look and is why their water has soft, dark
//      shorelines instead of a hard polygon edge.
//
//   3. FRESNEL REFLECTION ($reflect / $reflectamount / $fresnelpower)
//      Looking straight down you see through; at grazing angles the surface
//      turns into a mirror. Source blends reflection over refraction with a
//      Schlick-style term. We sample the scene's equirectangular env map for
//      the reflection (this is Source's "cheap water" / $envmap path; a true
//      planar reflection RTT would be the "expensive water" upgrade).
//
//   4. MULTI-SCALE ANIMATED NORMALS ($normalmap / $bumpframe)
//      Three scrolled samples of one tiling normal map at different scales and
//      drift directions. Different scales are what hides the tile repeat and
//      gives the surface both swell and chop.
//
// Scene depth comes from the geometry prepass (raw texture unit 14, linear view
// depth in .a). The refraction copy is on raw unit 15 and the env map on 12 --
// all three sit above Irrlicht's 8 material slots so its state tracker never
// disturbs them.
//
// COLOUR SPACE: the scene RTT is linear HDR and the tonemapper gamma-encodes on
// the way out. The refraction copy is therefore already linear and is used
// as-is; the env map and the author-picked tint colours are sRGB and are
// linearised here. Skipping either conversion washes the water out to cream.

// Per-frame constants -- std140 block filled once per frame by RenderManager
// (updatePerFrameUBO) and bound to binding point 0 by the fork patch in
// COpenGLSLMaterialRenderer (see Include/irrlicht/PATCHES.md). Member names
// match the old per-draw uniforms; layout must mirror struct PerFrameData in
// RenderManager.cpp. fTime is engine time in MILLISECONDS; uTime is seconds.
layout(std140) uniform PerFrame
{
    vec3  uAmbientColor;  float uHasShadow;
    vec3  uFogColor;      float uFogDensity;
    vec3  uCamRight;      float uFogStart;
    vec3  uCamUp;         float uHasEnvMap;
    vec3  uCamForward;    float uShadowBias;
    vec4  uClusterParams; // (tileW_px, tileH_px, sliceScale, sliceBias)
    float fTime;          float uTime;  float uUseClusters;  float uPrepassValid;
    mat4  uInvView;       // main camera view inverse -- world-pos reconstruction
    mat4  uShadowMat[4];  // lightProj*lightView per shadow atlas slot
    vec4  uShadowRect[4]; // xy = atlas offset, z = scale, w = 1 if slot active
    vec4  uFogVolParams;  // x = active fog-volume count, y = feather distance
    vec4  uFogVolMin[8];  // xyz = AABB min, w = density
    vec4  uFogVolMax[8];  // xyz = AABB max, w = start distance
    vec4  uFogVolColor[8];// rgb = color, w = reserved
};

uniform sampler2D tNormalMap;  // unit 0  : tiling tangent-space wave normals
uniform sampler2D tEnvMap;     // unit 12 : equirectangular sky/environment
uniform sampler2D tPrepass;    // unit 14 : view normal.xyz + linear view depth
uniform sampler2D tRefract;    // unit 15 : copy of the opaque scene (linear HDR)

uniform float uHasNormalMap;   // 0 = fall back to procedural sine waves
uniform float uHasRefract;     // 0 = no scene copy this frame; blend instead

// --- Per-entity look, encoded into material slots by RenderSystem ---
uniform vec3  uRefractTint;    // $refracttint  (WaterComponent::shallowColor)
uniform vec3  uWaterFogColor;  // $fogcolor     (WaterComponent::deepColor)
uniform vec3  uReflectTint;    // $reflecttint
uniform float uAlpha;          // only used on the no-refraction fallback path

uniform float uWaterFogStart;  // $fogstart     -- water depth where fog begins
uniform float uWaterFogEnd;    // $fogend       -- water depth of full fog
uniform float uRefractAmount;  // $refractamount
uniform float uReflectAmount;  // $reflectamount
uniform float uNormalTiling;   // normal-map tiles per world unit
uniform float uFlowSpeed;      // scroll rate multiplier
uniform float uFresnelPower;   // $fresnelpower
uniform float uWaveStrength;   // wave normal XY gain ("choppiness")

varying vec3  vWorldPos;
varying vec3  vWorldNormal;
varying float vViewZ;

const float PI = 3.14159265;

// Water's index of refraction is ~1.33, giving a normal-incidence reflectance
// of ((1.33-1)/(1.33+1))^2 = 0.02. Source hardcodes essentially this.
const float WATER_F0 = 0.02;

// ---------------------------------------------------------------------------
// Fallback wave normals when no normal map is bound. Five directional waves
// with irrational-ratio frequencies so the pattern never visibly repeats.
// Returns a tangent-space normal.
// ---------------------------------------------------------------------------
vec3 proceduralWaveNormal(vec2 p, float t)
{
    const vec2  DIR[5]  = vec2[5](vec2( 0.94,  0.34), vec2(-0.42,  0.91),
                                  vec2( 0.71, -0.71), vec2(-0.87, -0.50),
                                  vec2( 0.16,  0.99));
    const float FREQ[5] = float[5](1.00, 1.73, 2.61, 4.19, 6.83);
    const float AMP[5]  = float[5](1.00, 0.62, 0.38, 0.21, 0.11);
    const float SPD[5]  = float[5](0.55, 0.81, 1.13, 1.57, 2.09);

    vec2 grad = vec2(0.0);
    for (int i = 0; i < 5; ++i)
    {
        float w = FREQ[i] * 2.0 * PI;
        float a = dot(p, DIR[i]) * w + t * SPD[i] * w * 0.12;
        grad += DIR[i] * (cos(a) * AMP[i] * w * 0.06);
    }
    return normalize(vec3(-grad, 1.0));
}

// ---------------------------------------------------------------------------
// Pick a world-axis-aligned UV plane and matching tangent frame from the face
// normal. A water volume is usually a box: the top face tiles in XZ, and the
// side faces (visible from underwater, since back-face culling is off) tile in
// XY / ZY instead of smearing the top-face projection down the walls.
// ---------------------------------------------------------------------------
void worldProjection(vec3 n, vec3 wp, out vec2 uv, out vec3 T, out vec3 B)
{
    vec3 a = abs(n);
    if (a.y >= a.x && a.y >= a.z) { uv = wp.xz; T = vec3(1.0, 0.0, 0.0); B = vec3(0.0, 0.0, 1.0); }
    else if (a.x >= a.z)          { uv = wp.zy; T = vec3(0.0, 0.0, 1.0); B = vec3(0.0, 1.0, 0.0); }
    else                          { uv = wp.xy; T = vec3(1.0, 0.0, 0.0); B = vec3(0.0, 1.0, 0.0); }
}

void main()
{
    // ---------------------------------------------------------------- normals
    vec3 N = normalize(vWorldNormal);

    // Back-face culling is off so the surface is visible from underwater. The
    // shading normal must face the viewer or the fresnel term inverts and the
    // surface reads as a black sheet from below.
    if (!gl_FrontFacing)
        N = -N;

    vec2 uv; vec3 T, B;
    worldProjection(N, vWorldPos, uv, T, B);

    float tiling = max(uNormalTiling, 1e-4);
    vec2  baseUV = uv * tiling;
    float t      = uTime * uFlowSpeed;

    vec3 nT;
    if (uHasNormalMap > 0.5)
    {
        // Three scales, three drift directions. The scales are deliberately not
        // integer multiples of each other, so the combined pattern's period is
        // far longer than any single tile.
        vec3 n0 = texture(tNormalMap, baseUV * 1.00 + vec2( 0.031,  0.017) * t).xyz * 2.0 - 1.0;
        vec3 n1 = texture(tNormalMap, baseUV * 0.43 + vec2(-0.019,  0.026) * t).xyz * 2.0 - 1.0;
        vec3 n2 = texture(tNormalMap, baseUV * 2.13 + vec2( 0.045, -0.039) * t).xyz * 2.0 - 1.0;

        // Partial-derivative blend: combine the tangent-space slopes and rebuild
        // Z. Summing the full vectors and normalising instead would flatten the
        // result toward (0,0,1) as opposing layers cancel.
        //
        // The 1/2.5 is the sum of the layer weights and is NOT optional: adding
        // three taps raw triples the slope, and since the normal map already
        // carries its own gain that lands around 60-70 degrees of surface tilt.
        // Water is nearly flat; that much tilt swings the reflection vector
        // through the entire sphere and turns the surface into marbling.
        vec2 slope = (n0.xy + n1.xy + n2.xy * 0.5) * (uWaveStrength / 2.5);
        nT = normalize(vec3(slope, 1.0));
    }
    else
    {
        nT = proceduralWaveNormal(baseUV, t);
        nT = normalize(vec3(nT.xy * uWaveStrength, nT.z));
    }

    vec3 Nw = normalize(T * nT.x + B * nT.y + N * nT.z);

    // ------------------------------------------------------------ view vector
    vec3  camWS   = uInvView[3].xyz;
    vec3  toCam   = camWS - vWorldPos;
    float camDist = length(toCam);
    vec3  V       = toCam / max(camDist, 1e-4);
    float NdotV   = clamp(dot(Nw, V), 0.0, 1.0);

    // -------------------------------------------------------- screen sampling
    vec2 texel  = 1.0 / vec2(textureSize(tPrepass, 0));
    vec2 screen = gl_FragCoord.xy * texel;

    // Ray-length correction: the prepass stores depth along the view AXIS, but
    // the water fog needs distance along this fragment's view RAY. Without this
    // the fog visibly thins toward the edges of the screen.
    float rayScale = camDist / max(abs(vViewZ), 1e-3);

    // ------------------------------------------------------------ water depth
    // Depth is ALWAYS read from the undistorted tap. Offsetting this lookup by
    // the wave normal makes the fog term a discontinuous function of the wave
    // pattern -- in shallow water at a grazing angle, where the pool bottom and
    // the surface are only a fraction of a unit apart in view depth, it breaks
    // the surface into hard-edged blotches that follow the ripples.
    float sceneZ = (uPrepassValid > 0.5) ? texture(tPrepass, screen).a : 0.0;

    // sceneZ == 0 means the prepass saw no geometry (sky) -- treat that as
    // unbounded depth so water against the skybox fogs out completely.
    float waterDepth = (sceneZ <= 0.001)
                     ? 1e6
                     : max(sceneZ - vViewZ, 0.0) * rayScale;

    float fogT = clamp((waterDepth - uWaterFogStart)
                       / max(uWaterFogEnd - uWaterFogStart, 1e-4), 0.0, 1.0);

    // Looking UP at the surface from underwater, everything behind it is ABOVE
    // the water, so none of that distance is water to fog through -- the depth
    // subtraction above is measuring air. Without this the surface reads as a
    // flat sheet of fog colour whenever the player is submerged. (Submersion
    // itself is the camera's business, not this surface's.)
    if (!gl_FrontFacing)
        fogT = 0.0;

    // -------------------------------------------------------------- refraction
    // Screen-space distortion, expressed in the surface's own tangent plane.
    //
    // Two attenuations before the occluder guard below:
    //   - by distance, so far-off water settles instead of shimmering into
    //     aliasing (Source's refraction does the same);
    //   - by water DEPTH, because shallow water physically bends less. This
    //     also softens the shoreline, but note it is measured at the
    //     UNDISTORTED position and so says nothing about what the offset
    //     actually lands on.
    float edgeFade = clamp(waterDepth * 0.5, 0.0, 1.0);
    vec2  distort  = vec2(dot(Nw, T), dot(Nw, B)) * uRefractAmount * edgeFade
                   * (1.0 / max(abs(vViewZ) * 0.05, 1.0));
    vec2  duv      = clamp(screen + distort, texel, 1.0 - texel);

    // OCCLUDER GUARD. The refraction copy is the whole opaque frame, including
    // anything standing BETWEEN the camera and the water. Offsetting into it
    // blind drags those pixels sideways onto the surface, ringing every model
    // that overlaps the water with a ghost of its own silhouette. A model
    // standing in deep water is the worst case: edgeFade is 1 there, so the
    // offset is at full strength right where the occluder is.
    //
    // Test what the offset actually landed on, and scale the offset by how far
    // BEHIND the water surface that sample is. A sample in front of the surface
    // belongs to an occluder, drives this to 0, and collapses the tap back to
    // the undistorted position.
    //
    // This is deliberately applied to the COLOUR tap only. Feeding a distorted
    // depth into fogT is what shattered the surface into hard blotches before:
    // fogT swings between "clear" and "solid fog colour", so any discontinuity
    // in it is enormous. Here both sides of the boundary still sample the pool
    // bottom, just at slightly different offsets, so the seam is a slight kink
    // in the bottom's pattern rather than a ghost of a model.
    if (uPrepassValid > 0.5)
    {
        float zAtOffset = texture(tPrepass, duv).a;
        float behind    = (zAtOffset <= 0.001)      // sky behind: nothing to occlude
                        ? 1e6
                        : (zAtOffset - vViewZ) * rayScale;
        duv = mix(screen, duv, clamp(behind * 2.0, 0.0, 1.0));
    }

    // Author-facing colours are sRGB; the working space is linear.
    vec3 refractTint = pow(uRefractTint,   vec3(2.2));
    vec3 waterFog    = pow(uWaterFogColor, vec3(2.2));
    vec3 reflectTint = pow(uReflectTint,   vec3(2.2));

    vec3  refracted;
    float outAlpha;
    if (uHasRefract > 0.5)
    {
        // Already-linear HDR scene copy.
        refracted = texture(tRefract, duv).rgb * refractTint;
        outAlpha  = 1.0;        // opaque: the refraction IS the see-through
    }
    else
    {
        // Fallback for when no scene copy exists (preview scenes, or the
        // capture switched off): behave like ordinary alpha-blended water.
        refracted = refractTint;
        outAlpha  = mix(uAlpha, 1.0, fogT);
    }

    // The Source water-fog term.
    vec3 belowWater = mix(refracted, waterFog, fogT);

    // -------------------------------------------------------------- reflection
    vec3 R = reflect(-V, Nw);

    // A reflection off a water surface can only ever show what is ON THE SAME
    // SIDE of that surface. The wave normal routinely tilts far enough to swing
    // R past the surface plane, and an equirectangular env map answers those
    // directions perfectly happily -- with its lower hemisphere. On an outdoor
    // HDRI that is ground: the classic symptom is dirt and rock, magnified and
    // warped along the wave contours, marbled across the water.
    //
    // Project the below-plane component out rather than mirroring it back. That
    // slides R to lie exactly in the surface plane at the moment it would have
    // crossed, so the correction is zero at the boundary and grows smoothly --
    // a mirror-flip would put a hard seam right where it is most visible.
    float RdotN = dot(R, N);
    R = normalize(R - N * min(RdotN, 0.0));

    vec3 reflection;
    if (uHasEnvMap > 0.5)
    {
        // Equirectangular lookup -- same projection phong_perpixel uses, so a
        // scene's env map lines up between water and every glossy surface.
        vec2 envUV = vec2(
            atan(R.x, R.z) / (2.0 * PI),
            asin(clamp(-R.y, -1.0, 1.0)) / PI + 0.5
        );
        // Deliberately blurry. A sharp mirror of an 8K sky reads as chrome, and
        // every high-frequency detail in it crawls and aliases as the waves
        // move under it. Roughly a 1K lookup on a typical skydome.
        reflection = pow(textureLod(tEnvMap, envUV, 3.0).rgb, vec3(2.2));
    }
    else
    {
        // No env map bound. A flat fallback would make the water read as matte
        // paint exactly where fresnel is strongest, so approximate a sky with a
        // horizon-to-zenith ramp built from the scene's own fog and ambient
        // colours. Dark scenes stay dark; outdoor scenes get a usable gradient.
        float up   = clamp(R.y * 0.5 + 0.5, 0.0, 1.0);
        reflection = mix(uFogColor, uAmbientColor * 3.0, up);
    }
    reflection *= reflectTint;

    // Schlick fresnel with an author-controlled exponent. uFresnelPower is
    // Source's $fresnelpower (their default is 6; Schlick's physical value is
    // 5, and lower numbers turn the water mirror-ish much sooner).
    float fresnel = WATER_F0 + (1.0 - WATER_F0)
                  * pow(1.0 - NdotV, max(uFresnelPower, 0.01));
    fresnel = clamp(fresnel * uReflectAmount, 0.0, 1.0);

    // Seen from underneath there is no sky to reflect -- the env map would just
    // paste a sky onto the ceiling of the pool. Fade the reflection right down.
    if (!gl_FrontFacing)
        fresnel *= 0.15;

    vec3 color = mix(belowWater, reflection, fresnel);

    // ---------------------------------------------------------------- scene fog
    // Distance fog over the surface itself, so water does not punch a clear
    // hole through fog that the surrounding terrain is sitting in. The
    // refraction copy is already fogged for its own depth; this covers the
    // remaining distance from the eye to the water plane.
    if (uFogDensity > 0.0)
    {
        float d   = max(camDist - uFogStart, 0.0);
        float fog = clamp(1.0 - exp(-uFogDensity * d), 0.0, 1.0);
        color     = mix(color, uFogColor, fog);
    }

    gl_FragColor = vec4(color, outAlpha);
}

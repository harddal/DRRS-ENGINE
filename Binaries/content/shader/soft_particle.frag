#version 330 compatibility
// Soft particles — fade the fragment as it approaches scene geometry so quads
// never hard-clip against walls/floors. Scene depth comes from the geometry
// prepass on raw unit 14. The material base is EMT_ONETEXTURE_BLEND with
// SPARK's own packed blend factors — both its ADD and ALPHA modes are
// SRC_ALPHA-driven, so fading the ALPHA channel fades both correctly.

// Per-frame constants — std140 block filled once per frame by RenderManager
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
    mat4  uInvView;       // main camera view inverse — world-pos reconstruction
    mat4  uShadowMat[4];  // lightProj*lightView per shadow atlas slot
    vec4  uShadowRect[4]; // xy = atlas offset, z = scale, w = 1 if slot active
    vec4  uFogVolParams;  // x = active fog-volume count, y = feather distance
    vec4  uFogVolMin[8];  // xyz = AABB min, w = density
    vec4  uFogVolMax[8];  // xyz = AABB max, w = start distance
    vec4  uFogVolColor[8];// rgb = color, w = reserved
};

uniform sampler2D tDiffuse;   // unit 0: particle texture
uniform sampler2D tPrepass;   // unit 14: prepass (linear view depth in .a)
uniform float     uSoftDist;  // world-unit distance over which the fade ramps

varying vec2  vTexCoord;
varying vec4  vColor;
varying float vViewZ;

void main()
{
    vec4 c = texture2D(tDiffuse, vTexCoord) * vColor;

    float fade = 1.0;
    if (uPrepassValid > 0.5 && uSoftDist > 0.001)   // uSoftDist 0 = fade disabled
    {
        vec2  suv    = gl_FragCoord.xy / vec2(textureSize(tPrepass, 0));
        float sceneZ = texture2D(tPrepass, suv).a;
        if (sceneZ > 0.001)   // 0 = sky/no geometry: nothing to fade against
            fade = clamp((sceneZ - vViewZ) / uSoftDist, 0.0, 1.0);
    }

    gl_FragColor = vec4(c.rgb, c.a * fade);
}

#version 330 compatibility
// Per-Pixel PBR Vertex Shader
// Computes everything in VIEW SPACE using GL built-ins to avoid
// Irrlicht row-major / GLSL column-major convention issues entirely.

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
};

varying vec3 vViewPos;         // view-space vertex position
varying vec3 vViewNormal;      // view-space normal
varying vec2 vTexCoord;
varying vec2 vLightmapUV;      // lightmap UV (UV channel 1, set by LightmapBaker)
varying vec4 vShadowClip[4];   // light clip-space coords per shadow atlas slot
                               // (NOT divided by w — see fragment shader)

void main()
{
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;

    // View-space position and normal via GL built-ins — no custom matrix uniforms needed.
    // gl_ModelViewMatrix  = correct view*world transform
    // gl_NormalMatrix     = transpose(inverse(modelview)) upper-left 3x3 = correct normal matrix
    vec4 viewPos = gl_ModelViewMatrix * gl_Vertex;
    vViewPos    = viewPos.xyz;
    vViewNormal = normalize(gl_NormalMatrix * gl_Normal);

    vTexCoord   = gl_MultiTexCoord0.xy;
    vLightmapUV = gl_MultiTexCoord1.xy;

    // Shadow clip coords per atlas slot. World position is reconstructed as
    // uInvView * viewPos (the UBO holds the inverse of the exact view matrix
    // this pass renders with), so no per-object matrix upload is needed.
    // Pass clip-space coords WITHOUT dividing by w — the perspective divide
    // must happen in the fragment shader, or OpenGL's perspective-correct
    // varying interpolation would divide a second time, producing shadow
    // offsets that grow as the light gets closer.
    vec4 worldPos = uInvView * viewPos;
    for (int s = 0; s < 4; ++s)
        vShadowClip[s] = uShadowMat[s] * worldPos;
}

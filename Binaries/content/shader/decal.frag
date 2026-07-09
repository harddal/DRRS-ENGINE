#version 330 compatibility
// Screen-space decal — projects a texture onto whatever geometry the depth
// prepass says is inside the decal's unit-cube volume. Rendered as a box with
// depth test off (back faces, so it works with the camera inside the volume)
// and MODULATE blending (dst * src): unaffected pixels output 1.0, decaled
// pixels darken toward the decal color. Runs post-tonemap like SSAO — a
// multiplicative effect applied pre-tonemap would be crushed by the filmic
// curve's shoulder at high exposure.

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

uniform sampler2D tDecal;       // unit 0: decal texture (alpha = coverage)
uniform sampler2D tPrepass;     // unit 14: view normal.xyz + linear view depth
uniform mat4      uDecalInv;    // world -> decal local (unit cube, +-0.5)
uniform vec3      uDecalDirView;// projection direction in VIEW space
uniform vec2      uProjTan;     // (tan(fovX/2), tan(fovY/2))
uniform float     uFade;        // lifetime fade 0..1

void main()
{
    if (uPrepassValid < 0.5)
        discard;

    vec2  suv = gl_FragCoord.xy / vec2(textureSize(tPrepass, 0));
    vec4  pn  = texture2D(tPrepass, suv);
    float d   = pn.a;
    if (d <= 0.001)
        discard;   // sky

    // Reconstruct the surface position and test it against the decal volume.
    vec3 viewPos  = vec3((suv * 2.0 - 1.0) * uProjTan * d, d);
    vec4 worldPos = uInvView * vec4(viewPos, 1.0);
    vec3 local    = (uDecalInv * worldPos).xyz;
    if (abs(local.x) > 0.5 || abs(local.y) > 0.5 || abs(local.z) > 0.5)
        discard;

    // Reject surfaces facing away from the projection — stops the decal
    // smearing across perpendicular geometry (wall shot bleeding onto floor).
    if (dot(normalize(pn.rgb), -uDecalDirView) < 0.25)
        discard;

    vec4 c = texture2D(tDecal, local.xy + 0.5);
    gl_FragColor = vec4(mix(vec3(1.0), c.rgb, c.a * uFade), 1.0);
}

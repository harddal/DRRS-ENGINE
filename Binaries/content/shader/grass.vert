#version 330 compatibility

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
    float fTime;          float uTime;  float uUseClusters;  float uPerFramePad0;
    mat4  uInvView;       // main camera view inverse — world-pos reconstruction
    mat4  uShadowMat[4];  // lightProj*lightView per shadow atlas slot
    vec4  uShadowRect[4]; // xy = atlas offset, z = scale, w = 1 if slot active
};

// Grass Vertex Shader
// Animates vertices upward from the base by displacing them horizontally
// using a sine wave driven by world position and time.  Vertices near the
// base (low Y in object space) are anchored; those near the tip sway freely.
// Lighting varyings are computed in view space, same as phong_perpixel.vert.


varying vec3 vViewPos;
varying vec3 vViewNormal;
varying vec2 vTexCoord;
varying vec2 vLightmapUV;

// Wind parameters — tune to taste.
#define WIND_SPEED      1.2
#define WIND_STRENGTH   0.08
#define WIND_FREQUENCY  2.5

void main()
{
    vec4 worldPos = gl_ModelViewMatrix * gl_Vertex;

    // Sway factor: 0 at the base (Y = 0 in object space), 1 at the tip.
    // Use object-space Y so the anchoring follows the mesh regardless of
    // world-space position.
    float swayFactor = clamp(gl_Vertex.y, 0.0, 1.0);

    // Phase offset based on world XZ position gives each blade its own timing
    // so adjacent blades do not move in sync.
    float phase = gl_Vertex.x * 0.5 + gl_Vertex.z * 0.7;

    float wave = sin(fTime * WIND_SPEED * WIND_FREQUENCY + phase) * WIND_STRENGTH;

    // Displace in view-space X and Z proportionally to the sway factor.
    // Multiplied into view-space so the displacement is camera-independent,
    // which avoids visible popping when the camera rotates.
    worldPos.x += wave * swayFactor;
    worldPos.z += wave * 0.5 * swayFactor;

    gl_Position  = gl_ProjectionMatrix * worldPos;
    vViewPos     = worldPos.xyz;
    vViewNormal  = normalize(gl_NormalMatrix * gl_Normal);
    vTexCoord    = gl_MultiTexCoord0.xy;
    vLightmapUV  = gl_MultiTexCoord1.xy;
}

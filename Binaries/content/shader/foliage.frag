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
    float fTime;          float uTime;  float uUseClusters;  float uPrepassValid;
    mat4  uInvView;       // main camera view inverse — world-pos reconstruction
    mat4  uShadowMat[4];  // lightProj*lightView per shadow atlas slot
    vec4  uShadowRect[4]; // xy = atlas offset, z = scale, w = 1 if slot active
};

// Foliage Fragment Shader
// Alpha-tested leaves and foliage.  Identical PBR lighting to phong_perpixel
// but discards fragments whose alpha is below a threshold so leaf cut-outs
// work correctly without sorting artefacts.
// Uses phong_perpixel.vert (same varyings).

#define MAX_LIGHTS 8
#define PI 3.14159265358979323846

varying vec3 vViewPos;
varying vec3 vViewNormal;
varying vec2 vTexCoord;
varying vec2 vLightmapUV;

uniform sampler2D tDiffuse;
uniform sampler2D tLightmap;
uniform float     uHasLightmap;

uniform vec3  uLightPos[MAX_LIGHTS];
uniform vec4  uLightColor[MAX_LIGHTS];
uniform float uLightRadius[MAX_LIGHTS];
uniform float uLightCount;

// Clustered lighting (ClusteredLightManager). When uUseClusters > 0.5, per-light
// data comes from texelFetch textures on units 8-10 and the arrays above are
// unused. Constants must match ClusteredLightManager: 16x8x24 grid, 4 texels
// per light, 256-wide index texture.
uniform sampler2D tLightData;     // unit 8: t0=pos+radius, t1=color+cosInner, t2=dir+cosOuter(-2=point)
uniform sampler2D tClusterGrid;   // unit 9: (offset, count) per froxel
uniform sampler2D tLightIndices;  // unit 10: flat light index list


uniform float uRoughness;
uniform float uMetallic;


uniform sampler2D tEnvMap;

// Alpha discard threshold — values below this are fully transparent (cut-out).
// Match this to the texture's alpha channel cutoff.
#define ALPHA_CUTOFF 0.5

// ---------------------------------------------------------------------------
// GGX / Cook-Torrance helpers (shared with phong_perpixel)
// ---------------------------------------------------------------------------
float D_GGX(float NdotH, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float G_SmithSchlick(float NdotV, float NdotL, float roughness)
{
    float k  = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float GV = NdotV / (NdotV * (1.0 - k) + k);
    float GL = NdotL / (NdotL * (1.0 - k) + k);
    return GV * GL;
}

vec3 F_Schlick(float VdotH, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
}

// ---------------------------------------------------------------------------
void main()
{
    vec4 texColor = texture2D(tDiffuse, vTexCoord);

    // Alpha cut-out — discard transparent leaf regions entirely
    if (texColor.a < ALPHA_CUTOFF)
        discard;

    vec3 albedo = texColor.rgb;

    // Two-sided shading — flip the normal for back faces so leaves are lit
    // from both sides without requiring duplicate geometry.
    vec3 N = normalize(vViewNormal);
    if (!gl_FrontFacing)
        N = -N;

    vec3 V     = normalize(-vViewPos);
    float NdotV = max(dot(N, V), 0.0001);

    vec3 F0 = mix(vec3(0.04), albedo, uMetallic);

    vec3 diffuseAccum  = vec3(0.0);
    vec3 specularAccum = vec3(0.0);

    // Light list source: clustered path fetches this fragment's froxel list;
    // fallback path iterates the per-object uniform arrays (8 closest lights).
    int lightOffset = 0;
    int lightNum    = int(uLightCount);
    if (uUseClusters > 0.5)
    {
        ivec2 tile  = clamp(ivec2(gl_FragCoord.xy / uClusterParams.xy), ivec2(0), ivec2(15, 7));
        int   slice = clamp(int(floor(log2(max(vViewPos.z, 0.01)) * uClusterParams.z + uClusterParams.w)), 0, 23);
        vec2  oc    = texelFetch(tClusterGrid, ivec2(tile.x + tile.y * 16, slice), 0).xy;
        lightOffset = int(oc.x);
        lightNum    = int(oc.y);
    }

    for (int n = 0; n < lightNum; n++)
    {
        vec3  lightPos;
        vec3  lightColor;
        float lightRadius;
        if (uUseClusters > 0.5)
        {
            int  fi = lightOffset + n;
            int  li = int(texelFetch(tLightIndices, ivec2(fi & 255, fi >> 8), 0).x);
            vec4 t0 = texelFetch(tLightData, ivec2(li * 4 + 0, 0), 0);
            vec4 t1 = texelFetch(tLightData, ivec2(li * 4 + 1, 0), 0);
            lightPos    = t0.xyz;  lightRadius = t0.w;
            lightColor  = t1.xyz;
        }
        else
        {
            lightPos    = uLightPos[n];
            lightColor  = uLightColor[n].rgb;
            lightRadius = uLightRadius[n];
        }

        vec3  toLight = lightPos - vViewPos;
        float dist    = length(toLight);
        vec3  L       = toLight / dist;

        float atten = clamp(1.0 - (dist * dist) / (lightRadius * lightRadius), 0.0, 1.0);
        if (atten <= 0.0) continue;

        float NdotL = max(dot(N, L), 0.0);
        diffuseAccum += lightColor * NdotL * atten;

        if (uRoughness < 0.99 && NdotL > 0.0)
        {
            vec3  H     = normalize(L + V);
            float NdotH = max(dot(N, H), 0.0);
            float VdotH = max(dot(V, H), 0.0);
            float r   = max(uRoughness, 0.025);
            float D   = D_GGX(NdotH, r);
            float G   = G_SmithSchlick(NdotV, max(NdotL, 0.0001), r);
            vec3  F   = F_Schlick(VdotH, F0);
            specularAccum += (D * G * F) / max(4.0 * NdotV * NdotL, 0.001)
                           * lightColor * NdotL * atten;
        }
    }

    float diffuseFactor = 1.0 - uMetallic;

    vec3 bakedLight = (uHasLightmap > 0.5)
        ? texture2D(tLightmap, vLightmapUV).rgb + uAmbientColor
        : uAmbientColor;

    if (uHasEnvMap > 0.5 && uRoughness < 0.99)
    {
        vec3 R_view  = reflect(-V, N);
        vec3 R_world = mat3(uCamRight, uCamUp, uCamForward) * R_view;
        vec2 envUV = vec2(
            atan(R_world.x, R_world.z) / (2.0 * PI),
            asin(clamp(-R_world.y, -1.0, 1.0)) / PI + 0.5);
        vec3 envSample  = texture2D(tEnvMap, envUV).rgb;
        vec3 envF       = F_Schlick(NdotV, F0);
        float roughFade = (1.0 - uRoughness) * (1.0 - uRoughness);
        specularAccum += envSample * envF * roughFade;
    }

    vec3 color = albedo * max(bakedLight, diffuseAccum) * diffuseFactor
               + specularAccum;

    float fogDist   = length(vViewPos) - uFogStart;
    float fogFactor = 1.0 - exp(-uFogDensity * max(fogDist, 0.0));
    fogFactor       = clamp(fogFactor, 0.0, 1.0);
    gl_FragColor    = vec4(mix(color, uFogColor, fogFactor), 1.0);
}

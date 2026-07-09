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

// Barrel Heat Fragment Shader
//
// Extends phong_perpixel with a heat glow effect driven by uHeatLevel.
//
//   uHeatLevel = 0.0 : identical output to phong_perpixel (cold, no cost)
//   uHeatLevel = 0.3 : faint dark-red ember glow begins
//   uHeatLevel = 0.6 : orange glow, visible UV heat shimmer
//   uHeatLevel = 1.0 : white-hot, strong bloom-like emissive
//
// The heat colour follows a rough blackbody ramp:
//   black -> deep red -> orange -> orange-yellow -> pale white-yellow
//
// Texture UVs are perturbed at high heat to simulate rising hot air.

#define MAX_LIGHTS 8
#define PI 3.14159265358979323846

varying vec3 vViewPos;
varying vec3 vViewNormal;
varying vec2 vTexCoord;
varying vec2 vLightmapUV;
varying float vLocalZ;  // object-space Z from vertex shader

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

// Heat uniforms — pushed each frame by BarrelHeatShaderCallback
uniform float uHeatLevel;    // 0.0 cold .. 1.0 white-hot
uniform float uBarrelZMin;   // local-space Z at the rear of the barrel
uniform float uBarrelZMax;   // local-space Z at the muzzle end (Z+ = front)

// ---------------------------------------------------------------------------
// GGX / Cook-Torrance helpers (identical to phong_perpixel)
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
// Blackbody heat colour ramp
//   t=0.0  ->  (0, 0, 0)              no glow
//   t=0.3  ->  (0.9, 0.05, 0)         deep red ember
//   t=0.55 ->  (1.0, 0.35,  0)        orange
//   t=0.75 ->  (1.0, 0.65,  0.05)     bright orange-yellow
//   t=1.0  ->  (1.5, 1.15,  0.75)     white-hot (HDR values intentional — clamps on screen)
// ---------------------------------------------------------------------------
vec3 heatEmissiveColor(float t)
{
    float r = smoothstep(0.0,  0.25, t);
    float g = smoothstep(0.2,  0.75, t) * 0.77;
    float b = smoothstep(0.55, 1.0,  t) * 0.50;
    // Multiply by t*1.5 so the glow fades cleanly to zero at t=0 and
    // reaches HDR brightness at t=1 for a bloom-like appearance.
    return vec3(r, g, b) * t * 1.5;
}

// ---------------------------------------------------------------------------
void main()
{
    // --- Barrel length gradient -------------------------------------------
    // Normalize the fragment's object-space Z into 0..1 across the barrel.
    // uBarrelZMin/Max are set once at init from the actual mesh buffer bounds.
    // Z+ = muzzle (front, hottest), Z- = rear mount (coolest).
    float barrelRange = uBarrelZMax - uBarrelZMin;
    float barrelT     = (barrelRange > 0.001)
                        ? clamp((vLocalZ - uBarrelZMin) / barrelRange, 0.0, 1.0)
                        : 1.0;

    // Shape: rear gets 25% intensity, front gets 100%.
    // pow(..., 1.8) keeps the back two-thirds cool and accelerates sharply
    // into the front third.
    float barrelGradient = mix(0.25, 1.0, pow(barrelT, 1.8));

    // All heat-driven effects use this locally scaled value so every piece of
    // the effect (shimmer, desaturation, emissive) follows the same gradient.
    float effectiveHeat = uHeatLevel * barrelGradient;

    // --- UV heat shimmer ---------------------------------------------------
    // Perturbs the texture lookup to simulate rising hot air above the barrel.
    // Kicks in gradually above heat 0.25, squared for a soft onset.
    // Uses effectiveHeat so shimmer is strongest near the muzzle.
    vec2 uv = vTexCoord;
    float shimmerT = clamp((effectiveHeat - 0.25) / 0.75, 0.0, 1.0);
    shimmerT = shimmerT * shimmerT;
    float shimmer = shimmerT * 0.007;
    uv.x += sin(vTexCoord.y * 28.0 + fTime * 0.0025) * shimmer;
    uv.y += sin(vTexCoord.x * 22.0 + fTime * 0.0020) * shimmer;

    vec4 texColor = texture2D(tDiffuse, uv);
    vec3 albedo   = texColor.rgb;

    // --- Albedo desaturation at high heat ---------------------------------
    // Hot metal looks washed-out; at max heat the surface colour is mostly
    // overridden by the emissive glow anyway.
    float desatAmt = effectiveHeat * 0.35;
    float lum      = dot(albedo, vec3(0.299, 0.587, 0.114));
    albedo = mix(albedo, vec3(lum), desatAmt);

    // --- PBR lighting (identical to phong_perpixel) -----------------------
    vec3 N = normalize(vViewNormal);
    vec3 V = normalize(-vViewPos);

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

            vec3 spec = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
            specularAccum += spec * lightColor * NdotL * atten;
        }
    }

    float diffuseFactor = 1.0 - uMetallic;

    vec3 bakedLight = (uHasLightmap > 0.5)
        ? texture2D(tLightmap, vLightmapUV).rgb + uAmbientColor
        : uAmbientColor;

    vec3 color = albedo * max(bakedLight, diffuseAccum) * diffuseFactor
               + specularAccum;

    // --- Additive heat emissive -------------------------------------------
    // Added after all lighting so it blooms on top regardless of shadows/light.
    color += heatEmissiveColor(effectiveHeat);

    gl_FragColor = vec4(color, texColor.a);
}
